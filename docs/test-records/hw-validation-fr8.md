# Hardware Validation Record: FR-8

| Field | Value |
|---|---|
| Record ID | HW-VAL-008 |
| Requirement | FR-8: Text not available in this repository. Project notes state that it covers half-open connection detection and Ethernet disconnection scenarios. |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from retained logs |
| Base commit | `78290c9`, with uncommitted changes (`78290c9-dirty`) |

## Purpose and Scope

This record maps the completed recovery tests to FR-8. Earlier project decisions identify half-open connection detection and Ethernet disconnection scenarios as its open validations.

## Test Environment

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E |
| UART under test | UART1, TX GPIO32, RX GPIO33, 115200 8N1 |
| Hardware setup | GPIO32 connected to GPIO33 with a loopback jumper, as required by the test plan |
| Console | UART0 on `/dev/ttyUSB1`, 115200 baud |
| Network | ESP32 at `192.168.1.200`, TCP port `4001`; client seen as `192.168.1.170` |
| Host | Ubuntu 24.04.5 LTS, OpenBSD netcat, Python 3 |
| ESP-IDF | v6.1 |
| Firmware | App version `78290c9-dirty`, compile time `Nov 2 2026 13:25:46` |
| ELF SHA256 | `23f9915465ac02e8daec17c3cecff8f5fd4f9a777a9c4da64803fc5bae5042ba` |

The firmware was built from uncommitted changes, so the ELF SHA256 is its only exact identifier.

## Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E7 | Captured serial log | ESP-IDF monitor session ending 14:11:59, firmware `23f991546` | One continuous boot, about 865 s of device uptime. Complete, not truncated |
| E8 | Captured tool output | `idf.py` flash log | Bridge firmware flashed and hash verified at 13:57:17 |

E7 comes from `build/log/`, which Git ignores and `idf.py fullclean` can delete. The excerpts here are its preserved copy.

The log does not label tests. Each session was matched to a planned test by order and duration, so the match is an inference. Terminal output from the client was not retained for any session, and the test plan commands are shown as the intended procedure.

The E7 session shows no reset after boot, and no `task_wdt`, panic, backtrace or `E (` error lines.

## Test Summary

| Test ID | Plan | Test | Evidence | Result |
|---|---|---|---|---|
| HW-FR8-001 | H7 | Ethernet disconnection with a client connected | E7 | PASS |
| HW-FR8-002 | H7 | Static IP restored after link return | E7 | PASS |
| HW-FR8-003 | H7 | New TCP client accepted after link return | None retained | NOT VERIFIED |
| HW-FR8-004 | H8 | Outage longer than 2 minutes | Not available | NOT VERIFIED |
| HW-FR8-005 | H6 | Client that stops reading releases the slot | E7 | PASS |
| HW-FR8-006 | Not run | Half-open client detection by keepalive | None | PENDING |

### HW-FR8-001 and HW-FR8-002: Link Loss with Active Client (H7)

**Procedure**

1. Connect a client: `nc -v 192.168.1.200 4001`.
2. Disconnect the Ethernet cable.
3. Reconnect the cable and wait for `IPv4 ready`.

**Expected:** Link down is logged, the client session closes without a reboot, and the static IP returns with the link.

**Actual:** The client socket was aborted in the same logged millisecond as the link loss, and the bridge closed it. The link returned 66.0 s later and the static configuration was restored, with no reset.

**Evidence (E7)**

```text
I (788826) NETWORK: TCP client connected: 192.168.1.170:63664
W (798976) NETWORK: Ethernet link down
W (798976) BRIDGE: TCP client receive failed: errno 113
I (798976) BRIDGE: TCP client closed: socket error
I (864976) NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC c0:cd:d6:61:ae:b3
I (864976) esp_netif_handlers: eth ip: 192.168.1.200, mask: 255.255.255.0, gw: 192.168.1.1
I (864976) NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
```

`errno 113` is `ECONNABORTED`, the expected result of lwIP aborting the socket when the address is removed. The socket closed through this path, not through the disconnect request.

### HW-FR8-003: Reconnection After the Link Returns (H7)

**Expected:** A new `nc` connection is accepted without a reboot.

**Actual:** Not verified. E7 ends about 17 s after the link returned, with no new connection logged.

### HW-FR8-004: Long Outage (H8)

**Expected:** After an outage longer than 2 minutes, ping and a new TCP connection succeed without a reboot.

**Actual:** Not verified. The monitor session that may contain this test was still open when this record was prepared, so its output was not available. The longest captured outage is 66.0 s.

### HW-FR8-005: Stalled Client Releases the Slot (H6)

**Actual:** The session closed with reason `client stalled`. A later client was accepted, which confirms that the slot was released.

```text
I (610876) BRIDGE: TCP client closed: client stalled
I (788826) NETWORK: TCP client connected: 192.168.1.170:63664
```

### HW-FR8-006: Half-Open Detection

Not run. With the ESP32 link kept up and the client removed, the expected result is `TCP client receive failed` after about 12 s, followed by a successful new connection.

## Status

**FR-8: PARTIAL.** Link loss with an active client was handled safely on hardware. Reconnection after link return, the long outage and half-open detection are not verified.

## Limitations and Pending Validation

1. Half-open detection has not been tested.
2. Reconnection after link return needs serial or terminal evidence.
3. H8 needs its monitor output.
4. Ping after link recovery has no retained evidence.
5. The path between Ubuntu and the ESP32 was not confirmed.
