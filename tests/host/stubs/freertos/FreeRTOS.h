#pragma once
#include <stdint.h>
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10))
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef void *QueueHandle_t;
