#ifndef CLIENT_SOCKET_H
#define CLIENT_SOCKET_H

#include "esp_err.h"

/*
 * FR-8: a client that vanishes without closing is dropped once the connection
 * has been silent for IDLE + INTERVAL * PROBES seconds, about 11 s.
 */
#define CLIENT_KEEPALIVE_IDLE_S     5
#define CLIENT_KEEPALIVE_INTERVAL_S 2
#define CLIENT_KEEPALIVE_PROBES     3

/**
 * Configures an accepted client socket: TCP_NODELAY, and the keepalive that
 * detects half-open connections (FR-8).
 *
 * Each keepalive option is read back after it is set. Returns ESP_FAIL if one
 * cannot be set, cannot be read back, or reads back a different value. The
 * caller must then close the socket: lwIP's default keepalive would take
 * hours to drop a dead client. A TCP_NODELAY failure is only logged, because
 * it does not affect disconnect detection.
 *
 * On success, logs the effective keepalive values once.
 */
esp_err_t client_socket_configure(int fd);

#endif // CLIENT_SOCKET_H
