# Hardware Validation Record: FR-8

| Field | Value |
|---|---|
| Record ID | HW-VAL-008 |
| Requirement | FR-8: The device shall detect a client that disconnects or times out, including a half-open connection, within 30 seconds, and shall free any allocated resources. (*Requirements v1.0*) |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from retained logs |
| Base commit | `78290c9`, with uncommitted changes (`78290c9-dirty`) |
| Updated | 2026-11-05: adds the FR-8 test report (TR-FR8-002, firmware `8d79dbe83`), the H8 log (216 s outage) and H9 (firmware `ce94f6077`). FR-8 code is unchanged between `63cbb71` and the current source |

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

Evidence added on 2026-11-05:

| ID | Type | Source | Notes |
|---|---|---|---|
| R1 | Test report | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md), firmware `63cbb71` / `8d79dbe83` | Keepalive, RST, idle, half-open, soak and pending-data tests, with their own evidence (E1 to E3, O1 to O4). Logs preserved in [`docs/evidence/`](../evidence/) |
| E15 | Captured serial log | [2026-11-02_h8_fw23f991546.log](../evidence/2026-11-02_h8_fw23f991546.log), firmware `23f991546` | One boot, no reset, no `E (`, panic or watchdog line. Contains a 216 s link outage |
| E16 | Captured serial log and report | [2026-11-05_h9-and-npm-test_fwce94f6077.log](../evidence/2026-11-05_h9-and-npm-test_fwce94f6077.log) and the [H9 report](../test-reports/manual-h9-2026-11-05T14-51-27-137Z.md), firmware `ce94f6077` | 9 link outages and 5 sessions cut by them. See [HW-VAL-009](hw-validation-fr9.md) |

`git diff 63cbb71 HEAD` shows no change in `main/bridge/`, `main/uart/` or `main/network/client_socket.c`, so R1's results apply to the current FR-8 code. The only `network.c` change since then is FR-9 logging.

## Test Summary

| Test ID | Plan | Test | Evidence | Result |
|---|---|---|---|---|
| HW-FR8-001 | H7 | Ethernet disconnection with a client connected | E7 | PASS |
| HW-FR8-002 | H7 | Static IP restored after link return | E7 | PASS |
| HW-FR8-003 | H7, H9 | New TCP client accepted after link return | E16 | PASS (2026-11-05: 5 of 5 after H9 outages. Was NOT VERIFIED in E7) |
| HW-FR8-004 | H8 | Outage longer than 2 minutes | E15 | PASS (216 s outage, static IP restored, new client accepted, no reset) |
| HW-FR8-005 | H6 | Client that stops reading releases the slot | E7, and [automated-test-results-h6-2026-11-05.md](../test-reports/automated-test-results-h6-2026-11-05.md) on `ce94f6077` | PASS (rerun 2026-11-05: `client stalled`, heap logged, next client accepted) |
| HW-FR8-006 | R1 C1, C2 | Half-open client detected by keepalive and cleaned up | R1 | PASS (see the timing basis below) |
| HW-FR8-007 | R1 Test 1 | Keepalive (idle 5 s, interval 2 s, 3 probes) set and read back on every client | R1 | PASS |
| HW-FR8-008 | R1 Test B | Abrupt RST detected (`errno 104`) and cleaned up | R1 | PASS |
| HW-FR8-009 | R1 Test D | Healthy idle client kept for 300 s | R1 | PASS |
| HW-FR8-010 | R1 Test G | 200 connect and close cycles, stable heap | R1 | PASS |
| HW-FR8-011 | R1 Test H | Graceful close with pending data: resources freed, bytes accounted | R1 | PASS |
| HW-FR8-012 | R1, E16 | Heap stable across all sessions, no reset | R1, E16 | PASS |

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

### HW-FR8-003: New Client After Link Return (H9, 2026-11-05)

In each of the 5 H9 cycles, the session cut by the outage closed with `errno 113` and `socket error`, and a new client was accepted after the static IP returned. Details are in [HW-VAL-009](hw-validation-fr9.md).

### HW-FR8-004: Outage Longer Than 2 Minutes (E15)

```text
I (84316) BRIDGE: TCP client closed: disconnected by peer
W (105376) NETWORK: Ethernet link down
I (321376) NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC c0:cd:d6:61:ae:b3
I (321376) NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
I (344036) NETWORK: TCP client connected: 192.168.1.170:61171
```

- **The outage lasted 216.0 s.** The ESP-NETIF lost-IP timer (120 s) expired during it, and the static IP still returned with the link.
- **No client was open when the link went down**, so this test covers recovery, not session cleanup.
- **The match to H8 is an inference** from the date and the outage length. The log does not name tests.

### HW-FR8-006: Half-Open Detection (R1, C1 and C2)

Twice, with the client cut off by `iptables`, the ESP32 logged `TCP client receive failed: errno 113` (keepalive abort), closed the session, logged the heap, and then accepted a new client.

**Timing basis for the 30 s limit:**

1. **The keepalive values are verified on the hardware:** every accepted client logs `TCP keepalive: idle 5 s, interval 2 s, 3 probes`, and the firmware reads each value back after setting it (R1, section 3.3).
2. **Those values bound the detection time.** lwIP aborts a connection 5 s + 3 × 2 s = 11 s after the last segment it received. The block happens after that last segment, so detection takes at most about 11 s after the block.
3. **The operator observed about 9 s** for C1 (R1, O1).

No log records the moment of the block, so R1 reports the timing of C1 and C2 as INCONCLUSIVE. This record accepts the result on the three points above. A timestamped rerun would turn point 2 into a direct measurement (see "How to Run These Tests").

## How to Run These Tests

**Before you start**

1. Flash the firmware, and note the ELF SHA256 from the `MAIN: Firmware:` boot line.
2. Connect the GPIO32 to GPIO33 jumper.
3. Open the serial monitor and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr8-test.log`.
4. Close any other TCP client.

| Test | How to run it | What to check in the serial log | Passes when |
|---|---|---|---|
| HW-FR8-001 and 002 | `BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h7`, following its prompts | `Ethernet link down (#n)`, `TCP client receive failed: errno 113` and `TCP client closed: socket error` (or `disconnect requested`), then `IPv4 ready: 192.168.1.200, ...` and `Static IP restored after link recovery (#n)` | The session closes with no reset, and the static IP returns |
| HW-FR8-003 | Same H7 run. The script connects a new client after the link returns | A new `TCP client connected` after `Static IP restored` | The script reports the new client and its echo as PASS, and the log shows the connection |
| HW-FR8-004 | `BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h8` (the cable stays out more than 2 minutes) | The same lines as H7. After about 120 s: `ESP-NETIF lost-IP event during outage #n (...). Static IP 192.168.1.200 kept for reconnection` | The static IP returns and a new client is accepted, with no `static configuration was not kept` |
| HW-FR8-005 | `npm run test:h6` | `TCP client closed: client stalled`, then a new `TCP client connected` | The stalled client is closed and the slot is released |
| HW-FR8-006 | See the half-open procedure below | `TCP client receive failed`, `TCP client closed: socket error` and `Heap after session` | The session closes **within 30 s** of the client going silent, and a new client is then accepted |

**Half-open procedure (HW-FR8-006).** The ESP32 keeps its link, and the client stops answering without closing:

1. Connect a client: `nc -v 192.168.1.200 4001`, and type a line to check the echo.
2. In another terminal, make the PC drop everything from the ESP32: `sudo iptables -I INPUT -s 192.168.1.200 -j DROP`. Note the time.
3. Wait up to 30 s, watching the serial log. With the logged keepalive (`idle 5 s, interval 2 s, 3 probes`), the session should close after about 11 s.
4. Remove the rule: `sudo iptables -D INPUT -s 192.168.1.200 -j DROP`. Close `nc`, then connect again to confirm the slot is free.

While the rule is in place, ping to the ESP32 also fails. Remove the rule before running other tests.

## Status

**FR-8: VALIDATED.**

The following were all shown on hardware, and each session's resources were freed:
- normal close;
- abrupt RST;
- stalled client;
- link loss with an active client;
- half-open connections, detected by keepalive.

Healthy idle clients were kept, and heap stayed stable over 200 cycles. The 30 s limit for half-open detection rests on the verified keepalive configuration (at most about 11 s) and the operator's observation of about 9 s, not on a timestamped measurement.

Previous status (2026-11-02): PARTIAL.

## Limitations and Pending Validation

1. Half-open detection time has no timestamped measurement (see HW-FR8-006). A rerun with `date` before the block and the monitor on `--timestamps` would close it.
2. R1 was run on firmware `8d79dbe83`. Its code is unchanged in the current source, but it has not been rerun on `ce94f6077`.
3. HW-FR8-004 ran on firmware `23f991546`. Its static IP and Ethernet start-up code is unchanged since then. H9 repeated recovery on `ce94f6077` for outages up to 110 s.
4. Test-to-log matching is by order and duration, because the logs do not name tests.
