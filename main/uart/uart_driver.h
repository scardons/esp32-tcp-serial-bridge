
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

esp_err_t uart_driver_init(void);

int uart_driver_write(const uint8_t *data, size_t length);

int uart_driver_read(
    uint8_t *buffer,
    size_t capacity,
    uint32_t timeout_ms
);

#endif // UART_DRIVER_H
