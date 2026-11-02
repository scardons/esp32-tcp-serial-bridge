# Integration Tests

These tests run on a PC and talk to the ESP32 TCP-Serial Bridge over TCP. They do not flash or modify the firmware.

## Prerequisites

1. Node.js 22.18 or later, Python 3 for H6, and the dev dependencies:

   ```bash
   sudo apt install nodejs npm    # or use nvm, if your distribution ships an older version
   npm install
   ```

2. The ESP32-Ethernet-Kit is running the firmware under test, and its Ethernet link is up.
3. GPIO32 and GPIO33 are connected with a jumper. Without it, every echo test fails.
4. The PC can reach the ESP32. Check it with `ping 192.168.1.200`.
5. No other client is connected, because the bridge accepts only one.

## Commands

| Command | What it runs | Disruptive |
|---|---|---|
| `npm test` | H1 to H5, automated. H6 appears as skipped | No |
| `npm run test:h6` | H6, automated | Yes: forces a disconnect after flooding the UART for up to 100 s |
| `npm run manual:h7` | H7, operator-assisted | Yes: needs the cable to be pulled |
| `npm run manual:h8` | H8, operator-assisted | Yes: needs an outage longer than 2 minutes |
| `npm run test:host` | Firmware logic on the PC, with mocks. It needs `gcc`, not an ESP32 | No |
| `npm run typecheck` | TypeScript type check, no device needed | No |

Close other clients first. Keep the serial monitor open for H6, H7 and H8, because close reasons appear only there.

## Tests Without an ESP32

`tests/host/` builds the real `bridge.c`, `loss_stats.c`, `loss_reporter.c` and `uart_driver.c` for the PC, against mocks.

| Command | Coverage | Time |
|---|---|---|
| `bash tests/host/run.sh` | All suites with ASan and UBSan, including the real 10-second report period | About 3 minutes |
| `bash tests/host/run.sh quick` | Without sanitizers, skipping the 10-second period | About 2 minutes |
| `bash tests/host/run.sh tsan` | Data races, with ThreadSanitizer | About 3 minutes |

These tests check firmware logic. They are not hardware validation.

## Configuration

| Variable | Default | Purpose |
|---|---|---|
| `BRIDGE_HOST` | `192.168.1.200` | ESP32 address |
| `BRIDGE_PORT` | `4001` | TCP port |
| `BRIDGE_CONNECT_TIMEOUT_MS` | `3000` | TCP connection timeout |
| `BRIDGE_FIRMWARE` | Not set | Firmware ID written in the reports. It is declared by you, not read from the device |
| `BRIDGE_REPORT_PATH` | `docs/test-reports/automated-test-results.md` | Report location |

Example:

```bash
BRIDGE_FIRMWARE=23f991546 npm test
```

Use the `ELF file SHA256` value from the boot log as the firmware ID.

## Test Map

| Test | File | Requirement | Pass criteria |
|---|---|---|---|
| H1 | `integration/tcp-connection.test.ts` | FR-2 | Client accepted, second client closed with no data, first client still works, slot released |
| H2 | `integration/text-echo.test.ts` | FR-3, FR-4 | Text returns unchanged and in order |
| H3 | `integration/binary-integrity.test.ts` | FR-3, FR-4 | 20,480 bytes with all 256 values are identical; duration is consistent with the baud rate |
| H4 | `integration/throughput.test.ts` | FR-3, FR-4 | 256,000 bytes are identical; duration is consistent with the baud rate |
| H5 | `integration/session-isolation.test.ts` | FR-4 | Next client receives 0 bytes in 1.5 s, and its session then works |
| H6 | `integration/stalled-client.test.ts` | FR-4, FR-8 | Server ends the connection of a client that stops reading, then accepts a new one |
| H7, H8 | `manual/link-recovery.ts` | FR-1, FR-8 | See `docs/test-reports/manual-test-procedures.md` |

## Reading the Result

| Overall result | Meaning |
|---|---|
| `PASS` | Every test that ran passed |
| `FAIL` | A test ran and the bridge did not meet its expected result |
| `BLOCKED` | Nothing was tested, because the ESP32 is unreachable or another client holds the slot. This is not a firmware result |

Jest exits with an error for both `FAIL` and `BLOCKED`.

## Reports

`npm test` rewrites `docs/test-reports/automated-test-results.md`. `npm run test:h6` writes `automated-test-results-h6.md`, so it never replaces the main report. Manual runs create a separate `manual-h7-*.md` or `manual-h8-*.md` file.

Raw measurements go to `test-results/`, which Git ignores.

## Limitations

- The tests see TCP only. They cannot read the serial log or observe the UART pins.
- Echo tests exercise FR-3 and FR-4 together, through the jumper.
- H6 cannot read the `client stalled` reason, and buffers between the bridge and the client delay the stall, so its duration is not the 5-second timeout. Confirm the reason in the serial monitor.
- H6 uses `support/stalled_client.py`, because Node.js cannot set `SO_RCVBUF` on a TCP socket.
- A client cannot tell a rejection from any other immediate close. H1 confirms the behavior, and the serial log confirms the reason.
- The tests do not create half-open connections.
