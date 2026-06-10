#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>
#include "sys_config.h"

typedef enum {
    MQTT_ST_DISCONNECTED = 0,
    MQTT_ST_CONNECTING,
    MQTT_ST_CONNECTED,
    MQTT_ST_ERROR,
} mqtt_state_t;

/* Ket qua parse message nhan duoc */
typedef struct {
    char topic[128];
    char payload[256];
    int  topic_len;
    int  payload_len;
} mqtt_rx_msg_t;

esp_err_t    mqtt_service_connect(void);
esp_err_t    mqtt_service_publish(const char *topic,
                                   const char *payload,
                                   uint8_t     qos);
esp_err_t    mqtt_service_subscribe(const char *topic, uint8_t qos);

/**
 * Kiem tra va parse message den (URC +CMQTTRXSTART).
 * Tra ve true neu co message, ket qua luu vao msg.
 */
bool         mqtt_service_check_incoming(mqtt_rx_msg_t *msg);

/* Intercept URC from any AT command */
void         mqtt_service_push_urc(const char *urc_text);

void         mqtt_service_disconnect(void);
mqtt_state_t mqtt_service_get_state(void);

#endif /* MQTT_SERVICE_H */