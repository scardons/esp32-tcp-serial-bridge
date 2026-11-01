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
 * Initializes the Ethernet network.
 */
esp_err_t network_init(void);

/**
 * Starts the TCP server.
 */
esp_err_t network_start_server(void);

/**
 * Stops the TCP server.
 */
void network_stop_server(void);

#endif // NETWORK_H