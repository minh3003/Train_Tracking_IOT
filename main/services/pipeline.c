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
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "PIPELINE"

typedef struct {
    char     json[PAYLOAD_MAX_LEN];
    uint32_t ts;
} data_pkt_t;

extern volatile bool g_net_cut_active;

static QueueHandle_t s_queue    = NULL;
static volatile pipe_state_t s_state    = PIPE_INIT;
static bool          s_sd_ready = false;
static volatile uint32_t     s_drop_cnt = 0;
static volatile bool         s_modem_reset_detected = false;
static uint16_t      s_boot_id  = 0;  /* Random ID per boot session (YC3: Dedup key) */
static uint32_t      s_boot_epoch_sec = 0; /* Lưu mốc thời gian tuyệt đối lúc boot */

/* ---- Packet Telemetry Counters (YC2/YC3: Audit Trail) ---- */
static volatile uint32_t s_pkt_produced  = 0;  /* Tong goi tin sensor da sinh ra */
static volatile uint32_t s_pkt_delivered = 0;  /* Goi tin MQTT publish thanh cong */
static volatile uint32_t s_pkt_offline   = 0;  /* Goi tin da luu vao SD offline */
static volatile uint32_t s_pkt_dropped   = 0;  /* Goi tin bi mat (khong SD, khong queue) */

static volatile long s_sd_read_offset = 0;     /* Vi tri con tro dang doc trong processing.buf */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Flush offline buffer len MQTT - interleave voi queue */
#define FLUSH_BATCH_SIZE  4

/* Adaptive Rate thresholds (tu dong scale theo queue size) */
#define ADAPTIVE_SLOW_THRESHOLD  (PIPELINE_QUEUE_SIZE / 2)   /* 50% full */
#define ADAPTIVE_FAST_THRESHOLD  (PIPELINE_QUEUE_SIZE / 4)   /* 25% full */


static void prv_patch_json_time(char *json_line)
{
    if (s_boot_epoch_sec == 0) return;

    char *time_ptr = strstr(json_line, "\"time\":\"0000-00-00T00:00:00\"");
    if (!time_ptr) return;

    char *ts_ptr = strstr(json_line, "\"ts\":");
    if (!ts_ptr) return;

    uint32_t pkt_ts = 0;
    if (sscanf(ts_ptr, "\"ts\":%lu", &pkt_ts) != 1) return;

    uint32_t pkt_epoch = s_boot_epoch_sec + (pkt_ts / 1000);
    struct tm tm_pkt;
    time_t e = (time_t)pkt_epoch;
    localtime_r(&e, &tm_pkt);

    char iso_str[64]; /* [FIX] Enlarged from 32 to avoid -Werror=format-truncation */
    snprintf(iso_str, sizeof(iso_str), "%04d-%02d-%02dT%02d:%02d:%02d",
             tm_pkt.tm_year + 1900, tm_pkt.tm_mon + 1, tm_pkt.tm_mday,
             tm_pkt.tm_hour, tm_pkt.tm_min, tm_pkt.tm_sec);

    memcpy(time_ptr + 8, iso_str, 19);
}

static void prv_drain_queue(data_pkt_t *pkt, int max)
{
    int cnt = 0;
    while (cnt < max &&
           xQueueReceive(s_queue, pkt, pdMS_TO_TICKS(50)) == pdTRUE) {
        
        if (g_net_cut_active) {
            LOG_W(TAG, "Net fault detected during drain! Aborting...");
            if (s_sd_ready) { sd_log_append(SD_FILE_OFFLINE, pkt->json); s_pkt_offline++; }
            break;
        }

        prv_patch_json_time(pkt->json);
        if (mqtt_service_publish(MQTT_TOPIC_DATA, pkt->json, 1) != ESP_OK) {
            LOG_W(TAG, "Flush-drain: pub fail, save SD");
            if (s_sd_ready) { sd_log_append(SD_FILE_OFFLINE, pkt->json); s_pkt_offline++; }
        } else {
            s_pkt_delivered++;
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
    if (sz <= 0) {
        sd_log_delete(SD_FILE_PROCESSING);
        s_sd_read_offset = 0;
        return;
    }
    LOG_I(TAG, "Flushing offline buffer (%ld bytes)%s...",
          sz, resume_processing ? " [resume]" : "");

    int ok = 0, fail = 0;
    bool publish_failed = false;
    /* QUAN TRONG: static de tranh Stack Overflow. 
       Su dung 4 slot (Ty le 4:10) = 4 * 512 = 2048 Bytes luu tren .bss */
    static char lines[4][PAYLOAD_MAX_LEN];

    if (!resume_processing) {
        s_sd_read_offset = 0; /* Reset offset neu tao file processing moi */
    }

    while (!publish_failed) {
        // Kiem tra ngat mang khan cap ngay ca khi dang xa data
        if (g_net_cut_active) {
            LOG_W(TAG, "Net fault detected during flush! Aborting...");
            publish_failed = true;
            break;
        }

        // Lay 1 chunk 4 dong tu the nho (Slow-path: 4 goi Offline)
        // Ham nay tu dong cap nhat s_sd_read_offset thanh offset tiep theo an toan nhat
        int n_lines = sd_log_read_lines(SD_FILE_PROCESSING, (long*)&s_sd_read_offset, lines, 4);
        if (n_lines <= 0) break; // Da doc het file

        for (int i = 0; i < n_lines; i++) {

            if (g_net_cut_active) {
                LOG_W(TAG, "Net fault detected during flush inner loop! Aborting...");
                fail++;
                publish_failed = true;
                for (int j = i; j < n_lines; j++) {
                    sd_log_append(SD_FILE_OFFLINE, lines[j]);
                }
                break;
            }

            /* Cho phep doc lenh tu server trong khi flush */
            mqtt_rx_msg_t rx_msg;
            if (mqtt_service_check_incoming(&rx_msg)) {
                if (strcmp(rx_msg.topic, MQTT_TOPIC_CMD) == 0) {
                    cmd_handler_process(rx_msg.payload, rx_msg.payload_len);
                }
            }

            prv_patch_json_time(lines[i]);

            if (mqtt_service_publish(MQTT_TOPIC_DATA, lines[i], 1) == ESP_OK) {
                ok++; s_pkt_delivered++;
                vTaskDelay(pdMS_TO_TICKS(20)); /* [OPT] 100->20ms: giam delay giua cac ban tin flush */
            } else {
                fail++;
                publish_failed = true;
                
                // Ghi lai dong hien tai VA CAC DONG SAU DO trong chunk vao OFFLINE vi chua gui duoc
                for (int j = i; j < n_lines; j++) {
                    sd_log_append(SD_FILE_OFFLINE, lines[j]);
                }
                break;
            }
        }

        /* Xen ke xu ly queue de tranh tran (Uu tien Live data) */
        data_pkt_t pkt;
        prv_drain_queue(&pkt, 10);
    }

    /* [H1-FIX] Xu ly phan con lai neu flush that bai.
     * BUG CU: neu restore that bai mot phan (restored=false), processing file bi giu lai.
     * Lan boot sau se phat hien processing ton tai va re-flush tu dau (offset=0),
     * khien cac ban tin da publish thanh cong bi gui LAI = DUPLICATE DATA.
     * FIX: LUON xoa processing file. Neu mot so dong khong restore duoc vao offline,
     * do la mat mat du lieu can chap nhan, tot hon la gui duplicate. */
    int restore_ok = 0, restore_fail = 0;
    if (publish_failed) {
        LOG_W(TAG, "Flush interrupted. Re-buffering remaining data...");
        int max_rebuf_iters = 2000; /* [FIX] Phong thu: 2000 * 4 dong = toi da ~400KB buffer */
        while (max_rebuf_iters-- > 0) {
            int n_lines = sd_log_read_lines(SD_FILE_PROCESSING, (long*)&s_sd_read_offset, lines, 4);
            if (n_lines <= 0) break;

            for (int i = 0; i < n_lines; i++) {
                if (sd_log_append(SD_FILE_OFFLINE, lines[i])) {
                    restore_ok++;
                } else {
                    restore_fail++;
                }
            }
        }
        if (max_rebuf_iters <= 0) {
            LOG_E(TAG, "Re-buffer: hit iteration limit! SD may be stuck.");
        }
        LOG_W(TAG, "Re-buffer: %d ok, %d failed (lost)", restore_ok, restore_fail);
    }

    LOG_I(TAG, "Flush: %d sent, %d failed", ok, fail);

    /* Luon xoa processing de tranh duplicate data o lan boot sau */
    sd_log_delete(SD_FILE_PROCESSING);
    s_sd_read_offset = 0; /* Reset con tro sau khi xoa */

    if (fail == 0) {
        LOG_I(TAG, "Offline buffer cleared");
    } else if (restore_fail > 0) {
        LOG_W(TAG, "Flush done: %d lines could not be re-buffered (SD write error)", restore_fail);
    } else {
        LOG_I(TAG, "Flush done: interrupted data re-buffered to offline (%d lines)", restore_ok);
    }
}

/* Xa toan bo queue RAM xuong SD truoc khi task bi block (tranh mat data khi sap nguon) */
static void prv_drain_queue_to_sd(void)
{
    data_pkt_t tmp;
    int cnt = 0;
    while (xQueueReceive(s_queue, &tmp, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_sd_ready) {
            sd_log_append(SD_FILE_OFFLINE, tmp.json);
            sd_log_append(SD_FILE_DATA,    tmp.json);
            s_pkt_offline++;
        } else {
            s_pkt_dropped++;
        }
        cnt++;
    }
    if (cnt > 0) {
        LOG_W(TAG, "OFFLINE drain: %d pkts flushed RAM->SD (volatile RAM cleared)", cnt);
    }
}

/* --- Init --- */
void pipeline_init(bool sd_ok)
{
    s_sd_ready = sd_ok;
    s_queue    = xQueueCreate(PIPELINE_QUEUE_SIZE, sizeof(data_pkt_t));
    s_state    = PIPE_LTE_CONNECTING;
    
    /* --- NVS Boot Count (YC3: Persistent Dedup Key) --- */
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    uint16_t boot_count = 1; // Default
    if (err == ESP_OK) {
        nvs_get_u16(my_handle, "boot_count", &boot_count);
        s_boot_id = boot_count;
        boot_count++;
        nvs_set_u16(my_handle, "boot_count", boot_count);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    } else {
        s_boot_id = (uint16_t)(esp_random() & 0xFFFF); /* Fallback */
    }

    cmd_handler_init();
    LOG_I(TAG, "Pipeline init | SD=%s QueueSize=%d BootID=0x%04X (%d)",
          sd_ok ? "OK" : "DISABLED", PIPELINE_QUEUE_SIZE, s_boot_id, s_boot_id);
}

pipe_state_t pipeline_get_state(void) { return s_state; }

/* --- Sensor Task --- */
void pipeline_sensor_task(void *arg)
{
    LOG_I(TAG, "sensor_task started");
    sensor_data_t data;
    data_pkt_t    pkt;

    /* bool adaptive_slow = false; (DEAD CODE) */

    while (1) {
        /* Cho pipeline khoi tao xong */
        if (s_state == PIPE_INIT) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Offline + khong co SD = mat data */
        if (s_state == PIPE_OFFLINE && !s_sd_ready) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* Thuat toan Tan suat co gian (Adaptive Rate) - [DEAD CODE] 
           Da comment out vi FSM luon xa rong RAM Queue xuong SD khi mat ket noi.
           Queue khong bao gio dat nguong 50 Slot de kich hoat tinh nang nay.
        */
        /*
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
        */
        
        uint32_t current_interval = cmd_handler_get_sensor_interval();

        sensor_fake_read(&data);

        /* Lay thoi gian thuc NITZ (wall clock) de thiet lap mốc Neo (Time Anchor) */
        lte_time_t t;
        if (lte_time_get(&t)) {
            if (s_boot_epoch_sec == 0) {
                struct tm tm_time = {0};
                tm_time.tm_year = t.year - 1900;
                tm_time.tm_mon  = t.month - 1;
                tm_time.tm_mday = t.day;
                tm_time.tm_hour = t.hour;
                tm_time.tm_min  = t.min;
                tm_time.tm_sec  = t.sec;
                /* mktime hoat dong voi local timezone hoac UTC tuy theo setenv("TZ") */
                time_t current_epoch = mktime(&tm_time);
                /* [TZ-FIX] Do vi điều khiển không được cài đặt biến môi trường TZ,
                 * hàm localtime_r() ở prv_patch_json_time sẽ mặc định trả về giờ UTC.
                 * Để bù trừ, ta KHÔNG TRỪ tz_offset_sec ở đây. Ta sẽ dùng epoch "giả"
                 * (đã cộng sẵn 7 tiếng) làm mốc Neo. Khi localtime_r() dịch ngược lại,
                 * nó sẽ in ra chính xác giờ Local của Việt Nam. */
                int tz_offset_sec = t.tz_quarters * 15 * 60; 
                s_boot_epoch_sec = (uint32_t)current_epoch - (now_ms() / 1000);
                LOG_I(TAG, "Time Anchor captured: Boot Epoch = %lu (tz_offset=%ds)",
                      (unsigned long)s_boot_epoch_sec, tz_offset_sec);
            }
        }

        /* [CACHE-TIME FIX] KHONG de sensor_task tự dán thời gian LTE tĩnh (bị lưu cache 30s)
         * Luôn chèn NULL để ép JSON là "0000-00-00...". 
         * Thuật toán Neo thời gian (Time Anchoring) sẽ tự tính toán chi tiết
         * từng mili-giây cho bản tin này ngay trước lúc publish! */
        sensor_build_json(&data, pkt.json, sizeof(pkt.json), NULL, s_boot_id);
        pkt.ts = now_ms();
        s_pkt_produced++;

        if (xQueueSend(s_queue, &pkt, pdMS_TO_TICKS(500)) != pdTRUE) {
            s_drop_cnt++;
            if (s_sd_ready) {
                bool ok = sd_log_append(SD_FILE_OFFLINE, pkt.json);
                if (ok) s_pkt_offline++; else s_pkt_dropped++;
                if (s_drop_cnt <= 3 || (s_drop_cnt % 10) == 0)
                    LOG_W(TAG, "Queue full - %s #%lu (overflow: %lu)",
                          ok ? "saved SD" : "SD FAIL",
                          (unsigned long)data.seq, (unsigned long)s_drop_cnt);
            } else {
                s_pkt_dropped++;
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
            if (g_net_cut_active) {
                LOG_W(TAG, "Net fault active! Force disconnecting MQTT...");
                mqtt_service_disconnect();
                s_state = PIPE_OFFLINE;
                break;
            }

            if (mqtt_service_check_incoming(&rx_msg)) {
                /* Dispatch: chi xu ly topic command */
                if (strcmp(rx_msg.topic, MQTT_TOPIC_CMD) == 0) {
                    cmd_handler_process(rx_msg.payload, rx_msg.payload_len);
                } else {
                    LOG_I(TAG, "RX other topic: %s", rx_msg.topic);
                }
            }

            if (xQueueReceive(s_queue, &pkt, pdMS_TO_TICKS(100)) == pdTRUE) {
                prv_patch_json_time(pkt.json);
                if (mqtt_service_publish(MQTT_TOPIC_DATA, pkt.json, 1) != ESP_OK) {
                    LOG_W(TAG, "Publish fail >> OFFLINE");
                    if (s_sd_ready) { sd_log_append(SD_FILE_OFFLINE, pkt.json); s_pkt_offline++; }
                    s_state = PIPE_RECONNECTING;
                } else {
                    s_pkt_delivered++;
                    if (s_sd_ready) sd_log_append(SD_FILE_DATA, pkt.json);
                }
            }
            break;

        case PIPE_OFFLINE:
            /* Xa TOAN BO queue RAM xuong SD moi vong lap - dam bao RAM volatile khong giu data lau */
            {
                data_pkt_t tmp_pkt;
                while (xQueueReceive(s_queue, &tmp_pkt, pdMS_TO_TICKS(10)) == pdTRUE) {
                    if (s_sd_ready) {
                        sd_log_append(SD_FILE_OFFLINE, tmp_pkt.json);
                        sd_log_append(SD_FILE_DATA,    tmp_pkt.json);
                        s_pkt_offline++;
                    } else {
                        s_pkt_dropped++;
                        LOG_E(TAG, "OFFLINE + NO SD: DATA LOST!");
                    }
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
            if (g_net_cut_active) {
                s_state = PIPE_OFFLINE;
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
            }

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
                    prv_drain_queue_to_sd(); /* Xa queue truoc khi ngu - dam bao RAM an toan */
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
                
                if (g_net_cut_active) {
                    LOG_W(TAG, "Net fault injected during flush. Going offline.");
                    mqtt_service_disconnect();
                    s_state = PIPE_OFFLINE;
                } else {
                    LOG_I(TAG, "Reconnected [OK]");
                }
            } else {
                s_state = PIPE_OFFLINE;
                LOG_W(TAG, "Reconnect fail >> OFFLINE");
                prv_drain_queue_to_sd(); /* Xa queue truoc khi ngu - dam bao RAM an toan */
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
                n - last_time_sync > (30 * 1000)) {
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
                if (s_state == PIPE_ONLINE) s_state = PIPE_RECONNECTING;
            }
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

        /* Packet telemetry counters (YC2: Audit Trail) */
        LOG_I(TAG, "WDG: pkt produced=%lu delivered=%lu offline=%lu dropped=%lu",
              (unsigned long)s_pkt_produced,
              (unsigned long)s_pkt_delivered,
              (unsigned long)s_pkt_offline,
              (unsigned long)s_pkt_dropped);

        /* Publish heartbeat status len Broker moi 5 phut (YC1: Dong bo Broker) */
        {
            static uint32_t last_heartbeat = 0;
            uint32_t n = now_ms();
            if (s_state == PIPE_ONLINE &&
                (last_heartbeat == 0 || n - last_heartbeat > (30 * 1000))) {
                last_heartbeat = n;
                long sd_backlog = 0;
                if (s_sd_ready) {
                    long off_sz = sd_log_file_size(SD_FILE_OFFLINE);
                    long proc_sz = sd_log_file_size(SD_FILE_PROCESSING);
                    if (off_sz > 0) sd_backlog += off_sz;
                    if (proc_sz > 0) {
                        /* Tru di phan offset da doc de tinh con so thuc te con lai */
                        if (proc_sz > s_sd_read_offset) {
                            sd_backlog += (proc_sz - s_sd_read_offset);
                        }
                    }
                }
                int q_len = uxQueueMessagesWaiting(s_queue);

                char hb[300];
                snprintf(hb, sizeof(hb),
                    "{\"dev\":\"%s\","
                    "\"produced\":%lu,"
                    "\"delivered\":%lu,"
                    "\"offline\":%lu,"
                    "\"dropped\":%lu,"
                    "\"heap_free\":%lu,"
                    "\"queue_len\":%d,"
                    "\"sd_backlog\":%ld,"
                    "\"sd_pkts\":%ld,"
                    "\"state\":%d}",
                    DEVICE_ID,
                    (unsigned long)s_pkt_produced,
                    (unsigned long)s_pkt_delivered,
                    (unsigned long)s_pkt_offline,
                    (unsigned long)s_pkt_dropped,
                    (unsigned long)esp_get_free_heap_size(),
                    q_len,
                    sd_backlog,
                    (long)(sd_backlog / 195),
                    (int)s_state);
                /* [H2-FIX] Kiem tra return value de detect MQTT zombie state.
                 * Neu publish that bai, s_state cua mqtt_service se la MQTT_ST_ERROR
                 * nhung pipeline khong biet neu ta bo qua return value nay. */
                if (mqtt_service_publish(MQTT_TOPIC_STATUS, hb, 1) != ESP_OK) {
                    LOG_W(TAG, "WDG: heartbeat publish failed");
                }
            }
        }
    }
}
