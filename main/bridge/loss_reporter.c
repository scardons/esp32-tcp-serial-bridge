
#include "loss_reporter.h"

#include <stdbool.h>
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

#define LOSS_REPORT_LINE_SIZE       384

/*
 * More frame errors and breaks than this in one interval mean a faulty line or
 * a wrong baud rate, which can raise an interrupt for every received byte. The
 * two interrupts are then paused until the next report, so they cannot take
 * over the CPU, and the counts for that interval are a lower bound.
 */
#ifndef LOSS_LINE_ERROR_LIMIT
#define LOSS_LINE_ERROR_LIMIT       2000
#endif

// A stack with less free space than this at its deepest use is logged as a warning.
#define LOSS_STACK_WARN_BYTES       512

static const char *TAG = "LOSS";

typedef struct {
    loss_snapshot_t previous;
    uart_driver_drops_t drops;        // Drop counts already moved into loss_stats
    uint32_t line_errors_at_start;    // Frame errors and breaks when the interval began
    bool line_errors_paused;
    uint32_t stack_low;               // Lowest stack high-water mark logged so far
} reporter_t;

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
    case UART_DRIVER_EVENT_BREAK:
        loss_stats_add(LINE_UART_BREAK, 1);
        break;
    default:
        break;
    }
}

/*
 * Adds the events dropped since the last call to the same counters as queued
 * events. The driver counts each event either as queued or as dropped, never
 * both, so nothing is counted twice.
 */
static void collect_dropped_events(reporter_t *reporter)
{
    uart_driver_drops_t now;

    uart_driver_get_drops(&now);

    loss_stats_add(ACCOUNT_EVENT_QUEUE_DROPS, now.total - reporter->drops.total);
    loss_stats_add(LOSS_UART_RX_OVERFLOW,
                   now.by_event[UART_DRIVER_EVENT_RX_OVERFLOW] -
                   reporter->drops.by_event[UART_DRIVER_EVENT_RX_OVERFLOW]);
    loss_stats_add(LOSS_UART_RX_ERROR,
                   now.by_event[UART_DRIVER_EVENT_RX_ERROR] -
                   reporter->drops.by_event[UART_DRIVER_EVENT_RX_ERROR]);
    loss_stats_add(PRESSURE_RX_BUFFER_FULL,
                   now.by_event[UART_DRIVER_EVENT_RX_BUFFER_FULL] -
                   reporter->drops.by_event[UART_DRIVER_EVENT_RX_BUFFER_FULL]);
    loss_stats_add(LINE_UART_BREAK,
                   now.by_event[UART_DRIVER_EVENT_BREAK] -
                   reporter->drops.by_event[UART_DRIVER_EVENT_BREAK]);

    reporter->drops = now;
}

static uint32_t line_errors(void)
{
    return loss_stats_get(LOSS_UART_RX_ERROR) + loss_stats_get(LINE_UART_BREAK);
}

static void limit_line_errors(reporter_t *reporter)
{
    if (reporter->line_errors_paused ||
        line_errors() - reporter->line_errors_at_start < LOSS_LINE_ERROR_LIMIT) {
        return;
    }

    if (uart_driver_set_line_error_interrupts(false) == ESP_OK) {
        reporter->line_errors_paused = true;
        loss_stats_add(ACCOUNT_RX_ERROR_PAUSES, 1);
    }
}

static void resume_line_errors(reporter_t *reporter)
{
    if (reporter->line_errors_paused &&
        uart_driver_set_line_error_interrupts(true) == ESP_OK) {
        reporter->line_errors_paused = false;
    }

    reporter->line_errors_at_start = line_errors();
}

static void write_report(reporter_t *reporter, int64_t interval_us)
{
    // Static to keep it off the task stack. Only this task uses it.
    static char line[LOSS_REPORT_LINE_SIZE];
    loss_snapshot_t current;
    loss_snapshot_t delta;

    collect_dropped_events(reporter);
    loss_stats_snapshot(&current);
    loss_stats_delta(&reporter->previous, &current, &delta);
    loss_stats_format(&delta, (uint32_t)(interval_us / 1000), line, sizeof(line));

    if (loss_stats_needs_attention(&delta)) {
        ESP_LOGW(TAG, "%s", line);
    } else {
        ESP_LOGI(TAG, "%s", line);
    }

    reporter->previous = current;
}

// Measured after a report, which is the deepest use of the stack.
static void log_stack_low(reporter_t *reporter)
{
    const uint32_t unused = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    if (unused >= reporter->stack_low) {
        return;
    }

    reporter->stack_low = unused;

    if (unused < LOSS_STACK_WARN_BYTES) {
        ESP_LOGW(TAG, "Reporter stack: %u of %u bytes never used",
                 (unsigned)unused, (unsigned)LOSS_REPORTER_TASK_STACK);
    } else {
        ESP_LOGI(TAG, "Reporter stack: %u of %u bytes never used",
                 (unsigned)unused, (unsigned)LOSS_REPORTER_TASK_STACK);
    }
}

// Waiting on the event queue keeps it drained between reports.
static void loss_reporter_task(void *arg)
{
    static reporter_t reporter = {.stack_low = UINT32_MAX};
    int64_t last_report_us = esp_timer_get_time();
    int64_t next_report_us = last_report_us + LOSS_REPORT_PERIOD_US;

    uart_driver_get_drops(&reporter.drops);
    loss_stats_snapshot(&reporter.previous);
    reporter.line_errors_at_start = line_errors();

    while (true) {
        const int64_t now_us = esp_timer_get_time();

        if (now_us >= next_report_us) {
            write_report(&reporter, now_us - last_report_us);
            resume_line_errors(&reporter);
            log_stack_low(&reporter);
            last_report_us = now_us;

            // After a delay of several periods, one report covers all of
            // them, instead of a burst of near-empty ones. The phase is kept.
            do {
                next_report_us += LOSS_REPORT_PERIOD_US;
            } while (next_report_us <= now_us);

            continue;
        }

        const uint32_t wait_ms = (uint32_t)((next_report_us - now_us + 999) / 1000);

        record_uart_event(uart_driver_wait_event(wait_ms));
        collect_dropped_events(&reporter);
        limit_line_errors(&reporter);
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
