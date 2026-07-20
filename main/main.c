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

/* --- Network Fault Injection (Hardware Network Kill Switch) --- */
/* Cam day Jumper vao GND -> Kich hoat trang thai mat song gia lap */
/* GPIO25: Co tich hop tro keo noi (internal pull-up), an toan khi ho mach */
#define NET_CUT_PIN 25
volatile bool g_net_cut_active = false;

static void net_fault_task(void *arg)
{
    gpio_reset_pin(NET_CUT_PIN);
    gpio_set_direction(NET_CUT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(NET_CUT_PIN, GPIO_PULLUP_ONLY);

    bool last_state = true;

    while (1) {
        bool current_state = gpio_get_level(NET_CUT_PIN);
        
        /* Phat hien trang thai day thay doi */
        if (current_state != last_state) {
            /* Software Debounce 50ms */
            vTaskDelay(pdMS_TO_TICKS(50));
            
            if (gpio_get_level(NET_CUT_PIN) == current_state) {
                last_state = current_state;
                
                if (current_state == 0) {
                    /* Cam day Jumper vao GND -> Kich hoat mat song gia lap */
                    g_net_cut_active = true;
                    LOG_E(TAG, ">>> NET FAULT INJECTED (Network Killed) <<<");
                    /* [C3-FIX] KHONG goi lte_at_send truc tiep tai day!
                     * net_fault_task chay Priority 2, thap hon transmit_task (Prio 4)
                     * dang co the giu UART mutex toi 15-25 giay (publish + URC wait).
                     * Goi AT truc tiep se bi timeout va khong co tac dung.
                     * transmit_task va watchdog se detect g_net_cut_active = true
                     * va chuyen sang PIPE_OFFLINE de xu ly. */
                } else {
                    /* Rut day Jumper ra khoi GND (Pull-up keo len 1) -> Phuc hoi mang */
                    g_net_cut_active = false;
                    LOG_I(TAG, ">>> NET FAULT CLEARED (Network Restored) <<<");
                    /* [C3-FIX] Tuong tu: transmit_task se tu detect flag = false
                     * va trigger PIPE_RECONNECTING de ket noi lai. */
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

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
                "task_watchdog", 8192, NULL, 5, NULL, 0);

    /* Task Network Fault Injection: lang nghe Jumper Wire GPIO25 de gia lap mat song */
    xTaskCreatePinnedToCore(net_fault_task,
                "task_net_fault", 3072, NULL, 2, NULL, 0);

    LOG_I(TAG, "All tasks spawned [OK]");
    LOG_I(TAG, "System running...");
}