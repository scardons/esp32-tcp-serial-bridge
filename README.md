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
- **UART1:** 8N1, no hardware flow control, 115200 baud by default. The baud rate is set in `idf.py menuconfig` under *TCP-Serial Bridge Configuration* → *UART Configuration* (9600 to 921600; only 115200 is validated), and the boot log shows it: `MAIN: UART1 initialized successfully: 115200 baud, 8N1, no flow control`.
- **One client at a time:** a second client is closed immediately, and the first keeps working.
- **Flow control:** unread TCP data slows the client through its TCP window, so a fast client can't overrun the UART.
- **Session isolation:** UART data received with no client is discarded, so it never reaches the next client.
- **Recovery:** a client that accepts no data for 5 s is disconnected. The bridge also handles Ethernet link loss and returns without a restart. Each outage is numbered on UART0 (`Ethernet link down (#n)`, `Ethernet link restored after <x.x> s outage (#n)`, `Static IP restored after link recovery (#n)`).
- **Loss reporting:** one `LOSS:` line every 10 s on UART0. See [the FR-7 design](docs/fr7-data-loss-reporting.md).

## Design Decisions

| Decision | Reason |
|---|---|
| **One `bridge` task** moves data both ways. It waits on `select()` for TCP input (10 ms timeout) and sends with non-blocking `send()` | One owner for the session state and both buffers, so they need no locking. The timeout also paces send retries, so a socket that accepts nothing cannot cause a busy loop |
| **Two static 256-byte buffers**, one per direction, and no `malloc` in application code | Memory use is fixed and visible at build time ([Memory and Buffers](#memory-and-buffers)), with nothing to leak |
| **One client at a time.** An atomic `session busy` flag and a one-entry queue hand a socket to the bridge, and the TCP server closes any other client at once | The requirement (FR-2), and a serial line has only one owner. The first client keeps working |
| **Backpressure instead of buffering.** The bridge writes to UART1 only within a 100 ms TX budget (87 µs per byte at 115200) and otherwise stops reading TCP, so the TCP window slows the client | `uart_write_bytes()` spins when its TX ring is full. Keeping the queued time under 100 ms means it never spins, and a fast client cannot overrun the UART |
| **Session isolation.** UART input is discarded while no client is connected, and what UART1 still holds when a session ends is drained and counted | Data from one session never reaches the next client (H5) |
| **A client that accepts nothing for 5 s is aborted** with `SO_LINGER {1, 0}` | A stalled client would otherwise hold the only slot. A normal close would queue a FIN behind unsent data that never leaves |
| **TCP keepalive (idle 5 s, interval 2 s, 3 probes)** is set and read back on every client, and a client is rejected if any value can't be confirmed | Detects half-open clients in about 11 s, within FR-8's 30 s, instead of lwIP's two-hour default |
| **Static IPv4 with the DHCP client stopped.** A link down ends the session, and the static address is applied again when the link returns | FR-1 and FR-9. Each outage is numbered in the log, with its duration and the IP check |
| **Loss counters are lock-free atomics**, reported by their own task (priority 3, below the bridge's 5) every 10 s | They are counted from the bridge and the UART interrupt without locks, and logging cannot delay the data path (FR-7) |
| **Ethernet starts even if the bridge cannot** | The board stays reachable for diagnosis. Only the TCP server depends on the bridge |
| **The firmware is identified by its ELF SHA256**, printed at boot | Two builds of uncommitted changes can share the same version string. Every test record cites the SHA256 |
| **The baud rate is a menuconfig option** (default 115200), and the frame format is fixed at 8N1 | FR-5. The bridge's per-byte timing is derived from the configured rate |

## Assumptions

- **Hardware:** ESP32-Ethernet-Kit v1.2 (ESP32-WROVER-E) with the IP101 PHY, RMII with the 50 MHz clock from the PHY on GPIO0, MDC GPIO23, MDIO GPIO18 and PHY reset GPIO5.
- **Network:** a trusted local network, 192.168.1.0/24, with the bridge at the static address `192.168.1.200` and no other host using it.
- **Clients:** one client at a time, which tolerates TCP backpressure (its writes slow down when the UART is busy).
- **Serial device:** 3.3 V logic, 8N1, no hardware flow control, at 115200 baud.
- **Testing:** the GPIO32 to GPIO33 jumper for every echo test, a Linux PC on the same subnet, Node.js 22.18 or later, Python 3 for H6, `gcc` for the host tests, and ESP-IDF v6.1.

## Requirements Status

Requirement text from *ESP32 Serial-to-TCP Bridge: Requirements v1.0*. Each status comes from the hardware validation record linked in the last column, and every **Validated** status cites hardware evidence there.

### Functional Requirements

| Requirement | Description | Hardware status | Tests | Record |
|---|---|---|---|---|
| FR-1 | Connect over Ethernet with a static IPv4 address, netmask and gateway set in menuconfig. No DHCP | **Validated** | Boot log, ping, H7, H8, H9 | [FR-1](docs/test-records/hw-validation-fr1.md) |
| FR-2 | TCP server on port 4001, with at most one active client at a time | **Validated** | H1, H5, manual `nc` | [FR-2](docs/test-records/hw-validation-fr2.md) |
| FR-3 | Bytes received from the client are transmitted on UART1 TX (GPIO32) | **Validated** | H2, H3, H4 | [FR-3](docs/test-records/hw-validation-fr3.md) |
| FR-4 | Bytes received on UART1 RX (GPIO33) are sent to the client | **Validated** | H2 to H6 | [FR-4](docs/test-records/hw-validation-fr4.md) |
| FR-5 | UART1 at 8N1, no hardware flow control, baud rate set in menuconfig (default 115200, optionally 921600) | **Validated** at 115200 (921600 not supported) | Boot log, H3, H4 on `7ffc134`, 921600 run | [FR-5](docs/test-records/hw-validation-fr5.md) |
| FR-6 | No lost, duplicated or reordered bytes in either direction, at those baud rates | **Validated** at 115200 | H2, H3, H4, NFR-3 soak | [FR-6](docs/test-records/hw-validation-fr6.md) |
| FR-7 | Data loss that cannot be avoided is detected, counted and reported on UART0 every 10 s | Partial | Host tests, HW-FR7-001 to 011 | [FR-7](docs/test-records/hw-validation-fr7.md) |
| FR-8 | A client that disconnects or times out, including a half-open connection, is detected within 30 s and its resources are freed | **Validated** | H6, H7, H8, H9, [TR-FR8-002](docs/testing/FR-8-TEST-REPORT.md) (keepalive, RST, idle, half-open, soak, pending data) | [FR-8](docs/test-records/hw-validation-fr8.md) |
| FR-9 | Recover from an Ethernet cable reconnection without a reboot | **Validated** | H9 (9 outages, 4 s to 110 s), H8 (216 s outage) | [FR-9](docs/test-records/hw-validation-fr9.md) |

**What is still open:**

| Requirement | Missing evidence | How to close it |
|---|---|---|
| FR-7 | HW-FR7-004 (link loss with data in flight), HW-FR7-006 (line errors) and HW-FR7-009 (break). 7 of the 11 plan tests have passed on hardware | Pull the cable during H4 (about 1 minute). 006 needs a USB-UART adapter on GPIO33, and 009 a resistor from GPIO33 to GND |

**Final firmware:** version `7ffc134`, ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259`. This build passed `npm test` and the NFR-3 soak on hardware.

**Notes on the evidence:**
- **The jumper.** FR-3, FR-4 and FR-6 are verified through the GPIO32 to GPIO33 jumper at 115200 8N1, so the two directions are verified together.
- **FR-8 half-open timing.** The 30 s limit rests on the keepalive configuration read back on hardware (at most about 11 s) and the operator's observation of about 9 s. No timestamped measurement was retained (see the record).
- **921600.** At 921600 the bridge loses bytes under sustained traffic (UART1 FIFO overflow, detected and reported by FR-7), so it is not supported. FR-5 makes 921600 optional.
- **FR-8 and FR-9 firmware.** Their evidence ran on three firmware builds (`23f991546`, `8d79dbe83` and `ce94f6077`). The code each test exercises is unchanged between that build and the current source, as each record explains.

### Non-Functional Requirements

| Requirement | Description | Status | Evidence | Record |
|---|---|---|---|---|
| NFR-1 | All console logging on UART0. UART1 only for bridge data, with no log interference | **PASS** | Console configuration, source review, and H3/H4 integrity while logging | [NFR-1](docs/test-records/validation-nfr1.md) |
| NFR-2 | Buffer sizes, task stacks and heap budget documented | **PASS** | [Memory and buffers](#memory-and-buffers) below, with stack margins measured on hardware | [NFR-2](docs/test-records/validation-nfr2.md) |
| NFR-3 | 15-minute continuous run with stable free heap | **PASS** | 15.3 min, 33 of 33 sessions, 10.3 MB verified, 0 resets, heap 253,856 to 254,292 B, minimum unchanged | [NFR-3](docs/test-records/validation-nfr3.md) |
| NFR-4 | Network, UART and bridge modules. Bridge buffer logic testable without hardware | **PASS** | Module layout. Host tests 90/0, 62/0, 59/0, 25/0 with ASan/UBSan and with TSan | [NFR-4](docs/test-records/validation-nfr4.md) |
| NFR-5 | Return codes checked. Builds with ESP-IDF 6.1 without errors | **PASS** | Full return code review. `7ffc134` builds with no errors or warnings | [NFR-5](docs/test-records/validation-nfr5.md) |

### Memory and Buffers

| Item | Size | Measured (firmware `7ffc134`) |
|---|---|---|
| Bridge buffers (TCP→UART, UART→TCP) | 2 × 256 B, static | |
| UART1 RX / TX ring buffers | 2,048 B each | |
| UART1 hardware RX FIFO / event queue | 128 B / 128 entries | |
| `LOSS:` report line | 384 B, static | |
| lwIP TCP send buffer / window / MSS | 5,760 / 5,760 / 1,440 B | |
| Ethernet DMA buffers | 10 RX + 10 TX × 512 B | |
| `bridge` task stack (priority 5) | 4,096 B | 2,760 B never used (peak 1,336 B) |
| `tcp_server` task stack (priority 5) | 4,096 B | 2,952 B never used (peak 1,144 B) |
| `loss_reporter` task stack (priority 3) | 3,072 B | 2,028 B never used (peak 1,044 B) |
| Static DRAM (`.data` + `.bss`) | 17,985 B of 180,736 B (9.95 %) | |
| Heap regions at boot (DRAM and D/IRAM) | 313,960 B | |
| Free heap at steady state | | 253,856 to 254,292 B |
| Lowest free heap ever | | 239,252 B (peak use, during H4/H5) |

The application allocates no heap memory itself. The bridge, server and reporter stacks log their lowest margin whenever it reaches a new low. Every size, with its source, is in [the NFR-2 record](docs/test-records/validation-nfr2.md).

### Latest Results

| Run | Firmware | Result | Report |
|---|---|---|---|
| `npm test`, H1 to H5 (2026-11-05, final) | `7ffc134` / `ae7ba8e58` | **PASS**, 8 of 8. Boot line `115200 baud, 8N1, no flow control`. H3 and H4 SHA256 match, H4 11,512 bytes/s | [report](docs/test-reports/automated-test-results-2026-11-05-fw7ffc134.md) |
| NFR-3 soak, 15 min (2026-11-05) | `7ffc134` / `ae7ba8e58` | **PASS**, 33 of 33 sessions, 10,264,320 bytes verified, 0 resets, stable heap, no loss reported | [report](docs/test-reports/soak-2026-11-05T19-06-33-635Z.md), [log](docs/evidence/2026-11-05_final-run_fwae7ba8e58.log) |
| H2 to H4 at 921600 (2026-11-05) | 921600 build of `c8114ec` | **FAIL** (optional rate): H2 passed, H3 and H4 lost bytes, reported as `uart_rx_overflow` | [report](docs/test-reports/automated-test-results-921600-2026-11-05.md) |
| `npm test`, H1 to H5 (2026-11-05) | `ce94f6077` | **PASS**, 8 of 8. H3 and H4 SHA256 match, H4 11,509 bytes/s. H6 skipped | [automated-test-results-2026-11-05.md](docs/test-reports/automated-test-results-2026-11-05.md) |
| H9, 5 cycles (2026-11-05) | `ce94f6077` | **PASS**: 9 outages of 4.0 s to 109.9 s, static IP restored 9 of 9, no reset, all `LOSS:` reports at zero. Worst client acceptance 6.8 s from the operator's Enter | [H9 report](docs/test-reports/manual-h9-2026-11-05T14-51-27-137Z.md) |
| `npm run test:h6`, H6 (2026-11-05) | `ce94f6077` | **PASS**, 5 of 5. `client stalled` after 7.0 s, then `LOSS:` reported `tcp_send_failures=1`, `tcp_tx_bytes=2260` and `uart_rx_overflow=247`, and a new client was accepted | [automated-test-results-h6-2026-11-05.md](docs/test-reports/automated-test-results-h6-2026-11-05.md) |
| FR-8 test series (2026-11-04) | `8d79dbe83` | Keepalive, RST, 300 s idle, half-open detection, 200-cycle soak and pending data all observed. The report marks half-open timing as inconclusive (no timestamps) | [TR-FR8-002](docs/testing/FR-8-TEST-REPORT.md) |
| H8, 216 s outage (2026-11-02) | `23f991546` | Static IP restored, new client accepted, no reset | [log](docs/evidence/2026-11-02_h8_fw23f991546.log) |
| Host tests (2026-11-05) | Source `7ffc134` | **PASS** with ASan/UBSan and with TSan: FR-7 62/0 and 59/0, bridge 90/0, FR-8 keepalive 25/0 | `bash tests/host/run.sh`, `bash tests/host/run.sh tsan` |
| `npm test`, H1 to H5 (2026-11-03) | `0da27aa` | PASS | [automated-test-results.md](docs/test-reports/automated-test-results.md) |

The 2026-11-05 `npm test` and H6 reports don't name the firmware, because `BRIDGE_FIRMWARE` was not set. It is identified from the serial log of the same boot. The serial logs behind these results are kept in [`docs/evidence/`](docs/evidence/).

## Known Limitations

- **No security.** No authentication or encryption: any host that can reach port 4001 can use the serial line while the slot is free.
- **One client, IPv4 only, static address only.** There is no DHCP fallback and no runtime configuration: IP, port and baud rate change only through `idf.py menuconfig` and a rebuild.
- **921600 loses data.** Under sustained traffic the UART1 hardware FIFO overflows (H3 lost 9 bytes and H4 20). The loss is detected and reported as `uart_rx_overflow`. 115200 is the validated rate.
- **No RTS/CTS.** Flow control toward the serial device is only the bridge's TX pacing.
- **`LOSS:` lines report detected loss only.** "No data loss" can appear after an abnormal session, because data held in lwIP buffers isn't visible to the firmware. Data still held by UART1 is counted.
- **H5 can end either way.** The close races with the echo, so the report may or may not show loss, and `tcp_send_failures` may be 0 or 1. Only the isolation result matters for H5.
- **Frame-error storms are capped.** Above 2,000 frame errors and breaks in one interval, their interrupts pause until the next report, and `rx_error_pauses` shows it. The counts for that interval are a lower bound.
- **Not verified on hardware:** FR-7's line-error, break and link-loss-in-flight cases (HW-FR7-004, 006, 009), and a timestamped measurement of half-open detection time (FR-8).
- **Test method:** the jumper loops UART1 to itself, so the two directions are verified together, and UART1 was never observed with a logic analyzer. `network.c` has no host tests; it is covered by hardware tests.

## Potential Improvements

- **Support 921600:** lower the UART RX FIFO full threshold and move the UART interrupt to IRAM, then rerun H2 to H4 and the soak at that rate.
- **Optional RTS/CTS** hardware flow control, as a menuconfig option.
- **Runtime configuration** of IP, port and baud rate, stored in NVS, through a console command or a small setup interface.
- **Access control:** an IP allowlist, or TLS for clients outside a trusted network.
- **Watchdog:** subscribe the `bridge` task to the task watchdog. It is not subscribed today.
- **CI:** run the host tests (ASan, UBSan, TSan) and `idf.py build` on every push.
- **Test automation:** a USB-controlled Ethernet switch to automate H7 to H9, and a logic analyzer or a USB-UART adapter to verify each UART direction separately and to run HW-FR7-004, 006 and 009.

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
| Manual tests | Ethernet cable disconnection (H7, H8, H9), FR-8 half-open and RST tests, the NFR-3 soak, and the FR-7 hardware plan | Yes |

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
| `npm run manual:soak` | NFR-3 continuous run: back-to-back verified echo sessions (`SOAK_MINUTES`, default 15) | 15 minutes, unattended |
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

## Deliverables

| Item | Requirement | Status | Evidence |
|---|---|---|---|
| D-1 | Repository builds from a clean checkout with `idf.py build` and ESP-IDF v6.1 | **PASS** | 2026-11-05: `main` (`c2f43c0`) cloned from GitHub into an empty directory, then `idf.py set-target esp32` and `idf.py build` with ESP-IDF v6.1: exit 0, no errors, no warnings. The `sdkconfig` generated from `sdkconfig.defaults` and `main/Kconfig.projbuild` is identical, option for option, to the one of the hardware-tested firmware. No external components are needed |
| D-2 | README with design decisions, assumptions, known limitations and potential improvements | **PASS** | [Design Decisions](#design-decisions), [Assumptions](#assumptions), [Known Limitations](#known-limitations), [Potential Improvements](#potential-improvements) |
| D-3 | 15-minute walkthrough call, with one small live code change | **PENDING** until the call | Agenda and live-change plan in [docs/walkthrough.md](docs/walkthrough.md) |
| Process | Firmware tested manually on the same hardware | **PASS** | One ESP32-Ethernet-Kit v1.2 (MAC `c0:cd:d6:61:ae:b3` in every log). Procedures in [manual-test-procedures.md](docs/test-reports/manual-test-procedures.md) and in each record's *How to Run These Tests*. Serial logs in [`docs/evidence/`](docs/evidence/) |
| Process (optional) | Firmware stress-test script | **PASS** | `npm run manual:soak` ([`tests/manual/soak.ts`](tests/manual/soak.ts)): 15 minutes, 33 of 33 sessions ([NFR-3](docs/test-records/validation-nfr3.md)). Also H4 (256,000 bytes) and H6 (UART flood up to 100 s) |

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
  manual/             H7, H8, H9 assisted scripts and the NFR-3 soak
  support/            Shared client, configuration and report generator
docs/
  fr7-data-loss-reporting.md   FR-7 design
  test-records/       Validation record for each FR and NFR, with how to run its tests
  test-reports/       Generated test reports and manual procedures
  testing/            Per-requirement test reports (FR-8)
  evidence/           Serial logs and packet captures cited by the records
  walkthrough.md      D-3 walkthrough agenda and live-change plan
```
