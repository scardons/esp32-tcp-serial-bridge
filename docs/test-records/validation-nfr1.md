# Validation Record: NFR-1 Console Logging on UART0 Only

| Field | Value |
|---|---|
| Record ID | VAL-NFR-001 |
| Requirement | NFR-1: All console logging must use UART0 only. UART1 is used exclusively for bridge communication, and no debug message may interfere with UART1 data. |
| Project | ESP32 TCP Serial Bridge |
| Date | 2026-11-05 |
| Record prepared by | Claude Code, from the configuration, the source code and hardware test evidence |
| Firmware | Final: version `7ffc134`, ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259`. Also `ce94f6077` |
| Status | **PASS** |

## Implementation

| Item | Where | Value |
|---|---|---|
| Console UART | `sdkconfig` | `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, `CONFIG_ESP_CONSOLE_UART_NUM=0`, `CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200` |
| ROM and bootloader output | `sdkconfig` | `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=0` |
| Application logging | All of `main/` | Only `ESP_LOGI`, `ESP_LOGW` and `ESP_LOGE`, which write to the console. There are no `printf`, `puts`, `putchar`, `fprintf` or `esp_rom_printf` calls |
| Bridge UART | `main/uart/uart_driver.c` | `BRIDGE_UART_PORT` is `UART_NUM_1`, on GPIO32 (TX) and GPIO33 (RX). The only write is `uart_write_bytes(BRIDGE_UART_PORT, …)` in `uart_driver_write()` |
| Callers of `uart_driver_write()` | `main/bridge/bridge.c`, `main/main.c` | The bridge (TCP to UART1 data), and the optional boot loopback test, which is disabled (`CONFIG_BRIDGE_UART_LOOPBACK_TEST` is not set) |

## Tests

| Test ID | Test | Method | Expected | Actual | Result |
|---|---|---|---|---|---|
| NFR1-001 | Console on UART0 | Configuration review | Console, ROM and bootloader on UART0 | As listed above | PASS |
| NFR1-002 | No other writer on UART1 | Source review (`grep` for output calls and `UART_NUM_*`) | Only the bridge writes to UART1 | Only `uart_driver_write()`, called by the bridge and the disabled loopback test | PASS |
| NFR1-003 | Logs reach UART0 | Hardware: every boot and test log in this project was read from `/dev/ttyUSB1`, the kit's UART0 bridge | All `ESP_LOG` output on UART0 | Boot, `NETWORK`, `BRIDGE` and `LOSS` lines all on UART0 | PASS |
| NFR1-004 | Logs do not interfere with UART1 data | Hardware: H3 and H4 through the GPIO32 to GPIO33 jumper, while the firmware logs on UART0 | Echo identical to what was sent | H4 (256,000 bytes) and H3 (all 256 byte values) returned identical data, with matching SHA256, while `LOSS:` reports were logged on UART0 every 10 s during the transfer (firmware `ce94f6077` and `7ffc134`) | PASS |

A single log byte written to UART1 during H3 or H4 would have reached the client and changed the SHA256.

## Evidence

- [automated-test-results-2026-11-05.md](../test-reports/automated-test-results-2026-11-05.md) and [2026-11-05_h9-and-npm-test_fwce94f6077.log](../evidence/2026-11-05_h9-and-npm-test_fwce94f6077.log): H4 from 12:16:34 to 12:16:56, with `LOSS:` lines at 12:16:37 and 12:16:47.
- [automated-test-results-2026-11-05-fw7ffc134.md](../test-reports/automated-test-results-2026-11-05-fw7ffc134.md) and [2026-11-05_final-run_fwae7ba8e58.log](../evidence/2026-11-05_final-run_fwae7ba8e58.log): the same on the final firmware.

## Limitations

1. UART1 was not observed with a logic analyzer. NFR1-004 infers its contents from the echo through the jumper.
