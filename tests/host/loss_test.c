/* FR-7 unit tests. These run the real loss_stats.c, loss_reporter.c and uart_driver.c. */
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/uart_ll.h"
#include "loss_reporter.h"
#include "loss_stats.h"
#include "uart_driver.h"

#ifndef TIMING_LIMIT_S
#define TIMING_LIMIT_S 0.1
#endif

/* Same default as loss_reporter.c, which is built with the same flags. */
#ifndef LOSS_LINE_ERROR_LIMIT
#define LOSS_LINE_ERROR_LIMIT 2000
#endif
#define LOSS_LINE_ERROR_LIMIT_TEST LOSS_LINE_ERROR_LIMIT

static int passed, failed;
#define CHECK(c, ...) do { if (c) { passed++; printf("  ok   "); } else { failed++; printf("  FAIL "); } \
    printf(__VA_ARGS__); printf("\n"); } while (0)

#define MAX_REPORTS 64
#define LINE 384
static struct { char level; char text[LINE]; int64_t at; } reports[MAX_REPORTS];
static int report_count;
static char stack_lines[4][LINE + 4];
static int stack_line_count;
static pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;
/* When set, the next report blocks this long in the console, like a slow UART0. */
static atomic_long slow_console_us;

void test_log(char level, const char *tag, const char *format, ...)
{
    if (strcmp(tag, "LOSS") != 0) {
        return;
    }

    char text[LINE];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    pthread_mutex_lock(&report_mutex);

    if (strncmp(text, "Reporter stack:", 15) == 0) {
        if (stack_line_count < 4) {
            snprintf(stack_lines[stack_line_count], sizeof(stack_lines[0]), "%c %s", level, text);
        }

        stack_line_count++;
    } else if (report_count < MAX_REPORTS) {
        reports[report_count].level = level;
        memcpy(reports[report_count].text, text, sizeof(text));
        reports[report_count].at = esp_timer_get_time();
        report_count++;
    }

    pthread_mutex_unlock(&report_mutex);

    const long delay = atomic_exchange(&slow_console_us, 0);

    if (delay > 0) {
        usleep((useconds_t)delay);
    }
}

/* Driver mock, used by the real uart_driver.c */
static QueueHandle_t driver_queue;
static int installed_queue_size, installed_rx_size;
static int configured_parity = -1;
static int call_order, pins_set_at, frame_enabled_at;
static atomic_uint intr_enabled = 0;
static atomic_int enable_calls, disable_calls;

esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int size, QueueHandle_t *q, int f)
{
    (void)p; (void)tx; (void)f;
    installed_queue_size = size;
    installed_rx_size = rx;
    driver_queue = xQueueCreate(size, sizeof(uart_event_t));
    *q = driver_queue;
    /* The real driver enables breaks and parity errors, not frame errors. */
    intr_enabled = UART_INTR_BRK_DET | UART_INTR_PARITY_ERR;
    return ESP_OK;
}
esp_err_t uart_param_config(uart_port_t p, const uart_config_t *c) { (void)p; configured_parity = c->parity; return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t p, int t, int r, int a, int b) { (void)p; (void)t; (void)r; (void)a; (void)b; pins_set_at = ++call_order; return ESP_OK; }
esp_err_t uart_enable_intr_mask(uart_port_t p, uint32_t m)
{
    (void)p;
    if (m & UART_INTR_FRAM_ERR) frame_enabled_at = ++call_order;
    intr_enabled |= m;
    enable_calls++;
    return ESP_OK;
}
esp_err_t uart_disable_intr_mask(uart_port_t p, uint32_t m) { (void)p; intr_enabled &= ~m; disable_calls++; return ESP_OK; }
int uart_write_bytes(uart_port_t p, const void *d, size_t n) { (void)p; (void)d; return (int)n; }
int uart_read_bytes(uart_port_t p, void *d, size_t n, TickType_t t) { (void)p; (void)d; (void)n; (void)t; return 0; }
size_t test_rxfifo_len(void) { return 37; }

/* The real FreeRTOS function, which the firmware build reaches through --wrap.
 * When force_full is set, it behaves as if the queue were full. */
static atomic_int force_full;

BaseType_t __real_xQueueGenericSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken, BaseType_t position)
{
    (void)woken; (void)position;
    return force_full ? pdFALSE : xQueueSend(q, item, 0);
}

BaseType_t __wrap_xQueueGenericSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken, BaseType_t position);

/* What the UART ISR does: every event goes through the wrapped queue send. */
static void post_event(uart_event_type_t type)
{
    uart_event_t event = {.type = type};
    BaseType_t woken = 0;

    __wrap_xQueueGenericSendFromISR(driver_queue, &event, &woken, 0);
}

static void post_events(uart_event_type_t type, int count)
{
    for (int i = 0; i < count; i++) {
        post_event(type);
    }
}

static void *adder(void *arg)
{
    for (int i = 0; i < 200000; i++) {
        loss_stats_add((loss_counter_t)(intptr_t)arg, 1);
    }

    return NULL;
}

static void test_counters(void)
{
    loss_snapshot_t start, end, delta;

    printf("Counters\n");
    loss_stats_snapshot(&start);
    loss_stats_add(LOSS_TCP_TX_BYTES, 7);
    loss_stats_add(LOSS_TCP_TX_BYTES, 3);
    loss_stats_add(DISCARD_NO_CLIENT_BYTES, 5);
    loss_stats_add(LOSS_COUNTER_COUNT, 99);
    loss_stats_snapshot(&end);
    loss_stats_delta(&start, &end, &delta);
    CHECK(delta.values[LOSS_TCP_TX_BYTES] == 10, "increments add up");
    CHECK(delta.values[DISCARD_NO_CLIENT_BYTES] == 5, "counters are independent");

    pthread_t threads[4];

    loss_stats_snapshot(&start);

    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, adder, (void *)(intptr_t)LOSS_UART_RX_OVERFLOW);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    loss_stats_snapshot(&end);
    loss_stats_delta(&start, &end, &delta);
    CHECK(delta.values[LOSS_UART_RX_OVERFLOW] == 800000, "4 threads lose no increment (%u)", delta.values[LOSS_UART_RX_OVERFLOW]);

    loss_snapshot_t before = {{0}}, after = {{0}};

    before.values[LOSS_TCP_TX_BYTES] = 0xfffffff0u;
    after.values[LOSS_TCP_TX_BYTES] = 0x10u;
    loss_stats_delta(&before, &after, &delta);
    CHECK(delta.values[LOSS_TCP_TX_BYTES] == 0x20u, "delta is correct across a 32-bit wrap");
}

static void test_classification(void)
{
    loss_snapshot_t s = {{0}};

    printf("Classification\n");
    CHECK(!loss_stats_has_loss(&s), "all zero is no loss");

    const loss_counter_t lost[] = {LOSS_UART_RX_OVERFLOW, LOSS_UART_RX_ERROR, LOSS_TCP_SEND_FAILURE, LOSS_TCP_TX_BYTES, LOSS_UART_TX_BYTES};
    const loss_counter_t fine[] = {DISCARD_SESSION_BYTES, DISCARD_NO_CLIENT_BYTES, PRESSURE_RX_BUFFER_FULL,
                                   LINE_UART_BREAK, ACCOUNT_EVENT_QUEUE_DROPS, ACCOUNT_RX_ERROR_PAUSES};
    const size_t fine_count = sizeof(fine) / sizeof(fine[0]);
    int ok = 1;

    for (size_t i = 0; i < 5; i++) {
        memset(&s, 0, sizeof(s));
        s.values[lost[i]] = 1;
        ok &= loss_stats_has_loss(&s);
    }

    CHECK(ok, "each loss counter alone means data loss");
    ok = 1;

    for (size_t i = 0; i < fine_count; i++) {
        memset(&s, 0, sizeof(s));
        s.values[fine[i]] = 1000;
        ok &= !loss_stats_has_loss(&s);
    }

    CHECK(ok, "expected discards, pressure, breaks and accounting alone do not");

    memset(&s, 0, sizeof(s));
    CHECK(!loss_stats_needs_attention(&s), "all zero needs no attention");
    s.values[ACCOUNT_EVENT_QUEUE_DROPS] = 1;
    CHECK(loss_stats_needs_attention(&s) && !loss_stats_has_loss(&s), "queue drops: attention, but not data loss");
    memset(&s, 0, sizeof(s));
    s.values[ACCOUNT_RX_ERROR_PAUSES] = 1;
    CHECK(loss_stats_needs_attention(&s), "paused error interrupts: attention");
    memset(&s, 0, sizeof(s));
    s.values[LINE_UART_BREAK] = 5;
    s.values[DISCARD_NO_CLIENT_BYTES] = 5;
    CHECK(!loss_stats_needs_attention(&s), "breaks and expected discards: no attention");
    memset(&s, 0, sizeof(s));
    s.values[LOSS_TCP_TX_BYTES] = 1;
    CHECK(loss_stats_needs_attention(&s), "data loss: attention");
}

static void test_format(void)
{
    loss_snapshot_t s = {{0}};
    char line[LINE];

    printf("Format\n");
    loss_stats_format(&s, 10000, line, sizeof(line));
    printf("       %s\n", line);
    CHECK(strcmp(line, "10.0 s: no data loss: uart_rx_overflow=0 uart_rx_errors=0 tcp_send_failures=0 "
                       "tcp_tx_bytes=0 uart_tx_bytes=0 | expected: session_bytes=0 no_client_bytes=0 "
                       "| pressure: rx_buffer_full=0 | line: uart_breaks=0 "
                       "| accounting: event_queue_drops=0 rx_error_pauses=0") == 0,
          "zero report text");

    for (int i = 0; i < LOSS_COUNTER_COUNT; i++) {
        s.values[i] = (uint32_t)(i + 1);
    }

    loss_stats_format(&s, 10000, line, sizeof(line));
    CHECK(strstr(line, "uart_rx_overflow=1 uart_rx_errors=2 tcp_send_failures=3 tcp_tx_bytes=4 uart_tx_bytes=5 "
                       "| expected: session_bytes=6 no_client_bytes=7 | pressure: rx_buffer_full=8 "
                       "| line: uart_breaks=9 | accounting: event_queue_drops=10 rx_error_pauses=11") != NULL,
          "each field shows its own counter");

    for (int i = 0; i < LOSS_COUNTER_COUNT; i++) {
        s.values[i] = 0xffffffffu;
    }

    const int length = loss_stats_format(&s, 0xffffffffu, line, sizeof(line));

    CHECK(length > 0 && length < LINE, "largest report fits the 384-byte reporter line buffer (%d)", length);

    loss_stats_format(&s, 9980, line, sizeof(line));
    CHECK(strncmp(line, "10.0 s:", 7) == 0, "9,980 ms rounds to 10.0 s");
    loss_stats_format(&s, 10040, line, sizeof(line));
    CHECK(strncmp(line, "10.0 s:", 7) == 0, "10,040 ms rounds to 10.0 s");

    char small[8];

    loss_stats_format(&s, 10000, small, sizeof(small));
    CHECK(strlen(small) == 7, "short buffers are truncated safely");
}

static int reports_now(void)
{
    pthread_mutex_lock(&report_mutex);
    const int count = report_count;
    pthread_mutex_unlock(&report_mutex);
    return count;
}

static void sleep_until(int64_t at_us)
{
    const int64_t now = esp_timer_get_time();

    if (at_us > now) {
        usleep((useconds_t)(at_us - now));
    }
}

static uint32_t interval_ms_of(const char *text)
{
    unsigned whole = 0, tenth = 0;

    sscanf(text, "%u.%u s:", &whole, &tenth);
    return whole * 1000 + tenth * 100;
}

static void test_driver(void)
{
    uart_driver_drops_t drops;

    printf("UART driver (real uart_driver.c)\n");
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_NONE, "no queue yet: no event");
    CHECK(uart_driver_init() == ESP_OK, "init");
    CHECK(installed_queue_size == 128, "driver installed with a 128-entry event queue (%d)", installed_queue_size);
    CHECK(installed_rx_size == UART_DRIVER_RX_BUFFER_SIZE && installed_rx_size == 2048, "RX ring buffer unchanged at 2048 bytes");
    CHECK(configured_parity == UART_PARITY_DISABLE, "parity stays disabled (8N1)");
    CHECK(intr_enabled & UART_INTR_FRAM_ERR, "M1: frame-error interrupt enabled");
    CHECK(frame_enabled_at > pins_set_at && pins_set_at > 0, "M1: enabled after the RX pin is set");
    CHECK(intr_enabled & UART_INTR_BRK_DET, "break interrupt still enabled");
    CHECK(uart_driver_rx_fifo_len() == 37, "FIFO length comes from the LL register read");

    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_NONE, "empty queue times out");
    post_event(UART_FIFO_OVF);
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_RX_OVERFLOW, "UART_FIFO_OVF maps to overflow");
    post_event(UART_FRAME_ERR);
    post_event(UART_PARITY_ERR);
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_RX_ERROR, "UART_FRAME_ERR maps to RX error");
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_RX_ERROR, "UART_PARITY_ERR maps to RX error");
    post_event(UART_BUFFER_FULL);
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_RX_BUFFER_FULL, "UART_BUFFER_FULL maps to buffer full");
    post_event(UART_DATA);
    post_event(UART_BREAK);
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_NONE, "UART_DATA is not a loss event");
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_BREAK, "L4: UART_BREAK maps to break");

    printf("M2: events dropped on a full queue\n");
    uart_driver_get_drops(&drops);
    CHECK(drops.total == 0, "nothing dropped while the queue has room");

    post_events(UART_DATA, 128);
    post_events(UART_FIFO_OVF, 3);
    post_events(UART_FRAME_ERR, 2);
    post_event(UART_PARITY_ERR);
    post_event(UART_BUFFER_FULL);
    post_events(UART_BREAK, 4);
    post_events(UART_DATA, 5);
    uart_driver_get_drops(&drops);
    CHECK(drops.total == 16, "every event sent to the full queue is counted (%u)", drops.total);
    CHECK(drops.by_event[UART_DRIVER_EVENT_RX_OVERFLOW] == 3 && drops.by_event[UART_DRIVER_EVENT_RX_ERROR] == 3 &&
          drops.by_event[UART_DRIVER_EVENT_RX_BUFFER_FULL] == 1 && drops.by_event[UART_DRIVER_EVENT_BREAK] == 4 &&
          drops.by_event[UART_DRIVER_EVENT_NONE] == 5, "drops are counted by type");

    QueueHandle_t other = xQueueCreate(1, sizeof(uart_event_t));
    uart_event_t event = {.type = UART_FIFO_OVF};
    BaseType_t woken = 0;

    __wrap_xQueueGenericSendFromISR(other, &event, &woken, 0);
    __wrap_xQueueGenericSendFromISR(other, &event, &woken, 0);
    uart_driver_get_drops(&drops);
    CHECK(drops.total == 16, "a full queue that is not the UART's is not counted");
    vQueueDelete(other);

    int drained = 0;

    while (uart_driver_wait_event(10) != UART_DRIVER_EVENT_NONE || drained < 128) {
        if (++drained > 200) {
            break;
        }
    }

    post_event(UART_FIFO_OVF);
    uart_driver_get_drops(&drops);
    CHECK(drops.total == 16, "a queued event is never counted as dropped");
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_RX_OVERFLOW, "and the reader still receives it");
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_counters();
    test_classification();
    test_format();
    test_driver();

    const double period = LOSS_REPORT_PERIOD_US / 1e6;
    const int64_t P = LOSS_REPORT_PERIOD_US;
    loss_snapshot_t base;

    /* The tests above moved counters, and the reporter must count from its start. */
    loss_stats_snapshot(&base);
    printf("Reporter, period %.1f s\n", period);

    const int64_t started = esp_timer_get_time();

    CHECK(loss_reporter_start() == ESP_OK, "reporter started");

    /* Interval 1: no loss. Interval 2: loss events. Interval 3: quiet again. */
    sleep_until(started + P * 3 / 2);
    post_event(UART_FIFO_OVF);
    post_event(UART_FIFO_OVF);
    post_event(UART_BUFFER_FULL);
    post_event(UART_DATA);
    loss_stats_add(LOSS_TCP_TX_BYTES, 42);
    loss_stats_add(DISCARD_SESSION_BYTES, 9);

    /* Intervals 4 and 5: an event flood must not delay or suppress the report. */
    sleep_until(started + P * 7 / 2);
    const int before_flood = reports_now();
    const int64_t flood_end = esp_timer_get_time() + P * 13 / 10;
    long flooded = 0;

    while (esp_timer_get_time() < flood_end) {
        post_event(UART_DATA);
        flooded++;
    }

    const int after_flood = reports_now();

    /* Interval 6: only data events are dropped. Not loss, but a warning. */
    sleep_until(started + P * 11 / 2);
    force_full = 1;
    post_events(UART_DATA, 5);
    force_full = 0;

    /* Interval 7: overflow events dropped, and a frame-error storm partly
     * dropped. Every event must be counted exactly once. */
    sleep_until(started + P * 13 / 2);
    force_full = 1;
    post_events(UART_FIFO_OVF, 2);
    post_events(UART_FRAME_ERR, 7);
    force_full = 0;

    const int storm = LOSS_LINE_ERROR_LIMIT_TEST + 50;

    post_events(UART_FRAME_ERR, storm);
    post_events(UART_BREAK, 3);
    usleep((useconds_t)(P / 5));

    const int disables_in_storm = disable_calls;
    const int paused_in_storm = !(intr_enabled & UART_INTR_FRAM_ERR) && !(intr_enabled & UART_INTR_BRK_DET);

    /* Interval 8: quiet, interrupts back on. */
    sleep_until(started + P * 15 / 2);
    const int re_enabled = (intr_enabled & UART_INTR_FRAM_ERR) && (intr_enabled & UART_INTR_BRK_DET);

    sleep_until(started + P * 8 + 50000);

    pthread_mutex_lock(&report_mutex);

    for (int i = 0; i < report_count; i++) {
        printf("       %c LOSS: %s\n", reports[i].level, reports[i].text);
    }

    for (int i = 0; i < stack_line_count && i < 4; i++) {
        printf("       %s\n", stack_lines[i]);
    }

    CHECK(report_count >= 8, "%d reports in %.1f s", report_count, (esp_timer_get_time() - started) / 1e6);
    CHECK(reports[0].level == 'I' && strstr(reports[0].text, ": no data loss:") != NULL, "quiet interval: info level, no data loss");
    CHECK(reports[1].level == 'W' && strstr(reports[1].text, ": DATA LOSS:") != NULL, "interval with loss: warning level");
    CHECK(strstr(reports[1].text, "uart_rx_overflow=2 ") && strstr(reports[1].text, "tcp_tx_bytes=42 ") &&
          strstr(reports[1].text, "session_bytes=9 ") && strstr(reports[1].text, "rx_buffer_full=1 "),
          "interval report holds the right counts");
    CHECK(reports[2].level == 'I' && strstr(reports[2].text, "uart_rx_overflow=0 ") && strstr(reports[2].text, "tcp_tx_bytes=0 "),
          "next interval is reset, not cumulative");
    CHECK(after_flood > before_flood, "report still written during an event flood (%ld events)", flooded);

    CHECK(reports[5].level == 'W' && strstr(reports[5].text, ": no data loss:") && strstr(reports[5].text, "event_queue_drops=5 "),
          "M2: dropped data events are reported, as a warning without data loss");
    CHECK(strstr(reports[6].text, ": DATA LOSS:") && strstr(reports[6].text, "uart_rx_overflow=2 "),
          "M2: dropped overflow events still count as overflow");

    char want[64];

    snprintf(want, sizeof(want), "uart_rx_errors=%d ", 7 + storm);
    CHECK(strstr(reports[6].text, want) != NULL, "frame errors counted once each, queued or dropped (want %d)", 7 + storm);
    CHECK(strstr(reports[6].text, "uart_breaks=3 ") != NULL, "L4: breaks reported on their own line field");
    CHECK(strstr(reports[6].text, "rx_error_pauses=1") != NULL, "storm guard: one pause reported");
    CHECK(disables_in_storm == 1 && paused_in_storm, "storm guard: frame-error and break interrupts paused during the storm");
    CHECK(re_enabled, "storm guard: both re-enabled after the report");
    CHECK(reports[7].level == 'I' && strstr(reports[7].text, "uart_rx_errors=0 ") && strstr(reports[7].text, "rx_error_pauses=0"),
          "next interval is quiet again");

    double worst = 0;

    for (int i = 0; i < report_count; i++) {
        const double target = started / 1e6 + period * (i + 1);
        const double error = reports[i].at / 1e6 - target;

        if ((error < 0 ? -error : error) > worst) {
            worst = error < 0 ? -error : error;
        }
    }

    CHECK(worst < TIMING_LIMIT_S, "worst timing error %.1f ms, with no drift", worst * 1000);
    CHECK(stack_line_count == 1 && stack_lines[0][0] == 'I' && strstr(stack_lines[0], "1800 of 3072 bytes"),
          "L5: stack high-water mark logged once, at its first low (%d lines)", stack_line_count);
    pthread_mutex_unlock(&report_mutex);

    loss_snapshot_t now, total;

    loss_stats_snapshot(&now);
    loss_stats_delta(&base, &now, &total);
    CHECK(total.values[ACCOUNT_EVENT_QUEUE_DROPS] >= 14, "drops collected into the counters (%u)", total.values[ACCOUNT_EVENT_QUEUE_DROPS]);

    /* L5: a new low is logged, as a warning under 512 bytes. */
    extern _Atomic UBaseType_t test_stack_high_water;
    atomic_store(&test_stack_high_water, 400);
    sleep_until(started + P * 9 + 50000);
    pthread_mutex_lock(&report_mutex);
    CHECK(stack_line_count == 2 && stack_lines[1][0] == 'W' && strstr(stack_lines[1], "400 of 3072"),
          "L5: a lower stack mark is logged again, as a warning");
    pthread_mutex_unlock(&report_mutex);

    /* L1: a reporter held up for 2.5 periods writes one late report, then
     * keeps its phase. It must not catch up with near-empty reports. */
    if (LOSS_REPORT_PERIOD_US < 1000000) {
        sleep_until(started + P * 19 / 2);
        const int before_delay = reports_now();

        slow_console_us = P * 5 / 2;
        sleep_until(started + P * 14 + 50000);

        pthread_mutex_lock(&report_mutex);
        uint32_t shortest = UINT32_MAX;

        for (int i = before_delay; i < report_count; i++) {
            const uint32_t ms = interval_ms_of(reports[i].text);

            printf("       %c LOSS: %s\n", reports[i].level, reports[i].text);
            shortest = ms < shortest ? ms : shortest;
        }

        const int late = before_delay + 1;
        const double phase_error = report_count > late + 1
            ? reports[late + 1].at / 1e6 - (started / 1e6 + period * 13) : 1e9;

        CHECK(report_count > late + 1 && interval_ms_of(reports[late].text) >= (uint32_t)(P * 2 / 1000),
              "L1: one late report covers the delay (%u ms)", report_count > late ? interval_ms_of(reports[late].text) : 0);
        CHECK(shortest * 1000 >= (uint32_t)(P * 4 / 10), "L1: no near-empty catch-up report (shortest %u ms)", shortest);
        CHECK((phase_error < 0 ? -phase_error : phase_error) < TIMING_LIMIT_S,
              "L1: the next report keeps the original phase (%.1f ms off)", phase_error * 1000);
        pthread_mutex_unlock(&report_mutex);
    }

    printf("\nRESULT pass=%d fail=%d\n", passed, failed);
    return failed ? 1 : 0;
}
