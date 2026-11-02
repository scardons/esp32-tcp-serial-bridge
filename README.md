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
- **Recovery:** a client that accepts no data for 5 s is disconnected. The bridge also handles Ethernet link loss and returns without a restart.
- **Loss reporting:** one `LOSS:` line every 10 s on UART0. See [the FR-7 design](docs/fr7-data-loss-reporting.md).

## Requirements Status

| Requirement | Description | Hardware status | Tests | Record |
|---|---|---|---|---|
| FR-1 | Static IPv4 Ethernet configuration | **Validated** | H7, H8, ping | [FR-1](docs/test-records/hw-validation-fr1.md) |
| FR-2 | TCP server with a single active client | **Validated** | H1 | [FR-2](docs/test-records/hw-validation-fr2.md) |
| FR-3 | TCP client data goes out on UART1 TX | Partial | H2, H3, H4 | [FR-3](docs/test-records/hw-validation-fr3.md) |
| FR-4 | UART1 RX data goes to the active client | Partial | H2 to H6 | [FR-4](docs/test-records/hw-validation-fr4.md) |
| FR-5 | Text not available in this repository | Pending | Not defined | [FR-5](docs/test-records/hw-validation-fr5.md) |
| FR-6 | Text not available in this repository | Pending | Not defined | [FR-6](docs/test-records/hw-validation-fr6.md) |
| FR-7 | Report detected data loss every 10 s on UART0 | **Pending** | Host tests, HW-FR7-001 to 006 | [FR-7](docs/test-records/hw-validation-fr7.md) |
| FR-8 | Half-open connections and Ethernet disconnection (from project notes) | Partial | H6, H7, H8 | [FR-8](docs/test-records/hw-validation-fr8.md) |

The status comes from the records in [`docs/test-records/`](docs/test-records/). The FR-3 and FR-4 records don't yet cite the automated run below.

**Latest automated run:** H1 to H5 passed on firmware `23f991546`, and H4 measured 11,517 bytes/s with matching SHA256 hashes. See [the report](docs/test-reports/automated-test-results.md). H6 also passed, but [its report](docs/test-reports/automated-test-results-h6.md) doesn't identify the firmware. That build predates FR-7, so these results don't cover the current code.

### Known Issues

- **Frame errors aren't detected.** The UART driver's default interrupts don't include them, so `uart_rx_errors` stays at 0 and HW-FR7-006 will fail.
- **`LOSS:` lines report detected loss only.** "No data loss" can appear after an abnormal session, because data held in lwIP buffers isn't visible to the firmware.
- **H5 can end either way.** The close races with the echo, so the report may or may not show loss. Only the isolation result matters for H5.

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
| Manual tests | Ethernet cable disconnection (H7, H8) and the FR-7 hardware plan | Yes |

### Setup

You need Node.js 22.18 or later, Python 3 for H6, and `gcc` for the host tests.

```bash
npm install
```

For tests on the board:

1. Flash the firmware and note the `ELF file SHA256` from the boot log.
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
| FR-7 | `npm run test:host` | All host checks pass |
| FR-7 | [Hardware plan](docs/test-records/hw-validation-fr7.md) | Each HW-FR7 result is recorded from the UART0 log |

The tests can't verify FR-3 and FR-4 separately, because the jumper connects them. The Jest suite can't verify FR-7, because reports appear only on UART0.

H6 floods the UART for up to 100 s and forces a disconnect. H7 and H8 need someone to pull the cable, and the operator must confirm the serial log. See the [manual procedures](docs/test-reports/manual-test-procedures.md).

### Test Commands

| Command | What it runs | Time |
|---|---|---|
| `npm test` | H1 to H5, with H6 skipped | Under 1 minute |
| `npm run test:h6` | H6 | Up to about 2 minutes |
| `npm run manual:h7` | H7, assisted | Operator-paced |
| `npm run manual:h8` | H8, assisted | Over 2 minutes |
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
  manual/             H7 and H8 assisted scripts
  support/            Shared client, configuration and report generator
docs/
  fr7-data-loss-reporting.md   FR-7 design
  test-records/       Hardware validation record for each requirement
  test-reports/       Test results and manual procedures
```
