/**
 * cmd_handler.c - Xu ly lenh 2 chieu tu server
 *
 * Lightweight JSON parser (khong dung thu vien ngoai).
 * Moi lenh co handler rieng, ket qua ACK len MQTT_TOPIC_RESP.
 */

#include "cmd_handler.h"
#include "mqtt_service.h"
#include "pipeline.h"
#include "logger.h"
#include "sys_config.h"
#include "sd_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "CMD"

/* --- Tham so co the dieu chinh tu xa --- */
static uint32_t s_sensor_interval_ms = SENSOR_INTERVAL_MS;

/* --- Lightweight JSON helpers (khong malloc) --- */

/**
 * Lay gia tri string cua key trong JSON.
 * Tra ve do dai gia tri, -1 neu khong tim thay.
 * out_val tro vao vi tri trong json (khong copy).
 */
static int prv_json_str(const char *json, const char *key,
                        const char **out_val)
{
    char pat[48];
    int n = snprintf(pat, sizeof(pat), "\"%s\":", key);
    if (n <= 0) return -1;

    const char *p = strstr(json, pat);
    if (!p) return -1;

    p += strlen(pat);
    /* Bo qua khoang trang */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p != '"') return -1;
    p++; /* Bo qua dau ngoac kep mo */

    const char *end = strchr(p, '"');
    if (!end) return -1;

    *out_val = p;
    return (int)(end - p);
}

/**
 * Lay gia tri int cua key trong JSON.
 * Tra ve true neu tim thay.
 */
static bool prv_json_int(const char *json, const char *key, int *out)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);

    const char *p = strstr(json, pat);
    if (!p) return false;

    p += strlen(pat);
    /* Bo qua khoang trang */
    while (*p == ' ') p++;

    if (*p == '"') return false; /* La string, khong phai int */
    *out = atoi(p);
    return true;
}

/* --- ACK helper: gui phan hoi len topic response --- */
static void prv_send_ack(const char *cmd_name, const char *result,
                          const char *extra)
{
    char ack[256];
    if (extra && extra[0]) {
        snprintf(ack, sizeof(ack),
                 "{\"cmd\":\"%s\",\"result\":\"%s\",%s}",
                 cmd_name, result, extra);
    } else {
        snprintf(ack, sizeof(ack),
                 "{\"cmd\":\"%s\",\"result\":\"%s\"}",
                 cmd_name, result);
    }
    mqtt_service_publish(MQTT_TOPIC_RESP, ack, 1);
    LOG_I(TAG, "ACK >> %s", ack);
}

/* =================================================================
 * Command handlers
 * ================================================================= */

/* set_interval: thay doi tan suat doc sensor (ms) */
static void prv_cmd_set_interval(const char *json)
{
    int val = 0;
    if (!prv_json_int(json, "value", &val) || val < 1000 || val > 60000) {
        prv_send_ack("set_interval", "error",
                     "\"msg\":\"value 1000-60000 required\"");
        return;
    }
    s_sensor_interval_ms = (uint32_t)val;
    LOG_I(TAG, "Sensor interval = %lu ms", (unsigned long)s_sensor_interval_ms);

    char extra[64];
    snprintf(extra, sizeof(extra), "\"new_value\":%lu",
             (unsigned long)s_sensor_interval_ms);
    prv_send_ack("set_interval", "ok", extra);
}

/* get_status: tra ve trang thai hien tai */
static void prv_cmd_get_status(const char *json)
{
    (void)json;
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t heap = (uint32_t)esp_get_free_heap_size();
    pipe_state_t st = pipeline_get_state();

    static const char *st_names[] = {
        "init", "lte_connecting", "mqtt_connecting",
        "online", "offline", "reconnecting"
    };
    const char *st_str = (st <= PIPE_RECONNECTING) ? st_names[st] : "unknown";

    char extra[160];
    snprintf(extra, sizeof(extra),
             "\"state\":\"%s\",\"uptime_s\":%lu,"
             "\"heap\":%lu,\"interval_ms\":%lu",
             st_str,
             (unsigned long)uptime_s,
             (unsigned long)heap,
             (unsigned long)s_sensor_interval_ms);
    prv_send_ack("get_status", "ok", extra);
}

/* reboot: khoi dong lai ESP32 */
static void prv_cmd_reboot(const char *json)
{
    (void)json;
    prv_send_ack("reboot", "ok", "\"msg\":\"rebooting in 2s\"");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

/* sd_size: lay kich thuoc cac file tren SD card */
static void prv_cmd_sd_size(const char *json)
{
    (void)json;
    long data_sz = sd_log_file_size(SD_FILE_DATA);
    long off_sz = sd_log_file_size(SD_FILE_OFFLINE);
    long proc_sz = sd_log_file_size(SD_FILE_PROCESSING);

    if (data_sz < 0) data_sz = 0;
    if (off_sz < 0) off_sz = 0;
    if (proc_sz < 0) proc_sz = 0;

    LOG_I(TAG, "Command sd_size >> data=%ld offline=%ld processing=%ld",
          data_sz, off_sz, proc_sz);

    char extra[256];
    snprintf(extra, sizeof(extra),
             "\"data_size\":%ld,\"offline_size\":%ld,"
             "\"processing_size\":%ld,\"data_max\":%d,"
             "\"retention_days\":%d",
             data_sz, off_sz, proc_sz,
             DATA_LOG_MAX_BYTES, DATA_LOG_RETENTION_DAYS);
    prv_send_ack("sd_size", "ok", extra);
}

/* =================================================================
 * Dispatch table
 * ================================================================= */
typedef void (*cmd_fn_t)(const char *json);

typedef struct {
    const char *name;
    cmd_fn_t    handler;
} cmd_entry_t;

static const cmd_entry_t s_cmd_table[] = {
    { "set_interval", prv_cmd_set_interval },
    { "get_status",   prv_cmd_get_status   },
    { "reboot",       prv_cmd_reboot       },
    { "sd_size",      prv_cmd_sd_size      },
};
#define CMD_TABLE_SIZE  (sizeof(s_cmd_table) / sizeof(s_cmd_table[0]))

/* =================================================================
 * Public API
 * ================================================================= */

void cmd_handler_init(void)
{
    s_sensor_interval_ms = SENSOR_INTERVAL_MS;
    LOG_I(TAG, "cmd_handler init | %d commands registered",
          (int)CMD_TABLE_SIZE);
}

void cmd_handler_process(const char *payload, int pay_len)
{
    if (!payload || pay_len <= 2) return;

    /* Dam bao null-terminated (caller da lam nhung phong thu) */
    char buf[256];
    int copy_len = (pay_len < (int)sizeof(buf) - 1) ? pay_len : (int)sizeof(buf) - 1;
    memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    LOG_I(TAG, "RX cmd: %s", buf);

    /* Parse "cmd" field */
    const char *cmd_val = NULL;
    int cmd_len = prv_json_str(buf, "cmd", &cmd_val);
    if (cmd_len <= 0 || cmd_len > 32) {
        LOG_W(TAG, "No 'cmd' field in payload");
        prv_send_ack("unknown", "error", "\"msg\":\"missing cmd field\"");
        return;
    }

    /* Lookup trong dispatch table */
    for (int i = 0; i < (int)CMD_TABLE_SIZE; i++) {
        if ((int)strlen(s_cmd_table[i].name) == cmd_len &&
            strncmp(s_cmd_table[i].name, cmd_val, cmd_len) == 0) {
            s_cmd_table[i].handler(buf);
            return;
        }
    }

    /* Lenh khong ho tro */
    LOG_W(TAG, "Unknown cmd (len=%d)", cmd_len);
    prv_send_ack("unknown", "error", "\"msg\":\"unsupported command\"");
}

uint32_t cmd_handler_get_sensor_interval(void)
{
    return s_sensor_interval_ms;
}
