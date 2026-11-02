
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * UART Driver Interface
 *
 * Declares the functions used to initialize UART1
 * and send and receive serial data.
 */

typedef enum {
    UART_DRIVER_EVENT_NONE,           // Timed out, or an event without loss
    UART_DRIVER_EVENT_RX_OVERFLOW,    // RX FIFO overflowed and was reset
    UART_DRIVER_EVENT_RX_ERROR,       // Frame or parity error
    UART_DRIVER_EVENT_RX_BUFFER_FULL, // RX ring buffer is full
} uart_driver_event_t;

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

#endif // UART_DRIVER_H
