
#include "uart_driver.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"

#define BRIDGE_UART_PORT       UART_NUM_1
#define BRIDGE_UART_TX_PIN     32
#define BRIDGE_UART_RX_PIN     33
#define BRIDGE_UART_BAUD_RATE  115200
#define BRIDGE_UART_BUF_SIZE   2048
#define BRIDGE_UART_EVENT_QUEUE_SIZE 32

static QueueHandle_t s_event_queue = NULL;

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
            BRIDGE_UART_EVENT_QUEUE_SIZE,
            &s_event_queue,
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

uart_driver_event_t uart_driver_wait_event(uint32_t timeout_ms)
{
    uart_event_t event;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);

    // Short waits would round down to no wait, and the caller would spin.
    if (ticks == 0 && timeout_ms > 0) {
        ticks = 1;
    }

    if (s_event_queue == NULL ||
        xQueueReceive(s_event_queue, &event, ticks) != pdTRUE) {
        return UART_DRIVER_EVENT_NONE;
    }

    switch (event.type) {
    case UART_FIFO_OVF:
        return UART_DRIVER_EVENT_RX_OVERFLOW;
    case UART_FRAME_ERR:
    case UART_PARITY_ERR:
        return UART_DRIVER_EVENT_RX_ERROR;
    case UART_BUFFER_FULL:
        return UART_DRIVER_EVENT_RX_BUFFER_FULL;
    default:
        return UART_DRIVER_EVENT_NONE;
    }
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
