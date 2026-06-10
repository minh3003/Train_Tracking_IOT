/**
 * main.c - Entry point: init hardware, spawn tasks
 *
 * Core 0: transmit (LTE/MQTT), watchdog
 * Core 1: sensor
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "logger.h"
#include "lte_at.h"
#include "sd_log.h"
#include "pipeline.h"
#include "pin_config.h"
#include "sys_config.h"

#define TAG "MAIN"

void app_main(void)
{
    /* NVS Init */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Boot banner */
    LOG_I(TAG, "======================================");
    LOG_I(TAG, "  TRAIN TRACKING IOT");
    LOG_I(TAG, "  LTE + MQTT + SD + Offline Buffer");
    LOG_I(TAG, "  Build: %s %s", __DATE__, __TIME__);
    LOG_I(TAG, "======================================");
    LOG_I(TAG, "Free heap: %lu bytes",
          (unsigned long)esp_get_free_heap_size());

    /* LED init */
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 1);

    /* SD Card init */
    bool sd_ok = (sd_log_init() == ESP_OK);
    if (sd_ok) {
        sd_log_append(SD_FILE_DATA, "=== DEVICE BOOT ===");
    } else {
        LOG_W(TAG, "SD not available - offline buffering DISABLED");
    }

    /* LTE UART init */
    if (lte_at_init() != ESP_OK) {
        LOG_E(TAG, "UART init FAILED - check wiring");
        return;
    }

    /* Cho A7680C boot */
    LOG_I(TAG, "Waiting %ds for A7680C boot...", LTE_BOOT_WAIT_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(LTE_BOOT_WAIT_MS));

    /* Pipeline init */
    pipeline_init(sd_ok);

    /* Spawn tasks */
    LOG_I(TAG, "Spawning tasks...");

    xTaskCreatePinnedToCore(pipeline_sensor_task,
                "task_sensor", 4096, NULL, 3, NULL, 1);

    xTaskCreatePinnedToCore(pipeline_transmit_task,
                "task_transmit", 8192, NULL, 4, NULL, 0);

    xTaskCreatePinnedToCore(pipeline_watchdog_task,
                "task_watchdog", 4096, NULL, 5, NULL, 0);

    LOG_I(TAG, "All tasks spawned [OK]");
    LOG_I(TAG, "System running...");
}