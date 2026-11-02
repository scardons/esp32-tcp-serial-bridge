#ifndef LOSS_STATS_H
#define LOSS_STATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Data Loss Statistics (FR-7)
 *
 * Lock-free counters that any task can update without blocking.
 * The definition of each counter is in docs/fr7-data-loss-reporting.md.
 */

typedef enum {
    // Detected data loss.
    LOSS_UART_RX_OVERFLOW,   // UART RX FIFO overflow events
    LOSS_UART_RX_ERROR,      // UART frame or parity error events
    LOSS_TCP_SEND_FAILURE,   // Sessions ended by a send error or a stalled client
    LOSS_TCP_TX_BYTES,       // UART bytes never sent, session ended abnormally
    LOSS_UART_TX_BYTES,      // TCP bytes never written, session ended abnormally

    // Expected discards, reported apart from data loss.
    DISCARD_SESSION_BYTES,   // UART bytes dropped when a client closed normally
    DISCARD_NO_CLIENT_BYTES, // UART bytes dropped with no client connected

    // Pressure indicator: no bytes are lost by this event alone.
    PRESSURE_RX_BUFFER_FULL, // UART RX ring buffer full events

    LOSS_COUNTER_COUNT,
} loss_counter_t;

typedef struct {
    uint32_t values[LOSS_COUNTER_COUNT];
} loss_snapshot_t;

void loss_stats_add(loss_counter_t counter, uint32_t amount);

void loss_stats_snapshot(loss_snapshot_t *snapshot);

/**
 * Computes current - previous. Counters that wrapped around still
 * give the right result, because the arithmetic is unsigned.
 */
void loss_stats_delta(const loss_snapshot_t *previous,
                      const loss_snapshot_t *current,
                      loss_snapshot_t *delta);

/**
 * Returns true if the snapshot contains detected data loss.
 * Expected discards and pressure events do not count.
 */
bool loss_stats_has_loss(const loss_snapshot_t *snapshot);

/**
 * Writes the one-line report. Returns the length of the text,
 * which is truncated if it does not fit in the buffer.
 */
int loss_stats_format(const loss_snapshot_t *delta, uint32_t interval_ms,
                      char *buffer, size_t size);

#endif // LOSS_STATS_H
