#ifndef NETWORK_H
#define NETWORK_H

#include "esp_err.h"
#include <stdint.h>

/**
 * Network Module Interface
 *
 * Declares the functions used to initialize and manage
 * the network connection and TCP server.
 */

/**
 * Initializes Ethernet with the static IPv4 configuration
 * from menuconfig and starts the driver.
 *
 * Returns once the driver is running, without waiting for
 * the link. Link changes are reported through the log.
 */
esp_err_t network_init(void);

/**
 * Starts the TCP server task on CONFIG_BRIDGE_TCP_PORT.
 *
 * The server hands each accepted client to the bridge, which
 * allows one at a time. Any other connection is closed. It can
 * start before the Ethernet link is up and keeps listening
 * across link changes. The bridge must already be running.
 */
esp_err_t network_start_server(void);

/**
 * Stops the TCP server, closes its listening socket and asks
 * the bridge to end the active session.
 *
 * Blocks until the server task has finished. It does not wait
 * for the bridge to close the client.
 */
void network_stop_server(void);

#endif // NETWORK_H