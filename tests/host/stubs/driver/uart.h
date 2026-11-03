#pragma once
/* Just enough of the ESP-IDF UART driver to build the real uart_driver.c. */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
typedef int uart_port_t;
enum { UART_NUM_1 = 1 };
enum { UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT };
enum { UART_PIN_NO_CHANGE = -1 };
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
typedef enum { UART_DATA, UART_BREAK, UART_BUFFER_FULL, UART_FIFO_OVF, UART_FRAME_ERR, UART_PARITY_ERR, UART_EVENT_MAX } uart_event_type_t;
typedef struct { uart_event_type_t type; size_t size; } uart_event_t;
esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int queue_size, QueueHandle_t *queue, int flags);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
int uart_write_bytes(uart_port_t port, const void *data, size_t length);
int uart_read_bytes(uart_port_t port, void *data, size_t length, TickType_t ticks);
esp_err_t uart_enable_intr_mask(uart_port_t port, uint32_t mask);
esp_err_t uart_disable_intr_mask(uart_port_t port, uint32_t mask);
