/**
 * mqtt_service.c - MQTT client cho A7680C (AT+CMQTT*)
 *
 * Luu y: A7680C luon dung prefix "tcp://" ke ca khi TLS.
 */

#include "mqtt_service.h"
#include "lte_at.h"
#include "logger.h"
#include "pin_config.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "MQTT"

static mqtt_state_t s_state = MQTT_ST_DISCONNECTED;
static char         s_buf[2048];

#define MQTT_URC_QUEUE_LEN  8
typedef struct {
    char buf[1024];
} mqtt_urc_item_t;

static QueueHandle_t s_urc_queue = NULL;

static void prv_urc_queue_init(void)
{
    if (!s_urc_queue) {
        s_urc_queue = xQueueCreate(MQTT_URC_QUEUE_LEN, sizeof(mqtt_urc_item_t));
    }
}

void mqtt_service_push_urc(const char *urc_text)
{
    if (urc_text && strstr(urc_text, "+CMQTTRXSTART:")) {
        prv_urc_queue_init();
        if (!s_urc_queue) return;

        mqtt_urc_item_t item = {0};
        strncpy(item.buf, urc_text, sizeof(item.buf) - 1);

        if (xQueueSend(s_urc_queue, &item, 0) != pdTRUE) {
            mqtt_urc_item_t old;
            (void)xQueueReceive(s_urc_queue, &old, 0);
            (void)xQueueSend(s_urc_queue, &item, 0);
        }
    }
}

static bool prv_send_data(const char *at_cmd, const char *data, size_t len)
{
    lte_at_flush();
    esp_err_t r = lte_at_send_locked(at_cmd, ">", s_buf, sizeof(s_buf), 5000);
    if (r != ESP_OK) {
        LOG_E(TAG, "Data-mode prompt FAIL: %s", at_cmd);
        lte_at_send_raw((const uint8_t *)"\x1A", 1); /* Ctrl+Z thoat data-mode */
        vTaskDelay(pdMS_TO_TICKS(300));
        lte_at_flush();
        return false;
    }

    lte_at_send_raw((const uint8_t *)data, len);

    /* Cho OK/ERROR voi early exit (thay vi cho het 8s) */
    char resp[768];
    int total = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(8000);
    while (xTaskGetTickCount() < deadline && total < (int)sizeof(resp) - 1) {
        int got = uart_read_bytes(LTE_UART_NUM, (uint8_t *)(resp + total),
                                   sizeof(resp) - total - 1, pdMS_TO_TICKS(50));
        if (got > 0) {
            total += got;
            resp[total] = '\0';
            if (strstr(resp, "OK") || strstr(resp, "ERROR")) break;
        }
    }
    
    mqtt_service_push_urc(resp);

    if (total <= 0 || !strstr(resp, "OK")) {
        LOG_E(TAG, "Data-mode FAIL:\n%s", resp);
        lte_at_send_raw((const uint8_t *)"\x1A", 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        lte_at_flush();
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); /* [OPT] 200->50ms: modem da tra OK, cho ngan hon */
    return true;
}

/* Cho URC async (cho CMQTTCONNECT) */
static esp_err_t prv_at_wait_urc(const char *cmd,
                                   const char *urc_expect,
                                   uint32_t    timeout_ms)
{
    if (!lte_at_lock(15000)) {
        LOG_E(TAG, "UART lock timeout for URC");
        return ESP_ERR_TIMEOUT;
    }

    lte_at_flush();

    char full[256];
    int n = snprintf(full, sizeof(full), "%s\r\n", cmd);
    uart_write_bytes(LTE_UART_NUM, full, n);
    LOG_I(TAG, ">>> %s", cmd);

    memset(s_buf, 0, sizeof(s_buf));
    int total = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        int got = uart_read_bytes(LTE_UART_NUM,
                                   (uint8_t *)(s_buf + total),
                                   sizeof(s_buf) - total - 1,
                                   pdMS_TO_TICKS(100));
        if (got > 0) {
            total += got;
            s_buf[total] = '\0';

            if (urc_expect && strstr(s_buf, urc_expect)) {
                LOG_I(TAG, "  URC found: %s", urc_expect);
                lte_at_unlock();
                return ESP_OK;
            }
            if (strstr(s_buf, "\r\nERROR\r\n") ||
                strstr(s_buf, "+CME ERROR")) {
                LOG_E(TAG, "  ERROR in URC wait");
                lte_at_unlock();
                return ESP_FAIL;
            }
        }
    }
    LOG_W(TAG, "  URC timeout (%lu ms)", (unsigned long)timeout_ms);
    lte_at_unlock();
    return ESP_ERR_TIMEOUT;
}

/* --- Connect --- */
esp_err_t mqtt_service_connect(void)
{
    LOG_I(TAG, "Connecting >> %s:%d (TLS)", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    s_state = MQTT_ST_CONNECTING;
    char cmd[256];

    /* Cleanup session cu */
    LOG_I(TAG, "Step 1: Cleanup...");
    lte_at_flush();
    vTaskDelay(pdMS_TO_TICKS(500));
    lte_at_send("AT+CMQTTDISC=0,60", NULL, NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(300));
    lte_at_send("AT+CMQTTREL=0", NULL, NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(300));
    lte_at_send("AT+CMQTTSTOP", NULL, NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    lte_at_flush();

    /* Start MQTT engine */
    LOG_I(TAG, "Step 2: CMQTTSTART...");
    if (lte_at_send("AT+CMQTTSTART", "OK", s_buf, sizeof(s_buf), 10000) != ESP_OK) {
        if (strstr(s_buf, "+CMQTTSTART:")) {
            LOG_W(TAG, "  Engine already started");
        } else {
            LOG_E(TAG, "  CMQTTSTART FAIL");
            s_state = MQTT_ST_ERROR;
            return ESP_FAIL;
        }
    }
    LOG_I(TAG, "  [OK] Engine started");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Acquire client (TLS) */
    LOG_I(TAG, "Step 3: CMQTTACCQ...");
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",1", MQTT_CLIENT_ID);
    if (lte_at_send(cmd, "OK", s_buf, sizeof(s_buf), 5000) != ESP_OK) {
        LOG_E(TAG, "  CMQTTACCQ FAIL");
        s_state = MQTT_ST_ERROR;
        return ESP_FAIL;
    }
    LOG_I(TAG, "  [OK] Client acquired");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* SSL config */
    LOG_I(TAG, "Step 4: SSL config...");
    lte_at_send("AT+CSSLCFG=\"sslversion\",0,4", "OK", NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(200));
    lte_at_send("AT+CSSLCFG=\"authmode\",0,0", "OK", NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(200));
    lte_at_send("AT+CSSLCFG=\"enableSNI\",0,1", "OK", NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(200));
    lte_at_send("AT+CMQTTSSLCFG=0,0", "OK", NULL, 0, 3000);
    LOG_I(TAG, "  [OK] SSL linked");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Connect: thu hostname truoc, IP fallback sau */
    LOG_I(TAG, "Step 5: CMQTTCONNECT...");

    snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1",
             MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_KEEPALIVE_SEC);
    LOG_I(TAG, "  [A] Trying hostname: %s", MQTT_BROKER_HOST);
    esp_err_t r = prv_at_wait_urc(cmd, "+CMQTTCONNECT: 0,0", 30000);

    if (r != ESP_OK) {
        const char *broker_ip = lte_get_broker_ip();
        if (broker_ip[0] != '\0') {
            LOG_W(TAG, "  [A] Failed, trying IP: %s", broker_ip);
            lte_at_send("AT+CMQTTDISC=0,60", NULL, NULL, 0, 3000);
            vTaskDelay(pdMS_TO_TICKS(500));
            lte_at_send("AT+CMQTTREL=0", NULL, NULL, 0, 3000);
            vTaskDelay(pdMS_TO_TICKS(500));
            snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",1", MQTT_CLIENT_ID);
            lte_at_send(cmd, "OK", NULL, 0, 5000);
            vTaskDelay(pdMS_TO_TICKS(500));
            lte_at_send("AT+CSSLCFG=\"sslversion\",0,4", "OK", NULL, 0, 3000);
            lte_at_send("AT+CSSLCFG=\"authmode\",0,0", "OK", NULL, 0, 3000);
            lte_at_send("AT+CSSLCFG=\"enableSNI\",0,1", "OK", NULL, 0, 3000);
            snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"sni\",0,\"%s\"", MQTT_BROKER_HOST);
            lte_at_send(cmd, "OK", NULL, 0, 3000);
            lte_at_send("AT+CMQTTSSLCFG=0,0", "OK", NULL, 0, 3000);
            vTaskDelay(pdMS_TO_TICKS(500));
            snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1",
                     broker_ip, MQTT_BROKER_PORT, MQTT_KEEPALIVE_SEC);
            LOG_I(TAG, "  [B] Connecting via IP...");
            r = prv_at_wait_urc(cmd, "+CMQTTCONNECT: 0,0", 30000);
        }
    }

    if (r != ESP_OK) {
        char *urc = strstr(s_buf, "+CMQTTCONNECT: 0,");
        int err_code = urc ? atoi(urc + 17) : -1;
        LOG_E(TAG, "CMQTTCONNECT FAIL (err=%d)", err_code);
        s_state = MQTT_ST_ERROR;
        return ESP_FAIL;
    }

    s_state = MQTT_ST_CONNECTED;
    LOG_I(TAG, "MQTT CONNECTED [OK] %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);

    mqtt_service_publish(MQTT_TOPIC_STATUS,
        "{\"status\":\"online\",\"device\":\"esp32_train\"}", 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}

/* --- Publish --- */
esp_err_t mqtt_service_publish(const char *topic, const char *payload, uint8_t qos)
{
    if (s_state != MQTT_ST_CONNECTED) {
        LOG_W(TAG, "Not connected - skip publish");
        return ESP_FAIL;
    }
    if (!topic || !payload) return ESP_ERR_INVALID_ARG;

    if (!lte_at_lock(20000)) {
        LOG_E(TAG, "Publish: UART lock timeout");
        return ESP_FAIL;
    }

    /* Drain moi URC con sot lai tu pub truoc (+CMQTTPUB, etc) */
    lte_at_flush();
    vTaskDelay(pdMS_TO_TICKS(20)); /* [OPT] 100->20ms: flush nhanh hon, khong can doi lau */
    {
        char drain[256];
        int n = lte_at_read(drain, sizeof(drain), 50);
        if (n > 0) {
            lte_at_flush();
        }
    }

    char cmd[128];
    int tlen = (int)strlen(topic);
    int plen = (int)strlen(payload);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", tlen);
    if (!prv_send_data(cmd, topic, tlen)) {
        LOG_E(TAG, "PUB topic FAIL");
        lte_at_unlock();
        return ESP_FAIL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", plen);
    if (!prv_send_data(cmd, payload, plen)) {
        LOG_E(TAG, "PUB payload FAIL");
        lte_at_unlock();
        return ESP_FAIL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,%d,60", qos);
    esp_err_t r = lte_at_send_locked(cmd, "OK", s_buf, sizeof(s_buf), 10000);
    if (r == ESP_OK) {
        bool pub_done = false;
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(15000);
        while (xTaskGetTickCount() < deadline) {
            char buf[512];
            int got = lte_at_read(buf, sizeof(buf), 50); /* [OPT] 200->50ms: poll nhanh hon de phat hien PUBACK som */
            if (got > 0) {
                if (strstr(buf, "+CMQTTPUB: 0,")) pub_done = true;
                mqtt_service_push_urc(buf);
                if (pub_done) break;
            }
        }
        
        if (pub_done) {
            LOG_I(TAG, "PUB OK >> [%s]", topic);
        } else {
            LOG_E(TAG, "PUB TIMEOUT (No URC) >> [%s]", topic);
            s_state = MQTT_ST_ERROR;
            r = ESP_FAIL;
        }
        
        /* Neu s_buf chua URC (tu lte_at_send_locked) */
        mqtt_service_push_urc(s_buf);
    } else {
        LOG_E(TAG, "PUB FAIL >> [%s]", topic);
        s_state = MQTT_ST_ERROR;
    }
    
    lte_at_unlock();
    return r;
}

/* --- Subscribe --- */
esp_err_t mqtt_service_subscribe(const char *topic, uint8_t qos)
{
    if (!topic) return ESP_ERR_INVALID_ARG;
    
    if (!lte_at_lock(15000)) {
        LOG_E(TAG, "Subscribe: UART lock timeout");
        return ESP_FAIL;
    }

    lte_at_flush();
    vTaskDelay(pdMS_TO_TICKS(500));

    char cmd[128];
    int tlen = (int)strlen(topic);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%d,%d", tlen, qos);
    if (!prv_send_data(cmd, topic, tlen)) {
        LOG_E(TAG, "SUB topic FAIL");
        lte_at_unlock();
        return ESP_FAIL;
    }

    esp_err_t r = lte_at_send_locked("AT+CMQTTSUB=0", "OK", NULL, 0, 10000);
    if (r == ESP_OK) LOG_I(TAG, "SUB OK >> [%s]", topic);
    
    lte_at_unlock();
    return r;
}

/* --- Check Incoming (parse URC cua A7680C) ---
 *
 * URC format:
 *   +CMQTTRXSTART: 0,<topic_len>,<payload_len>
 *   <topic_bytes>
 *   +CMQTTRXPAYLOAD: 0,<payload_len>
 *   <payload_bytes>
 *   +CMQTTRXEND: 0
 */
bool mqtt_service_check_incoming(mqtt_rx_msg_t *msg)
{
    if (!msg) return false;

    char buf[1024];
    int n = 0;
    bool locked = false;
    mqtt_urc_item_t item;
    
    prv_urc_queue_init();
    if (s_urc_queue && xQueueReceive(s_urc_queue, &item, 0) == pdTRUE) {
        strncpy(buf, item.buf, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        n = strlen(buf);
    } else {
        if (!lte_at_lock(200)) return false;
        locked = true;
        n = lte_at_read(buf, sizeof(buf), 200);
        if (n <= 0) { lte_at_unlock(); return false; }
    }

    /* Tim header URC */
    char *start = strstr(buf, "+CMQTTRXSTART: 0,");
    if (!start) {
        if (locked) lte_at_unlock();
        return false;
    }

    /* Chua nhan het URC -> doc them cho den khi co RXEND */
    if (!strstr(buf, "+CMQTTRXEND:")) {
        if (!locked) {
            if (!lte_at_lock(500)) return false;
            locked = true;
        }
        int extra = lte_at_read(buf + n, sizeof(buf) - n - 1, 500);
        if (extra > 0) { n += extra; buf[n] = '\0'; }
        if (!strstr(buf, "+CMQTTRXEND:")) {
            LOG_W(TAG, "RX incomplete (no RXEND)");
            if (locked) lte_at_unlock();
            return false;
        }
    }
    
    if (locked) lte_at_unlock();

    /* Parse topic_len, payload_len tu RXSTART */
    int tlen = 0, plen = 0;
    const char *rxstart_prefix = "+CMQTTRXSTART: 0,";
    char *p = start + strlen(rxstart_prefix);
    tlen = atoi(p);
    char *comma = strchr(p, ',');
    if (comma) plen = atoi(comma + 1);

    if (tlen <= 0 || plen <= 0 ||
        tlen >= (int)sizeof(msg->topic) ||
        plen >= (int)sizeof(msg->payload)) {
        LOG_W(TAG, "RX bad lengths: t=%d p=%d", tlen, plen);
        return false;
    }

    char *buf_end = buf + n;
    char *pay_hdr = strstr(start, "+CMQTTRXPAYLOAD:");
    if (!pay_hdr) return false;

    /* A7680C thuong dat topic bytes ngay sau RXSTART. Mot so firmware co
     * them +CMQTTRXTOPIC, nen ho tro ca hai dang. */
    char *topic_hdr = strstr(start, "+CMQTTRXTOPIC:");
    char *topic_start = NULL;
    if (topic_hdr && topic_hdr < pay_hdr) {
        topic_start = strchr(topic_hdr, '\n');
        if (!topic_start) return false;
        topic_start++;
    } else {
        topic_start = strchr(start, '\n');
        if (!topic_start) return false;
        topic_start++;
    }
    while (*topic_start == '\r' || *topic_start == '\n') topic_start++;
    if (topic_start + tlen > buf_end) return false;

    memcpy(msg->topic, topic_start, tlen);
    msg->topic[tlen] = '\0';
    msg->topic_len = tlen;

    char *pay_start = strchr(pay_hdr, '\n');
    if (!pay_start) return false;
    pay_start++;
    while (*pay_start == '\r' || *pay_start == '\n') pay_start++;
    if (pay_start + plen > buf_end) return false;

    memcpy(msg->payload, pay_start, plen);
    msg->payload[plen] = '\0';
    msg->payload_len = plen;

    LOG_I(TAG, "[RX] topic=%s (%d) payload=%s (%d)",
          msg->topic, tlen, msg->payload, plen);
    return true;
}

/* --- Disconnect --- */
void mqtt_service_disconnect(void)
{
    lte_at_send("AT+CMQTTDISC=0,60", NULL, NULL, 0, 5000);
    vTaskDelay(pdMS_TO_TICKS(500));
    lte_at_send("AT+CMQTTREL=0", NULL, NULL, 0, 3000);
    vTaskDelay(pdMS_TO_TICKS(300));
    lte_at_send("AT+CMQTTSTOP", NULL, NULL, 0, 3000);
    s_state = MQTT_ST_DISCONNECTED;
    LOG_I(TAG, "MQTT disconnected");
}

mqtt_state_t mqtt_service_get_state(void) { return s_state; }
