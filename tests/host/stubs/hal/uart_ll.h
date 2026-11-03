#pragma once
/* Just enough of the ESP32 UART LL layer for uart_driver.c. Tests implement test_rxfifo_len(). */
#include <stdint.h>
#include <stddef.h>
typedef enum { UART_INTR_PARITY_ERR = (0x1 << 2), UART_INTR_FRAM_ERR = (0x1 << 3), UART_INTR_BRK_DET = (0x1 << 7) } uart_intr_t;
size_t test_rxfifo_len(void);
#define UART_LL_GET_HW(num) ((void *)(intptr_t)(num))
static inline uint32_t uart_ll_get_rxfifo_len(void *hw) { (void)hw; return (uint32_t)test_rxfifo_len(); }
