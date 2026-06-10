#ifndef LTE_AT_H
#define LTE_AT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "pin_config.h"
#include "sys_config.h"

typedef struct {
    int  year;
    int  month;
    int  day;
    int  hour;
    int  min;
    int  sec;
    int  tz_quarters;
    bool valid;
} lte_time_t;

/* Init UART cho A7680C + tao mutex */
esp_err_t lte_at_init(void);

/* Gui AT command, cho response (thread-safe, tu lay mutex) */
esp_err_t lte_at_send(const char *cmd,
                       const char *expect,
                       char       *resp_buf,
                       size_t      resp_len,
                       uint32_t    timeout_ms);

/* Gui AT command (caller da giu mutex bang lte_at_lock) */
esp_err_t lte_at_send_locked(const char *cmd,
                             const char *expect,
                             char       *resp_buf,
                             size_t      resp_len,
                             uint32_t    timeout_ms);

/* Lay/tra mutex UART tu ben ngoai (dung khi can giu lock nhieu lenh) */
bool lte_at_lock(uint32_t timeout_ms);
void lte_at_unlock(void);

/* Doc data tu UART (non-blocking voi timeout) */
int lte_at_read(char *buf, size_t len, uint32_t timeout_ms);

/* Gui raw bytes (cho CMQTTPAYLOAD data-mode) */
void lte_at_send_raw(const uint8_t *data, size_t len);

/* Xoa RX buffer */
void lte_at_flush(void);

/* Hardware reset module qua chan RST/PWRKEY (GPIO) */
void lte_hard_reset(void);

/* Full init: alive -> SIM -> signal -> network -> PDP -> NETOPEN -> DNS */
bool lte_full_init(void);

/* Lay IP broker da resolve trong lte_full_init() */
const char *lte_get_broker_ip(void);

/* Sync/read thoi gian thuc tu module LTE (AT+CCLK?). */
bool lte_time_sync(void);
bool lte_time_get(lte_time_t *out);
bool lte_time_is_valid(void);
int  lte_time_days_since_2000(const lte_time_t *t);

#endif /* LTE_AT_H */
