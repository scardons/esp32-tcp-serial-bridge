# Validation Record: NFR-2 Memory and Buffer Documentation

| Field | Value |
|---|---|
| Record ID | VAL-NFR-002 |
| Requirement | NFR-2: Document all buffer sizes, task stack sizes and the firmware heap budget, and include them in the README. |
| Project | ESP32 TCP Serial Bridge |
| Date | 2026-11-05 |
| Record prepared by | Claude Code, from the source, `sdkconfig`, `idf.py size` and the final run's serial log |
| Firmware | Version `7ffc134`, ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259` |
| Status | **PASS** |

Sizes come from the source and `sdkconfig`. Measured values come from [2026-11-05_final-run_fwae7ba8e58.log](../evidence/2026-11-05_final-run_fwae7ba8e58.log): one boot, `npm test` (H1 to H5) and the 15-minute soak ([VAL-NFR-003](validation-nfr3.md)).

## Application Buffers

| Buffer | Size | Where | Purpose |
|---|---|---|---|
| `s_tcp_rx_buffer` | 256 B, static | `bridge.c` (`BRIDGE_BUFFER_SIZE`) | TCP to UART1 data in transit |
| `s_uart_rx_buffer` | 256 B, static | `bridge.c` | UART1 to TCP data in transit |
| UART1 RX ring buffer | 2,048 B | `uart_driver.h` (`UART_DRIVER_RX_BUFFER_SIZE`), allocated by `uart_driver_install` | About 178 ms of input at 115200 |
| UART1 TX ring buffer | 2,048 B | Same | Bytes queued for GPIO32 |
| UART1 hardware RX FIFO | 128 B | `UART_DRIVER_RX_FIFO_SIZE` (hardware) | |
| UART1 event queue | 128 entries × 12 B | `UART_DRIVER_EVENT_QUEUE_SIZE` | About 165 ms of events at 921600 |
| Bridge client queue | 1 entry × 4 B | `bridge.c` (`xQueueCreate(1, sizeof(int))`) | Hands one accepted socket to the bridge |
| `LOSS:` report line | 384 B, static | `loss_reporter.c` (`LOSS_REPORT_LINE_SIZE`) | One report line |

**Flow limits** (`bridge.c`):
- UART TX backlog at most 100 ms (`BRIDGE_UART_TX_MAX_BACKLOG_US`), at 87 µs per byte at 115200;
- send stall timeout 5 s;
- session poll 10 ms;
- idle drain 100 ms.

## Network Buffers (`sdkconfig`)

| Setting | Value |
|---|---|
| TCP send buffer (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`) | 5,760 B |
| TCP receive window (`CONFIG_LWIP_TCP_WND_DEFAULT`) | 5,760 B |
| TCP MSS | 1,440 B |
| Socket limit (`CONFIG_LWIP_MAX_SOCKETS`) | 10 |
| TCP/IP mailbox, TCP receive and accept mailboxes | 32, 6, 6 |
| Ethernet DMA buffers | 10 RX + 10 TX × 512 B |
| TCP listen backlog | 1 (`TCP_LISTEN_QUEUE`) |

## Task Stacks

Measured as the stack's high-water mark, logged whenever it reaches a new low. The last value logged is the lowest of the run.

| Task | Stack | Priority | Lowest unused (measured) | Peak use | Source of the size |
|---|---|---|---|---|---|
| `bridge` | 4,096 B | 5 | **2,760 B** | 1,336 B (33 %) | `BRIDGE_TASK_STACK` |
| `tcp_server` | 4,096 B | 5 | **2,952 B** | 1,144 B (28 %) | `TCP_SERVER_TASK_STACK` |
| `loss_reporter` | 3,072 B | 3 | **2,028 B** | 1,044 B (34 %). A `W` line appears below 512 B | `LOSS_REPORTER_TASK_STACK` |
| `main` | 3,584 B | 1 | Not measured | Returns after start-up | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` |
| Event loop (`sys_evt`) | 2,304 B | 20 | Not measured | Runs the network event handlers | `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` |
| lwIP `tcpip` | 3,072 B | 18 | Not measured | | `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE` |
| `esp_timer` | 3,584 B | 22 | Not measured | | `CONFIG_ESP_TIMER_TASK_STACK_SIZE` |
| FreeRTOS idle ×2, timer | 1,536 B each, 2,048 B | 0, 1 | Not measured | | `sdkconfig` |

The stack values come from these log lines:

```text
I (10368) LOSS: Reporter stack: 2028 of 3072 bytes never used
I (12168) BRIDGE: Bridge stack: 2776 of 4096 bytes never used
I (39608) BRIDGE: Bridge stack: 2760 of 4096 bytes never used
I (11918) NETWORK: TCP server stack: 2984 of 4096 bytes never used
I (37718) NETWORK: TCP server stack: 2952 of 4096 bytes never used
```

The lows were reached during `npm test` (H1 to H5), and none dropped further during the 15-minute soak.

## Static Memory (`idf.py size`, `7ffc134`)

| Section | Used | Of |
|---|---|---|
| DRAM (`.data` 11,545 + `.bss` 6,440) | 17,985 B (9.95 %) | 180,736 B |
| IRAM | 51,287 B (39.1 %) | 131,072 B |
| Flash code / data | 218,010 B / 83,736 B | |
| Image | 364,618 B | 1,048,576 B app partition (65 % free) |

## Heap Budget

| Item | Value | Source |
|---|---|---|
| Heap regions at boot | 6 + 174 + 14 + 111 KiB of DRAM and D/IRAM (313,960 B), plus 77 KiB IRAM usable only for 32-bit or executable allocations | `heap_init:` boot lines |
| Free heap at steady state, after each session | 253,856 to 254,292 B | 48 `Heap after session` samples |
| Lowest free heap ever (peak use) | 239,252 B, during H4/H5 | `minimum ever` |
| Dynamic use: ESP-IDF, lwIP, Ethernet, UART driver and task stacks | About 59,700 B at steady state and about 74,700 B at peak (313,960 B minus the values above) | Derived |
| Headroom at peak use | About 239 KB free | |

The application allocates no memory with `malloc` itself. Its dynamic memory is the 3 task stacks (11,264 B), the UART driver buffers (about 5.6 KB) and the queue. Everything else is ESP-IDF, lwIP and Ethernet.

## Tests

| Test ID | Test | Expected | Actual | Result |
|---|---|---|---|---|
| NFR2-001 | Buffer and stack sizes documented | Every buffer and task stack listed with its source | Tables above, and in the README | PASS |
| NFR2-002 | Stack margins measured on hardware for the application tasks | Lowest unused value logged, above 512 B | 2,760, 2,952 and 2,028 B | PASS |
| NFR2-003 | Heap budget documented from measurement | Steady-state and peak values from hardware | 253,856 to 254,292 B free, lowest 239,252 B | PASS |

## Limitations

1. ESP-IDF system task stacks were not measured. Their sizes are the ESP-IDF defaults.
2. Dynamic use is derived from free-heap readings, not from a per-allocation trace.
