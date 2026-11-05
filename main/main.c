
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "bridge.h"
#include "loss_reporter.h"
#include "network.h"
#include "uart_driver.h"

static const char *TAG = "MAIN";

#if CONFIG_BRIDGE_UART_LOOPBACK_TEST

#define LOOPBACK_TIMEOUT_MS 1000

/**
 * Executes a UART1 loopback test.
 *
 * TX: GPIO32
 * RX: GPIO33
 *
 * Sends a test payload through UART1 TX and verifies
 * that the same bytes are received through UART1 RX.
 */
static void run_uart_loopback_test(void)
{
    const uint8_t tx_data[] = "ESP32 UART LOOPBACK TEST\r\n";
    const size_t tx_length = sizeof(tx_data) - 1;

    uint8_t rx_data[sizeof(tx_data)] = {0};
    size_t total_received = 0;

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "Starting UART1 loopback test");
    ESP_LOGI(TAG, "================================");

    ESP_LOGI(TAG, "Expected payload: %s",
             (const char *)tx_data);

    /*
     * Step 1: Transmit test payload through UART1 TX.
     */
    int bytes_written = uart_driver_write(
        tx_data,
        tx_length
    );

    if (bytes_written < 0) {
        ESP_LOGE(TAG, "TX FAILED: UART write error");
        return;
    }

    if (bytes_written != (int)tx_length) {
        ESP_LOGE(
            TAG,
            "TX FAILED: expected %d bytes, wrote %d",
            (int)tx_length,
            bytes_written
        );
        return;
    }

    ESP_LOGI(TAG, "TX: %d bytes transmitted",
             bytes_written);

    /*
     * Step 2: Receive the complete payload through UART1 RX.
     */
    const int64_t deadline =
        esp_timer_get_time() +
        ((int64_t)LOOPBACK_TIMEOUT_MS * 1000);

    while (total_received < tx_length) {

        const int64_t remaining_us =
            deadline - esp_timer_get_time();

        if (remaining_us <= 0) {
            break;
        }

        const uint32_t remaining_ms =
            (uint32_t)((remaining_us + 999) / 1000);

        const int bytes_read = uart_driver_read(
            rx_data + total_received,
            tx_length - total_received,
            remaining_ms
        );

        if (bytes_read < 0) {
            ESP_LOGE(TAG, "RX FAILED: UART read error");
            return;
        }

        if (bytes_read == 0) {
            break;
        }

        total_received += (size_t)bytes_read;
    }

    ESP_LOGI(
        TAG,
        "RX: %d of %d bytes received",
        (int)total_received,
        (int)tx_length
    );

    /*
     * Step 3: Verify that all expected bytes were received.
     */
    if (total_received != tx_length) {
        ESP_LOGE(
            TAG,
            "RESULT: FAIL - Timeout or incomplete data"
        );

        ESP_LOG_BUFFER_HEX(
            TAG,
            rx_data,
            total_received
        );

        return;
    }

    /*
     * Step 4: Compare transmitted and received data.
     */
    if (memcmp(tx_data, rx_data, tx_length) != 0) {

        ESP_LOGE(TAG, "RESULT: FAIL - Data mismatch");

        ESP_LOGI(TAG, "Expected data:");
        ESP_LOG_BUFFER_HEX(TAG, tx_data, tx_length);

        ESP_LOGI(TAG, "Received data:");
        ESP_LOG_BUFFER_HEX(TAG, rx_data, total_received);

        return;
    }

    /*
     * Step 5: Report successful test.
     */
    ESP_LOGI(
        TAG,
        "Received payload: %.*s",
        (int)total_received,
        (const char *)rx_data
    );

    ESP_LOGI(TAG, "RESULT: PASS - All bytes match");
}

/**
 * Runs the loopback test once. UART1 must already be initialized,
 * and the bridge must not be running yet.
 */
static void run_uart_diagnostics(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "ESP32 UART1 LOOPBACK TEST");
    ESP_LOGI(TAG, "================================");

    ESP_LOGI(
        TAG,
        "TX: GPIO32 | RX: GPIO33 | Baud: %d",
        UART_DRIVER_BAUD_RATE
    );

    /*
     * Allow UART initialization to settle.
     */
    vTaskDelay(pdMS_TO_TICKS(500));

    /*
     * Execute loopback test once.
     */
    run_uart_loopback_test();

    ESP_LOGI(TAG, "Loopback test finished");
}

#endif // CONFIG_BRIDGE_UART_LOOPBACK_TEST

/**
 * Starts UART1 and the bridge. The bridge task is the only
 * user of UART1 once it is running.
 */
static esp_err_t start_bridge(void)
{
    esp_err_t err = uart_driver_init();

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "UART initialization failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    ESP_LOGI(TAG, "UART1 initialized successfully: %d baud, 8N1, no flow control",
             UART_DRIVER_BAUD_RATE);

#if CONFIG_BRIDGE_UART_LOOPBACK_TEST
    run_uart_diagnostics();
#endif

    err = bridge_start();

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bridge start failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    // The bridge can run without the report, so this is not fatal.
    const esp_err_t report_err = loss_reporter_start();

    if (report_err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Loss reporter start failed: %s",
            esp_err_to_name(report_err)
        );
    }

    return ESP_OK;
}

/**
 * Logs one line that identifies the firmware, for test records. The ELF
 * SHA256 is the exact identity: the version comes from `git describe`, and
 * the same version can name different builds of uncommitted changes.
 */
static void log_firmware_identity(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char elf_sha256[65] = "";

    esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));

    ESP_LOGI(TAG, "Firmware: version %s, ELF SHA256 %s, built %s %s, ESP-IDF %s",
             app->version, elf_sha256, app->date, app->time, app->idf_ver);

    if (strstr(app->version, "dirty") != NULL) {
        ESP_LOGW(TAG, "Built from uncommitted changes: identify this firmware "
                      "by its ELF SHA256, not its version");
    }
}

/**
 * Main application entry point.
 *
 * Ethernet starts even if the bridge cannot, so the board stays
 * reachable for diagnosis. Only the TCP server depends on the bridge.
 */
void app_main(void)
{
    log_firmware_identity();

    const bool bridge_ready = (start_bridge() == ESP_OK);

    esp_err_t err = network_init();

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Network initialization failed: %s",
            esp_err_to_name(err)
        );
    } else if (bridge_ready) {
        err = network_start_server();

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "TCP server start failed: %s",
                esp_err_to_name(err)
            );
        }
    }
}
