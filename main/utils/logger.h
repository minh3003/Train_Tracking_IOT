#ifndef LOGGER_H
#define LOGGER_H

#include "esp_log.h"
#include "esp_timer.h"

/* Log wrapper macros */
#define LOG_I(tag, fmt, ...)  ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...)  ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...)  ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...)  ESP_LOGD(tag, fmt, ##__VA_ARGS__)

/* Log voi timestamp ms */
#define LOG_TS(tag, fmt, ...) \
    do { \
        uint32_t _ts = (uint32_t)(esp_timer_get_time()/1000ULL); \
        ESP_LOGI(tag, "[%lu ms] " fmt, (unsigned long)_ts, ##__VA_ARGS__); \
    } while(0)

#endif /* LOGGER_H */