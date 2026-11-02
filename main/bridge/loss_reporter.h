#ifndef LOSS_REPORTER_H
#define LOSS_REPORTER_H

#include "esp_err.h"

/**
 * Starts the task that collects UART loss events and writes a
 * one-line summary to the UART0 console every 10 seconds.
 * UART1 must already be initialized.
 */
esp_err_t loss_reporter_start(void);

#endif // LOSS_REPORTER_H
