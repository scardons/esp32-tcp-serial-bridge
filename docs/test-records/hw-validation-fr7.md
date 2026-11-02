# Hardware Validation Record: FR-7

| Field | Value |
|---|---|
| Record ID | HW-VAL-007 |
| Requirement | FR-7: The firmware must report detected data loss every 10 seconds through the UART0 console. |
| Project | ESP32 TCP Serial Bridge |
| Status | **PENDING: no hardware validation performed** |
| Implementation | `docs/fr7-data-loss-reporting.md` |

## Purpose and Scope

This record defines the hardware validation of FR-7. Results will be added here after the tests are run on the ESP32. Tests on the PC are not hardware validation, and no result below has been observed on hardware.

## Prerequisites

1. Flash the firmware that contains FR-7, and note its `ELF file SHA256`.
2. Keep the GPIO32 to GPIO33 jumper connected, except where a test says otherwise.
3. Keep `idf.py -p /dev/ttyUSB1 monitor` open, and save its log.

## Planned Tests

| Test ID | Test | Procedure | Expected | Result |
|---|---|---|---|---|
| HW-FR7-001 | Report cadence | Boot and leave idle for 60 s | One `LOSS:` line about every 10 s, with `no data loss` | PENDING |
| HW-FR7-002 | No false positives | Run `npm test` (H1 to H5, with H5 noted below) | Sessions with clean closes report no loss | PENDING |
| HW-FR7-003 | Stalled client | Run `npm run test:h6` | A `W` line with `tcp_send_failures=1`. `rx_buffer_full` and `uart_rx_overflow` above 0 are expected from the jumper echo | PENDING |
| HW-FR7-004 | Link loss | Run `npm run manual:h7` while data is flowing | A `W` line with non-zero `tcp_tx_bytes` or `uart_tx_bytes` | PENDING |
| HW-FR7-005 | Data with no client | Feed GPIO33 from an external 3.3 V source with no client | `no_client_bytes` grows, and the line says `no data loss` | PENDING |
| HW-FR7-006 | Line errors | Feed GPIO33 at a wrong baud rate | `uart_rx_errors` above 0 | PENDING |
| HW-FR7-007 | Regression | Compare a full `npm test` and H6 run with the earlier reports | Same results, with no `task_wdt` | PENDING |

H5 closes its first client abruptly, so a `W` line with `tcp_send_failures=1` is expected around that test.

## Status

**FR-7: PENDING.** Implemented and tested on the PC only.
