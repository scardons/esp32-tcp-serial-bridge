#pragma once
#include <unistd.h>
void vTaskDelay(unsigned ticks);
int xTaskCreate(void (*task)(void *), const char *name, int stack, void *arg, int priority, void *handle);
