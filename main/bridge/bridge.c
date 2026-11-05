
#include "bridge.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "loss_stats.h"
#include "lwip/sockets.h"
#include "uart_driver.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"

// A stalled client is aborted through SO_LINGER, which lwIP ignores without it.
#if !CONFIG_LWIP_SO_LINGER
#error "CONFIG_LWIP_SO_LINGER must be enabled: a stalled client is aborted with SO_LINGER"
#endif
#endif

#define BRIDGE_TASK_STACK     4096
#define BRIDGE_TASK_PRIORITY  5
#define BRIDGE_POLL_PERIOD_US 10000
#define BRIDGE_BUFFER_SIZE    256

// Shorter than the time to fill the 2048-byte UART RX ring buffer, about 178 ms,
// so data with no client is dropped by the bridge instead of overflowing.
#define BRIDGE_IDLE_DRAIN_PERIOD_MS 100

// A client that accepts no UART data for this long is disconnected.
#define BRIDGE_TX_STALL_TIMEOUT_US (5 * 1000 * 1000)

// Time to transmit a full UART TX ring buffer and FIFO: (2048 + 128) bytes at
// 115200 baud. Bytes still leaving on TX can return on RX through an external
// loopback or device, and must not reach the next client.
#define BRIDGE_UART_TX_DRAIN_US    (200 * 1000)

// Read timeout that blocks the task for at least one tick.
#define BRIDGE_UART_DRAIN_READ_MS  20

// Most bytes UART1 can hold at once: the ring buffer, the up to one FIFO of
// bytes the driver keeps aside when the ring buffer is full, and the FIFO.
#define BRIDGE_UART_RX_HELD_MAX \
    (UART_DRIVER_RX_BUFFER_SIZE + 2 * UART_DRIVER_RX_FIFO_SIZE)

// uart_driver_write() spins instead of sleeping when the TX ring buffer is full.
// Estimating the bytes still queued keeps it well below capacity, so it never
// spins, and unread TCP data slows the client down through its receive window.
// 10 bits per byte (8N1), rounded up: 87 us at 115200, 11 us at 921600.
#define BRIDGE_UART_US_PER_BYTE \
    ((10 * 1000000 + UART_DRIVER_BAUD_RATE - 1) / UART_DRIVER_BAUD_RATE)
#define BRIDGE_UART_TX_MAX_BACKLOG_US (100 * 1000)

typedef struct {
    size_t length;
    size_t offset;
    int64_t last_progress_us;
} pending_tx_t;

typedef struct {
    size_t length;
    size_t offset;
} pending_uart_t;

static const char *TAG = "BRIDGE";

// The only reason that ends a session without a failure.
static const char REASON_PEER_CLOSED[] = "disconnected by peer";

// The only reason that ends a session with a reset instead of a FIN.
static const char REASON_CLIENT_STALLED[] = "client stalled";

static QueueHandle_t s_client_queue = NULL;
static atomic_bool s_session_busy = false;
static atomic_bool s_disconnect_requested = false;

static uint8_t s_tcp_rx_buffer[BRIDGE_BUFFER_SIZE];
static uint8_t s_uart_rx_buffer[BRIDGE_BUFFER_SIZE];
static int64_t s_last_uart_write_us = 0;
static int64_t s_uart_tx_done_us = 0;

static bool is_temporary_socket_error(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK ||
           error == EINTR || error == ENOMEM;
}

// Returns how many bytes fit in the UART TX backlog limit right now.
static size_t uart_tx_budget(void)
{
    const int64_t now = esp_timer_get_time();
    const int64_t backlog_us = s_uart_tx_done_us > now ? s_uart_tx_done_us - now : 0;

    if (backlog_us >= BRIDGE_UART_TX_MAX_BACKLOG_US) {
        return 0;
    }

    return (size_t)((BRIDGE_UART_TX_MAX_BACKLOG_US - backlog_us) /
                    BRIDGE_UART_US_PER_BYTE);
}

// Writes what the budget allows. The rest stays pending for a later call.
static const char *write_to_uart(pending_uart_t *pending)
{
    const size_t remaining = pending->length - pending->offset;
    const size_t budget = uart_tx_budget();
    const size_t length = remaining < budget ? remaining : budget;

    if (length == 0) {
        return NULL;
    }

    const int count = uart_driver_write(s_tcp_rx_buffer + pending->offset, length);

    if (count <= 0) {
        ESP_LOGE(TAG, "UART write failed");
        return "UART error";
    }

    const int64_t now = esp_timer_get_time();

    s_last_uart_write_us = now;
    s_uart_tx_done_us = (s_uart_tx_done_us > now ? s_uart_tx_done_us : now) +
                        (int64_t)count * BRIDGE_UART_US_PER_BYTE;

    pending->offset += (size_t)count;

    if (pending->offset == pending->length) {
        pending->length = 0;
        pending->offset = 0;
    }

    return NULL;
}

static const char *receive_from_client(int fd, pending_uart_t *pending)
{
    const int length = recv(fd, s_tcp_rx_buffer, sizeof(s_tcp_rx_buffer),
                            MSG_DONTWAIT);

    if (length > 0) {
        pending->length = (size_t)length;
        pending->offset = 0;
        return NULL;
    }

    if (length == 0) {
        return REASON_PEER_CLOSED;
    }

    if (is_temporary_socket_error(errno)) {
        vTaskDelay(1);
        return NULL;
    }

    ESP_LOGW(TAG, "TCP client receive failed: errno %d", errno);
    return "socket error";
}

static const char *read_from_uart(pending_tx_t *pending)
{
    const int length = uart_driver_read(s_uart_rx_buffer,
                                         sizeof(s_uart_rx_buffer), 0);

    if (length < 0) {
        ESP_LOGE(TAG, "UART read failed");
        return "UART error";
    }

    if (length > 0) {
        pending->length = (size_t)length;
        pending->offset = 0;
        pending->last_progress_us = esp_timer_get_time();
    }

    return NULL;
}

// Keeps unsent bytes in place, so a partial send resumes at the right offset.
static const char *send_to_client(int fd, pending_tx_t *pending)
{
    const int sent = send(fd, s_uart_rx_buffer + pending->offset,
                          pending->length - pending->offset, MSG_DONTWAIT);

    if (sent > 0) {
        pending->offset += (size_t)sent;
        pending->last_progress_us = esp_timer_get_time();

        if (pending->offset == pending->length) {
            pending->length = 0;
            pending->offset = 0;
        }

        return NULL;
    }

    if (sent < 0 && !is_temporary_socket_error(errno)) {
        ESP_LOGW(TAG, "TCP client send failed: errno %d", errno);
        loss_stats_add(LOSS_TCP_SEND_FAILURE, 1);
        return "socket error";
    }

    if (esp_timer_get_time() - pending->last_progress_us >=
        BRIDGE_TX_STALL_TIMEOUT_US) {
        loss_stats_add(LOSS_TCP_SEND_FAILURE, 1);
        return REASON_CLIENT_STALLED;
    }

    return NULL;
}

/*
 * Returns why the session ended. The caller still owns the socket.
 *
 * select() only waits for reads, so its timeout paces every send() retry. A
 * socket that reports writable but accepts nothing cannot cause a busy loop.
 */
static const char *run_session(int fd, pending_tx_t *pending_tx,
                               pending_uart_t *pending_uart)
{
    while (true) {
        if (atomic_load(&s_disconnect_requested)) {
            return "disconnect requested";
        }

        const char *reason = NULL;

        if (pending_tx->length == 0) {
            reason = read_from_uart(pending_tx);
        }

        if (reason == NULL && pending_tx->length > 0) {
            reason = send_to_client(fd, pending_tx);
        }

        if (reason == NULL && pending_uart->length > 0) {
            reason = write_to_uart(pending_uart);
        }

        if (reason != NULL) {
            return reason;
        }

        // Stop reading TCP until the UART has room. The block above keeps
        // running on every pass, so UART to TCP is not held back.
        if (pending_uart->length > 0 || uart_tx_budget() == 0) {
            vTaskDelay(1);
            continue;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        struct timeval timeout = {.tv_usec = BRIDGE_POLL_PERIOD_US};
        const int ready = select(fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready < 0) {
            if (is_temporary_socket_error(errno)) {
                vTaskDelay(1);
                continue;
            }

            ESP_LOGE(TAG, "Bridge select failed: errno %d", errno);
            return "socket error";
        }

        if (ready > 0) {
            reason = receive_from_client(fd, pending_uart);

            if (reason != NULL) {
                return reason;
            }
        }
    }
}

// NFR-2: logged after a session, the deepest use of this task's stack, and
// only when it reaches a new low, so repeated sessions add no lines.
static void log_stack_low(void)
{
    static uint32_t s_stack_low = UINT32_MAX;
    const uint32_t unused = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    if (unused >= s_stack_low) {
        return;
    }

    s_stack_low = unused;
    ESP_LOGI(TAG, "Bridge stack: %" PRIu32 " of %d bytes never used", unused,
             BRIDGE_TASK_STACK);
}

// Reads and counts UART input that has no client to go to.
static int discard_uart_read(uint32_t timeout_ms)
{
    const int length = uart_driver_read(s_uart_rx_buffer,
                                        sizeof(s_uart_rx_buffer), timeout_ms);

    if (length > 0) {
        loss_stats_add(DISCARD_NO_CLIENT_BYTES, (uint32_t)length);
    }

    return length;
}

// Drops everything received on UART1 while no client was connected.
static void discard_uart_input(void)
{
    while (esp_timer_get_time() - s_last_uart_write_us < BRIDGE_UART_TX_DRAIN_US) {
        discard_uart_read(BRIDGE_UART_DRAIN_READ_MS);
    }

    while (discard_uart_read(0) > 0) {
    }
}

/*
 * Reads and drops what UART1 had already received when the session ended, and
 * returns the byte count. Those bytes were meant for that session's client, so
 * they are counted with it, not as data with no client. Each byte is read, and
 * so counted, once.
 *
 * Bytes arriving while this runs, a few microseconds, are counted too. The
 * limit stops a continuous stream from keeping it busy.
 */
static uint32_t drain_session_input(void)
{
    uint32_t total = 0;

    // The ring buffer, and the bytes the driver kept aside while it was full,
    // which it adds to the ring buffer as soon as a read makes room.
    while (total < BRIDGE_UART_RX_HELD_MAX) {
        const int length = uart_driver_read(s_uart_rx_buffer, sizeof(s_uart_rx_buffer), 0);

        if (length <= 0) {
            break;
        }

        total += (uint32_t)length;
    }

    // Bytes still in the hardware FIFO reach the ring buffer on the RX
    // timeout, after 10 idle symbol times: under 1 ms at 115200 baud.
    const size_t in_fifo = uart_driver_rx_fifo_len();

    if (in_fifo > 0) {
        const size_t wanted = in_fifo < sizeof(s_uart_rx_buffer) ? in_fifo
                                                                 : sizeof(s_uart_rx_buffer);
        const int length = uart_driver_read(s_uart_rx_buffer, wanted,
                                            BRIDGE_UART_DRAIN_READ_MS);

        if (length > 0) {
            total += (uint32_t)length;
        }
    }

    return total;
}

// A client that closes normally is expected to leave bytes behind. In any
// other case those bytes were meant for an endpoint that did not get them.
static void record_session_end(const char *reason, const pending_tx_t *pending_tx,
                               const pending_uart_t *pending_uart)
{
    const uint32_t tx_bytes = (uint32_t)(pending_tx->length - pending_tx->offset) +
                              drain_session_input();
    const uint32_t uart_bytes = (uint32_t)(pending_uart->length - pending_uart->offset);

    if (reason == REASON_PEER_CLOSED) {
        loss_stats_add(DISCARD_SESSION_BYTES, tx_bytes + uart_bytes);
    } else {
        loss_stats_add(LOSS_TCP_TX_BYTES, tx_bytes);
        loss_stats_add(LOSS_UART_TX_BYTES, uart_bytes);
    }
}

/*
 * A stalled client keeps its receive window at zero, so a FIN queued behind
 * unsent data never leaves, and lwIP keeps the connection for as long as the
 * client acknowledges its window probes. With linger 0, close() aborts it:
 * lwIP sends a RST and frees the connection and its buffers at once.
 */
static void abort_on_close(int fd)
{
    const struct linger abort_linger = {.l_onoff = 1, .l_linger = 0};

    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &abort_linger, sizeof(abort_linger)) != 0) {
        ESP_LOGW(TAG, "Cannot abort the stalled client: SO_LINGER failed, errno %d", errno);
    }
}

static void bridge_task(void *arg)
{
    while (true) {
        int fd = -1;

        if (xQueueReceive(s_client_queue, &fd,
                          pdMS_TO_TICKS(BRIDGE_IDLE_DRAIN_PERIOD_MS)) != pdTRUE) {
            while (discard_uart_read(0) > 0) {
            }

            continue;
        }

        discard_uart_input();

        pending_tx_t pending_tx = {0};
        pending_uart_t pending_uart = {0};

        const char *reason = run_session(fd, &pending_tx, &pending_uart);

        record_session_end(reason, &pending_tx, &pending_uart);

        ESP_LOGI(TAG, "TCP client closed: %s", reason);

        if (reason == REASON_CLIENT_STALLED) {
            abort_on_close(fd);
        }

        close(fd);

        // FR-8: compare across sessions. lwIP can keep a closed connection for
        // a while (TIME_WAIT, unacknowledged data), so a single value says little.
        ESP_LOGI(TAG, "Heap after session: %" PRIu32 " bytes free, %" PRIu32 " minimum ever",
                 esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
        log_stack_low();

        // Last step: a new client can only arrive after this socket is closed.
        atomic_store(&s_session_busy, false);
    }
}

esp_err_t bridge_start(void)
{
    ESP_RETURN_ON_FALSE(s_client_queue == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Bridge is already running");

    s_client_queue = xQueueCreate(1, sizeof(int));
    ESP_RETURN_ON_FALSE(s_client_queue != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to create bridge queue");

    if (xTaskCreate(bridge_task, "bridge", BRIDGE_TASK_STACK, NULL,
                    BRIDGE_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bridge task");
        vQueueDelete(s_client_queue);
        s_client_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t bridge_attach_client(int client_fd)
{
    ESP_RETURN_ON_FALSE(s_client_queue != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Bridge is not running");

    bool expected = false;

    if (!atomic_compare_exchange_strong(&s_session_busy, &expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }

    // A request made before this client arrived does not apply to it.
    atomic_store(&s_disconnect_requested, false);

    if (xQueueSend(s_client_queue, &client_fd, 0) != pdTRUE) {
        atomic_store(&s_session_busy, false);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void bridge_request_disconnect(void)
{
    atomic_store(&s_disconnect_requested, true);
}
