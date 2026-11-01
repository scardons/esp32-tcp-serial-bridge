
#include "network.h"

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_log.h"

static const char *TAG = "NETWORK";

static esp_eth_handle_t eth_handle = NULL;

esp_err_t network_init(void)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    eth_esp32_emac_config_t emac_config =
        ETH_ESP32_EMAC_DEFAULT_CONFIG();

    // Board-specific PHY reset pin.
    phy_config.phy_addr = -1;
    phy_config.reset_gpio_num = 5;

    esp_eth_mac_t *mac =
        esp_eth_mac_new_esp32(&emac_config, &mac_config);

    if (mac == NULL) {
        ESP_LOGE(TAG, "Failed to create Ethernet MAC");
        return ESP_FAIL;
    }

    esp_eth_phy_t *phy =
        esp_eth_phy_new_generic(&phy_config);

    if (phy == NULL) {
        ESP_LOGE(TAG, "Failed to create Ethernet PHY");
        mac->del(mac);
        return ESP_FAIL;
    }

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);

    esp_err_t err = esp_eth_driver_install(&config, &eth_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install Ethernet driver: %s",
                 esp_err_to_name(err));
        phy->del(phy);
        mac->del(mac);
        return err;
    }

    ESP_LOGI(TAG, "Ethernet driver initialized");
    return ESP_OK;
}

esp_err_t network_start_server(void)
{
    ESP_LOGW(TAG, "TCP server is not implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

void network_stop_server(void)
{
    // TCP server cleanup will be implemented later.
}
