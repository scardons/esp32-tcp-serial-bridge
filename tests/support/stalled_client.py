"""Stalled client for H6.

Node.js cannot set SO_RCVBUF on TCP sockets, and Linux can grow the receive
buffer of an idle socket to many megabytes. Setting a small buffer before
connecting is the only reliable way to make the client look stalled.

A small receive buffer only stalls the ESP32 when nothing between them ends
TCP on its own. A NAT engine, such as VirtualBox NAT, answers the client for
the ESP32 and can hide its reset, so the path is checked before the flood.
When it cannot be validated, the client closes without flooding and reports
blocked: the run then says nothing about the firmware.

Prints one JSON line:
{"blocked": bool, "disconnected": bool, "seconds": float, "error": str,
 "path": {"local": str, "remote": str, "mss": int|null, "expected_mss": int,
          "route": str, "problems": [str]}}

Run `python3 -I tests/support/stalled_client.py --self-test` to test the path
check without an ESP32.
"""
import json
import os
import socket
import subprocess
import sys
import threading
import time

HOST = os.environ.get("BRIDGE_HOST", "192.168.1.200")
PORT = int(os.environ.get("BRIDGE_PORT", "4001"))
TIMEOUT_S = float(os.environ.get("H6_TIMEOUT_S", "100"))
# CONFIG_LWIP_TCP_MSS in sdkconfig. lwIP does not use TCP timestamps, so the
# client keeps the full value. A proxy in the path usually advertises its own.
EXPECTED_MSS = int(os.environ.get("H6_EXPECTED_MSS", "1440"))
RECEIVE_BUFFER = 4096
CHUNK = bytes(1024)


def describe_route(route_json):
    """Returns (description, gateway) from `ip -j route get` output.

    The gateway is None for an on-link route. Raises ValueError when the
    output cannot be read, because then the path is unknown.
    """
    routes = json.loads(route_json)

    if not isinstance(routes, list) or len(routes) != 1 or "dev" not in routes[0]:
        raise ValueError(f"unexpected route output: {route_json.strip()}")

    route = routes[0]
    gateway = route.get("gateway")
    where = f"via {gateway}" if gateway else "on-link"

    return f"{where} dev {route['dev']}", gateway


def lookup_route(host):
    """Returns (description, gateway), or (None, None) if the lookup failed."""
    try:
        output = subprocess.run(["ip", "-j", "route", "get", host], capture_output=True,
                                text=True, timeout=5, check=True).stdout
        return describe_route(output)
    except (OSError, subprocess.SubprocessError, ValueError):
        return None, None


def path_problems(mss, expected_mss, route, gateway):
    """Lists why the path is not known to be direct. Empty means no problem.

    An empty list is not proof of a direct link: a proxy can match the MSS on
    an on-link network. It only means that nothing contradicts one.
    """
    problems = []

    if route is None:
        problems.append(f"cannot read the route to {HOST}")
    elif gateway is not None:
        problems.append(f"the ESP32 is reached through gateway {gateway}, not on-link "
                        "(VirtualBox NAT? use a Bridged Adapter)")

    if mss is None:
        problems.append("cannot read the TCP MSS")
    elif mss != expected_mss:
        problems.append(f"TCP MSS is {mss}, the ESP32 advertises {expected_mss}: "
                        "something in the path answers for it")

    return problems


def read_mss(sock):
    try:
        return sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
    except OSError:
        return None


def address(pair):
    return f"{pair[0]}:{pair[1]}"


def check_path(sock):
    route, gateway = lookup_route(HOST)
    mss = read_mss(sock)

    return {
        "local": address(sock.getsockname()),
        "remote": address(sock.getpeername()),
        "mss": mss,
        "expected_mss": EXPECTED_MSS,
        "route": route or "unknown",
        "problems": path_problems(mss, EXPECTED_MSS, route, gateway),
    }


def send_without_reading(sock, started, result, finished):
    # Sends until the server closes: only a failed send can reveal that here,
    # because this client never reads. The kernel can absorb a fixed payload.
    try:
        while True:
            sock.sendall(CHUNK)
    except OSError as error:
        result.update(disconnected=True, seconds=time.monotonic() - started,
                      error=type(error).__name__)
    finished.set()


def main():
    client = socket.socket()
    client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, RECEIVE_BUFFER)
    client.settimeout(5)
    client.connect((HOST, PORT))
    client.settimeout(None)

    path = check_path(client)
    result = {"blocked": bool(path["problems"]), "path": path}

    if result["blocked"]:
        # Nothing was sent, so this close does not exercise the stall detection.
        client.close()
        result.update(disconnected=False, seconds=0.0, error="")
        print(json.dumps(result), flush=True)
        return

    finished = threading.Event()
    start = time.monotonic()
    threading.Thread(target=send_without_reading, args=(client, start, result, finished),
                     daemon=True).start()
    finished.wait(TIMEOUT_S)

    if "disconnected" not in result:
        result.update(disconnected=False, seconds=time.monotonic() - start, error="")

    print(json.dumps(result), flush=True)
    os._exit(0)


def self_test():
    import unittest

    # Recorded on a Bridged Adapter VM and behind VirtualBox NAT.
    on_link = '[{"dst":"192.168.1.200","dev":"enp0s3","prefsrc":"192.168.1.58","flags":[]}]'
    via_nat = ('[{"dst":"192.168.1.200","gateway":"10.0.2.2","dev":"enp0s3",'
               '"prefsrc":"10.0.2.15","flags":[]}]')

    class PathCheck(unittest.TestCase):
        def test_on_link_route(self):
            self.assertEqual(describe_route(on_link), ("on-link dev enp0s3", None))

        def test_route_through_gateway(self):
            self.assertEqual(describe_route(via_nat), ("via 10.0.2.2 dev enp0s3", "10.0.2.2"))

        def test_unreadable_route(self):
            for output in ("[]", "{}", '[{"dst":"192.168.1.200"}]'):
                with self.assertRaises(ValueError):
                    describe_route(output)

        def test_direct_path_has_no_problem(self):
            self.assertEqual(path_problems(1440, 1440, "on-link dev enp0s3", None), [])

        def test_virtualbox_nat_is_blocked_by_both_signals(self):
            problems = path_problems(1460, 1440, "via 10.0.2.2 dev enp0s3", "10.0.2.2")
            self.assertEqual(len(problems), 2)
            self.assertIn("gateway 10.0.2.2", problems[0])
            self.assertIn("TCP MSS is 1460", problems[1])

        def test_mss_mismatch_alone_is_blocked(self):
            self.assertEqual(len(path_problems(1460, 1440, "on-link dev enp0s3", None)), 1)

        def test_matching_mss_does_not_excuse_a_gateway(self):
            self.assertEqual(len(path_problems(1440, 1440, "via 10.0.2.2 dev enp0s3",
                                               "10.0.2.2")), 1)

        def test_unknown_signals_are_blocked(self):
            self.assertEqual(len(path_problems(None, 1440, None, None)), 2)

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PathCheck)
    passed = unittest.TextTestRunner(stream=sys.stderr, verbosity=2).run(suite).wasSuccessful()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    else:
        main()
