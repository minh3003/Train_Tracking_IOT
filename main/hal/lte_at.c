/**
 * lte_at.c - A7680C LTE HAL (UART + AT command engine)
 *
 * Thread-safe: moi ham gui AT command deu bao ve bang mutex.
 * Init sequence: alive -> SIM -> signal -> network -> PDP -> NETOPEN -> DNS
 */

#include "lte_at.h"
#include "logger.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "mqtt_service.h"

#define TAG "LTE_AT"

static char s_resp[2048];
static char s_broker_ip[64] = {0};
static lte_time_t s_lte_time = {0};
static SemaphoreHandle_t s_uart_mutex = NULL;

const char *lte_get_broker_ip(void) { return s_broker_ip; }

static bool prv_is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int prv_days_before_month(int year, int month)
{
    static const int days_norm[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    if (month < 1 || month > 12) return -1;
    int days = days_norm[month - 1];
    if (month > 2 && prv_is_leap_year(year)) days++;
    return days;
}

int lte_time_days_since_2000(const lte_time_t *t)
{
    if (!t || !t->valid || t->year < 2000 ||
        t->month < 1 || t->month > 12 ||
        t->day < 1 || t->day > 31) {
        return -1;
    }

    int days = 0;
    for (int y = 2000; y < t->year; y++) {
        days += prv_is_leap_year(y) ? 366 : 365;
    }

    int month_days = prv_days_before_month(t->year, t->month);
    if (month_days < 0) return -1;
    days += month_days;
    days += t->day - 1;
    return days;
}

static bool prv_parse_cclk(const char *resp, lte_time_t *out)
{
    if (!resp || !out) return false;

    const char *q = strchr(resp, '"');
    if (!q) return false;
    q++;

    int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0, tz = 0;
    char sign = '+';
    int n = sscanf(q, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d",
                   &yy, &mo, &dd, &hh, &mm, &ss, &sign, &tz);
    if (n < 6) return false;

    int year = 2000 + yy;
    if (year < 2024 || mo < 1 || mo > 12 || dd < 1 || dd > 31 ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
        return false;
    }

    out->year = year;
    out->month = mo;
    out->day = dd;
    out->hour = hh;
    out->min = mm;
    out->sec = ss;
    out->tz_quarters = (sign == '-') ? -tz : tz;
    out->valid = true;
    return true;
}

bool lte_time_get(lte_time_t *out)
{
    if (!out || !s_lte_time.valid) return false;
    *out = s_lte_time;
    return true;
}

bool lte_time_is_valid(void)
{
    return s_lte_time.valid;
}

bool lte_time_sync(void)
{
    char resp[128];

    (void)lte_at_send("AT+CTZU=1", "OK", NULL, 0, AT_TIMEOUT_SHORT);
    vTaskDelay(pdMS_TO_TICKS(200));
    (void)lte_at_send("AT+CTZR=1", "OK", NULL, 0, AT_TIMEOUT_SHORT);
    vTaskDelay(pdMS_TO_TICKS(200));

    if (lte_at_send("AT+CCLK?", "+CCLK:", resp, sizeof(resp), 5000) != ESP_OK) {
        LOG_W(TAG, "LTE time unavailable");
        return false;
    }

    lte_time_t t = {0};
    if (!prv_parse_cclk(resp, &t)) {
        LOG_W(TAG, "LTE time invalid: %s", resp);
        return false;
    }

    s_lte_time = t;
    LOG_I(TAG, "LTE time synced: %04d-%02d-%02d %02d:%02d:%02d tz=%d",
          t.year, t.month, t.day, t.hour, t.min, t.sec, t.tz_quarters);
    return true;
}

/* Doc het bytes con thua trong UART RX (boot messages, URC cu) */
static void prv_drain_uart(void)
{
    uint8_t tmp[256];
    int total = 0;
    int max_iters = 200; /* [FIX] Chống treo vô hạn do nhiễu RX (floating pin) */
    while (max_iters-- > 0) {
        int n = uart_read_bytes(LTE_UART_NUM, tmp, sizeof(tmp),
                                pdMS_TO_TICKS(100));
        if (n <= 0) break;
        total += n;
    }
    if (total > 0) {
        LOG_I(TAG, "Drained %d stale bytes", total);
    }
}

/* Check response da co ket thuc chua (OK, ERROR, prompt)
 * Chi nhan dien OK/ERROR khi co \r\n phia truoc de tranh
 * false positive khi payload MQTT chua chuoi "OK\r\n". */
static bool prv_resp_done(const char *buf)
{
    if (strstr(buf, "\r\nOK\r\n"))    return true;
    if (strstr(buf, "\r\nERROR\r\n")) return true;
    if (strstr(buf, "+CME ERROR"))    return true;
    /* [M2-FIX-CORRECTED] A7680C tra ve "\r\n>" ma KHONG co khoang trang.
     * Sua lai de khong bi timeout oan 5s. */
    if (strstr(buf, "\r\n>"))    return true;
    if (buf[0] == '>')           return true;
    return false;
}

/* Log response, thay ky tu khong doc duoc bang '.' */
static void prv_log_resp(const char *resp)
{
    char buf[256];
    strncpy(buf, resp, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf; *p; p++) {
        if (*p == '\r' || *p == '\n')
            *p = '|';
        else if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E)
            *p = '.';
    }
    LOG_I(TAG, "<<< %s", buf);
}

/* --- UART Init --- */
esp_err_t lte_at_init(void)
{
    if (s_uart_mutex == NULL) {
        s_uart_mutex = xSemaphoreCreateMutex();
        if (!s_uart_mutex) {
            LOG_E(TAG, "Mutex create FAILED");
            return ESP_ERR_NO_MEM;
        }
    }

    /* Init RST pin cho hardware reset A7680C */
    gpio_reset_pin(LTE_RESET_PIN);
    gpio_set_direction(LTE_RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LTE_RESET_PIN, 1);  /* HIGH = normal operation */

    uart_config_t cfg = {
        .baud_rate  = LTE_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t r;
    r = uart_param_config(LTE_UART_NUM, &cfg);
    if (r != ESP_OK) { LOG_E(TAG, "param_config: %s", esp_err_to_name(r)); return r; }

    r = uart_set_pin(LTE_UART_NUM,
                     LTE_TX_PIN, LTE_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (r != ESP_OK) { LOG_E(TAG, "set_pin: %s", esp_err_to_name(r)); return r; }

    r = uart_driver_install(LTE_UART_NUM,
                             LTE_RX_BUF_SIZE, LTE_TX_BUF_SIZE,
                             0, NULL, 0);
    if (r != ESP_OK) { LOG_E(TAG, "driver_install: %s", esp_err_to_name(r)); return r; }

    LOG_I(TAG, "UART%d OK | TX=GPIO%d RX=GPIO%d Baud=%d RST=GPIO%d",
          LTE_UART_NUM, LTE_TX_PIN, LTE_RX_PIN, LTE_UART_BAUD, LTE_RESET_PIN);
    return ESP_OK;
}

/* --- Core: doc response voi timeout chinh xac --- */
static esp_err_t prv_read_response(const char *cmd,
                                    const char *expect,
                                    char       *resp_buf,
                                    size_t      resp_len,
                                    uint32_t    timeout_ms)
{
    memset(s_resp, 0, sizeof(s_resp));
    int total = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        int space_left = sizeof(s_resp) - total - 1;
        if (space_left <= 0) {
            LOG_E(TAG, "AT response buffer FULL (Noise detected)");
            break;
        }

        int got = uart_read_bytes(LTE_UART_NUM,
                                   (uint8_t *)(s_resp + total),
                                   space_left,
                                   pdMS_TO_TICKS(50));
        if (got > 0) {
            total += got;
            s_resp[total] = '\0';
            if (prv_resp_done(s_resp)) break;
        }
    }

    esp_err_t result;

    if (total == 0) {
        LOG_W(TAG, "NO RESPONSE: '%s' (%lu ms)", cmd, (unsigned long)timeout_ms);
        result = ESP_ERR_TIMEOUT;
    } else {
        prv_log_resp(s_resp);
        mqtt_service_push_urc(s_resp);

        if (resp_buf && resp_len > 0) {
            strncpy(resp_buf, s_resp, resp_len - 1);
            resp_buf[resp_len - 1] = '\0';
        }

        if (!expect) {
            result = ESP_OK;
        } else {
            result = strstr(s_resp, expect) ? ESP_OK : ESP_FAIL;
        }
    }
    return result;
}

/* --- Send AT Command (thread-safe, tu lay mutex) --- */
esp_err_t lte_at_send(const char *cmd,
                       const char *expect,
                       char       *resp_buf,
                       size_t      resp_len,
                       uint32_t    timeout_ms)
{
    if (s_uart_mutex &&
        xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        LOG_E(TAG, "UART mutex timeout - skip: %s", cmd);
        return ESP_ERR_TIMEOUT;
    }

    lte_at_flush();

    char full[256];
    int n = snprintf(full, sizeof(full), "%s\r\n", cmd);
    uart_write_bytes(LTE_UART_NUM, full, n);
    LOG_I(TAG, ">>> %s", cmd);

    esp_err_t result = prv_read_response(cmd, expect, resp_buf, resp_len,
                                          timeout_ms);

    if (s_uart_mutex) xSemaphoreGive(s_uart_mutex);
    return result;
}

/* --- Send AT Command (caller da giu mutex) --- */
esp_err_t lte_at_send_locked(const char *cmd,
                             const char *expect,
                             char       *resp_buf,
                             size_t      resp_len,
                             uint32_t    timeout_ms)
{
    lte_at_flush();

    char full[256];
    int n = snprintf(full, sizeof(full), "%s\r\n", cmd);
    uart_write_bytes(LTE_UART_NUM, full, n);
    LOG_I(TAG, ">>> %s", cmd);

    return prv_read_response(cmd, expect, resp_buf, resp_len, timeout_ms);
}

/* --- External UART mutex control --- */
bool lte_at_lock(uint32_t timeout_ms)
{
    if (!s_uart_mutex) return false;
    return xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lte_at_unlock(void)
{
    if (s_uart_mutex) xSemaphoreGive(s_uart_mutex);
}

/* --- Read UART (non-blocking voi timeout) --- */
int lte_at_read(char *buf, size_t len, uint32_t timeout_ms)
{
    if (!buf || len == 0) return 0;
    memset(buf, 0, len);
    int total = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline && total < (int)(len - 1)) {
        int got = uart_read_bytes(LTE_UART_NUM,
                                   (uint8_t *)(buf + total),
                                   len - total - 1,
                                   pdMS_TO_TICKS(20));
        if (got > 0) { total += got; buf[total] = '\0'; }
    }
    return total;
}

void lte_at_send_raw(const uint8_t *data, size_t len)
{
    uart_write_bytes(LTE_UART_NUM, (const char *)data, len);
}

void lte_at_flush(void)
{
    char tmp[257];
    int max_iters = 200; /* [FIX] Chống treo vô hạn do nhiễu RX (floating pin) */
    while (max_iters-- > 0) {
        int n = uart_read_bytes(LTE_UART_NUM, (uint8_t *)tmp,
                                sizeof(tmp) - 1, pdMS_TO_TICKS(20));
        if (n <= 0) break;
        tmp[n] = '\0';
        mqtt_service_push_urc(tmp);
    }
}

/* --- Hardware Reset: toggle RST pin de ep module khoi dong lai --- */
void lte_hard_reset(void)
{
    LOG_W(TAG, "=== HARD RESET A7680C ===");
    gpio_set_level(LTE_RESET_PIN, 0);     /* Pull LOW -> reset */
    vTaskDelay(pdMS_TO_TICKS(500));        /* Giu 500ms */
    gpio_set_level(LTE_RESET_PIN, 1);     /* Release */
    LOG_I(TAG, "RST released, waiting boot...");
    vTaskDelay(pdMS_TO_TICKS(5000));       /* Cho module boot */
    prv_drain_uart();                      /* Xoa boot messages */
    LOG_I(TAG, "HARD RESET complete");
}

/* --- Full LTE Init Sequence --- */
bool lte_full_init(void)
{
    char resp[256];

    LOG_I(TAG, "==================================");
    LOG_I(TAG, "   A7680C LTE INIT SEQUENCE");
    LOG_I(TAG, "==================================");

    /* [1] Drain garbage + check alive */
    LOG_I(TAG, "[1/8] Module alive...");
    prv_drain_uart();

    bool alive = false;
    for (int i = 0; i < 10; i++) {
        if (lte_at_send("AT", "OK", NULL, 0, AT_TIMEOUT_SHORT) == ESP_OK) {
            alive = true;
            break;
        }
        LOG_W(TAG, "  retry %d/10", i + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        prv_drain_uart();
    }
    if (!alive) {
        LOG_E(TAG, "MODULE DEAD!");
        return false;
    }
    LOG_I(TAG, "  [OK] ALIVE");

    /* Tat echo, verify thanh cong */
    if (lte_at_send("ATE0", "OK", NULL, 0, AT_TIMEOUT_SHORT) != ESP_OK) {
        LOG_W(TAG, "ATE0 retry...");
        vTaskDelay(pdMS_TO_TICKS(500));
        lte_at_send("ATE0", "OK", NULL, 0, AT_TIMEOUT_SHORT);
    }
    prv_drain_uart();
    vTaskDelay(pdMS_TO_TICKS(300));

    /* Bat NITZ auto-update ngay tu dau de don ban tin tu nha mang */
    (void)lte_at_send("AT+CTZU=1", "OK", NULL, 0, AT_TIMEOUT_SHORT);
    (void)lte_at_send("AT+CTZR=1", "OK", NULL, 0, AT_TIMEOUT_SHORT);

    /* [2] SIM */
    LOG_I(TAG, "[2/8] SIM card...");
    if (lte_at_send("AT+CPIN?", "READY", resp, sizeof(resp), 5000) != ESP_OK) {
        LOG_E(TAG, "SIM FAIL: %s", resp);
        return false;
    }
    LOG_I(TAG, "  [OK] SIM READY");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* [3] Signal */
    LOG_I(TAG, "[3/8] Signal...");
    lte_at_send("AT+CSQ", "+CSQ:", resp, sizeof(resp), AT_TIMEOUT_SHORT);
    int rssi = 99;
    char *p = strstr(resp, "+CSQ:");
    if (p) rssi = atoi(p + 5);
    if (rssi == 99 || rssi == 0) {
        LOG_E(TAG, "NO SIGNAL");
        return false;
    }
    LOG_I(TAG, "  [OK] RSSI=%d/31 (%s)", rssi,
          rssi > 20 ? "Excellent" : rssi > 10 ? "Good" : "Weak");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* [4] Network registration (max 60s) */
    LOG_I(TAG, "[4/8] Network (max 60s)...");
    bool registered = false;
    for (int i = 0; i < 60 && !registered; i++) {
        lte_at_send("AT+CREG?", "+CREG:", resp, sizeof(resp), 3000);
        p = strstr(resp, "+CREG:");
        if (p) {
            int n = 0, stat = 0;
            sscanf(p, "+CREG: %d,%d", &n, &stat);
            if (stat == 1 || stat == 5) {
                registered = true;
                LOG_I(TAG, "  [OK] REGISTERED (stat=%d, %s)",
                      stat, stat == 5 ? "roaming" : "home");
            } else {
                LOG_I(TAG, "  waiting %ds (stat=%d)...", i + 1, stat);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    if (!registered) {
        LOG_E(TAG, "NETWORK FAIL - check SIM/coverage");
        return false;
    }

    /* [4.5] Network time (NITZ) - Lay gio ngay khi co song, khong cho Internet */
    LOG_I(TAG, "[4.5/8] Network time...");
    if (!lte_time_sync()) {
        LOG_W(TAG, "  LTE time not synced yet, will retry in background");
    }

    /* [5] PDP context (internet) */
    LOG_I(TAG, "[5/8] Internet (APN: %s)...", SIM_APN);
    char apn_cmd[64];
    snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", SIM_APN);
    lte_at_send(apn_cmd, "OK", NULL, 0, 5000);
    vTaskDelay(pdMS_TO_TICKS(500));

    lte_at_send("AT+CGACT=1,1", "OK", NULL, 0, 20000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Verify IP */
    lte_at_send("AT+CGPADDR=1", "+CGPADDR:", resp, sizeof(resp), 5000);
    char ip[32] = {0};
    char *qs = strstr(resp, "\"");
    char *qe = qs ? strchr(qs + 1, '"') : NULL;
    if (qs && qe && qe > qs + 1) {
        size_t l = qe - qs - 1;
        if (l < sizeof(ip) - 1) {
            strncpy(ip, qs + 1, l);
            ip[l] = '\0';
        }
    }
    if (strlen(ip) < 4) {
        p = strstr(resp, "+CGPADDR:");
        if (p) {
            char *comma = strchr(p, ',');
            if (comma) {
                strncpy(ip, comma + 1, sizeof(ip) - 1);
                for (char *c = ip; *c; c++) {
                    if (*c == '\r' || *c == '\n' || *c == '"') *c = '\0';
                }
            }
        }
    }
    if (strlen(ip) < 4) {
        LOG_E(TAG, "NO IP - check APN: %s", SIM_APN);
        return false;
    }
    LOG_I(TAG, "  [OK] IP = %s", ip);

    /* [6] Open TCP/IP stack for MQTT */
    LOG_I(TAG, "[6/8] Opening TCP/IP stack...");
    lte_at_send("AT+NETCLOSE", NULL, NULL, 0, 8000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_err_t net_r = lte_at_send("AT+NETOPEN", "OK", resp, sizeof(resp), 15000);
    if (net_r != ESP_OK) {
        if (strstr(resp, "already opened")) {
            LOG_I(TAG, "  [OK] NETOPEN already active");
        } else {
            LOG_E(TAG, "  NETOPEN FAIL: %s", resp);
            return false;
        }
    } else {
        LOG_I(TAG, "  [OK] TCP stack ready");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* [7] DNS Resolution */
    LOG_I(TAG, "[7/8] DNS resolution...");
    char dns_cmd[128];
    snprintf(dns_cmd, sizeof(dns_cmd), "AT+CDNSGIP=\"%s\"", MQTT_BROKER_HOST);

    bool dns_ok = false;

    /* Try carrier DNS first */
    LOG_I(TAG, "  [A] Carrier DNS...");
    esp_err_t dns_r = lte_at_send(dns_cmd, "+CDNSGIP: 1", resp, sizeof(resp), 10000);
    if (dns_r == ESP_OK) {
        dns_ok = true;
        LOG_I(TAG, "  [OK] Carrier DNS");
    }

    /* Fallback: Google DNS */
    if (!dns_ok) {
        LOG_I(TAG, "  [B] Google DNS...");
        lte_at_send("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"", "OK", NULL, 0, 5000);
        vTaskDelay(pdMS_TO_TICKS(1000));
        dns_r = lte_at_send(dns_cmd, "+CDNSGIP: 1", resp, sizeof(resp), 10000);
        if (dns_r == ESP_OK) {
            dns_ok = true;
            LOG_I(TAG, "  [OK] Google DNS");
        }
    }

    /* Parse DNS result or use fallback IP */
    if (dns_ok) {
        char *q1 = strstr(resp, ",\"");
        if (q1) q1 = strstr(q1 + 2, ",\"");
        if (q1) {
            q1 += 2;
            char *q2 = strchr(q1, '"');
            if (q2 && (q2 - q1) < (int)sizeof(s_broker_ip) - 1) {
                strncpy(s_broker_ip, q1, q2 - q1);
                s_broker_ip[q2 - q1] = '\0';
            }
        }
        LOG_I(TAG, "  Resolved: %s", s_broker_ip);
    } else {
        LOG_W(TAG, "  DNS failed, using fallback IP: %s", MQTT_BROKER_IP);
        strncpy(s_broker_ip, MQTT_BROKER_IP, sizeof(s_broker_ip) - 1);
        s_broker_ip[sizeof(s_broker_ip) - 1] = '\0';
    }

    /* Done */
    LOG_I(TAG, "[8/8] Broker IP = %s", s_broker_ip);
    vTaskDelay(pdMS_TO_TICKS(500));

    LOG_I(TAG, "==================================");
    LOG_I(TAG, "  LTE READY [OK]");
    LOG_I(TAG, "==================================");
    return true;
}
