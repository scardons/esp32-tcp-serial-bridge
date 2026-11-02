#pragma once
#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(int length, int item_size);
void vQueueDelete(QueueHandle_t queue);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned ticks);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned ticks);
