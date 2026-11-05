# Hardware Validation Record: FR-7

| Field | Value |
|---|---|
| Record ID | HW-VAL-007 |
| Requirement | FR-7: The firmware must report detected data loss every 10 seconds through the UART0 console. |
| Project | ESP32 TCP Serial Bridge |
| Status | **PARTIAL: 3 of 11 hardware tests passed and 1 partly (2026-11-05, firmware `ce94f6077`); the rest pending** |
| Requirement text | Any data loss that cannot be avoided shall be detected, counted, and reported on the console (UART0) every 10 seconds. (*Requirements v1.0*) |
| Implementation | `docs/fr7-data-loss-reporting.md` |

## Purpose and Scope

This record defines the hardware validation of FR-7. Results are added here as the tests are run on the ESP32. Tests on the PC are not hardware validation. HW-FR7-001, 002 and 011 have passed on hardware, and HW-FR7-008 has partly passed. The rest have not been run.

The plan covers the firmware with the P0 audit corrections: frame errors (M1), event queue drops (M2), session-end accounting (M3), and L1 to L6. Firmware `f10f8b076` predates them and is not a candidate for this record.

## Prerequisites

1. Flash the firmware under test. Record the `MAIN: Firmware:` line from the boot log, and use its ELF SHA256 as the firmware ID. If the boot log warns that it was built from uncommitted changes, the version alone does not identify it.
2. Keep the GPIO32 to GPIO33 jumper connected, except where a test says otherwise.
3. Keep `idf.py -p /dev/ttyUSB1 monitor` open from boot, and save its log.

## Planned Tests

| Test ID | Test | Procedure | Expected | Result |
|---|---|---|---|---|
| HW-FR7-001 | Report cadence | Boot and leave idle for 60 s | One `LOSS:` line about every 10 s, at `I` level, with `no data loss` and every field 0 | **PASS** (E11): 10 reports exactly 10,000 ms apart, 09:27:47 to 09:29:17, before any client, all `I` level and all 0. The same cadence held for 364 reports over the next hour |
| HW-FR7-002 | No false positives | Run `npm test` (H1 to H5, with H5 noted below) | Sessions with clean closes report no loss | **PASS** (E13, E14): every report was `no data loss` except the one covering H5's abrupt close, as the note below expects |
| HW-FR7-003 | Stalled client | Run `npm run test:h6` | A `W` line with `tcp_send_failures=1` and `tcp_tx_bytes` of at least 2,048, because the full RX ring buffer is now counted at session end. `rx_buffer_full` and `uart_rx_overflow` above 0 are expected from the jumper echo. `no_client_bytes` must not hold the ring buffer contents | PENDING |
| HW-FR7-004 | Link loss | Start a continuous transfer, for example H4, then pull the cable while data is flowing | A `W` line with non-zero `tcp_tx_bytes` or `uart_tx_bytes`. With no data in flight at the moment of the pull, `no data loss` is correct, so the test must be repeated | PENDING |
| HW-FR7-005 | Data with no client | Feed GPIO33 from an external 3.3 V source at 115200 8N1 with no client | `no_client_bytes` grows, and the line says `no data loss` | PENDING |
| HW-FR7-006 | Line errors | Feed GPIO33 at a wrong baud rate | `uart_rx_errors` above 0. A sustained stream at a wrong rate may also show `rx_error_pauses=1`, and the next interval must report again | PENDING |
| HW-FR7-007 | Regression | Compare a full `npm test` and H6 run with the earlier reports | Same results, with no `task_wdt` | PENDING |
| HW-FR7-008 | Event queue under load | During H4 (256,000 bytes) and H6, read every `LOSS:` line | `event_queue_drops=0`. Any drop is still counted in the other fields, but means that the reporter fell behind | **PARTIAL** (E14): `event_queue_drops=0` in every report during H4. H6 not run |
| HW-FR7-009 | Break | With no client, hold GPIO33 low for about 1 s through a resistor, then release it | `uart_breaks` above 0. Record whether `uart_rx_errors` also rises, which is Limitation 5 of the design | PENDING |
| HW-FR7-010 | Reporter stack | After HW-FR7-003 and HW-FR7-006, search the log for `Reporter stack:` | The lowest value is at least 512 bytes, so no `W` stack line | PENDING |
| HW-FR7-011 | Firmware identity | Read the boot log | One `MAIN: Firmware:` line whose ELF SHA256 matches the flashed file | **PASS** (E9, E11): `MAIN: Firmware: version 63cbb71-dirty, ELF SHA256 ce94f6077`, matching the flashed ELF `ce94f607…aa533` |

H5 closes its first client abruptly. Depending on which reaches the bridge first, the RST is seen by `recv()`, which ends the session as `socket error`, or by `send()`, which also counts `tcp_send_failures=1`. Either way, any bytes in flight count as loss. Only the isolation result decides H5. This matches the README.

## How to Run These Tests

**Host tests (no ESP32 needed):**

```bash
npm run test:host                 # ASan and UBSan, including the real 10 s report period, about 3 minutes
bash tests/host/run.sh quick      # faster, without sanitizers
bash tests/host/run.sh tsan       # ThreadSanitizer
```

They pass when the script ends with every check passing. Run them on an idle machine, because the timing checks can fail under load.

**Hardware tests (HW-FR7-001 to 011):**

1. Flash the firmware. On the PC, run `sha256sum build/tcp_serial_bridge.elf` for HW-FR7-011.
2. Open the serial monitor and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr7-test.log`. Every FR-7 result is read from this log.
3. Run the procedure in the table above for each test. The commands are:
   - HW-FR7-002 and 007: `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test`.
   - HW-FR7-003 and 008: `npm run test:h6`.
   - HW-FR7-004: run `npm test -- -t "H4:"` and pull the cable while it runs.
   - HW-FR7-005, 006 and 009: remove the jumper, and feed GPIO33 from a 3.3 V USB-UART adapter, for example `picocom -b 115200 /dev/ttyUSB<n>` (`-b 57600` for HW-FR7-006).
4. Pull the results out of the log:

```bash
grep "LOSS:" fr7-test.log                          # every report
grep "LOSS:" fr7-test.log | grep -v "no data loss" # only reports with loss or attention fields
grep -E "W \([0-9]+\) LOSS:|Reporter stack" fr7-test.log
grep "MAIN: Firmware:" fr7-test.log                # HW-FR7-011
```

Each test passes when its `LOSS:` lines show the values in the "Expected" column above.

## Status

**FR-7: PARTIAL.** On hardware (firmware `ce94f6077`), the 10 s report cadence, the absence of false positives on clean sessions and the firmware identity line are verified, and the event queue kept up during H4.

The detection of each loss type is not yet verified on hardware: stalled client, link loss with data in flight, data with no client, line errors and breaks (HW-FR7-003 to 006 and 009). Neither are the regression run with H6 (HW-FR7-007) or the reporter stack after load (HW-FR7-010). HW-FR7-008 still needs H6.

### Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E9 | Captured tool output | Flash and verification of `ce94f6077`, recorded in [HW-VAL-009](hw-validation-fr9.md) | Full ELF SHA256 read back from flash |
| E11 | Captured serial log | `fr9-h9-test.log`, from boot at 09:27:36 | One continuous boot. Not tracked by Git |
| E13 | Test report | [automated-test-results-2026-11-05.md](../test-reports/automated-test-results-2026-11-05.md) | `npm test` H1 to H5, 8 passed, H6 skipped |
| E14 | Captured serial log | `fr9-h9-test.log`, 12:16:30 to 12:17:00 | The `LOSS:` lines during E13, same boot as E11 |
