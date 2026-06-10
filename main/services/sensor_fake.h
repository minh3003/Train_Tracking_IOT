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
    int8_t   temperature_c;

    /* System */
    uint32_t timestamp_ms;
    uint32_t seq;
} sensor_data_t;

void sensor_fake_read(sensor_data_t *out);
void sensor_build_json(const sensor_data_t *data,
                        char *out_buf, size_t buf_len);

#endif /* SENSOR_FAKE_H */