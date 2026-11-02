#ifndef BRIDGE_H
#define BRIDGE_H

#include "esp_err.h"

/**
 * Bridge Module Interface
 *
 * Connects the active TCP client to UART1. A single task owns
 * the client socket and moves data in both directions.
 */

/**
 * Starts the bridge task. UART1 must already be initialized.
 */
esp_err_t bridge_start(void);

/**
 * Transfers a connected TCP socket to the bridge.
 *
 * On ESP_OK the bridge is the exclusive owner of the socket,
 * and the caller must not use or close it again. On any error
 * the caller keeps ownership and must close it.
 *
 * Returns ESP_ERR_INVALID_STATE when a client is already active.
 */
esp_err_t bridge_attach_client(int client_fd);

/**
 * Asks the bridge to end the active session, if there is one.
 *
 * Does not block, and is safe to call from event handlers.
 */
void bridge_request_disconnect(void);

#endif // BRIDGE_H
