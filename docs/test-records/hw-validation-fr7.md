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

The plan covers the firmware with the P0 audit corrections: frame errors (M1), event queue drops (M2), session-end accounting (M3), and L1 to L6. Firmware `f10f8b076` predates them and is not a candidate for this record.

## Prerequisites

1. Flash the firmware under test. Record the `MAIN: Firmware:` line from the boot log, and use its ELF SHA256 as the firmware ID. If the boot log warns that it was built from uncommitted changes, the version alone does not identify it.
2. Keep the GPIO32 to GPIO33 jumper connected, except where a test says otherwise.
3. Keep `idf.py -p /dev/ttyUSB1 monitor` open from boot, and save its log.

## Planned Tests

| Test ID | Test | Procedure | Expected | Result |
|---|---|---|---|---|
| HW-FR7-001 | Report cadence | Boot and leave idle for 60 s | One `LOSS:` line about every 10 s, at `I` level, with `no data loss` and every field 0 | PENDING |
| HW-FR7-002 | No false positives | Run `npm test` (H1 to H5, with H5 noted below) | Sessions with clean closes report no loss | PENDING |
| HW-FR7-003 | Stalled client | Run `npm run test:h6` | A `W` line with `tcp_send_failures=1` and `tcp_tx_bytes` of at least 2,048, because the full RX ring buffer is now counted at session end. `rx_buffer_full` and `uart_rx_overflow` above 0 are expected from the jumper echo. `no_client_bytes` must not hold the ring buffer contents | PENDING |
| HW-FR7-004 | Link loss | Start a continuous transfer, for example H4, then pull the cable while data is flowing | A `W` line with non-zero `tcp_tx_bytes` or `uart_tx_bytes`. With no data in flight at the moment of the pull, `no data loss` is correct, so the test must be repeated | PENDING |
| HW-FR7-005 | Data with no client | Feed GPIO33 from an external 3.3 V source at 115200 8N1 with no client | `no_client_bytes` grows, and the line says `no data loss` | PENDING |
| HW-FR7-006 | Line errors | Feed GPIO33 at a wrong baud rate | `uart_rx_errors` above 0. A sustained stream at a wrong rate may also show `rx_error_pauses=1`, and the next interval must report again | PENDING |
| HW-FR7-007 | Regression | Compare a full `npm test` and H6 run with the earlier reports | Same results, with no `task_wdt` | PENDING |
| HW-FR7-008 | Event queue under load | During H4 (256,000 bytes) and H6, read every `LOSS:` line | `event_queue_drops=0`. Any drop is still counted in the other fields, but means that the reporter fell behind | PENDING |
| HW-FR7-009 | Break | With no client, hold GPIO33 low for about 1 s through a resistor, then release it | `uart_breaks` above 0. Record whether `uart_rx_errors` also rises, which is Limitation 5 of the design | PENDING |
| HW-FR7-010 | Reporter stack | After HW-FR7-003 and HW-FR7-006, search the log for `Reporter stack:` | The lowest value is at least 512 bytes, so no `W` stack line | PENDING |
| HW-FR7-011 | Firmware identity | Read the boot log | One `MAIN: Firmware:` line whose ELF SHA256 matches the flashed file | PENDING |

H5 closes its first client abruptly. Depending on which reaches the bridge first, the RST is seen by `recv()`, which ends the session as `socket error`, or by `send()`, which also counts `tcp_send_failures=1`. Either way, any bytes in flight count as loss. Only the isolation result decides H5. This matches the README.

## Status

**FR-7: PENDING.** Implemented and tested on the PC only.
