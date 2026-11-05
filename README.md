# ESP32 TCP-Serial Bridge

Firmware that turns an ESP32-Ethernet-Kit into a TCP-to-serial bridge. A client connects over Ethernet, and the bytes it sends and receives pass through UART1, so a serial device can be reached from the network.

```text
                 Ethernet                       UART1, 8N1, 115200 by default
TCP client  <---- port 4001 ---->  ESP32  <---- TX GPIO32 / RX GPIO33 ---->  Serial device
                                     |
                                     +---- UART0 console: logs and loss reports
```

## Behavior

- **Static IP:** `192.168.1.200`, TCP port `4001`, set in `idf.py menuconfig` under *TCP-Serial Bridge Configuration*.
- **UART1:** 8N1, no hardware flow control, 115200 baud by default. The baud rate is set in `idf.py menuconfig` under *TCP-Serial Bridge Configuration* → *UART Configuration* (9600 to 921600), and the boot log shows it: `MAIN: UART1 initialized successfully: 115200 baud, 8N1, no flow control`.
- **One client at a time:** a second client is closed immediately, and the first keeps working.
- **Flow control:** unread TCP data slows the client through its TCP window, so a fast client can't overrun the UART.
- **Session isolation:** UART data received with no client is discarded, so it never reaches the next client.
- **Recovery:** a client that accepts no data for 5 s is disconnected. The bridge also handles Ethernet link loss and returns without a restart. Each outage is numbered on UART0 (`Ethernet link down (#n)`, `Ethernet link restored after <x.x> s outage (#n)`, `Static IP restored after link recovery (#n)`).
- **Loss reporting:** one `LOSS:` line every 10 s on UART0. See [the FR-7 design](docs/fr7-data-loss-reporting.md).

## Requirements Status

Requirement text from *ESP32 Serial-to-TCP Bridge: Requirements v1.0*. Each status comes from the hardware validation record linked in the last column, and every **Validated** status cites hardware evidence there.

### Functional Requirements

| Requirement | Description | Hardware status | Tests | Record |
|---|---|---|---|---|
| FR-1 | Connect over Ethernet with a static IPv4 address, netmask and gateway set in menuconfig. No DHCP | **Validated** | Boot log, ping, H7, H8, H9 | [FR-1](docs/test-records/hw-validation-fr1.md) |
| FR-2 | TCP server on port 4001, with at most one active client at a time | **Validated** | H1, H5, manual `nc` | [FR-2](docs/test-records/hw-validation-fr2.md) |
| FR-3 | Bytes received from the client are transmitted on UART1 TX (GPIO32) | **Validated** | H2, H3, H4 | [FR-3](docs/test-records/hw-validation-fr3.md) |
| FR-4 | Bytes received on UART1 RX (GPIO33) are sent to the client | **Validated** | H2 to H6 | [FR-4](docs/test-records/hw-validation-fr4.md) |
| FR-5 | UART1 at 8N1, no hardware flow control, baud rate set in menuconfig (default 115200, optionally 921600) | Implemented, hardware check pending | H3, H4 (8N1), build at 115200 and 921600 | [FR-5](docs/test-records/hw-validation-fr5.md) |
| FR-6 | No lost, duplicated or reordered bytes in either direction, at those baud rates | **Validated** at 115200 | H2, H3, H4 | [FR-6](docs/test-records/hw-validation-fr6.md) |
| FR-7 | Data loss that cannot be avoided is detected, counted and reported on UART0 every 10 s | Partial | Host tests, HW-FR7-001 to 011 | [FR-7](docs/test-records/hw-validation-fr7.md) |
| FR-8 | A client that disconnects or times out, including a half-open connection, is detected within 30 s and its resources are freed | **Validated** | H6, H7, H8, H9, [TR-FR8-002](docs/testing/FR-8-TEST-REPORT.md) (keepalive, RST, idle, half-open, soak, pending data) | [FR-8](docs/test-records/hw-validation-fr8.md) |
| FR-9 | Recover from an Ethernet cable reconnection without a reboot | **Validated** | H9 (9 outages, 4 s to 110 s), H8 (216 s outage) | [FR-9](docs/test-records/hw-validation-fr9.md) |

**What is still open:**

| Requirement | Missing evidence | How to close it |
|---|---|---|
| FR-5 | The firmware with the menuconfig option (`c726a91`) has not yet run on hardware | Flash it, check the `UART1 initialized successfully: 115200 baud, 8N1, no flow control` boot line, and run `npm test` (automated, about 1 minute). 921600 is optional |
| FR-7 | HW-FR7-004 (link loss with data in flight), HW-FR7-006 (line errors) and HW-FR7-009 (break). 7 of the 11 plan tests have passed on hardware | Pull the cable during H4 (about 1 minute). 006 needs a USB-UART adapter on GPIO33, and 009 a resistor from GPIO33 to GND |

**Notes on the evidence:**
- **The jumper.** FR-3, FR-4 and FR-6 are verified through the GPIO32 to GPIO33 jumper at 115200 8N1, so the two directions are verified together.
- **FR-8 half-open timing.** The 30 s limit rests on the keepalive configuration read back on hardware (at most about 11 s) and the operator's observation of about 9 s. No timestamped measurement was retained (see the record).
- **FR-8 and FR-9 firmware.** Their evidence ran on three firmware builds (`23f991546`, `8d79dbe83` and `ce94f6077`). The code each test exercises is unchanged between that build and the current source, as each record explains.

### Non-Functional Requirements

NFR-1 to NFR-5 are defined in *Requirements v1.0*, but their text is not yet in this repository, so they have not been assessed. None is claimed as validated.

### Latest Results

| Run | Firmware | Result | Report |
|---|---|---|---|
| `npm test`, H1 to H5 (2026-11-05) | `ce94f6077` | **PASS**, 8 of 8. H3 and H4 SHA256 match, H4 11,509 bytes/s. H6 skipped | [automated-test-results-2026-11-05.md](docs/test-reports/automated-test-results-2026-11-05.md) |
| H9, 5 cycles (2026-11-05) | `ce94f6077` | **PASS**: 9 outages of 4.0 s to 109.9 s, static IP restored 9 of 9, no reset, all `LOSS:` reports at zero. Worst client acceptance 6.8 s from the operator's Enter | [H9 report](docs/test-reports/manual-h9-2026-11-05T14-51-27-137Z.md) |
| `npm run test:h6`, H6 (2026-11-05) | `ce94f6077` | **PASS**, 5 of 5. `client stalled` after 7.0 s, then `LOSS:` reported `tcp_send_failures=1`, `tcp_tx_bytes=2260` and `uart_rx_overflow=247`, and a new client was accepted | [automated-test-results-h6-2026-11-05.md](docs/test-reports/automated-test-results-h6-2026-11-05.md) |
| FR-8 test series (2026-11-04) | `8d79dbe83` | Keepalive, RST, 300 s idle, half-open detection, 200-cycle soak and pending data all observed. The report marks half-open timing as inconclusive (no timestamps) | [TR-FR8-002](docs/testing/FR-8-TEST-REPORT.md) |
| H8, 216 s outage (2026-11-02) | `23f991546` | Static IP restored, new client accepted, no reset | [log](docs/evidence/2026-11-02_h8_fw23f991546.log) |
| Host tests (2026-11-05) | Source `c726a91` | **PASS**: FR-7 62/0 and 59/0, bridge 90/0, FR-8 keepalive 25/0 | `bash tests/host/run.sh` |
| `npm test`, H1 to H5 (2026-11-03) | `0da27aa` | PASS | [automated-test-results.md](docs/test-reports/automated-test-results.md) |

The 2026-11-05 `npm test` and H6 reports don't name the firmware, because `BRIDGE_FIRMWARE` was not set. It is identified from the serial log of the same boot. The serial logs behind these results are kept in [`docs/evidence/`](docs/evidence/).

### Known Issues

- **`LOSS:` lines report detected loss only.** "No data loss" can appear after an abnormal session, because data held in lwIP buffers isn't visible to the firmware. Data still held by UART1 is counted.
- **H5 can end either way.** The close races with the echo, so the report may or may not show loss, and `tcp_send_failures` may be 0 or 1. Only the isolation result matters for H5.
- **921600 is not yet tested on hardware.** It builds, but FR-5 and FR-6 are only shown at 115200.
- **The FR-7 line-error and break counters** have only been exercised by the host tests.
- **Frame-error storms are capped.** Above 2,000 frame errors and breaks in one interval, their interrupts pause until the next report, and `rx_error_pauses` shows it. The counts for that interval are a lower bound.

## Hardware

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E |
| Bridge UART | UART1: TX GPIO32, RX GPIO33, 8N1, no flow control. Baud rate set in `idf.py menuconfig` → *TCP-Serial Bridge Configuration* → *UART Configuration* (default 115200, up to 921600) |
| Console | UART0, `/dev/ttyUSB1` on the test PC, 115200 baud |
| Test wiring | Jumper between GPIO32 and GPIO33, so TCP data echoes back to the client |

## Build and Flash

You need [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v6.1, with its environment loaded.

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB1 flash monitor
```

Then try it from a PC on the same network, with the jumper connected. Anything you type comes back:

```bash
ping 192.168.1.200
nc -v 192.168.1.200 4001
```

## Testing

There are three levels. Only the last two run against the board, and only results from real hardware count as validation.

| Level | What it checks | ESP32 needed |
|---|---|---|
| Host tests | Real firmware source built for the PC, against mocks | No |
| Automated integration tests | The running bridge, over TCP (H1 to H6) | Yes |
| Manual tests | Ethernet cable disconnection (H7, H8, H9), FR-8 half-open and RST tests, and the FR-7 hardware plan | Yes |

### Setup

You need Node.js 22.18 or later, Python 3 for H6, and `gcc` for the host tests.

```bash
npm install
```

For tests on the board:

1. Flash the firmware and note the ELF SHA256 from the `MAIN: Firmware:` boot line. A build from uncommitted changes also logs a warning, because its version doesn't identify it.
2. Connect the GPIO32 to GPIO33 jumper. Without it, every echo test fails.
3. Keep `idf.py -p /dev/ttyUSB1 monitor` open and save its log. Close reasons and `LOSS:` lines appear only there.
4. Close any other client, because the bridge accepts only one.

### How to Test Each Requirement

| Requirement | Command | Pass criteria |
|---|---|---|
| FR-1 | `ping 192.168.1.200`, then `npm run manual:h7` and `npm run manual:h8` | The static IP is set at boot and returns after the link does |
| FR-2 | `npm test` (H1) | Client accepted, second client closed, first client still works, slot released |
| FR-3, FR-4 | `npm test` (H2, H3, H4) | Text and binary data return unchanged and in order, at a rate consistent with the baud rate |
| FR-4 | `npm test` (H5) | The next client receives 0 bytes from a closed session, and its own session works |
| FR-4, FR-8 | `npm run test:h6` | A client that stops reading is disconnected, and a new one is accepted |
| FR-8 | Manual iptables, RST, idle, soak and pending-data tests ([FR-8 test report](docs/testing/FR-8-TEST-REPORT.md)) | A half-open client ends with `errno 113` within 30 s, a RST with `errno 104`, and a new client is accepted |
| FR-1, FR-8 | `npm run manual:h7` | The client session ends safely when the cable is pulled, and the bridge recovers |
| FR-1, FR-8 | `npm run manual:h8` | Same as H7, with an outage longer than 2 minutes |
| FR-5 | Boot log, then `npm test` (H3, H4) | The boot line shows the menuconfig baud rate with 8N1 and no flow control, and H4's rate matches it. For another rate, set it in menuconfig and run with `BRIDGE_BAUD=<rate>` |
| FR-6 | `npm test` (H2, H3, H4) | Byte counts and SHA256 match, in order, with no `LOSS:` line reporting loss |
| FR-9 | `npm run manual:h9`, and `H9_OUTAGE_S=130 npm run manual:h9 -- 1` for an outage past the 120 s lost-IP timer | Every cycle recovers without a reboot, with a client accepted within 10 s of reconnection (project criterion) |
| FR-7 | `npm run test:host` | All host checks pass |
| FR-7 | [Hardware plan](docs/test-records/hw-validation-fr7.md) | Each HW-FR7 result is recorded from the UART0 log |

Each record in `docs/test-records/` has a *How to Run These Tests* section with the exact commands, what to check in the serial log, and the pass criteria for every test.

The tests can't verify FR-3 and FR-4 separately, because the jumper connects them. The Jest suite can't verify FR-7, because reports appear only on UART0.

H6 floods the UART for up to 100 s and forces a disconnect. H7, H8 and H9 need someone to pull the cable, and the operator must confirm the serial log. See the [manual procedures](docs/test-reports/manual-test-procedures.md).

### Test Commands

| Command | What it runs | Time |
|---|---|---|
| `npm test` | H1 to H5, with H6 skipped | Under 1 minute |
| `npm run test:h6` | H6 | Up to about 2 minutes |
| `npm run manual:h7` | H7, assisted | Operator-paced |
| `npm run manual:h8` | H8, assisted | Over 2 minutes |
| `npm run manual:h9` | H9, assisted: repeated link recovery, timed (FR-9) | Operator-paced, about 30 s per cycle |
| `npm run test:host` | Host tests with ASan and UBSan, including the real 10 s report period | About 3 minutes |
| `bash tests/host/run.sh quick` | Host tests without sanitizers or the real 10 s period | About 2 minutes |
| `bash tests/host/run.sh tsan` | Host tests with ThreadSanitizer | About 3 minutes |
| `npm run typecheck` | TypeScript type check | Seconds |

Timing checks in the host tests can fail on a heavily loaded machine, so run them on an idle one before treating a failure as a firmware problem.

### Identifying the Firmware and Keeping Reports

Use the ELF SHA256 as the firmware ID, because two different builds can show the same version and compile time. Reports record what you declare here, and they don't read it from the device:

```bash
BRIDGE_FIRMWARE=<ELF SHA256> npm test
```

`npm test` and `npm run test:h6` overwrite the reports in `docs/test-reports/`. To keep an earlier report, write to a new file:

```bash
BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test

RUN_H6=true BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new h6 file>.md \
  npx jest tests/integration/stalled-client.test.ts
```

| Overall result | Meaning |
|---|---|
| `PASS` | Every test that ran passed |
| `FAIL` | A test ran and the bridge didn't meet its expected result |
| `BLOCKED` | Nothing was tested, because the ESP32 is unreachable or another client holds the slot |

The host and port can be changed with `BRIDGE_HOST` and `BRIDGE_PORT`, and the expected UART rate with `BRIDGE_BAUD` (default 115200). For the full test guide, see [`tests/README.md`](tests/README.md).

## Project Layout

```text
main/
  main.c              Startup order, optional UART loopback test
  bridge/             Bridge task, loss counters and loss reporter
  network/            Ethernet, static IP and TCP server
  uart/               UART1 driver wrapper (baud rate from menuconfig)
tests/
  host/               Firmware logic on the PC, with mocks
  integration/        H1 to H6 over TCP
  manual/             H7, H8 and H9 assisted scripts
  support/            Shared client, configuration and report generator
docs/
  fr7-data-loss-reporting.md   FR-7 design
  test-records/       Hardware validation record for each requirement, with how to run its tests
  test-reports/       Generated test reports and manual procedures
  testing/            Per-requirement test reports (FR-8)
  evidence/           Serial logs cited by the records
```
