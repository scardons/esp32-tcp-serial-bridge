#pragma once
/* Tests implement this hook, so they can inspect what the firmware logs. */
void test_log(char level, const char *tag, const char *format, ...);
#define ESP_LOGI(t, f, ...) test_log('I', t, f, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) test_log('W', t, f, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) test_log('E', t, f, ##__VA_ARGS__)
