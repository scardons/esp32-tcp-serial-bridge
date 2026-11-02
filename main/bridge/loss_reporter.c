
#include "loss_reporter.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "loss_stats.h"
#include "uart_driver.h"

#define LOSS_REPORTER_TASK_STACK    3072
#define LOSS_REPORTER_TASK_PRIORITY 3    // Below the bridge, so logging cannot delay it

#ifndef LOSS_REPORT_PERIOD_US
#define LOSS_REPORT_PERIOD_US       (10 * 1000 * 1000)
#endif

#define LOSS_REPORT_LINE_SIZE       256

static const char *TAG = "LOSS";

static void record_uart_event(uart_driver_event_t event)
{
    switch (event) {
    case UART_DRIVER_EVENT_RX_OVERFLOW:
        loss_stats_add(LOSS_UART_RX_OVERFLOW, 1);
        break;
    case UART_DRIVER_EVENT_RX_ERROR:
        loss_stats_add(LOSS_UART_RX_ERROR, 1);
        break;
    case UART_DRIVER_EVENT_RX_BUFFER_FULL:
        loss_stats_add(PRESSURE_RX_BUFFER_FULL, 1);
        break;
    default:
        break;
    }
}

static void write_report(loss_snapshot_t *previous, int64_t interval_us)
{
    loss_snapshot_t current;
    loss_snapshot_t delta;
    char line[LOSS_REPORT_LINE_SIZE];

    loss_stats_snapshot(&current);
    loss_stats_delta(previous, &current, &delta);
    loss_stats_format(&delta, (uint32_t)(interval_us / 1000), line, sizeof(line));

    if (loss_stats_has_loss(&delta)) {
        ESP_LOGW(TAG, "%s", line);
    } else {
        ESP_LOGI(TAG, "%s", line);
    }

    *previous = current;
}

// Waiting on the event queue keeps it drained between reports.
static void loss_reporter_task(void *arg)
{
    loss_snapshot_t previous;
    int64_t last_report_us = esp_timer_get_time();
    int64_t next_report_us = last_report_us + LOSS_REPORT_PERIOD_US;

    loss_stats_snapshot(&previous);

    while (true) {
        const int64_t now_us = esp_timer_get_time();

        if (now_us >= next_report_us) {
            write_report(&previous, now_us - last_report_us);
            last_report_us = now_us;
            next_report_us += LOSS_REPORT_PERIOD_US;
            continue;
        }

        const uint32_t wait_ms = (uint32_t)((next_report_us - now_us + 999) / 1000);

        record_uart_event(uart_driver_wait_event(wait_ms));
    }
}

esp_err_t loss_reporter_start(void)
{
    if (xTaskCreate(loss_reporter_task, "loss_reporter", LOSS_REPORTER_TASK_STACK,
                    NULL, LOSS_REPORTER_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
