# ESP32 TCP-Serial Bridge

Firmware that turns an ESP32-Ethernet-Kit into a TCP-to-serial bridge. A client connects over Ethernet, and the bytes it sends and receives pass through UART1, so a serial device can be reached from the network.

```text
                 Ethernet                       UART1, 115200 8N1
TCP client  <---- port 4001 ---->  ESP32  <---- TX GPIO32 / RX GPIO33 ---->  Serial device
                                     |
                                     +---- UART0 console: logs and loss reports
```

## Behavior

- **Static IP:** `192.168.1.200`, TCP port `4001`, set in `idf.py menuconfig` under *TCP-Serial Bridge Configuration*.
- **One client at a time:** a second client is closed immediately, and the first keeps working.
- **Flow control:** unread TCP data slows the client through its TCP window, so a fast client can't overrun the 115200 baud UART.
- **Session isolation:** UART data received with no client is discarded, so it never reaches the next client.
- **Recovery:** a client that accepts no data for 5 s is disconnected. The bridge also handles Ethernet link loss and returns without a restart. Each outage is numbered on UART0 (`Ethernet link down (#n)`, `Ethernet link restored after <x.x> s outage (#n)`, `Static IP restored after link recovery (#n)`).
- **Loss reporting:** one `LOSS:` line every 10 s on UART0. See [the FR-7 design](docs/fr7-data-loss-reporting.md).

## Requirements Status

Requirement text from *ESP32 Serial-to-TCP Bridge: Requirements v1.0*.

| Requirement | Description | Hardware status | Tests | Record |
|---|---|---|---|---|
| FR-1 | Connect over Ethernet with a static IPv4 address, netmask and gateway set in menuconfig. No DHCP | **Validated** | H7, H8, ping | [FR-1](docs/test-records/hw-validation-fr1.md) |
| FR-2 | TCP server on port 4001, with at most one active client at a time | **Validated** | H1 | [FR-2](docs/test-records/hw-validation-fr2.md) |
| FR-3 | Bytes received from the client are transmitted on UART1 TX (GPIO32) | Partial | H2, H3, H4 | [FR-3](docs/test-records/hw-validation-fr3.md) |
| FR-4 | Bytes received on UART1 RX (GPIO33) are sent to the client | Partial | H2 to H6 | [FR-4](docs/test-records/hw-validation-fr4.md) |
| FR-5 | UART1 at 8N1, no hardware flow control, baud rate set in menuconfig (default 115200, optionally 921600) | Pending | Not defined | [FR-5](docs/test-records/hw-validation-fr5.md) |
| FR-6 | No lost, duplicated or reordered bytes in either direction, at those baud rates | Pending | Not defined | [FR-6](docs/test-records/hw-validation-fr6.md) |
| FR-7 | Data loss that cannot be avoided is detected, counted and reported on UART0 every 10 s | **Pending** | Host tests, HW-FR7-001 to 011 | [FR-7](docs/test-records/hw-validation-fr7.md) |
| FR-8 | A client that disconnects or times out, including a half-open connection, is detected within 30 s and its resources are freed | Partial | H6, H7, H8 | [FR-8](docs/test-records/hw-validation-fr8.md) |
| FR-9 | Recover from an Ethernet cable reconnection without a reboot | Partial | H9 | [H9 report](docs/test-reports/manual-h9-2026-11-05T14-51-27-137Z.md) |

The status comes from the records in [`docs/test-records/`](docs/test-records/), and for FR-9 from the H9 report, because FR-9 has no record yet. The FR-3 and FR-4 records don't yet cite the automated run below. The FR-5 and FR-6 records predate the requirement text above.

**Latest automated run:** H1 to H5 passed on firmware `23f991546`, and H4 measured 11,517 bytes/s with matching SHA256 hashes. See [the report](docs/test-reports/automated-test-results.md). H6 also passed, but [its report](docs/test-reports/automated-test-results-h6.md) doesn't identify the firmware. That build predates FR-7, so these results don't cover the current code. Firmware `f10f8b076` (FR-7 before the audit corrections) has no hardware results.

**Latest manual run (H9, FR-9):** on firmware `ce94f6077`, the bridge recovered from 9 Ethernet outages of 4.0 s to 109.9 s without a reset. It restored the static IP every time, and every `LOSS:` report showed zero. All 5 cycles passed. In cycles 2 to 5, a client was accepted within 6.8 s, measured from the operator's Enter. The result is PASS for the scenarios exercised: no outage exceeded 120 s, so the ESP-NETIF lost-IP path is untested, and the time from physical reconnection was not reliably measured. See [the H9 report](docs/test-reports/manual-h9-2026-11-05T14-51-27-137Z.md).

### Known Issues

- **`LOSS:` lines report detected loss only.** "No data loss" can appear after an abnormal session, because data held in lwIP buffers isn't visible to the firmware. Data still held by UART1 is counted.
- **H5 can end either way.** The close races with the echo, so the report may or may not show loss, and `tcp_send_failures` may be 0 or 1. Only the isolation result matters for H5.
- **Frame-error storms are capped.** Above 2,000 frame errors and breaks in one interval, their interrupts pause until the next report, and `rx_error_pauses` shows it. The counts for that interval are a lower bound.

## Hardware

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E |
| Bridge UART | UART1: TX GPIO32, RX GPIO33, 115200 8N1 |
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
| Manual tests | Ethernet cable disconnection (H7, H8, H9) and the FR-7 hardware plan | Yes |

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
| FR-1, FR-8 | `npm run manual:h7` | The client session ends safely when the cable is pulled, and the bridge recovers |
| FR-1, FR-8 | `npm run manual:h8` | Same as H7, with an outage longer than 2 minutes |
| FR-9 | `npm run manual:h9` | Every cycle recovers without a reboot, with a client accepted within 10 s of reconnection (project criterion) |
| FR-7 | `npm run test:host` | All host checks pass |
| FR-7 | [Hardware plan](docs/test-records/hw-validation-fr7.md) | Each HW-FR7 result is recorded from the UART0 log |

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

The host and port can be changed with `BRIDGE_HOST` and `BRIDGE_PORT`. For the full test guide, see [`tests/README.md`](tests/README.md).

## Project Layout

```text
main/
  main.c              Startup order, optional UART loopback test
  bridge/             Bridge task, loss counters and loss reporter
  network/            Ethernet, static IP and TCP server
  uart/               UART1 driver wrapper
tests/
  host/               Firmware logic on the PC, with mocks
  integration/        H1 to H6 over TCP
  manual/             H7, H8 and H9 assisted scripts
  support/            Shared client, configuration and report generator
docs/
  fr7-data-loss-reporting.md   FR-7 design
  test-records/       Hardware validation record for each requirement
  test-reports/       Test results and manual procedures
```
