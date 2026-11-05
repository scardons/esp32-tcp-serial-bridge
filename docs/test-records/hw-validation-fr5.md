# Hardware Validation Record: FR-5

| Field | Value |
|---|---|
| Record ID | HW-VAL-005 |
| Requirement | FR-5: UART1 shall run at 8N1 with no hardware flow control. The baud rate shall be set in menuconfig, default 115200. Optionally, it may also work at 921600. (*ESP32 Serial-to-TCP Bridge: Requirements v1.0*) |
| Project | ESP32 TCP Serial Bridge |
| Updated | 2026-11-05 |
| Record prepared by | Claude Code, from the source code, build output and retained test evidence |
| Implementation | Commit `c726a91` (`feat(uart): set the UART1 baud rate in menuconfig (FR-5)`) |
| Status | **IMPLEMENTED, HARDWARE CHECK OF THE NEW FIRMWARE PENDING** |

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
| E20 | Host tests | `bash tests/host/run.sh` (ASan and UBSan) on `c726a91` | FR-7 reporter at 200 ms 62/0 and at 10 s 59/0, bridge logic 90/0, FR-8 keepalive 25/0 |

## Test Summary

| Test ID | Test | Evidence | Result |
|---|---|---|---|
| HW-FR5-001 | 8 data bits: every byte value 0x00 to 0xFF passes through UART1 | E13 (H3, firmware `ce94f6077`) | PASS |
| HW-FR5-002 | 8N1 on the wire: 10 bits per byte at 115200 | E13 (H4: 11,509 bytes/s, where 8N1 allows 11,520 and 8E1 or 8N2 about 10,470) | PASS |
| HW-FR5-003 | No hardware flow control | Code review (`UART_HW_FLOWCTRL_DISABLE`, RTS and CTS unassigned). On hardware, H2 to H4 passed with only TX and RX connected | PASS |
| HW-FR5-004 | Baud rate set in menuconfig, default 115200 | E18, E19: the option exists, defaults to 115200, and the build follows it | PASS (build) |
| HW-FR5-005 | The new firmware (E18) runs at the configured rate on hardware | Not yet run | PENDING |
| HW-FR5-006 | Optional: works at 921600 | E19 builds. Not yet run on hardware | PENDING (optional) |

## How to Close the Pending Tests

**HW-FR5-005 (automated, about 1 minute, no operator action):**
1. Flash the E18 firmware.
2. Check the boot log for `MAIN: UART1 initialized successfully: 115200 baud, 8N1, no flow control`.
3. Run `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test`.

It passes when the boot line shows the configured rate and H1 to H5 pass.

**HW-FR5-006 (optional, automated, about 10 minutes):**
1. Flash the E19 firmware (921600).
2. Run `BRIDGE_BAUD=921600 npm test -- -t "H[234]:"`.
3. Flash the E18 firmware again.

It passes when H2 to H4 pass. The jumper loops UART1 to itself, so both ends always use the same rate.

To change the rate for production, use `idf.py menuconfig` → *TCP-Serial Bridge Configuration* → *UART Configuration* → *UART1 baud rate*, then build and flash.

## Status

**FR-5: IMPLEMENTED; hardware check of the new firmware pending.**
- **8N1 and no flow control** are shown on hardware.
- **The menuconfig baud rate** is implemented and verified by build.
- **HW-FR5-005**, the hardware run of the firmware that contains it, is the remaining required step.
- **921600** is optional.

## Limitations

1. HW-FR5-001 to 003 ran on `ce94f6077`, before `c726a91`. At 115200, `c726a91` keeps the same UART configuration and the same per-byte time.
2. The jumper cannot show the rate on the wire independently. The rate is inferred from the measured throughput.
