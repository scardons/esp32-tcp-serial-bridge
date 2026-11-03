
#include "uart_driver.h"

#include <stdatomic.h>

#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "hal/uart_ll.h"

#define BRIDGE_UART_PORT       UART_NUM_1
#define BRIDGE_UART_TX_PIN     32
#define BRIDGE_UART_RX_PIN     33
#define BRIDGE_UART_BAUD_RATE  115200

// Frame errors are not in the driver's default interrupt set, so the driver
// would never report them. Breaks are. Both are paused together when a faulty
// line raises them faster than the reporter can use them.
#define LINE_ERROR_INTERRUPTS  (UART_INTR_FRAM_ERR | UART_INTR_BRK_DET)

static QueueHandle_t s_event_queue = NULL;

// Written from interrupts, so lock-free atomics in internal RAM.
static atomic_uint_least32_t s_drops_total;
static atomic_uint_least32_t s_drops_by_event[UART_DRIVER_EVENT_COUNT];

/**
 * UART Driver Implementation
 *
 * Initializes UART1 and provides functions to transmit
 * and receive serial data for the TCP-Serial Bridge.
 */

// Also runs in an interrupt, so it uses comparisons and not a switch, which
// the compiler can turn into a jump table in flash.
FORCE_INLINE_ATTR uart_driver_event_t map_event(uart_event_type_t type)
{
    if (type == UART_FIFO_OVF) {
        return UART_DRIVER_EVENT_RX_OVERFLOW;
    }

    if (type == UART_FRAME_ERR || type == UART_PARITY_ERR) {
        return UART_DRIVER_EVENT_RX_ERROR;
    }

    if (type == UART_BUFFER_FULL) {
        return UART_DRIVER_EVENT_RX_BUFFER_FULL;
    }

    if (type == UART_BREAK) {
        return UART_DRIVER_EVENT_BREAK;
    }

    return UART_DRIVER_EVENT_NONE;
}

BaseType_t __real_xQueueGenericSendFromISR(QueueHandle_t queue, const void *item,
                                           BaseType_t *woken, BaseType_t position);

/*
 * The linker calls this in place of xQueueGenericSendFromISR() everywhere,
 * because main/CMakeLists.txt passes --wrap for it. The UART driver drops an
 * event without notice when its queue is full, and this counts the drop.
 *
 * It runs in every interrupt that sends to a queue, so it stays in IRAM and
 * never blocks: one pointer comparison, and atomic additions only on a drop.
 */
IRAM_ATTR BaseType_t __wrap_xQueueGenericSendFromISR(QueueHandle_t queue, const void *item,
                                                     BaseType_t *woken, BaseType_t position)
{
    const BaseType_t sent = __real_xQueueGenericSendFromISR(queue, item, woken, position);

    if (sent != pdPASS && queue != NULL && queue == s_event_queue) {
        const uart_event_t *event = item;

        atomic_fetch_add_explicit(&s_drops_total, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s_drops_by_event[map_event(event->type)], 1,
                                  memory_order_relaxed);
    }

    return sent;
}

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
            UART_DRIVER_RX_BUFFER_SIZE,
            UART_DRIVER_RX_BUFFER_SIZE,
            UART_DRIVER_EVENT_QUEUE_SIZE,
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

    // Only once RX is on its pin, so an unconnected default pin cannot
    // produce errors. Enabling also clears any pending frame error.
    ESP_RETURN_ON_ERROR(
        uart_driver_set_line_error_interrupts(true),
        "UART_DRIVER",
        "Failed to enable UART frame error interrupt"
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

    return map_event(event.type);
}

void uart_driver_get_drops(uart_driver_drops_t *drops)
{
    drops->total = atomic_load_explicit(&s_drops_total, memory_order_relaxed);

    for (size_t index = 0; index < UART_DRIVER_EVENT_COUNT; index++) {
        drops->by_event[index] = atomic_load_explicit(&s_drops_by_event[index],
                                                      memory_order_relaxed);
    }
}

size_t uart_driver_rx_fifo_len(void)
{
    // A read of status registers only, so it does not disturb the driver.
    return uart_ll_get_rxfifo_len(UART_LL_GET_HW(BRIDGE_UART_PORT));
}

esp_err_t uart_driver_set_line_error_interrupts(bool enable)
{
    return enable ? uart_enable_intr_mask(BRIDGE_UART_PORT, LINE_ERROR_INTERRUPTS)
                  : uart_disable_intr_mask(BRIDGE_UART_PORT, LINE_ERROR_INTERRUPTS);
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
