
#include "loss_stats.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_uint_least32_t s_counters[LOSS_COUNTER_COUNT];

void loss_stats_add(loss_counter_t counter, uint32_t amount)
{
    if (counter < LOSS_COUNTER_COUNT) {
        atomic_fetch_add_explicit(&s_counters[counter], amount,
                                  memory_order_relaxed);
    }
}

void loss_stats_snapshot(loss_snapshot_t *snapshot)
{
    for (size_t index = 0; index < LOSS_COUNTER_COUNT; index++) {
        snapshot->values[index] = atomic_load_explicit(&s_counters[index],
                                                       memory_order_relaxed);
    }
}

void loss_stats_delta(const loss_snapshot_t *previous,
                      const loss_snapshot_t *current,
                      loss_snapshot_t *delta)
{
    for (size_t index = 0; index < LOSS_COUNTER_COUNT; index++) {
        delta->values[index] = current->values[index] - previous->values[index];
    }
}

bool loss_stats_has_loss(const loss_snapshot_t *snapshot)
{
    return snapshot->values[LOSS_UART_RX_OVERFLOW] != 0 ||
           snapshot->values[LOSS_UART_RX_ERROR] != 0 ||
           snapshot->values[LOSS_TCP_SEND_FAILURE] != 0 ||
           snapshot->values[LOSS_TCP_TX_BYTES] != 0 ||
           snapshot->values[LOSS_UART_TX_BYTES] != 0;
}

int loss_stats_format(const loss_snapshot_t *delta, uint32_t interval_ms,
                      char *buffer, size_t size)
{
    // Rounded, so that a 9,980 ms interval reads as 10.0 s.
    const uint32_t tenths = interval_ms / 100 + (interval_ms % 100 >= 50 ? 1 : 0);

    return snprintf(
        buffer, size,
        "%" PRIu32 ".%" PRIu32 " s: %s: uart_rx_overflow=%" PRIu32
        " uart_rx_errors=%" PRIu32 " tcp_send_failures=%" PRIu32
        " tcp_tx_bytes=%" PRIu32 " uart_tx_bytes=%" PRIu32
        " | expected: session_bytes=%" PRIu32 " no_client_bytes=%" PRIu32
        " | rx_buffer_full=%" PRIu32,
        tenths / 10, tenths % 10,
        loss_stats_has_loss(delta) ? "DATA LOSS" : "no data loss",
        delta->values[LOSS_UART_RX_OVERFLOW],
        delta->values[LOSS_UART_RX_ERROR],
        delta->values[LOSS_TCP_SEND_FAILURE],
        delta->values[LOSS_TCP_TX_BYTES],
        delta->values[LOSS_UART_TX_BYTES],
        delta->values[DISCARD_SESSION_BYTES],
        delta->values[DISCARD_NO_CLIENT_BYTES],
        delta->values[PRESSURE_RX_BUFFER_FULL]);
}
