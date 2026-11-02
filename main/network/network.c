
#include "network.h"

#include <errno.h>
#include <fcntl.h>
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
#define TCP_RX_BUFFER_SIZE       256

// A peer that vanishes without closing is dropped after about 12 seconds.
#define TCP_KEEPALIVE_IDLE_S     5
#define TCP_KEEPALIVE_INTERVAL_S 2
#define TCP_KEEPALIVE_PROBES     3

static const char *TAG = "NETWORK";

static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

static TaskHandle_t s_server_task = NULL;
static SemaphoreHandle_t s_server_stopped = NULL;
static atomic_bool s_server_stop_requested = false;
static atomic_bool s_link_lost = false;

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
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        atomic_store(&s_link_lost, true);
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
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip);
    esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event);

    // The glue holds a driver reference, so it must go before the driver.
    if (s_eth_glue != NULL) {
        esp_eth_del_netif_glue(s_eth_glue);
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

static void configure_client_socket(int fd)
{
    set_socket_option(fd, IPPROTO_TCP, TCP_NODELAY, 1);
    set_socket_option(fd, SOL_SOCKET, SO_KEEPALIVE, 1);
    set_socket_option(fd, IPPROTO_TCP, TCP_KEEPIDLE, TCP_KEEPALIVE_IDLE_S);
    set_socket_option(fd, IPPROTO_TCP, TCP_KEEPINTVL, TCP_KEEPALIVE_INTERVAL_S);
    set_socket_option(fd, IPPROTO_TCP, TCP_KEEPCNT, TCP_KEEPALIVE_PROBES);
}

static void close_client(int *client_fd, const char *reason)
{
    if (*client_fd < 0) {
        return;
    }

    ESP_LOGI(TAG, "TCP client closed: %s", reason);
    close(*client_fd);
    *client_fd = -1;
}

static void accept_client(int *listen_fd, int *client_fd)
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

    if (*client_fd >= 0) {
        ESP_LOGW(TAG, "TCP client %s:%u rejected: a client is already connected",
                 peer_ip, ntohs(peer.sin_port));
        close(fd);
        return;
    }

    configure_client_socket(fd);
    *client_fd = fd;
    ESP_LOGI(TAG, "TCP client connected: %s:%u", peer_ip, ntohs(peer.sin_port));
}

// Reading is what reveals a disconnect. Data is dropped until the bridge exists.
static void read_client(int *client_fd)
{
    uint8_t buffer[TCP_RX_BUFFER_SIZE];
    const int length = recv(*client_fd, buffer, sizeof(buffer), MSG_DONTWAIT);

    if (length > 0) {
        ESP_LOGI(TAG, "TCP client data discarded: %d bytes", length);
    } else if (length == 0) {
        close_client(client_fd, "disconnected by peer");
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "TCP client receive failed: errno %d", errno);
        close_client(client_fd, "socket error");
    }
}

/*
 * The listener stays open across link changes: being bound to INADDR_ANY, it
 * is unaffected when ESP-NETIF clears the IP address. lwIP aborts the client
 * socket on link down, which wakes select(); s_link_lost covers any other case.
 */
static void tcp_server_task(void *arg)
{
    int listen_fd = -1;
    int client_fd = -1;

    while (!atomic_load(&s_server_stop_requested)) {
        if (atomic_exchange(&s_link_lost, false)) {
            close_client(&client_fd, "Ethernet link down");
        }

        if (listen_fd < 0) {
            listen_fd = open_listener();
        }

        if (listen_fd < 0 && client_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
            continue;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);

        if (listen_fd >= 0) {
            FD_SET(listen_fd, &read_fds);
        }

        if (client_fd >= 0) {
            FD_SET(client_fd, &read_fds);
        }

        struct timeval timeout = {.tv_sec = TCP_SELECT_TIMEOUT_S};
        const int max_fd = listen_fd > client_fd ? listen_fd : client_fd;
        const int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }

            // Out of memory says nothing about the sockets, so keep them.
            if (errno == ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
                continue;
            }

            ESP_LOGE(TAG, "TCP server select failed: errno %d", errno);
            close_client(&client_fd, "server error");

            if (listen_fd >= 0) {
                close(listen_fd);
                listen_fd = -1;
            }

            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
            continue;
        }

        // Client first, so a departing client frees the slot for a waiting one.
        if (client_fd >= 0 && FD_ISSET(client_fd, &read_fds)) {
            read_client(&client_fd);
        }

        if (listen_fd >= 0 && FD_ISSET(listen_fd, &read_fds)) {
            accept_client(&listen_fd, &client_fd);
        }
    }

    close_client(&client_fd, "server stopped");

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
    atomic_store(&s_link_lost, false);

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
}
