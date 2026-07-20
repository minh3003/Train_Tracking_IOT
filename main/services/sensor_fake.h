#ifndef SENSOR_FAKE_H
#define SENSOR_FAKE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    /* GPS */
    double   latitude;
    double   longitude;
    float    speed_kmh;
    uint8_t  satellites;
    bool     gps_valid;

    /* MPU6050 */
    int16_t  accel_x;
    int16_t  accel_y;
    int16_t  accel_z;
    int16_t  gyro_x;
    int16_t  temperature_c;   /* [M4-FIX] int8_t -> int16_t, tranh overflow khi > 127 */

    /* System */
    uint32_t timestamp_ms;
    uint32_t seq;
} sensor_data_t;

void sensor_fake_read(sensor_data_t *out);

/**
 * Build JSON payload tu sensor data.
 * @param time_iso  Chuoi thoi gian NITZ ISO-8601 (vd: "2026-06-11T21:55:00"), NULL neu chua sync
 * @param boot_id   Ma phien boot (random), dung de tao unique key: boot_id + seq
 */
void sensor_build_json(const sensor_data_t *data,
                        char *out_buf, size_t buf_len,
                        const char *time_iso, uint16_t boot_id);

#endif /* SENSOR_FAKE_H */