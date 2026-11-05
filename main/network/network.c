
#include "network.h"

#include "bridge.h"
#include "client_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/select.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

// The ESP32 can only take an external RMII clock on GPIO0.
#define BRIDGE_ETH_RMII_CLK_GPIO 0

#define TCP_SERVER_TASK_STACK    4096
#define TCP_SERVER_TASK_PRIORITY 5
#define TCP_LISTEN_QUEUE         1
#define TCP_SELECT_TIMEOUT_S     1
#define TCP_RETRY_DELAY_MS       1000

static const char *TAG = "NETWORK";

static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

static TaskHandle_t s_server_task = NULL;
static SemaphoreHandle_t s_server_stopped = NULL;
static atomic_bool s_server_stop_requested = false;

/*
 * FR-9 link recovery state, for the log only. Every Ethernet and IP handler
 * runs on the default event loop task, so this needs no locking.
 */
static esp_netif_ip_info_t s_static_ip_info = {0};
static uint32_t s_link_down_count = 0;
static int64_t s_link_down_us = -1;     // When the current outage began, or -1
static bool s_ip_restore_pending = false;

static uint32_t ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static bool matches_static_ip(const esp_netif_ip_info_t *ip_info)
{
    return ip_info->ip.addr == s_static_ip_info.ip.addr &&
           ip_info->netmask.addr == s_static_ip_info.netmask.addr &&
           ip_info->gw.addr == s_static_ip_info.gw.addr;
}

static void on_link_down(void)
{
    s_link_down_count++;
    s_link_down_us = esp_timer_get_time();
    s_ip_restore_pending = false;

    ESP_LOGW(TAG, "Ethernet link down (#%" PRIu32 ")", s_link_down_count);
}

// Runs after log_link_up(). The first link up after boot is not a recovery.
static void on_link_restored(void)
{
    if (s_link_down_us < 0) {
        return;
    }

    const uint32_t outage_ms = ms_since(s_link_down_us);

    s_link_down_us = -1;
    s_ip_restore_pending = true;

    ESP_LOGI(TAG, "Ethernet link restored after %" PRIu32 ".%01" PRIu32 " s outage (#%" PRIu32 ")",
             outage_ms / 1000, (outage_ms % 1000) / 100, s_link_down_count);
}

static void log_link_up(void)
{
    uint8_t mac_addr[6] = {0};
    eth_speed_t speed = ETH_SPEED_10M;
    eth_duplex_t duplex = ETH_DUPLEX_HALF;

    if (esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr) != ESP_OK ||
        esp_eth_ioctl(s_eth_handle, ETH_CMD_G_SPEED, &speed) != ESP_OK ||
        esp_eth_ioctl(s_eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex) != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet link up, but its details are unavailable");
        return;
    }

    ESP_LOGI(TAG, "Ethernet link up: %s Mbps, %s duplex, MAC %02x:%02x:%02x:%02x:%02x:%02x",
             speed == ETH_SPEED_100M ? "100" : "10",
             duplex == ETH_DUPLEX_FULL ? "full" : "half",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
}

static void on_eth_event(void *arg, esp_event_base_t event_base,
                         int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_CONNECTED:
        log_link_up();
        on_link_restored();
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        on_link_down();
        bridge_request_disconnect();
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        break;
    default:
        break;
    }
}

// With DHCP stopped, ESP-NETIF posts this event on every link up.
static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    const ip_event_got_ip_t *event = event_data;

    ESP_LOGI(TAG, "IPv4 ready: " IPSTR ", mask " IPSTR ", gateway " IPSTR,
             IP2STR(&event->ip_info.ip),
             IP2STR(&event->ip_info.netmask),
             IP2STR(&event->ip_info.gw));

    // Only reported: the address in use is whatever ESP-NETIF applied.
    if (!matches_static_ip(&event->ip_info)) {
        ESP_LOGE(TAG, "IPv4 does not match the static configuration " IPSTR ", mask " IPSTR
                      ", gateway " IPSTR, IP2STR(&s_static_ip_info.ip),
                 IP2STR(&s_static_ip_info.netmask), IP2STR(&s_static_ip_info.gw));
    } else if (s_ip_restore_pending) {
        ESP_LOGI(TAG, "Static IP restored after link recovery (#%" PRIu32 ")", s_link_down_count);
    }

    s_ip_restore_pending = false;
}

/*
 * ESP-NETIF posts this when its lost-IP timer (CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL)
 * fires while the netif has no address. The timer starts at a link down but is
 * not cancelled at link up, and a later link down does not restart it, so it
 * can fire during a later, shorter outage. The time logged is only how long
 * the current outage has lasted, not when the timer started.
 *
 * With DHCP stopped, ESP-NETIF keeps the static configuration and applies it
 * again on the next link up. While the netif is down, esp_netif_get_ip_info()
 * returns that stored configuration, so this checks it was kept.
 */
static void on_lost_ip(void *arg, esp_event_base_t event_base,
                       int32_t event_id, void *event_data)
{
    esp_netif_ip_info_t ip_info = {0};
    const esp_err_t err = esp_netif_get_ip_info(s_eth_netif, &ip_info);
    const uint32_t outage_ms = s_link_down_us >= 0 ? ms_since(s_link_down_us) : 0;

    if (err == ESP_OK && matches_static_ip(&ip_info)) {
        ESP_LOGW(TAG, "ESP-NETIF lost-IP event during outage #%" PRIu32 " (%" PRIu32 " s into it; "
                      "the timer may date from an earlier link down). "
                      "Static IP " IPSTR " kept for reconnection",
                 s_link_down_count, outage_ms / 1000, IP2STR(&ip_info.ip));
    } else {
        ESP_LOGE(TAG, "ESP-NETIF lost-IP event during outage #%" PRIu32 " (%" PRIu32 " s into it), "
                      "and the static configuration was not kept: " IPSTR " (%s)",
                 s_link_down_count, outage_ms / 1000, IP2STR(&ip_info.ip),
                 esp_err_to_name(err));
    }
}

static esp_err_t install_eth_driver(void)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    eth_esp32_emac_config_t emac_config =
        ETH_ESP32_EMAC_DEFAULT_CONFIG();

    emac_config.smi_gpio.mdc_num = CONFIG_BRIDGE_ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = CONFIG_BRIDGE_ETH_MDIO_GPIO;
    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = BRIDGE_ETH_RMII_CLK_GPIO;

    phy_config.phy_addr = CONFIG_BRIDGE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_BRIDGE_ETH_PHY_RESET_GPIO;

    s_mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    ESP_RETURN_ON_FALSE(s_mac != NULL, ESP_FAIL, TAG,
                        "Failed to create Ethernet MAC");

    s_phy = esp_eth_phy_new_generic(&phy_config);
    ESP_RETURN_ON_FALSE(s_phy != NULL, ESP_FAIL, TAG,
                        "Failed to create Ethernet PHY");

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);

    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&config, &s_eth_handle), TAG,
                        "Failed to install Ethernet driver");

    uint32_t phy_addr = 0;

    ESP_RETURN_ON_ERROR(esp_eth_ioctl(s_eth_handle, ETH_CMD_G_PHY_ADDR, &phy_addr),
                        TAG, "Failed to read PHY address");
    ESP_LOGI(TAG, "Ethernet driver installed, PHY address %" PRIu32, phy_addr);

    return ESP_OK;
}

/*
 * Must run before esp_eth_start(). Stopping DHCP here, rather than clearing
 * ESP_NETIF_DHCP_CLIENT, keeps IP_EVENT_ETH_GOT_IP firing on each link up.
 */
static esp_err_t apply_static_ip(void)
{
    esp_netif_ip_info_t ip_info = {0};

    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(CONFIG_BRIDGE_ETH_IP, &ip_info.ip),
                        TAG, "Invalid IP address: %s", CONFIG_BRIDGE_ETH_IP);
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(CONFIG_BRIDGE_ETH_NETMASK, &ip_info.netmask),
                        TAG, "Invalid subnet mask: %s", CONFIG_BRIDGE_ETH_NETMASK);
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(CONFIG_BRIDGE_ETH_GATEWAY, &ip_info.gw),
                        TAG, "Invalid gateway: %s", CONFIG_BRIDGE_ETH_GATEWAY);

    esp_err_t err = esp_netif_dhcpc_stop(s_eth_netif);

    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(err));
        return err;
    }

    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_eth_netif, &ip_info), TAG,
                        "Failed to set static IP");

    s_static_ip_info = ip_info;

    return ESP_OK;
}

static esp_err_t attach_netif(void)
{
    const esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();

    s_eth_netif = esp_netif_new(&netif_config);
    ESP_RETURN_ON_FALSE(s_eth_netif != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to create network interface");

    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_RETURN_ON_FALSE(s_eth_glue != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to create Ethernet glue");

    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, s_eth_glue), TAG,
                        "Failed to attach Ethernet to the network interface");

    return ESP_OK;
}

static void network_cleanup(void)
{
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_LOST_IP, &on_lost_ip);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip);
    esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event);

    // The glue holds a driver reference, so it must go before the driver.
    if (s_eth_glue != NULL) {
        const esp_err_t glue_err = esp_eth_del_netif_glue(s_eth_glue);

        if (glue_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete Ethernet glue: %s", esp_err_to_name(glue_err));
        }

        s_eth_glue = NULL;
    }

    if (s_eth_handle != NULL) {
        esp_err_t err = esp_eth_driver_uninstall(s_eth_handle);

        if (err != ESP_OK) {
            // The driver still uses the netif, MAC and PHY, so keep them.
            ESP_LOGE(TAG, "Failed to uninstall Ethernet driver: %s",
                     esp_err_to_name(err));
            return;
        }

        s_eth_handle = NULL;
    }

    // Received frames are delivered to the netif until the driver is gone.
    if (s_eth_netif != NULL) {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
    }

    if (s_phy != NULL) {
        s_phy->del(s_phy);
        s_phy = NULL;
    }

    if (s_mac != NULL) {
        s_mac->del(s_mac);
        s_mac = NULL;
    }
}

esp_err_t network_init(void)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(s_eth_handle == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Network is already initialized");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize ESP-NETIF");

    ret = esp_event_loop_create_default();

    // ESP_ERR_INVALID_STATE means the default loop already exists.
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_GOTO_ON_ERROR(install_eth_driver(), err, TAG, "Ethernet driver setup failed");
    ESP_GOTO_ON_ERROR(attach_netif(), err, TAG, "Network interface setup failed");
    ESP_GOTO_ON_ERROR(apply_static_ip(), err, TAG, "Static IP setup failed");

    ESP_GOTO_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                                 &on_eth_event, NULL),
                      err, TAG, "Failed to register Ethernet events");
    ESP_GOTO_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                 &on_got_ip, NULL),
                      err, TAG, "Failed to register IP events");
    ESP_GOTO_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                                                 &on_lost_ip, NULL),
                      err, TAG, "Failed to register lost IP events");

    ret = esp_eth_start(s_eth_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Ethernet: %s", esp_err_to_name(ret));
        // A failed start can leave the driver marked as started,
        // and it can only be uninstalled once stopped.
        esp_eth_stop(s_eth_handle);
        goto err;
    }

    return ESP_OK;

err:
    network_cleanup();
    return ret;
}

static void set_socket_option(int fd, int level, int option, int value)
{
    if (setsockopt(fd, level, option, &value, sizeof(value)) != 0) {
        ESP_LOGW(TAG, "Failed to set socket option %d: errno %d", option, errno);
    }
}

static int open_listener(void)
{
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to create TCP socket: errno %d", errno);
        return -1;
    }

    set_socket_option(fd, SOL_SOCKET, SO_REUSEADDR, 1);

    const struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_BRIDGE_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    // select() can report a connection that is gone by the time of accept().
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, TCP_LISTEN_QUEUE) != 0) {
        ESP_LOGE(TAG, "Failed to listen on TCP port %d: errno %d",
                 CONFIG_BRIDGE_TCP_PORT, errno);
        close(fd);
        return -1;
    }

    ESP_LOGI(TAG, "TCP server listening on port %d", CONFIG_BRIDGE_TCP_PORT);
    return fd;
}

// NFR-2: logged after a client is handled, the deepest use of this task's
// stack, and only when it reaches a new low.
static void log_server_stack_low(void)
{
    static uint32_t s_stack_low = UINT32_MAX;
    const uint32_t unused = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    if (unused >= s_stack_low) {
        return;
    }

    s_stack_low = unused;
    ESP_LOGI(TAG, "TCP server stack: %" PRIu32 " of %d bytes never used", unused,
             TCP_SERVER_TASK_STACK);
}

static void accept_client(int *listen_fd)
{
    struct sockaddr_in peer = {0};
    socklen_t peer_length = sizeof(peer);
    const int fd = accept(*listen_fd, (struct sockaddr *)&peer, &peer_length);

    if (fd < 0) {
        if (errno == ENFILE || errno == EMFILE) {
            ESP_LOGW(TAG, "No free sockets to accept a TCP client: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
        } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                   errno != ECONNABORTED && errno != ECONNRESET &&
                   errno != ENOTCONN) {
            ESP_LOGE(TAG, "Failed to accept TCP client: errno %d", errno);
            close(*listen_fd);
            *listen_fd = -1;
        }
        return;
    }

    char peer_ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));

    // Without its keepalive, a half-open client would hold the slot for hours.
    if (client_socket_configure(fd) != ESP_OK) {
        ESP_LOGE(TAG, "TCP client %s:%u rejected: keepalive could not be configured",
                 peer_ip, ntohs(peer.sin_port));
        close(fd);
        return;
    }

    // After a successful attach the socket belongs to the bridge.
    const esp_err_t err = bridge_attach_client(fd);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TCP client connected: %s:%u", peer_ip, ntohs(peer.sin_port));
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TCP client %s:%u rejected: a client is already connected",
                 peer_ip, ntohs(peer.sin_port));
        close(fd);
    } else {
        ESP_LOGE(TAG, "TCP client %s:%u rejected: %s", peer_ip,
                 ntohs(peer.sin_port), esp_err_to_name(err));
        close(fd);
    }
}

/*
 * The listener stays open across link changes: being bound to INADDR_ANY, it
 * is unaffected when ESP-NETIF clears the IP address.
 */
static void tcp_server_task(void *arg)
{
    int listen_fd = -1;

    while (!atomic_load(&s_server_stop_requested)) {
        if (listen_fd < 0) {
            listen_fd = open_listener();
        }

        if (listen_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
            continue;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);

        struct timeval timeout = {.tv_sec = TCP_SELECT_TIMEOUT_S};
        const int ready = select(listen_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }

            // Out of memory says nothing about the socket, so keep it.
            if (errno == ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
                continue;
            }

            ESP_LOGE(TAG, "TCP server select failed: errno %d", errno);
            close(listen_fd);
            listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
            continue;
        }

        if (ready > 0) {
            accept_client(&listen_fd);
            log_server_stack_low();
        }
    }

    if (listen_fd >= 0) {
        close(listen_fd);
    }

    ESP_LOGI(TAG, "TCP server stopped");
    xSemaphoreGive(s_server_stopped);
    vTaskDelete(NULL);
}

esp_err_t network_start_server(void)
{
    ESP_RETURN_ON_FALSE(s_server_task == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "TCP server is already running");

    s_server_stopped = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_server_stopped != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to create TCP server semaphore");

    atomic_store(&s_server_stop_requested, false);

    if (xTaskCreate(tcp_server_task, "tcp_server", TCP_SERVER_TASK_STACK, NULL,
                    TCP_SERVER_TASK_PRIORITY, &s_server_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        vSemaphoreDelete(s_server_stopped);
        s_server_stopped = NULL;
        s_server_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void network_stop_server(void)
{
    if (s_server_task == NULL) {
        return;
    }

    atomic_store(&s_server_stop_requested, true);

    xSemaphoreTake(s_server_stopped, portMAX_DELAY);

    vSemaphoreDelete(s_server_stopped);
    s_server_stopped = NULL;
    s_server_task = NULL;

    // Requested after the task ends, so no client can be attached later.
    bridge_request_disconnect();
}
