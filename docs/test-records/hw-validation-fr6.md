# Hardware Validation Record: FR-6

| Field | Value |
|---|---|
| Record ID | HW-VAL-006 |
| Requirement | FR-6: In both directions, at those baud rates (FR-5: default 115200, optionally 921600), the bridge shall not lose, duplicate, or reorder bytes. (*ESP32 Serial-to-TCP Bridge: Requirements v1.0*) |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-05 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from the retained test report and serial log |
| Firmware | App version `63cbb71-dirty`, ELF SHA256 `ce94f60773ad9e775294d72253025f176b3bea90d52169b7e96e2383cf9aa533` (source committed as `81b7fa7`) |

## Purpose and Scope

This record maps the H2, H3 and H4 echo tests to FR-6. Each one checks that the bytes returned are exactly the bytes sent: the same count, the same values and the same order.

The tests use the GPIO32 to GPIO33 jumper, so each byte crosses both directions, TCP to UART and UART back to TCP. A pass therefore covers both directions together. A fault in one direction cannot be told apart from one in the other.

Only 115200 8N1 was tested. 921600 is optional in FR-5, and the firmware does not yet allow the baud rate to be changed in menuconfig.

## Test Environment

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E |
| UART under test | UART1, TX GPIO32, RX GPIO33, 115200 8N1 |
| Hardware setup | GPIO32 connected to GPIO33 with a loopback jumper |
| Console | UART0 on `/dev/ttyUSB1`, 115200 baud, `idf.py monitor --timestamps` |
| Network | ESP32 at `192.168.1.200`, TCP port `4001`. Client seen as `192.168.1.58` |
| Host | Ubuntu 24.04.5 LTS, Node.js v24.22.2, Jest 30.5.2 |
| ESP-IDF | v6.1 |

The firmware was flashed and verified as recorded in [HW-VAL-009](hw-validation-fr9.md) (E9).

## Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E13 | Test report | [automated-test-results-2026-11-05.md](../test-reports/automated-test-results-2026-11-05.md), `npm test` finished 2026-11-05T17:17:00Z | 8 passed, 0 failed, H6 skipped. Its `Firmware` field reads "Not provided", because `BRIDGE_FIRMWARE` was not set; the firmware is identified by E14 |
| E14 | Captured serial log | `fr9-h9-test.log`, 12:16:30 to 12:17:00 | The same boot as the flashed `ce94f6077`: no reset since 09:27:36. Preserved as [docs/evidence/2026-11-05_h9-and-npm-test_fwce94f6077.log](../evidence/2026-11-05_h9-and-npm-test_fwce94f6077.log) |

## Test Summary

| Test ID | Plan | Test | Evidence | Result |
|---|---|---|---|---|
| HW-FR6-001 | H2 | No reordering: several messages return in the order sent | E13 | PASS |
| HW-FR6-002 | H3 | No lost, duplicated or altered byte across every value 0x00 to 0xFF (20,480 bytes) | E13 | PASS |
| HW-FR6-003 | H4 | No loss, duplication or reordering over 256,000 bytes | E13, E14 | PASS |
| HW-FR6-004 | H2 to H4 | No loss reported by the firmware during the transfers | E14 | PASS |
| HW-FR6-005 | Not run | The same at 921600 (optional in FR-5) | None | NOT APPLICABLE until FR-5 allows it |

### HW-FR6-001 to HW-FR6-003: Echo Integrity

**Procedure:** `npm test`, with the jumper connected and the monitor open.

**Results (E13):**

| Test | Sent | Received | Check | Result |
|---|---|---|---|---|
| H2 | A text line, then several messages in sequence | The same, in the same order | Exact comparison | PASS |
| H3 | 20,480 bytes, every value 0x00 to 0xFF | 20,480 bytes | SHA256 `a4759e7aa20338328866a2ea17eaf8c7fe4ec6bbe3bb71cee7df7c0461b3c22f` on both sides | PASS, 1.80 s |
| H4 | 256,000 bytes | 256,000 bytes | SHA256 `319ec64cc32f46da036038618fabdf40401e2baec3c8d98eef01847e245a9017` on both sides | PASS, 22.24 s, 11,509 bytes/s |

Equal byte counts rule out lost and duplicated bytes, and equal SHA256 hashes rule out altered and reordered ones.

### HW-FR6-004: No Loss Reported

Every `LOSS:` report covering H2, H3 and H4 said `no data loss`, with all counters at 0. The H4 session (client :40082, 12:16:34 to 12:16:56) was covered by the 12:16:37, 12:16:47 and 12:16:57 reports.

The 12:16:57 report also covers the start of H5 and shows `DATA LOSS` (`tcp_send_failures=1`, `tcp_tx_bytes=259`, `uart_tx_bytes=82`). This comes from H5's abrupt close, not from H4:
- H4 closed with `disconnected by peer`, which counts its pending bytes under `session_bytes`, and that counter was 0;
- the lost bytes belong to the H5 session that ended with `send failed: errno 104`.

H5 is expected to show this (see the README's Known Issues). It tests session isolation, not FR-6.

## How to Run These Tests

**Before you start**

1. Flash the firmware, and note the ELF SHA256.
2. Connect the GPIO32 to GPIO33 jumper.
3. Open the serial monitor and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr6-test.log`.

| Test | How to run it | What to check | Passes when |
|---|---|---|---|
| HW-FR6-001 | `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test -- -t "H2:"` | Several messages echoed in sequence | They return in the order sent |
| HW-FR6-002 | Same, with `-t "H3:"` | 20,480 bytes covering 0x00 to 0xFF | They return unchanged, with matching SHA256 |
| HW-FR6-003 | Same, with `-t "H4:"` | 256,000 bytes, compared by SHA256 | The byte count and the hashes match |
| HW-FR6-004 | After HW-FR6-001 to 003: `grep "LOSS:" fr6-test.log \| grep -v "no data loss"` | `LOSS:` lines that report loss | None during H2 to H4. If H5 ran in the same log, its interval may show loss, as explained above |
| HW-FR6-005 | Once FR-5 allows 921600: set it in menuconfig, build and flash, and send a known file both ways through a USB-UART adapter in place of the jumper, comparing `sha256sum` on each side | The hashes | They match |

## Status

**FR-6: VALIDATED at 115200 8N1** (2026-11-05, firmware `ce94f6077`). In both directions together, no byte was lost, duplicated, altered or reordered, and the firmware reported no loss during the transfers.

## Limitations

1. Both directions are verified together, through the jumper. Each direction alone was not verified with an independent serial device.
2. 921600 was not tested. It is optional in FR-5, and the baud rate is not yet configurable in menuconfig.
3. The longest transfer was 256,000 bytes (about 22 s). No long soak test was run.
4. The E13 report does not name the firmware. It is identified by the serial log (E14).
