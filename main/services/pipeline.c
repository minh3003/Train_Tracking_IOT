#include "pipeline.h"
#include "mqtt_service.h"
#include "cmd_handler.h"
#include "sd_log.h"
#include "sensor_fake.h"
#include "lte_at.h"
#include "logger.h"
#include "sys_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_system.h"

#include <stdio.h>
#include <string.h>

#define TAG "PIPELINE"

typedef struct {
    char     json[PAYLOAD_MAX_LEN];
    uint32_t ts;
} data_pkt_t;

static QueueHandle_t s_queue    = NULL;
static volatile pipe_state_t s_state    = PIPE_INIT;
static bool          s_sd_ready = false;
static volatile uint32_t     s_drop_cnt = 0;
static volatile bool         s_modem_reset_detected = false;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Flush offline buffer len MQTT - interleave voi queue */
#define FLUSH_BATCH_SIZE  4

/* Adaptive Rate thresholds (tu dong scale theo queue size) */
#define ADAPTIVE_SLOW_THRESHOLD  (PIPELINE_QUEUE_SIZE / 2)   /* 50% full */
#define ADAPTIVE_FAST_THRESHOLD  (PIPELINE_QUEUE_SIZE / 4)   /* 25% full */

static void prv_strip_eol(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

static void prv_drain_queue(data_pkt_t *pkt, int max)
{
    int cnt = 0;
    while (cnt < max &&
           xQueueReceive(s_queue, pkt, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (mqtt_service_publish(MQTT_TOPIC_DATA, pkt->json, 1) != ESP_OK) {
            LOG_W(TAG, "Flush-drain: pub fail, save SD");
            if (s_sd_ready) sd_log_append(SD_FILE_OFFLINE, pkt->json);
        } else {
            if (s_sd_ready) sd_log_append(SD_FILE_DATA, pkt->json);
        }
        cnt++;
    }
}

static void prv_flush_offline(void)
{
    if (!s_sd_ready) return;

    bool resume_processing = sd_log_file_exists(SD_FILE_PROCESSING);
    if (!resume_processing) {
        if (!sd_log_file_exists(SD_FILE_OFFLINE)) return;

        long off_sz = sd_log_file_size(SD_FILE_OFFLINE);
        if (off_sz <= 0) return;

        if (!sd_log_rename(SD_FILE_OFFLINE, SD_FILE_PROCESSING)) {
            LOG_W(TAG, "Flush skipped: cannot move offline buffer");
            return;
        }
    }

    long sz = sd_log_file_size(SD_FILE_PROCESSING);
    if (sz <= 0) return;
    LOG_I(TAG, "Flushing offline buffer (%ld bytes)%s...",
          sz, resume_processing ? " [resume]" : "");

    int ok = 0, fail = 0;
    int batch_cnt = 0;
    bool restored = true;
    char line[PAYLOAD_MAX_LEN];

    FILE *f = sd_log_open_read(SD_FILE_PROCESSING);
    if (!f) {
        LOG_E(TAG, "Flush: cannot open processing buffer");
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        prv_strip_eol(line);
        if (strlen(line) < 2) continue;

        /* Cho phep doc lenh tu server trong khi flush */
        mqtt_rx_msg_t rx_msg;
        if (mqtt_service_check_incoming(&rx_msg)) {
            if (strcmp(rx_msg.topic, MQTT_TOPIC_CMD) == 0) {
                cmd_handler_process(rx_msg.payload, rx_msg.payload_len);
            }
        }

        if (mqtt_service_publish(MQTT_TOPIC_DATA, line, 1) == ESP_OK) {
            ok++;
        } else {
            fail++;
            restored = sd_log_append(SD_FILE_OFFLINE, line);

            while (fgets(line, sizeof(line), f)) {
                prv_strip_eol(line);
                if (strlen(line) < 2) continue;
                if (!sd_log_append(SD_FILE_OFFLINE, line)) {
                    restored = false;
                }
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
        batch_cnt++;

        if (batch_cnt >= FLUSH_BATCH_SIZE) {
            /* Xen ke xu ly queue de tranh tran (Uu tien Live data) */
            data_pkt_t pkt;
            prv_drain_queue(&pkt, 10);
            batch_cnt = 0;
        }
    }
    sd_log_close_read(f);

    LOG_I(TAG, "Flush: %d sent, %d failed", ok, fail);
    if (fail == 0 || restored) {
        sd_log_delete(SD_FILE_PROCESSING);
    }

    if (fail == 0) {
        LOG_I(TAG, "Offline buffer cleared");
    } else if (!restored) {
        LOG_E(TAG, "Offline restore failed - keeping processing buffer");
    }
}

/* --- Init --- */
void pipeline_init(bool sd_ok)
{
    s_sd_ready = sd_ok;
    s_queue    = xQueueCreate(PIPELINE_QUEUE_SIZE, sizeof(data_pkt_t));
    s_state    = PIPE_LTE_CONNECTING;
    cmd_handler_init();
    LOG_I(TAG, "Pipeline init | SD=%s QueueSize=%d",
          sd_ok ? "OK" : "DISABLED", PIPELINE_QUEUE_SIZE);
}

pipe_state_t pipeline_get_state(void) { return s_state; }

/* --- Sensor Task --- */
void pipeline_sensor_task(void *arg)
{
    LOG_I(TAG, "sensor_task started");
    sensor_data_t data;
    data_pkt_t    pkt;

    bool adaptive_slow = false;

    while (1) {
        /* Cho pipeline san sang */
        if (s_state == PIPE_INIT ||
            s_state == PIPE_LTE_CONNECTING ||
            s_state == PIPE_MQTT_CONNECTING ||
            s_state == PIPE_RECONNECTING) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Offline + khong co SD = mat data */
        if (s_state == PIPE_OFFLINE && !s_sd_ready) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* Thuat toan Tan suat co gian (Adaptive Rate) */
        int q_len = uxQueueMessagesWaiting(s_queue);
        if (q_len >= ADAPTIVE_SLOW_THRESHOLD) {
            if (!adaptive_slow) {
                LOG_W(TAG, "Queue congested (%d/%d). Enabling adaptive delay.",
                      q_len, PIPELINE_QUEUE_SIZE);
                adaptive_slow = true;
            }
        } else if (q_len <= ADAPTIVE_FAST_THRESHOLD) {
            if (adaptive_slow) {
                LOG_I(TAG, "Queue recovered (%d/%d). Restoring normal delay.",
                      q_len, PIPELINE_QUEUE_SIZE);
                adaptive_slow = false;
            }
        }

        uint32_t base_interval = cmd_handler_get_sensor_interval();
        uint32_t current_interval = base_interval;
        if (adaptive_slow) {
            current_interval = base_interval * 2;
            if (current_interval < 5000) current_interval = 5000;
        }

        sensor_fake_read(&data);
        sensor_build_json(&data, pkt.json, sizeof(pkt.json));
        pkt.ts = now_ms();

        if (xQueueSend(s_queue, &pkt, pdMS_TO_TICKS(500)) != pdTRUE) {
            s_drop_cnt++;
            if (s_sd_ready) {
                bool ok = sd_log_append(SD_FILE_OFFLINE, pkt.json);
                if (s_drop_cnt <= 3 || (s_drop_cnt % 10) == 0)
                    LOG_W(TAG, "Queue full - %s #%lu (overflow: %lu)",
                          ok ? "saved SD" : "SD FAIL",
                          (unsigned long)data.seq, (unsigned long)s_drop_cnt);
            } else {
                if (s_drop_cnt <= 3 || (s_drop_cnt % 10) == 0)
                    LOG_E(TAG, "Queue full - DATA LOST #%lu (no SD!)",
                          (unsigned long)data.seq);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(current_interval));
    }
}

/* --- Transmit Task (State Machine) --- */
void pipeline_transmit_task(void *arg)
{
    LOG_I(TAG, "transmit_task started");

    /* LTE init */
    s_state = PIPE_LTE_CONNECTING;
    if (!lte_full_init()) {
        LOG_E(TAG, "LTE init FAILED");
        s_state = PIPE_OFFLINE;
    }

    /* MQTT connect */
    if (s_state != PIPE_OFFLINE) {
        s_state = PIPE_MQTT_CONNECTING;
        if (mqtt_service_connect() == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (mqtt_service_subscribe(MQTT_TOPIC_CMD, 1) != ESP_OK) {
                LOG_W(TAG, "Subscribe failed - telemetry continues");
            }
            s_state = PIPE_ONLINE;
            vTaskDelay(pdMS_TO_TICKS(1000));
            prv_flush_offline();
        } else {
            s_state = PIPE_OFFLINE;
            LOG_W(TAG, "MQTT failed >> OFFLINE mode");
        }
    }

    data_pkt_t    pkt;
    mqtt_rx_msg_t rx_msg;

    while (1) {
        switch (s_state) {

        case PIPE_ONLINE:
            if (mqtt_service_check_incoming(&rx_msg)) {
                /* Dispatch: chi xu ly topic command */
                if (strcmp(rx_msg.topic, MQTT_TOPIC_CMD) == 0) {
                    cmd_handler_process(rx_msg.payload, rx_msg.payload_len);
                } else {
                    LOG_I(TAG, "RX other topic: %s", rx_msg.topic);
                }
            }

            if (xQueueReceive(s_queue, &pkt, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (mqtt_service_publish(MQTT_TOPIC_DATA, pkt.json, 1) != ESP_OK) {
                    LOG_W(TAG, "Publish fail >> OFFLINE");
                    if (s_sd_ready) sd_log_append(SD_FILE_OFFLINE, pkt.json);
                    s_state = PIPE_RECONNECTING;
                } else {
                    if (s_sd_ready) sd_log_append(SD_FILE_DATA, pkt.json);
                }
            }
            break;

        case PIPE_OFFLINE:
            if (xQueueReceive(s_queue, &pkt, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (s_sd_ready) {
                    sd_log_append(SD_FILE_OFFLINE, pkt.json);
                    sd_log_append(SD_FILE_DATA, pkt.json);
                    LOG_W(TAG, "OFFLINE: saved >> %s", pkt.json);
                } else {
                    LOG_E(TAG, "OFFLINE + NO SD: DATA LOST!");
                }
            }
            /* Retry reconnect dinh ky */
            {
                static uint32_t last_retry = 0;
                uint32_t n = now_ms();
                if (n - last_retry > RECONNECT_DELAY_MS) {
                    last_retry = n;
                    s_state = PIPE_RECONNECTING;
                }
            }
            break;

        case PIPE_RECONNECTING:
        {
            static int s_reinit_fail_count = 0;

            LOG_I(TAG, "Reconnecting... (drops: %lu, reinit_fails: %d)",
                  (unsigned long)s_drop_cnt, s_reinit_fail_count);
            mqtt_service_disconnect();
            vTaskDelay(pdMS_TO_TICKS(2000));

            /* Check modem con song khong */
            if (s_modem_reset_detected ||
                lte_at_send("AT", "OK", NULL, 0, AT_TIMEOUT_SHORT) != ESP_OK) {

                /* Escalation: hard reset sau 3 lan soft fail */
                if (s_reinit_fail_count >= 3) {
                    LOG_E(TAG, "Soft recovery failed %d times >> HARD RESET",
                          s_reinit_fail_count);
                    lte_hard_reset();
                    s_reinit_fail_count = 0;
                }

                LOG_W(TAG, "LTE dead >> full reinit");
                s_modem_reset_detected = false;
                vTaskDelay(pdMS_TO_TICKS(LTE_BOOT_WAIT_MS));

                if (!lte_full_init()) {
                    s_reinit_fail_count++;
                    LOG_E(TAG, "LTE reinit FAILED (#%d)", s_reinit_fail_count);
                    s_state = PIPE_OFFLINE;
                    vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
                    break;
                }
                s_reinit_fail_count = 0;
            }

            if (mqtt_service_connect() == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(500));
                if (mqtt_service_subscribe(MQTT_TOPIC_CMD, 1) != ESP_OK) {
                    LOG_W(TAG, "Subscribe failed - telemetry continues");
                }
                s_state = PIPE_ONLINE;
                s_drop_cnt = 0;
                vTaskDelay(pdMS_TO_TICKS(1000));
                prv_flush_offline();
                LOG_I(TAG, "Reconnected [OK]");
            } else {
                s_state = PIPE_OFFLINE;
                LOG_W(TAG, "Reconnect fail >> OFFLINE");
                vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            }
            break;
        }

        default:
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* --- Watchdog Task --- */
void pipeline_watchdog_task(void *arg)
{
    LOG_I(TAG, "watchdog_task started");
    int wdg_fail_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_INTERVAL_MS));

        if (s_state == PIPE_LTE_CONNECTING ||
            s_state == PIPE_MQTT_CONNECTING ||
            s_state == PIPE_RECONNECTING) {
            LOG_I(TAG, "WDG: skip (state=%d, connecting)", (int)s_state);
            continue;
        }

        char resp[128];
        esp_err_t r = lte_at_send("AT+CSQ", "+CSQ:",
                                   resp, sizeof(resp), AT_TIMEOUT_SHORT);
        if (r == ESP_OK) {
            wdg_fail_count = 0;
            char *p = strstr(resp, "+CSQ:");
            int rssi = p ? atoi(p + 5) : 99;
            static uint32_t last_time_sync = 0;
            uint32_t n = now_ms();

            /* Phat hien modem tu reset qua URC */
            if (strstr(resp, "RDY") ||
                strstr(resp, "*ATREADY") ||
                strstr(resp, "+CFUN:") ||
                strstr(resp, "NORMAL POWER DOWN")) {
                LOG_E(TAG, "WDG: MODEM RESET DETECTED!");
                s_modem_reset_detected = true;
                if (s_state == PIPE_ONLINE) s_state = PIPE_RECONNECTING;
            }

            LOG_I(TAG, "WDG: RSSI=%d | State=%d | Drops=%lu",
                  rssi, (int)s_state, (unsigned long)s_drop_cnt);

            if (rssi > 0 && rssi <= 5) {
                LOG_W(TAG, "WDG: SIGNAL WEAK (RSSI=%d)", rssi);
            }

            if (!lte_time_is_valid() ||
                n - last_time_sync > (5 * 60 * 1000)) {
                last_time_sync = n;
                (void)lte_time_sync();
            }
        } else {
            wdg_fail_count++;
            LOG_E(TAG, "WDG: LTE NOT RESPONDING (fail #%d)", wdg_fail_count);

            if (wdg_fail_count >= 3) {
                LOG_E(TAG, "WDG: MODEM LIKELY RESET (3 fails)");
                s_modem_reset_detected = true;
                wdg_fail_count = 0;
            }

            if (s_state == PIPE_ONLINE) s_state = PIPE_RECONNECTING;
        }

        /* SD + queue status */
        if (s_sd_ready) {
            long data_sz = sd_log_file_size(SD_FILE_DATA);
            long off_sz = sd_log_file_size(SD_FILE_OFFLINE);
            long proc_sz = sd_log_file_size(SD_FILE_PROCESSING);

            /* SD health check: neu tat ca file deu fail -> SD co the da chet */
            static int sd_fail_streak = 0;
            if (data_sz < 0 && off_sz < 0 && proc_sz < 0) {
                sd_fail_streak++;
                LOG_W(TAG, "WDG: SD unresponsive (streak=%d)", sd_fail_streak);
                if (sd_fail_streak >= 3) {
                    LOG_E(TAG, "WDG: SD dead for %d cycles >> REINIT", sd_fail_streak);
                    if (sd_log_reinit() == ESP_OK) {
                        sd_fail_streak = 0;
                    }
                }
            } else {
                sd_fail_streak = 0;
            }

            if (data_sz < 0) data_sz = 0;
            if (off_sz < 0) off_sz = 0;
            if (proc_sz < 0) proc_sz = 0;

            LOG_I(TAG, "WDG: data=%ldB offline=%ldB processing=%ldB queue=%d/%d",
                  data_sz,
                  off_sz,
                  proc_sz,
                  (int)uxQueueMessagesWaiting(s_queue),
                  PIPELINE_QUEUE_SIZE);
        }

        /* Heap monitoring: chung minh khong co Memory Leak */
        LOG_I(TAG, "WDG: heap_free=%lu heap_min=%lu",
              (unsigned long)esp_get_free_heap_size(),
              (unsigned long)esp_get_minimum_free_heap_size());
    }
}
