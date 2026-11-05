# Validation Record: NFR-4 Modular Architecture and Independent Testing

| Field | Value |
|---|---|
| Record ID | VAL-NFR-004 |
| Requirement | NFR-4: The code is separated into a network module, a UART module and a bridge logic module. Bridge buffer management can be tested independently of the physical hardware, and the unit tests cover the bridge logic and buffer management. |
| Project | ESP32 TCP Serial Bridge |
| Date | 2026-11-05 |
| Record prepared by | Claude Code, from the source tree and host test runs |
| Source | Commit `7ffc134` |
| Status | **PASS** |

## Module Structure

| Module | Files | Responsibility | Depends on |
|---|---|---|---|
| Network | `main/network/network.c`, `network.h` | Ethernet driver, static IPv4, link events and recovery (FR-1, FR-9), TCP server task, accepting clients (FR-2) | ESP-NETIF, `esp_eth`, lwIP, the bridge API (`bridge_attach_client`, `bridge_request_disconnect`) |
| Network: client socket | `main/network/client_socket.c`, `client_socket.h` | Per-client keepalive, set and read back (FR-8) | lwIP sockets only |
| UART | `main/uart/uart_driver.c`, `uart_driver.h` | UART1 configuration (FR-5), read and write, RX events, line errors and event drop counts | ESP-IDF UART driver |
| Bridge logic | `main/bridge/bridge.c`, `bridge.h` | Session loop, the two 256-byte buffers, UART TX throttling, stall detection, session isolation and end-of-session accounting (FR-3, FR-4, FR-6, FR-8) | `uart_driver.h`, lwIP sockets, `loss_stats.h` |
| Bridge: loss counters and reporter | `main/bridge/loss_stats.c`, `loss_reporter.c` | FR-7 counters and the 10 s `LOSS:` report | `uart_driver.h` |
| Application | `main/main.c` | Start-up order and firmware identity line | All modules, through their headers |

The bridge reaches the UART only through `uart_driver.h`, and the network module reaches the bridge only through `bridge.h`.

## Independent Testing

`tests/host/run.sh` compiles the **real** firmware sources for the PC, against stubs of ESP-IDF, FreeRTOS, lwIP and the UART driver (`tests/host/stubs/`). No ESP32 is needed. The bridge runs against a mock UART that echoes like the jumper, at the 115200 byte time, and real TCP sockets on the PC. Selected socket calls are wrapped (`-Dsend=test_send`, and the same for `close` and `select`) to inject failures.

| Suite | Sources under test | Buffer management and logic covered |
|---|---|---|
| Bridge logic (`bridge_test.c`), 90 checks | `bridge.c`, `loss_stats.c` | T1 all byte values. T2 partial and failing sends. T3 large transfers. T4 a second client rejected. T5 connect and disconnect cycles. T5b heap report per session. T6 stale data never reaches a new client. T7 disconnect request. T8 to T10 stall detection and its timer. T11 peer reset. T12 slow UART with throttling. T13 UART to TCP served while TCP to UART is throttled. T14 block and close with no byte lost. T15 disconnect while throttled. T16 and T17 FR-7 end-of-session accounting |
| FR-7 counters and reporter (`loss_test.c`), 62 checks at a 200 ms period and 59 at the real 10 s period | `loss_stats.c`, `loss_reporter.c`, `uart_driver.c` | Counters, deltas, report format, event drops, line-error limiting, report cadence |
| FR-8 client socket (`client_socket_test.c`), 25 checks | `client_socket.c` | Keepalive set and read back, every failure path |

## Tests

| Test ID | Test | Expected | Actual (2026-11-05, source `7ffc134`) | Result |
|---|---|---|---|---|
| NFR4-001 | Module separation | Network, UART and bridge in separate directories, with header-only coupling | As in the table above | PASS |
| NFR4-002 | Host tests with ASan and UBSan: `bash tests/host/run.sh` | All pass, with no sanitizer report | 62/0, 59/0, 90/0, 25/0 | PASS |
| NFR4-003 | Host tests with ThreadSanitizer: `bash tests/host/run.sh tsan` | All pass, with no data race reported | 62/0, 59/0, 90/0, 25/0 | PASS |

## Limitations

1. `network.c` (Ethernet and event handling) has no host test. It is covered by the hardware tests (H1, H7 to H9, and the FR-8 series).
2. The host tests model 115200 baud only.
