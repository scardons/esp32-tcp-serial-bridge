#pragma once
#include <stdint.h>
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10))
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
