#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>

typedef enum {
    PIPE_INIT = 0,
    PIPE_LTE_CONNECTING,
    PIPE_MQTT_CONNECTING,
    PIPE_ONLINE,
    PIPE_OFFLINE,
    PIPE_RECONNECTING,
} pipe_state_t;

void pipeline_init(bool sd_ok);
void pipeline_sensor_task(void *arg);   /* FreeRTOS task */
void pipeline_transmit_task(void *arg); /* FreeRTOS task */
void pipeline_watchdog_task(void *arg); /* FreeRTOS task */
pipe_state_t pipeline_get_state(void);

#endif /* PIPELINE_H */