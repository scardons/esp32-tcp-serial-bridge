#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * UART Driver Interface
 *
 * Declares the functions used to initialize UART1
 * and send and receive serial data.
 */

#define UART_DRIVER_RX_BUFFER_SIZE 2048 // RX ring buffer
#define UART_DRIVER_RX_FIFO_SIZE   128  // Hardware RX FIFO

/*
 * At 921600 baud a continuous stream raises about 768 data events per second,
 * one per 120 received bytes, and an overflowing FIFO about 720 per second.
 * 128 entries hold about 165 ms of either, several times the longest pause of
 * the reader, which is one console line of about 25 ms. Each entry is 12 bytes.
 */
#define UART_DRIVER_EVENT_QUEUE_SIZE 128

typedef enum {
    UART_DRIVER_EVENT_NONE,           // Timed out, or an event without loss
    UART_DRIVER_EVENT_RX_OVERFLOW,    // RX FIFO overflowed and was reset
    UART_DRIVER_EVENT_RX_ERROR,       // Frame or parity error
    UART_DRIVER_EVENT_RX_BUFFER_FULL, // RX ring buffer is full
    UART_DRIVER_EVENT_BREAK,          // Break condition on the RX line

    UART_DRIVER_EVENT_COUNT,
} uart_driver_event_t;

/**
 * Events the driver could not queue because the event queue was full.
 * The values only increase, and wrap around at 2^32.
 */
typedef struct {
    uint32_t total;                                // Events of every type
    uint32_t by_event[UART_DRIVER_EVENT_COUNT];    // [NONE] holds data and other events
} uart_driver_drops_t;

esp_err_t uart_driver_init(void);

int uart_driver_write(const uint8_t *data, size_t length);

int uart_driver_read(
    uint8_t *buffer,
    size_t capacity,
    uint32_t timeout_ms
);

/**
 * Waits for the next driver event. The caller must keep reading
 * events, so that the event queue does not fill up.
 */
uart_driver_event_t uart_driver_wait_event(uint32_t timeout_ms);

/**
 * Reads the count of events dropped on a full event queue.
 * Safe to call from any task.
 */
void uart_driver_get_drops(uart_driver_drops_t *drops);

/**
 * Returns the bytes in the hardware RX FIFO. They reach the ring buffer
 * on the next RX interrupt, at the latest after 10 idle symbol times.
 */
size_t uart_driver_rx_fifo_len(void);

/**
 * Enables or disables the frame-error and break interrupts.
 * Frame errors are enabled by uart_driver_init().
 */
esp_err_t uart_driver_set_line_error_interrupts(bool enable);

#endif // UART_DRIVER_H
