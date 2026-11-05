# Hardware Validation Record: FR-7

| Field | Value |
|---|---|
| Record ID | HW-VAL-007 |
| Requirement | FR-7: The firmware must report detected data loss every 10 seconds through the UART0 console. |
| Project | ESP32 TCP Serial Bridge |
| Status | **PARTIAL: 7 of 11 hardware tests passed, 1 partly (010); 3 have no hardware evidence (004, 006, 009)** |
| Requirement text | Any data loss that cannot be avoided shall be detected, counted, and reported on the console (UART0) every 10 seconds. (*Requirements v1.0*) |
| Implementation | `docs/fr7-data-loss-reporting.md` |

## Purpose and Scope

This record defines the hardware validation of FR-7. Results are added here as the tests are run on the ESP32. Tests on the PC are not hardware validation. HW-FR7-001, 002, 003, 005, 007, 008 and 011 have passed on hardware, and HW-FR7-010 has partly passed. HW-FR7-004, 006 and 009 have no hardware evidence.

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
| HW-FR7-003 | Stalled client | Run `npm run test:h6` | A `W` line with `tcp_send_failures=1` and `tcp_tx_bytes` of at least 2,048, because the full RX ring buffer is now counted at session end. `rx_buffer_full` and `uart_rx_overflow` above 0 are expected from the jumper echo. `no_client_bytes` must not hold the ring buffer contents | **PASS** (E21, `ce94f6077`): `client stalled`, then `W LOSS: DATA LOSS: uart_rx_overflow=247 ... tcp_send_failures=1 tcp_tx_bytes=2260 ... no_client_bytes=0 ... rx_buffer_full=1 ... event_queue_drops=0`. Earlier runs on `1044bbf1b` (E17) showed the same pattern |
| HW-FR7-004 | Link loss | Start a continuous transfer, for example H4, then pull the cable while data is flowing | A `W` line with non-zero `tcp_tx_bytes` or `uart_tx_bytes`. With no data in flight at the moment of the pull, `no data loss` is correct, so the test must be repeated | PENDING: no retained log shows a link loss with data in flight |
| HW-FR7-005 | Data with no client | Feed GPIO33 from an external 3.3 V source at 115200 8N1 with no client | `no_client_bytes` grows, and the line says `no data loss` | **PASS**, by equivalent evidence (R1 Test H, firmware `8d79dbe83`): bytes that arrived after the client closed were counted as `no_client_bytes=64`, with `no data loss`. The external-source procedure was not run |
| HW-FR7-006 | Line errors | Feed GPIO33 at a wrong baud rate | `uart_rx_errors` above 0. A sustained stream at a wrong rate may also show `rx_error_pauses=1`, and the next interval must report again | PENDING: needs a USB-UART adapter on GPIO33. No retained log shows `uart_rx_errors` above 0 |
| HW-FR7-007 | Regression | Compare a full `npm test` and H6 run with the earlier reports | Same results, with no `task_wdt` | **PASS** (E13, E21): `npm test` and H6 on `ce94f6077` match the earlier reports, with no `task_wdt` |
| HW-FR7-008 | Event queue under load | During H4 (256,000 bytes) and H6, read every `LOSS:` line | `event_queue_drops=0`. Any drop is still counted in the other fields, but means that the reporter fell behind | **PASS** (E14, E21): `event_queue_drops=0` in every report during H4 and H6 |
| HW-FR7-009 | Break | With no client, hold GPIO33 low for about 1 s through a resistor, then release it | `uart_breaks` above 0. Record whether `uart_rx_errors` also rises, which is Limitation 5 of the design | PENDING: needs a resistor from GPIO33 to GND. No retained log shows `uart_breaks` above 0 |
| HW-FR7-010 | Reporter stack | After HW-FR7-003 and HW-FR7-006, search the log for `Reporter stack:` | The lowest value is at least 512 bytes, so no `W` stack line | **PARTIAL**: lowest 2,028 bytes unused on `8d79dbe83` and `ce94f6077` (after H1 to H5, H9 and the FR-8 tests), and 1,932 on `1044bbf1b` after H6. All are above 512, including after HW-FR7-003 on `ce94f6077`. HW-FR7-006 has not run |
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

**FR-7: PARTIAL.** On hardware, these are verified:
- the 10 s report cadence;
- no false positives on clean sessions;
- the counting of data that arrives with no client;
- the firmware identity line;
- stalled-client loss counting (H6);
- the event queue keeping up during H4 and H6;
- reporter stack margin.

On the PC, the host tests pass (FR-7 reporter at 200 ms, 62/0, and at the real 10 s period, 59/0).

**Not yet verified on the current FR-7 code:**

| Test | What is missing | How to close it |
|---|---|---|
| HW-FR7-004 | Link loss with data in flight | Pull the cable during H4, about 1 minute |
| HW-FR7-006 | Line errors | Needs a 3.3 V USB-UART adapter on GPIO33 |
| HW-FR7-009 | Break | Needs a resistor from GPIO33 to GND |
| HW-FR7-010 | Reporter stack after HW-FR7-006 | Read `Reporter stack:` after HW-FR7-006 |

### Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E9 | Captured tool output | Flash and verification of `ce94f6077`, recorded in [HW-VAL-009](hw-validation-fr9.md) | Full ELF SHA256 read back from flash |
| E11 | Captured serial log | `fr9-h9-test.log` (now [2026-11-05_monitor-session_fwce94f6077.log](../evidence/2026-11-05_monitor-session_fwce94f6077.log)), from boot at 09:27:36 | One continuous boot. Preserved in [docs/evidence/](../evidence/2026-11-05_h9-and-npm-test_fwce94f6077.log) |
| E13 | Test report | [automated-test-results-2026-11-05.md](../test-reports/automated-test-results-2026-11-05.md) | `npm test` H1 to H5, 8 passed, H6 skipped |
| E14 | Captured serial log | `fr9-h9-test.log` (now [2026-11-05_monitor-session_fwce94f6077.log](../evidence/2026-11-05_monitor-session_fwce94f6077.log)), 12:16:30 to 12:17:00, preserved in [docs/evidence/](../evidence/2026-11-05_h9-and-npm-test_fwce94f6077.log) | The `LOSS:` lines during E13, same boot as E11 |
| R1 | Test report | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md), firmware `8d79dbe83` | Test H: graceful close with 95 bytes pending, `session_bytes=31` and `no_client_bytes=64`, `no data loss` |
| E21 | Test report and serial log | [automated-test-results-h6-2026-11-05.md](../test-reports/automated-test-results-h6-2026-11-05.md) and [2026-11-05_h6_fwce94f6077.log](../evidence/2026-11-05_h6_fwce94f6077.log), firmware `ce94f6077`, H6 finished 2026-11-05T18:03:48Z | 5 of 5 passed. Same boot as E11, no reset since 09:27:36 |
| E23 | Test report and serial log | [921600 report](../test-reports/automated-test-results-921600-2026-11-05.md) and [log](../evidence/2026-11-05_921600_fwc903d6e80.log), 921600 build of the current source | Supporting evidence, not a plan test. Real loss on the wire (H3 short by 9 bytes, H4 by 20) was detected and reported as `W LOSS: DATA LOSS: uart_rx_overflow=2` and `=1` |
| E17 | Captured serial logs | `build/log/idf_py_stdout_output_48554`, `_5503` and `_10515`, firmware `1044bbf1b` (`0da27aa-dirty`, built 2026-11-03 11:22) | Built before the FR-7 commit `66840b3`, so its FR-7 source is not proven identical to the current one. Used as supporting evidence only |

FR-7 code (`main/bridge/`, `main/uart/`) is unchanged between `63cbb71` and the current source, so R1 and E11 to E14 apply to it. The FR-5 change (`c726a91`) only replaces the hardcoded baud rate, with the same values at 115200.
