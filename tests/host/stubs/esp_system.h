#pragma once
#include <stdint.h>
/* Tests define these, so they can count the calls. */
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
