#include "sensor_fake.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <stdio.h>
#include <math.h>

/* Hà Nội tọa độ gốc — giả lập tàu di chuyển */
#define BASE_LAT    21.027763
#define BASE_LON    105.834160

static uint32_t s_seq = 0;

void sensor_fake_read(sensor_data_t *out)
{
    if (!out) return;
    s_seq++;

    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000ULL);

    /* GPS fake — tàu di chuyển theo hướng Bắc */
    float progress = (float)(s_seq % 200) / 200.0f;
    out->latitude   = BASE_LAT  + (progress * 0.05);
    out->longitude  = BASE_LON  + (progress * 0.02);
    out->speed_kmh  = 40.0f + (float)(esp_random() % 20);
    out->satellites = 8 + (esp_random() % 4);
    out->gps_valid  = true;

    /* MPU6050 fake — rung nhẹ khi tàu chạy */
    out->accel_x     = (int16_t)(esp_random() % 500 - 250);
    out->accel_y     = (int16_t)(esp_random() % 300 - 150);
    out->accel_z     = 16200 + (int16_t)(esp_random() % 400 - 200);
    out->gyro_x      = (int16_t)(esp_random() % 100 - 50);
    out->temperature_c = 28 + (int8_t)(esp_random() % 5);

    out->timestamp_ms = ts;
    out->seq          = s_seq;
}

void sensor_build_json(const sensor_data_t *d,
                        char *buf, size_t len)
{
    snprintf(buf, len,
             "{"
             "\"seq\":%lu,"
             "\"ts\":%lu,"
             "\"lat\":%.6f,"
             "\"lon\":%.6f,"
             "\"speed\":%.1f,"
             "\"sats\":%d,"
             "\"ax\":%d,"
             "\"ay\":%d,"
             "\"az\":%d,"
             "\"gx\":%d,"
             "\"temp\":%d,"
             "\"gps_ok\":%s"
             "}",
             (unsigned long)d->seq,
             (unsigned long)d->timestamp_ms,
             d->latitude,
             d->longitude,
             d->speed_kmh,
             d->satellites,
             d->accel_x,
             d->accel_y,
             d->accel_z,
             d->gyro_x,
             d->temperature_c,
             d->gps_valid ? "true" : "false");
}