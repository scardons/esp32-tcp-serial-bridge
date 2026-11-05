# Hardware Validation Record: FR-5

| Field | Value |
|---|---|
| Record ID | HW-VAL-005 |
| Requirement | FR-5: UART1 shall run at 8N1 with no hardware flow control. The baud rate shall be set in menuconfig, default 115200. Optionally, it may also work at 921600. (*ESP32 Serial-to-TCP Bridge: Requirements v1.0*) |
| Project | ESP32 TCP Serial Bridge |
| Updated | 2026-11-05 |
| Record prepared by | Claude Code, from the source code, build output and retained test evidence |
| Implementation | Commit `c726a91` (`feat(uart): set the UART1 baud rate in menuconfig (FR-5)`), Kconfig help corrected in `7ffc134` |
| Firmware validated | Version `7ffc134`, ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259` |
| Status | **VALIDATED at 115200** (921600 optional, not supported) |

## Purpose and Scope

This record maps the evidence for each part of FR-5:
- 8N1 and no flow control are shown on hardware with firmware `ce94f6077`;
- the menuconfig baud rate option was added in `c726a91` and is verified by build;
- the new firmware has not yet run on hardware.

Before `c726a91`, the baud rate was hardcoded (`#define BRIDGE_UART_BAUD_RATE 115200` in `main/uart/uart_driver.c`), so the menuconfig part of FR-5 was not met.

## Implementation (`c726a91`)

| File | Change |
|---|---|
| `main/Kconfig.projbuild` | `CONFIG_BRIDGE_UART_BAUD_RATE`, under *TCP-Serial Bridge Configuration* → *UART Configuration*. Integer, default 115200, range 9600 to 921600 |
| `main/uart/uart_driver.h` | `UART_DRIVER_BAUD_RATE` comes from the menuconfig value. The host tests, which have no sdkconfig, model 115200 |
| `main/uart/uart_driver.c` | `uart_config_t.baud_rate` uses that value. 8N1 and no flow control are unchanged: `UART_DATA_8_BITS`, `UART_PARITY_DISABLE`, `UART_STOP_BITS_1`, `UART_HW_FLOWCTRL_DISABLE`, with RTS and CTS left unassigned in `uart_set_pin()` |
| `main/bridge/bridge.c` | The per-byte UART time comes from the baud rate instead of a hardcoded 87 µs. At 115200 it is still 87 µs, so behaviour at the default is unchanged |
| `main/main.c` | The boot log shows `UART1 initialized successfully: <baud> baud, 8N1, no flow control` |
| `tests/support/config.ts` | `BRIDGE_BAUD` (default 115200) sets the rate H3 and H4 expect |

## Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E13 | Test report | [automated-test-results-2026-11-05.md](../test-reports/automated-test-results-2026-11-05.md), firmware `ce94f6077` | H3 and H4 results |
| E18 | Build output | `idf.py build` of `c726a91` at the default configuration | `CONFIG_BRIDGE_UART_BAUD_RATE=115200`, no warnings, ELF SHA256 `726621edd4d4ba94474d9cb1a58a1fabf95e5197578fe0d7f9222449093ac736` |
| E19 | Build output | `idf.py build` of `c726a91` with `CONFIG_BRIDGE_UART_BAUD_RATE=921600` in a separate build directory | No warnings, ELF SHA256 `4fa276e49e6f70dcd84194d6b086cb6b36b37da1e2fc958778ef75385c23a016` |
| E22 | Test report and serial log | [automated-test-results-2026-11-05-fw7ffc134.md](../test-reports/automated-test-results-2026-11-05-fw7ffc134.md), [2026-11-05_final-run_fwae7ba8e58.log](../evidence/2026-11-05_final-run_fwae7ba8e58.log), firmware `7ffc134` | 8 of 8 passed. Boot line `MAIN: UART1 initialized successfully: 115200 baud, 8N1, no flow control` |
| E23 | Test report and serial log | [automated-test-results-921600-2026-11-05.md](../test-reports/automated-test-results-921600-2026-11-05.md), [2026-11-05_921600_fwc903d6e80.log](../evidence/2026-11-05_921600_fwc903d6e80.log), 921600 build of the same source (ELF `c903d6e8…`) | Boot line `921600 baud, 8N1, no flow control`. H2 passed, H3 and H4 failed |
| E20 | Host tests | `bash tests/host/run.sh` (ASan and UBSan) on `c726a91` | FR-7 reporter at 200 ms 62/0 and at 10 s 59/0, bridge logic 90/0, FR-8 keepalive 25/0 |

## Test Summary

| Test ID | Test | Evidence | Result |
|---|---|---|---|
| HW-FR5-001 | 8 data bits: every byte value 0x00 to 0xFF passes through UART1 | E13 (H3, firmware `ce94f6077`) | PASS |
| HW-FR5-002 | 8N1 on the wire: 10 bits per byte at 115200 | E13 (H4: 11,509 bytes/s, where 8N1 allows 11,520 and 8E1 or 8N2 about 10,470) | PASS |
| HW-FR5-003 | No hardware flow control | Code review (`UART_HW_FLOWCTRL_DISABLE`, RTS and CTS unassigned). On hardware, H2 to H4 passed with only TX and RX connected | PASS |
| HW-FR5-004 | Baud rate set in menuconfig, default 115200 | E18, E19: the option exists, defaults to 115200, and the build follows it | PASS (build) |
| HW-FR5-005 | The firmware with the menuconfig option runs at the configured rate on hardware | E22: boot line shows 115200 8N1 with no flow control. H3 matched SHA256 for all 256 byte values. H4 matched SHA256 at 11,512 bytes/s | PASS |
| HW-FR5-006 | Optional: works at 921600 | E23: the menuconfig value takes effect (boot line `921600 baud`) and H2 passes, but H3 lost 9 of 20,480 bytes and H4 20 of 256,000, with `uart_rx_overflow` reported | FAIL (optional): 921600 is not supported |

## How to Run These Tests

**HW-FR5-005:**
1. Flash the firmware.
2. Check the boot line `MAIN: UART1 initialized successfully: <baud> baud, 8N1, no flow control`.
3. Run `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test`.

It passes when the boot line shows the configured rate and H1 to H5 pass.

**HW-FR5-006 (another rate):**
1. Set the rate in `idf.py menuconfig` → *TCP-Serial Bridge Configuration* → *UART Configuration* → *UART1 baud rate*, then build and flash.
2. Run `BRIDGE_BAUD=<rate> npm test -- -t "H[234]:"`.

### Why 921600 Fails

The losses are counted as `uart_rx_overflow` while `rx_buffer_full=0`. So the 128-byte hardware FIFO overflowed before the driver emptied it; the 2,048-byte ring buffer never filled. At 921600 the FIFO fills in 1.4 ms, and the driver's default interrupt threshold leaves about 8 bytes, or 87 µs, of margin.

Supporting 921600 would need the UART driver's interrupt configuration to be tuned, for example a lower RX full threshold or the ISR in IRAM. It was not attempted, because 921600 is optional in FR-5 and the change would affect the validated 115200 path. FR-7 detected and reported the loss on hardware.

## Status

**FR-5: VALIDATED at 115200** (firmware `7ffc134`):
- UART1 runs at 8N1 with no hardware flow control;
- the baud rate is set in menuconfig and defaults to 115200;
- the firmware uses the configured value, as shown at 115200 and at 921600.

The optional 921600 rate is **not supported**: it loses data under sustained traffic (HW-FR5-006), and the Kconfig help says so.

## Limitations

1. HW-FR5-001 to 003 ran on `ce94f6077`. HW-FR5-005 repeats H3 and H4 on `7ffc134` with the same results.
2. The jumper cannot show the rate on the wire independently. The rate is inferred from the measured throughput.
