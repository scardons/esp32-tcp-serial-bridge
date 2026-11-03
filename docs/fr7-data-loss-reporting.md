# FR-7: Periodic Data Loss Reporting

**Requirement:** The firmware must report detected data loss every 10 seconds through the UART0 console.

**Status:** Implemented and tested on the PC, including the P0 audit corrections (M1 to M3, L1 to L6). **Hardware validation is pending.** No result in this document was observed on hardware.

## Where Data Can Be Lost

| Path | Where | Detectable | How |
|---|---|---|---|
| UART to TCP | RX FIFO overflows because the RX ring buffer is full | Yes, as an event | `UART_FIFO_OVF`. The byte count is not available |
| UART to TCP | Frame or parity error on the line | Yes, as an event | `UART_FRAME_ERR`, `UART_PARITY_ERR`. The frame-error interrupt is enabled by the firmware. Parity is disabled (8N1), so parity errors cannot occur |
| UART to TCP | Bytes waiting for `send()` when a session ends | Yes, in bytes | Bridge pending buffer |
| UART to TCP | Bytes UART1 already received but the bridge had not read when a session ends | Yes, in bytes | Read and counted at session end: RX ring buffer, the bytes the driver holds aside while the ring buffer is full, and the hardware FIFO |
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
| Frame error | **Data loss** | A byte arrived corrupted |
| TCP send failure or stalled client | **Data loss** | The session failed with data on its way |
| Session ended by link loss, a socket error or a disconnect request | **Data loss** | Pending bytes, and bytes UART1 held for the session, never reached their destination |
| Client closes normally | **Expected discard** | The client chose to leave. This is approved design decision 3 |
| UART data with no client connected | **Expected discard** | This is the approved behavior |
| RX ring buffer full | **Pressure, not loss** | The driver holds the data. A loss is reported only if the FIFO then overflows |
| Break on the RX line | **Line condition, not loss** | A break is a signal, not data |
| UART event not queued | **Accounting, not loss** | The event is still counted. See [Event Queue](#event-queue) |

Buffer exhaustion appears in two counters: `rx_buffer_full` is the warning, and `uart_rx_overflow` is the actual loss.

`bridge_request_disconnect()` is counted as loss because its only active caller is the Ethernet link-down event.

## Counters

Each counter is a lock-free 32-bit atomic in `main/bridge/loss_stats.c`. Reports show the change since the previous report.

| Report field | Unit | Exact definition | Updated by |
|---|---|---|---|
| `uart_rx_overflow` | Events | `UART_FIFO_OVF` events, queued or dropped: the RX FIFO overflowed and the driver reset it, losing up to 128 bytes each | Reporter task |
| `uart_rx_errors` | Events | `UART_FRAME_ERR` and `UART_PARITY_ERR` events, queued or dropped: a byte arrived corrupted | Reporter task |
| `tcp_send_failures` | Sessions | Sessions ended by a permanent `send()` error, or by 5 s without send progress | Bridge task |
| `tcp_tx_bytes` | Bytes | UART bytes the client never received when a session ended for any reason except a normal client close: the pending buffer, plus what UART1 still held | Bridge task |
| `uart_tx_bytes` | Bytes | TCP bytes not written to UART when a session ended for any reason except a normal client close | Bridge task |
| `session_bytes` | Bytes | The same bytes as `tcp_tx_bytes` and `uart_tx_bytes`, when the client closed normally | Bridge task |
| `no_client_bytes` | Bytes | UART bytes received after a session ended, or with no session, read and dropped. Includes the cleanup at session start | Bridge task |
| `rx_buffer_full` | Events | `UART_BUFFER_FULL` events, queued or dropped: the RX ring buffer filled | Reporter task |
| `uart_breaks` | Events | `UART_BREAK` events, queued or dropped | Reporter task |
| `event_queue_drops` | Events | UART driver events of any type that the full event queue rejected. They are still counted in the fields above | Reporter task, from the ISR count |
| `rx_error_pauses` | Times | Frame-error and break interrupts were paused because more than 2,000 arrived in one interval | Reporter task |

The first five fields are data loss. `session_bytes` and `no_client_bytes` are expected discards. `rx_buffer_full` is a pressure indicator, `uart_breaks` a line condition, and the last two describe the accounting itself.

## Report Format

One line is written every 10 seconds, even with no loss, so a silent console means that the reporter stopped.

The status is `DATA LOSS` when any loss field is above 0, and `no data loss` otherwise. The level is `W` when there is data loss, or when `event_queue_drops` or `rx_error_pauses` is above 0. Otherwise it is `I`.

```text
I (10326) LOSS: 10.0 s: no data loss: uart_rx_overflow=0 uart_rx_errors=0 tcp_send_failures=0 tcp_tx_bytes=0 uart_tx_bytes=0 | expected: session_bytes=0 no_client_bytes=0 | pressure: rx_buffer_full=0 | line: uart_breaks=0 | accounting: event_queue_drops=0 rx_error_pauses=0
W (20326) LOSS: 10.0 s: DATA LOSS: uart_rx_overflow=2 uart_rx_errors=0 tcp_send_failures=1 tcp_tx_bytes=2210 uart_tx_bytes=0 | expected: session_bytes=0 no_client_bytes=0 | pressure: rx_buffer_full=3 | line: uart_breaks=0 | accounting: event_queue_drops=0 rx_error_pauses=0
```

These lines illustrate the format. They are not captured from hardware. The longest possible line is under the 384-byte line buffer.

After each report that reaches a new low, the reporter also logs how much of its stack was never used:

```text
I (10330) LOSS: Reporter stack: 1840 of 3072 bytes never used
```

It is a `W` line below 512 bytes. It appears once after boot, then only if a later report goes deeper.

## Design

```text
UART1 ISR ── xQueueSendFromISR ──> --wrap counts drops ──> event queue (128) ──> loss_reporter ──> UART0
                                          │ atomic, by type                          ^  ^
                                          └──────────── uart_driver_get_drops() ─────┘  │ loads
bridge task ── atomic add ──> loss_stats ───────────────────────────────────────────────┘
```

- **The bridge never blocks on reporting** in its data path. It only performs atomic additions, and it writes no log lines for data loss. See [Console Lock](#console-lock) for the session-end log.
- **The reporter has priority 3, below the bridge at 5.** A slow console write delays only the reporter.
- **The reporter waits on the event queue.** It keeps the queue drained, and it uses `esp_timer` deadlines so reports do not drift.
- **The reporter does not touch sockets or the UART data path.**

### Frame Errors

ESP-IDF 6.1 installs the UART driver with `RXFIFO_FULL`, `RXFIFO_TOUT`, `RXFIFO_OVF`, `BRK_DET` and `PARITY_ERR` interrupts, but not `FRAM_ERR` (`esp_driver_uart/src/uart.c`, `UART_INTR_CONFIG_FLAG`). Without it, `uart_rx_errors` could never rise.

`uart_driver_init()` calls `uart_enable_intr_mask(UART_NUM_1, UART_INTR_FRAM_ERR | UART_INTR_BRK_DET)` after `uart_set_pin()`, so an unrouted default pin cannot raise errors. The call also clears any pending status. The driver ISR already handles the interrupt: it posts `UART_FRAME_ERR` and leaves the byte in the FIFO. Parity stays disabled.

A wrong baud rate or a noisy line can raise one frame error per received byte, up to 11,520 interrupts per second at 115200, and 8 times more at 921600. When frame errors and breaks together pass 2,000 in one interval, the reporter disables both interrupts until the next report. That interval's counts are then a lower bound, and `rx_error_pauses` shows it.

### Event Queue

**Size.** The driver posts an event for every RX interrupt, about one per 120 received bytes, and the reporter reads them all. A continuous stream gives about 96 data events per second at 115200, and about 768 at 921600. An overflowing FIFO gives up to about 720 overflow events per second at 921600. The queue has 128 entries of 12 bytes, which hold about 165 ms of events at 921600. The reporter's longest pause is writing one console line, about 25 ms.

**Drop detection.** When the queue is full, the driver's `xQueueSendFromISR()` fails, and the driver only logs at verbose level, which is compiled out. `main/CMakeLists.txt` links the firmware with `-Wl,--wrap=xQueueGenericSendFromISR`, so every ISR queue send in the image calls `__wrap_xQueueGenericSendFromISR()` in `uart_driver.c`. It calls the real function, and if that fails for the UART event queue, it adds 1 to a total and to a counter for the event's type.

- It is in IRAM, never blocks and takes no lock. Other queues cost one pointer comparison.
- It does not use a `switch`, which could become a jump table in flash.
- Without the linker option, the build fails, because `__real_xQueueGenericSendFromISR` is undefined.

The reporter adds the dropped overflow, error, buffer-full and break events to the same counters as queued ones. An event is either queued or dropped, never both, so each is counted once. Dropped data events matter only as `event_queue_drops`.

### Session-End Accounting

When a session ends, the bridge task reads and drops what UART1 already holds before it closes the socket:

1. It reads the ring buffer until it is empty. Each read lets the driver move the bytes it held aside while the buffer was full. The read stops at 2,304 bytes, the most UART1 can hold, so a continuous stream cannot keep it busy.
2. It reads the hardware FIFO length (`uart_ll_get_rxfifo_len()`, a register read), then reads that many bytes with a 20 ms timeout. They reach the ring buffer on the RX timeout, after 10 idle symbol times, under 1 ms at 115200.

Those bytes, plus the pending buffer, are counted as `tcp_tx_bytes` after an abnormal end, or as `session_bytes` after a normal close.

**No double counting:** every byte is counted only when it is read, and it is read only once. Bytes counted at session end are gone from the driver, so the later no-client cleanup cannot count them again. Bytes that arrive afterwards are counted as `no_client_bytes`.

### Reporter Timing

The reporter keeps the 10 s phase set at start. If it is delayed by more than one period, it writes one report covering the whole delay, then returns to the original phase. It does not write a burst of near-empty reports to catch up.

### Console Lock

ESP-IDF logging writes to UART0 under the standard output lock. The bridge logs only when a session ends, or on an error, never per byte. If the reporter is writing a line at that moment, the bridge waits for it, at most about 25 ms for a 300-character line at 115200 baud, and priority inheritance applies. This delays closing that one socket. The data path never takes the lock, so no change was made.

### Firmware Identity

At boot, `main.c` logs one line with the version, the ELF SHA256, the build time and the ESP-IDF version:

```text
I (310) MAIN: Firmware: version 0da27aa-dirty, ELF SHA256 <64 hex digits>, built Nov  3 2026 10:00:00, ESP-IDF v6.1
W (310) MAIN: Built from uncommitted changes: identify this firmware by its ELF SHA256, not its version
```

The version comes from `git describe`. Builds of uncommitted changes share one version, so the warning appears for them. Test records must cite the ELF SHA256.

## Changes to Existing Behavior

| Change | Reason | Effect |
|---|---|---|
| `uart_driver_install()` gets an event queue of 128 entries | Without it, the driver discards every overflow notice | The ISR also posts events. Baud rate and buffers are unchanged |
| The idle bridge reads and drops UART data every 100 ms | Otherwise an idle stream fills the ring buffer and looks like loss | The buffer fills in about 178 ms, so 100 ms keeps it from overflowing. Data with no client is still discarded |
| Frame-error interrupt enabled | M1: frame errors were never reported | One more interrupt source. Paused under a storm |
| ISR queue sends are wrapped | M2: drops were silent | One comparison per ISR queue send in the image |
| Session end reads what UART1 holds | M3: those bytes were reported as no-client data | Up to 2,304 bytes read without waiting, and at most one 20 ms wait for FIFO bytes, before the socket closes |

Baud rate, pacing, the 200 ms session cleanup, and the socket handling are unchanged.

## Test Coverage

Run the PC tests with `npm run test:host`. They need `gcc`, and no ESP32.

| Area | Test | File |
|---|---|---|
| Counter increments and independence | Unit | `tests/host/loss_test.c` |
| 800,000 increments from 4 threads | Unit | `tests/host/loss_test.c` |
| Delta across a 32-bit wrap | Unit | `tests/host/loss_test.c` |
| Which counters mean data loss, and which need attention | Unit | `tests/host/loss_test.c` |
| Exact report text, each field, and the largest possible line | Unit | `tests/host/loss_test.c` |
| M1: frame-error interrupt enabled after the pins, parity disabled, queue and buffer sizes | Real `uart_driver.c` with a mock driver | `tests/host/loss_test.c` |
| UART event mapping, including breaks | Real `uart_driver.c` | `tests/host/loss_test.c` |
| M2: drops counted by type, other queues ignored, queued events never counted as dropped | Real `uart_driver.c`, calling the wrapper as the ISR does | `tests/host/loss_test.c` |
| Report per interval, levels, reset between intervals, no drift | Real reporter with a mock driver | `tests/host/loss_test.c` |
| Report during an event flood | Real reporter with a mock driver | `tests/host/loss_test.c` |
| M2: dropped events in the report, each counted once in a partly dropped storm | Real reporter with a mock driver | `tests/host/loss_test.c` |
| Storm guard: interrupts paused, then resumed after the report | Real reporter with a mock driver | `tests/host/loss_test.c` |
| L1: one late report after a 2.5-period delay, with the phase kept | Real reporter, 200 ms period only | `tests/host/loss_test.c` |
| L5: stack line at the first low and at a new low, warning under 512 bytes | Real reporter | `tests/host/loss_test.c` |
| Real 10-second period | Real reporter with a mock driver | `tests/host/loss_test.c` |
| No client, clean session, normal close, send error, stall, disconnect request | Real `bridge.c` with sockets | `tests/host/bridge_test.c` |
| M3: bytes held by UART1 at the end of an H6 stall, a link loss, an RST and a normal close, with FIFO bytes, each counted once | Real `bridge.c` with sockets | `tests/host/bridge_test.c` |
| Existing bridge behavior | Real `bridge.c` with sockets | `tests/host/bridge_test.c` |

The Jest suite cannot verify FR-7, because the report appears only on UART0.

## Limitations

1. **Overflow bytes are unknown.** `uart_rx_overflow` counts events, and each one loses up to 128 bytes.
2. **lwIP buffers are not visible.** Data accepted by `send()` but not delivered, and data unread in the lwIP receive buffer, are not counted. A stalled session can lose up to the 5,760-byte send buffer beyond `tcp_tx_bytes`.
3. **Session-end boundary.** Bytes arriving during the session-end read, which takes microseconds, are counted with the session. If the FIFO overflows before its bytes are read, the 20 ms read can take later bytes instead. Neither case counts a byte twice.
4. **Storm counts are a lower bound.** After a pause, frame errors and breaks are not seen until the next report.
5. **Breaks and frame errors.** On the ESP32 a break may also raise a frame error, which would count as `uart_rx_errors`. This has not been checked on hardware.
6. **Counters are not one atomic set.** A value updated during a snapshot appears in the next report. Nothing is lost.
7. **Boundary reports.** Events are counted when the reporter reads them, so one can land in the next interval.
8. **Abrupt client exits count as loss.** A client that resets the connection with data in flight, as in H5, reports loss. Its data really was not delivered.
9. **The queue-send wrapper is image-wide.** It adds one comparison to every ISR queue send, including those of other drivers.
10. **No hardware validation yet.**

## Required Hardware Validation

See `docs/test-records/hw-validation-fr7.md`.
