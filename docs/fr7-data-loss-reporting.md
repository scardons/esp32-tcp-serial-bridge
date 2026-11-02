# FR-7: Periodic Data Loss Reporting

**Requirement:** The firmware must report detected data loss every 10 seconds through the UART0 console.

**Status:** Implemented and tested on the PC. **Hardware validation is pending.**

## Where Data Can Be Lost

| Path | Where | Detectable | How |
|---|---|---|---|
| UART to TCP | RX FIFO overflows because the RX ring buffer is full | Yes, as an event | `UART_FIFO_OVF`. The byte count is not available |
| UART to TCP | Frame or parity error on the line | Yes, as an event | `UART_FRAME_ERR`, `UART_PARITY_ERR` |
| UART to TCP | Bytes waiting for `send()` when a session ends | Yes, in bytes | Bridge pending buffer |
| UART to TCP | Bytes accepted by lwIP but never delivered to the client | **No** | lwIP does not report them |
| TCP to UART | Bytes waiting for UART budget when a session ends | Yes, in bytes | Bridge pending buffer |
| TCP to UART | Bytes in the lwIP receive buffer when a session ends | **No** | They are never read |
| TCP to UART | Bytes already queued for UART TX | Not lost | They are still transmitted |

TCP to UART has no buffer exhaustion path: unread data slows the client through its TCP window.

## What Counts as Data Loss

Data loss is data that the firmware accepted, or that reached it, which an active path did not deliver, for a reason that nobody chose.

| Case | Classification | Reasoning |
|---|---|---|
| UART driver buffer overflow | **Data loss** | Received bytes were destroyed |
| TCP send failure or stalled client | **Data loss** | The session failed with data on its way |
| Session ended by link loss, a socket error or a disconnect request | **Data loss** | Pending bytes never reached their destination |
| Client closes normally | **Expected discard** | The client chose to leave. This is approved design decision 3 |
| UART data with no client connected | **Expected discard** | This is the approved behavior |
| RX ring buffer full | **Pressure, not loss** | The driver holds the data. A loss is reported only if the FIFO then overflows |

Buffer exhaustion appears in two counters: `rx_buffer_full` is the warning, and `uart_rx_overflow` is the actual loss.

`bridge_request_disconnect()` is counted as loss because its only active caller is the Ethernet link-down event.

## Counters

Each counter is a lock-free 32-bit atomic in `main/bridge/loss_stats.c`. Reports show the change since the previous report.

| Report field | Unit | Exact definition | Updated by |
|---|---|---|---|
| `uart_rx_overflow` | Events | `UART_FIFO_OVF` events: the RX FIFO overflowed and the driver reset it, losing up to 128 bytes each | Reporter task |
| `uart_rx_errors` | Events | `UART_FRAME_ERR` and `UART_PARITY_ERR` events: a byte arrived corrupted | Reporter task |
| `tcp_send_failures` | Sessions | Sessions ended by a permanent `send()` error, or by 5 s without send progress | Bridge task |
| `tcp_tx_bytes` | Bytes | UART bytes not accepted by `send()` when a session ended for any reason except a normal client close | Bridge task |
| `uart_tx_bytes` | Bytes | TCP bytes not written to UART when a session ended for any reason except a normal client close | Bridge task |
| `session_bytes` | Bytes | Pending bytes dropped because the client closed normally | Bridge task |
| `no_client_bytes` | Bytes | UART bytes read and dropped with no active session, including the cleanup at session start | Bridge task |
| `rx_buffer_full` | Events | `UART_BUFFER_FULL` events: the RX ring buffer filled | Reporter task |

The first five fields are data loss. `session_bytes` and `no_client_bytes` are expected discards. `rx_buffer_full` is a pressure indicator.

## Report Format

One line is written every 10 seconds, even with no loss, so a silent console means that the reporter stopped. The level is `I` with no loss and `W` with loss.

```text
I (10326) LOSS: 10.0 s: no data loss: uart_rx_overflow=0 uart_rx_errors=0 tcp_send_failures=0 tcp_tx_bytes=0 uart_tx_bytes=0 | expected: session_bytes=0 no_client_bytes=0 | rx_buffer_full=0
W (20326) LOSS: 10.0 s: DATA LOSS: uart_rx_overflow=2 uart_rx_errors=0 tcp_send_failures=1 tcp_tx_bytes=120 uart_tx_bytes=0 | expected: session_bytes=0 no_client_bytes=0 | rx_buffer_full=3
```

These lines illustrate the format. They are not captured from hardware.

## Design

```text
UART1 ISR ── event queue (32) ──> loss_reporter task ──> UART0 console
                                         ^  loads
bridge task ── atomic add ──> loss_stats ┘
```

- **The bridge never blocks on reporting.** It only performs atomic additions, and it writes no log lines for data loss.
- **The reporter has priority 3, below the bridge at 5.** A slow console write delays only the reporter.
- **The reporter waits on the event queue.** It keeps the queue drained, and it uses `esp_timer` deadlines so reports do not drift.
- **The reporter does not touch sockets or the UART data path.**

## Changes to Existing Behavior

| Change | Reason | Effect |
|---|---|---|
| `uart_driver_install()` gets an event queue of 32 entries | Without it, the driver discards every overflow notice | The ISR also posts events. Baud rate and buffers are unchanged |
| The idle bridge reads and drops UART data every 100 ms | Otherwise an idle stream fills the ring buffer and looks like loss | The buffer fills in about 178 ms, so 100 ms keeps it from overflowing. Data with no client is still discarded |

Pacing, the 200 ms session cleanup, and the socket handling are unchanged.

## Test Coverage

Run the PC tests with `npm run test:host`. They need `gcc`, and no ESP32.

| Area | Test | File |
|---|---|---|
| Counter increments and independence | Unit | `tests/host/loss_test.c` |
| 800,000 increments from 4 threads | Unit | `tests/host/loss_test.c` |
| Delta across a 32-bit wrap | Unit | `tests/host/loss_test.c` |
| Which counters mean data loss | Unit | `tests/host/loss_test.c` |
| Exact report text, and the largest possible line | Unit | `tests/host/loss_test.c` |
| UART event mapping, with the real `uart_driver.c` | Unit | `tests/host/loss_test.c` |
| Report per interval, levels, reset between intervals, no drift | Real reporter with a mock driver | `tests/host/loss_test.c` |
| Report during an event flood | Real reporter with a mock driver | `tests/host/loss_test.c` |
| Real 10-second period | Real reporter with a mock driver | `tests/host/loss_test.c` |
| No client, clean session, normal close, send error, stall, disconnect request | Real `bridge.c` with sockets | `tests/host/bridge_test.c` |
| Existing bridge behavior | Real `bridge.c` with sockets | `tests/host/bridge_test.c` |

The Jest suite cannot verify FR-7, because the report appears only on UART0.

## Limitations

1. **Overflow bytes are unknown.** `uart_rx_overflow` counts events, and each one loses up to 128 bytes.
2. **lwIP buffers are not visible.** Data accepted by `send()` but not delivered, and data unread in the lwIP receive buffer, are not counted. A stalled session can lose up to the 5,760-byte send buffer beyond `tcp_tx_bytes`.
3. **Event queue limit.** If the 32-entry queue is full, the driver drops events, including overflow events, without notice. The reporter should keep it drained, but the firmware cannot detect this case.
4. **Counters are not one atomic set.** A value updated during a snapshot appears in the next report. Nothing is lost.
5. **Boundary reports.** Events are counted when the reporter reads them, so one can land in the next interval.
6. **Abrupt client exits count as loss.** A client that resets the connection with data in flight, as in H5, reports loss. Its data really was not delivered.
7. **No hardware validation yet.**

## Required Hardware Validation

See `docs/test-records/hw-validation-fr7.md`.
