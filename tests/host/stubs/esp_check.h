#pragma once
#include "esp_err.h"
#include "esp_log.h"
#define ESP_RETURN_ON_FALSE(c, r, t, f, ...) do { if (!(c)) { ESP_LOGE(t, f, ##__VA_ARGS__); return (r); } } while (0)
#define ESP_RETURN_ON_ERROR(x, t, f, ...) do { esp_err_t e_ = (x); if (e_ != ESP_OK) { ESP_LOGE(t, f, ##__VA_ARGS__); return e_; } } while (0)
