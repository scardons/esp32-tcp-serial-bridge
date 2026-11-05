# Hardware Validation Record: FR-4

| Field | Value |
|---|---|
| Record ID | HW-VAL-004 |
| Requirement | FR-4: Data received through UART1 RX (GPIO33) must be forwarded to the active TCP client. |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from retained logs |
| Base commit | `78290c9`, with uncommitted changes (`78290c9-dirty`) |
| Updated | 2026-11-05: H2 to H5 rerun on firmware `ce94f6077` with a retained report and serial log (HW-FR4-006 to 009) |

## Purpose and Scope

This record maps the completed bridge tests to FR-4. UART RX data came from the GPIO32-to-GPIO33 jumper, so forwarding was exercised by the echo of data sent by the client.

It also records two bridge behaviors on the UART-to-TCP path: session isolation and the stalled-client timeout.

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
| HW-FR4-001 | H2 | Text echo received by the client | None retained | NOT VERIFIED |
| HW-FR4-002 | H3 | Binary echo, all 256 byte values | E7, timing only | PARTIAL |
| HW-FR4-003 | H4 | Sustained 256,000-byte transfer | E7, timing only | PARTIAL |
| HW-FR4-004 | H5 | No data from a previous session reaches a new client | E7, timing only | PARTIAL |
| HW-FR4-005 | H6 | Client that stops reading is disconnected | E7 | PASS |
| HW-FR4-006 | H2 | Text echo received by the client, firmware `ce94f6077` | E13, E14 | PASS |
| HW-FR4-007 | H3 | Binary echo, all 256 byte values, firmware `ce94f6077` | E13, E14 | PASS |
| HW-FR4-008 | H4 | Sustained 256,000-byte transfer, firmware `ce94f6077` | E13, E14 | PASS |
| HW-FR4-009 | H5 | No data from a previous session reaches a new client, firmware `ce94f6077` | E13, E14 | PASS |

HW-FR4-006 to 009 supersede the PARTIAL and NOT VERIFIED results of HW-FR4-001 to 004, which are kept for history. HW-FR4-005 (H6) was not rerun: H6 was skipped in the 2026-11-05 run.

### HW-FR4-001 to HW-FR4-003: Echo Tests (H2, H3, H4)

These are the same sessions recorded in `hw-validation-fr3.md`, as HW-FR3-001 to HW-FR3-003, and the same evidence limits apply. A complete echo needs FR-3 and FR-4 to work together.

| Test ID | Session duration | Expected for a full transfer |
|---|---|---|
| HW-FR4-002 | 1.79 s | 1.78 s |
| HW-FR4-003 | 22.26 s | 22.22 s |

**Evidence needed to close this test:** the terminal output of the script (`igual: True`). The serial log cannot show payload content.

### HW-FR4-004: Session Isolation (H5)

**Procedure:** A script connects, sends 2,000 bytes of `0xCD`, closes after 50 ms, and reconnects until a client is accepted. It then waits 1.5 s for data and prints the byte count.

**Expected:** `recibidos: 0`. A rejection is allowed if the reconnection is very fast.

**Actual:** The first reconnection was rejected, and the next was accepted about 120 ms later. That session stayed open 1.51 s, which matches the 1.5 s wait ending with no data, because any received byte would have ended the script at once.

**Evidence (E7)**

```text
I (546566) NETWORK: TCP client connected: 192.168.1.170:60087
W (546626) NETWORK: TCP client 192.168.1.170:60088 rejected: a client is already connected
W (546636) BRIDGE: TCP client send failed: errno 104
I (546636) BRIDGE: TCP client closed: socket error
I (546746) NETWORK: TCP client connected: 192.168.1.170:60090
I (548256) BRIDGE: TCP client closed: disconnected by peer
```

`errno 104` is the connection reset caused by the first client closing while its echo was still arriving.

**Evidence needed to close this test:** the terminal output `recibidos: 0`.

### HW-FR4-005: Stalled Client (H6)

**Procedure:** A script connects with a 4,096-byte receive buffer, sends 300,000 bytes and does not read.

**Expected:** `BRIDGE: TCP client closed: client stalled`.

**Actual:** The session closed with that reason. It lasted 24.08 s from connection. The 5-second interval itself could not be measured, because the log does not show when transmission stopped progressing.

**Evidence (E7)**

```text
I (586796) NETWORK: TCP client connected: 192.168.1.170:58219
I (610876) BRIDGE: TCP client closed: client stalled
```

### HW-FR4-006 to HW-FR4-009: Rerun on Firmware `ce94f6077` (2026-11-05)

**Firmware:** ELF SHA256 `ce94f60773ad9e775294d72253025f176b3bea90d52169b7e96e2383cf9aa533`, flashed and verified as recorded in [HW-VAL-009](hw-validation-fr9.md) (E9).

| ID | Type | Source | Notes |
|---|---|---|---|
| E13 | Test report | [automated-test-results-2026-11-05.md](../test-reports/automated-test-results-2026-11-05.md), `npm test` finished 2026-11-05T17:17:00Z | 8 passed, 0 failed, H6 skipped. Its `Firmware` field reads "Not provided", because `BRIDGE_FIRMWARE` was not set; the firmware is identified by E14 |
| E14 | Captured serial log | `fr9-h9-test.log`, 12:16:30 to 12:17:00 | The same boot as the flashed `ce94f6077`: no reset since 09:27:36. Not tracked by Git |

**Results (E13):**

| Test | Check | Result |
|---|---|---|
| H2 | The client receives a text line, and several messages in sequence, unchanged and in order | PASS |
| H3 | The client receives all 20,480 bytes covering 0x00 to 0xFF, unchanged. SHA256 `a4759e7a…c22f` on both sides | PASS |
| H4 | The client receives all 256,000 bytes unchanged and in order. SHA256 `319ec64c…9017` on both sides, 11,509 bytes/s | PASS |
| H5 | The next client receives `0` bytes from the closed session, and its own session works | PASS |

**Evidence (E14), H5.** The first client closes abruptly, a second is rejected while the first is still active, and the next one is accepted:

```text
2026-11-05 12:16:57 I (10159908) NETWORK: TCP client connected: 192.168.1.58:51082
2026-11-05 12:16:57 W (10160218) NETWORK: TCP client 192.168.1.58:51084 rejected: a client is already connected
2026-11-05 12:16:57 W (10160228) BRIDGE: TCP client send failed: errno 104
2026-11-05 12:16:57 I (10160238) BRIDGE: TCP client closed: socket error
2026-11-05 12:16:57 I (10160338) NETWORK: TCP client connected: 192.168.1.58:51086
2026-11-05 12:16:57 W (10160368) LOSS: 10.0 s: DATA LOSS: uart_rx_overflow=0 uart_rx_errors=0 tcp_send_failures=1 tcp_tx_bytes=259 uart_tx_bytes=82 | expected: session_bytes=0 no_client_bytes=971 | ...
2026-11-05 12:16:59 I (10162108) BRIDGE: TCP client closed: disconnected by peer
```

The `DATA LOSS` line is expected for H5 (see the README's Known Issues). The abrupt close leaves bytes in flight, and they are counted as lost rather than delivered to the next client. Only the isolation result decides H5, and the next client received 0 bytes.

## How to Run These Tests

**Before you start**

1. Flash the firmware, and note the ELF SHA256 from the `MAIN: Firmware:` boot line.
2. Connect the GPIO32 to GPIO33 jumper.
3. Open the serial monitor and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr4-test.log`.
4. Close any other TCP client.

| Test | How to run it | What to check | Passes when |
|---|---|---|---|
| HW-FR4-001 | `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test -- -t "H2:"` | Jest output and the report | The client receives the text unchanged and in order |
| HW-FR4-002 | Same, with `-t "H3:"` | Jest output | All 20,480 bytes are received unchanged |
| HW-FR4-003 | Same, with `-t "H4:"` | Jest output, plus `LOSS:` in the serial log | All 256,000 bytes are received unchanged and in order, with `no data loss` |
| HW-FR4-004 | Same, with `-t "H5:"` | Jest output, plus the two sessions in the serial log | The next client receives 0 bytes from the closed session, and its own echo works |
| HW-FR4-005 | `RUN_H6=true BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new h6 file>.md npx jest tests/integration/stalled-client.test.ts` (or `npm run test:h6`, which overwrites the H6 report) | `BRIDGE: TCP client closed: client stalled`, then a new `TCP client connected` | The stalled client is disconnected and a new one is accepted. A `BLOCKED` result (for example, a NAT path) means the test could not judge the firmware |

H6 floods the UART for up to 100 s, so allow about 2 minutes.

**Testing FR-4 on its own (optional).**
1. Remove the jumper, and connect the TX pin of a 3.3 V USB-UART adapter to GPIO33, with a common ground.
2. Run `nc -v 192.168.1.200 4001`.
3. Type in `picocom -b 115200 /dev/ttyUSB<n>`.

The text must appear in the `nc` terminal unchanged.

## Status

**FR-4: VALIDATED** (2026-11-05, firmware `ce94f6077`, through the loopback jumper at 115200 8N1). UART1 RX data reached the active client unchanged and in order, including every byte value and a 256,000-byte transfer, and none reached the next client after a session closed. The stalled-client path (HW-FR4-005) passed on earlier firmware.

## Limitations

1. UART data came only from the jumper, together with FR-3. An independent serial device was not tested (see "Testing FR-4 on its own").
2. HW-FR4-005 (H6) was observed on firmware `23f991546`, not on `ce94f6077`.
3. The E13 report does not name the firmware. It is identified by the serial log (E14).
4. UART data can be lost without notice if TCP stalls longer than the UART RX buffer can absorb, about 178 ms. The driver provides no overflow counter.
5. The Ubuntu network path was not confirmed, and NAT buffering could affect HW-FR4-005 timing.
