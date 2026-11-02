"""Stalled client for H6.

Node.js cannot set SO_RCVBUF on TCP sockets, and Linux can grow the receive
buffer of an idle socket to many megabytes. Setting a small buffer before
connecting is the only reliable way to make the client look stalled.

Prints one JSON line: {"disconnected": bool, "seconds": float, "error": str}
"""
import json
import os
import socket
import threading
import time

HOST = os.environ.get("BRIDGE_HOST", "192.168.1.200")
PORT = int(os.environ.get("BRIDGE_PORT", "4001"))
TIMEOUT_S = float(os.environ.get("H6_TIMEOUT_S", "100"))
RECEIVE_BUFFER = 4096
CHUNK = bytes(1024)

result = {}
finished = threading.Event()


def send_without_reading(sock, started):
    # Sends until the server closes: only a failed send can reveal that here,
    # because this client never reads. The kernel can absorb a fixed payload.
    try:
        while True:
            sock.sendall(CHUNK)
    except OSError as error:
        result.update(disconnected=True, seconds=time.monotonic() - started,
                      error=type(error).__name__)
    finished.set()


client = socket.socket()
client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, RECEIVE_BUFFER)
client.settimeout(5)
client.connect((HOST, PORT))
client.settimeout(None)

start = time.monotonic()
threading.Thread(target=send_without_reading, args=(client, start), daemon=True).start()
finished.wait(TIMEOUT_S)

if not result:
    result.update(disconnected=False, seconds=time.monotonic() - start, error="")

print(json.dumps(result), flush=True)
os._exit(0)
