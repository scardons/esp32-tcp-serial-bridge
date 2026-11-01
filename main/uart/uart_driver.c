
#include "uart_driver.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define BRIDGE_UART_PORT       UART_NUM_1
#define BRIDGE_UART_TX_PIN     32
#define BRIDGE_UART_RX_PIN     33
#define BRIDGE_UART_BAUD_RATE  115200
#define BRIDGE_UART_BUF_SIZE   2048

/**
 * UART Driver Implementation
 *
 * Initializes UART1 and provides functions to transmit
 * and receive serial data for the TCP-Serial Bridge.
 */

esp_err_t uart_driver_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = BRIDGE_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(
        uart_driver_install(
            BRIDGE_UART_PORT,
            BRIDGE_UART_BUF_SIZE,
            BRIDGE_UART_BUF_SIZE,
            0,
            NULL,
            0
        ),
        "UART_DRIVER",
        "Failed to install UART driver"
    );

    ESP_RETURN_ON_ERROR(
        uart_param_config(BRIDGE_UART_PORT, &uart_config),
        "UART_DRIVER",
        "Failed to configure UART"
    );

    ESP_RETURN_ON_ERROR(
        uart_set_pin(
            BRIDGE_UART_PORT,
            BRIDGE_UART_TX_PIN,
            BRIDGE_UART_RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        ),
        "UART_DRIVER",
        "Failed to configure UART pins"
    );

    return ESP_OK;
}

int uart_driver_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return -1;
    }

    return uart_write_bytes(BRIDGE_UART_PORT, data, length);
}

int uart_driver_read(
    uint8_t *buffer,
    size_t capacity,
    uint32_t timeout_ms
)
{
    if (buffer == NULL || capacity == 0) {
        return -1;
    }

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    return uart_read_bytes(
        BRIDGE_UART_PORT,
        buffer,
        capacity,
        timeout_ticks
    );
}
