/* FR-7 unit tests. These run the real loss_stats.c, loss_reporter.c and uart_driver.c. */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "loss_reporter.h"
#include "loss_stats.h"
#include "uart_driver.h"

#ifndef TIMING_LIMIT_S
#define TIMING_LIMIT_S 0.1
#endif

static int passed, failed;
#define CHECK(c, ...) do { if (c) { passed++; printf("  ok   "); } else { failed++; printf("  FAIL "); } \
    printf(__VA_ARGS__); printf("\n"); } while (0)

#define MAX_REPORTS 64
static struct { char level; char text[256]; int64_t at; } reports[MAX_REPORTS];
static int report_count;
static pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;

void test_log(char level, const char *tag, const char *format, ...)
{
    if (strcmp(tag, "LOSS") != 0) {
        return;
    }

    va_list args;

    va_start(args, format);
    pthread_mutex_lock(&report_mutex);

    if (report_count < MAX_REPORTS) {
        reports[report_count].level = level;
        vsnprintf(reports[report_count].text, sizeof(reports[0].text), format, args);
        reports[report_count].at = esp_timer_get_time();
        report_count++;
    }

    pthread_mutex_unlock(&report_mutex);
    va_end(args);
}

/* Driver mock, used by the real uart_driver.c */
static QueueHandle_t driver_queue;
static int installed_queue_size;

esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int size, QueueHandle_t *q, int f)
{
    (void)p; (void)rx; (void)tx; (void)f;
    installed_queue_size = size;
    driver_queue = xQueueCreate(size, sizeof(uart_event_t));
    *q = driver_queue;
    return ESP_OK;
}
esp_err_t uart_param_config(uart_port_t p, const uart_config_t *c) { (void)p; (void)c; return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t p, int t, int r, int a, int b) { (void)p; (void)t; (void)r; (void)a; (void)b; return ESP_OK; }
int uart_write_bytes(uart_port_t p, const void *d, size_t n) { (void)p; (void)d; return (int)n; }
int uart_read_bytes(uart_port_t p, void *d, size_t n, TickType_t t) { (void)p; (void)d; (void)n; (void)t; return 0; }

static void post_event(uart_event_type_t type)
{
    uart_event_t event = {.type = type};

    xQueueSend(driver_queue, &event, 0);
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
    const loss_counter_t fine[] = {DISCARD_SESSION_BYTES, DISCARD_NO_CLIENT_BYTES, PRESSURE_RX_BUFFER_FULL};
    int ok = 1;

    for (size_t i = 0; i < 5; i++) {
        memset(&s, 0, sizeof(s));
        s.values[lost[i]] = 1;
        ok &= loss_stats_has_loss(&s);
    }

    CHECK(ok, "each loss counter alone means data loss");
    ok = 1;

    for (size_t i = 0; i < 3; i++) {
        memset(&s, 0, sizeof(s));
        s.values[fine[i]] = 1000;
        ok &= !loss_stats_has_loss(&s);
    }

    CHECK(ok, "expected discards and pressure alone do not");
}

static void test_format(void)
{
    loss_snapshot_t s = {{0}};
    char line[256];

    printf("Format\n");
    loss_stats_format(&s, 10000, line, sizeof(line));
    printf("       %s\n", line);
    CHECK(strcmp(line, "10.0 s: no data loss: uart_rx_overflow=0 uart_rx_errors=0 tcp_send_failures=0 "
                       "tcp_tx_bytes=0 uart_tx_bytes=0 | expected: session_bytes=0 no_client_bytes=0 | rx_buffer_full=0") == 0,
          "zero report text");

    for (int i = 0; i < LOSS_COUNTER_COUNT; i++) {
        s.values[i] = 0xffffffffu;
    }

    const int length = loss_stats_format(&s, 0xffffffffu, line, sizeof(line));

    CHECK(length > 0 && length < 256, "largest report fits the reporter line buffer (%d)", length);

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

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_counters();
    test_classification();
    test_format();

    printf("UART driver event mapping (real uart_driver.c)\n");
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_NONE, "no queue yet: no event");
    CHECK(uart_driver_init() == ESP_OK, "init");
    CHECK(installed_queue_size == 32, "driver installed with a 32-entry event queue");
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
    CHECK(uart_driver_wait_event(10) == UART_DRIVER_EVENT_NONE, "UART_BREAK is not a loss event");

    const double period = LOSS_REPORT_PERIOD_US / 1e6;
    const int64_t started = esp_timer_get_time();

    printf("Reporter, period %.1f s\n", period);
    CHECK(loss_reporter_start() == ESP_OK, "reporter started");

    /* First interval: no loss. Second: loss events. Third: quiet again. */
    usleep((useconds_t)(LOSS_REPORT_PERIOD_US * 1.5));
    post_event(UART_FIFO_OVF);
    post_event(UART_FIFO_OVF);
    post_event(UART_BUFFER_FULL);
    post_event(UART_DATA);
    loss_stats_add(LOSS_TCP_TX_BYTES, 42);
    loss_stats_add(DISCARD_SESSION_BYTES, 9);
    usleep((useconds_t)(LOSS_REPORT_PERIOD_US * 2));

    /* Fourth interval: an event flood must not delay or suppress the report. */
    const int before_flood = reports_now();
    const int64_t flood_end = esp_timer_get_time() + (int64_t)(LOSS_REPORT_PERIOD_US * 1.3);
    long flooded = 0;

    while (esp_timer_get_time() < flood_end) {
        post_event(UART_DATA);
        flooded++;
    }

    usleep(50000);

    pthread_mutex_lock(&report_mutex);

    for (int i = 0; i < report_count; i++) {
        printf("       %c LOSS: %s\n", reports[i].level, reports[i].text);
    }

    CHECK(report_count >= 4, "%d reports in %.1f s", report_count, (esp_timer_get_time() - started) / 1e6);
    CHECK(reports[0].level == 'I' && strstr(reports[0].text, ": no data loss:") != NULL, "quiet interval: info level, no data loss");
    CHECK(reports[1].level == 'W' && strstr(reports[1].text, ": DATA LOSS:") != NULL, "interval with loss: warning level");
    CHECK(strstr(reports[1].text, "uart_rx_overflow=2 ") && strstr(reports[1].text, "tcp_tx_bytes=42 ") &&
          strstr(reports[1].text, "session_bytes=9 ") && strstr(reports[1].text, "rx_buffer_full=1"),
          "interval report holds the right counts");
    CHECK(reports[2].level == 'I' && strstr(reports[2].text, "uart_rx_overflow=0 ") && strstr(reports[2].text, "tcp_tx_bytes=0 "),
          "next interval is reset, not cumulative");
    CHECK(report_count > before_flood, "report still written during an event flood (%ld events)", flooded);

    double worst = 0;

    for (int i = 0; i < report_count; i++) {
        const double target = started / 1e6 + period * (i + 1);
        const double error = reports[i].at / 1e6 - target;

        if ((error < 0 ? -error : error) > worst) {
            worst = error < 0 ? -error : error;
        }
    }

    pthread_mutex_unlock(&report_mutex);
    CHECK(worst < TIMING_LIMIT_S, "worst timing error %.1f ms, with no drift", worst * 1000);

    printf("\nRESULT pass=%d fail=%d\n", passed, failed);
    return failed ? 1 : 0;
}
