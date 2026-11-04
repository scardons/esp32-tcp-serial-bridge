#include "client_socket.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"
#include "lwip/sockets.h"

// lwIP checks keepalive every 500 ms, so detection can take that much longer.
_Static_assert(CLIENT_KEEPALIVE_IDLE_S + CLIENT_KEEPALIVE_INTERVAL_S * CLIENT_KEEPALIVE_PROBES < 30,
               "FR-8: a half-open client must be detected within 30 seconds");

static const char *TAG = "NETWORK";

typedef struct {
    int level;
    int option;
    const char *name;
    int value;
    // lwIP reads SO_KEEPALIVE back as its internal flag bit, not as 1.
    bool is_flag;
} keepalive_option_t;

static const keepalive_option_t KEEPALIVE_OPTIONS[] = {
    {SOL_SOCKET, SO_KEEPALIVE, "SO_KEEPALIVE", 1, true},
    {IPPROTO_TCP, TCP_KEEPIDLE, "TCP_KEEPIDLE", CLIENT_KEEPALIVE_IDLE_S, false},
    {IPPROTO_TCP, TCP_KEEPINTVL, "TCP_KEEPINTVL", CLIENT_KEEPALIVE_INTERVAL_S, false},
    {IPPROTO_TCP, TCP_KEEPCNT, "TCP_KEEPCNT", CLIENT_KEEPALIVE_PROBES, false},
};

#define KEEPALIVE_OPTION_COUNT (sizeof(KEEPALIVE_OPTIONS) / sizeof(KEEPALIVE_OPTIONS[0]))

// Sets the option and returns the value the stack reports afterwards.
static bool apply_keepalive_option(int fd, const keepalive_option_t *option, int *effective)
{
    if (setsockopt(fd, option->level, option->option, &option->value,
                   sizeof(option->value)) != 0) {
        ESP_LOGE(TAG, "Cannot set %s: errno %d", option->name, errno);
        return false;
    }

    int value = 0;
    socklen_t length = sizeof(value);

    if (getsockopt(fd, option->level, option->option, &value, &length) != 0) {
        ESP_LOGE(TAG, "Cannot read back %s: errno %d", option->name, errno);
        return false;
    }

    const bool applied = option->is_flag ? value != 0 : value == option->value;

    if (!applied) {
        ESP_LOGE(TAG, "%s reads back %d after it was set to %d",
                 option->name, value, option->value);
        return false;
    }

    *effective = value;
    return true;
}

esp_err_t client_socket_configure(int fd)
{
    const int enable = 1;

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0) {
        ESP_LOGW(TAG, "Failed to set TCP_NODELAY: errno %d", errno);
    }

    int effective[KEEPALIVE_OPTION_COUNT] = {0};

    for (size_t i = 0; i < KEEPALIVE_OPTION_COUNT; i++) {
        if (!apply_keepalive_option(fd, &KEEPALIVE_OPTIONS[i], &effective[i])) {
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "TCP keepalive: idle %d s, interval %d s, %d probes",
             effective[1], effective[2], effective[3]);

    return ESP_OK;
}
