#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/* SIM / APN */
#define SIM_APN             "v-internet"

/* Device Identity (dung cho multi-device va packet tracing) */
#define DEVICE_ID           "Bamboo"

/* MQTT Broker */
#define MQTT_BROKER_HOST    "broker.emqx.io"
#define MQTT_BROKER_PORT    8883
#define MQTT_BROKER_IP      "44.232.241.40"     /* Fallback khi DNS fail */
#define MQTT_CLIENT_ID      "traintrack_" DEVICE_ID
#define MQTT_KEEPALIVE_SEC  60

/* MQTT Topics */
#define MQTT_TOPIC_DATA     "traintrack/" DEVICE_ID "/telemetry"
#define MQTT_TOPIC_STATUS   "traintrack/" DEVICE_ID "/status"
#define MQTT_TOPIC_CMD      "traintrack/" DEVICE_ID "/command"
#define MQTT_TOPIC_RESP     "traintrack/" DEVICE_ID "/response"

/* Timing (ms) */
#define SENSOR_INTERVAL_MS      3000
#define PUBLISH_INTERVAL_MS     5000
#define RECONNECT_DELAY_MS      10000
#define WATCHDOG_INTERVAL_MS    30000
#define LTE_BOOT_WAIT_MS        5000
#define LTE_NETWORK_TIMEOUT_MS  60000

/* SD Card */
#define SD_MOUNT_POINT      "/sdcard"
#define SD_FILE_DATA        "data.log"
#define SD_FILE_OFFLINE     "offline.buf"
#define SD_FILE_PROCESSING  "offline.processing"
#define SD_FULL_THRESHOLD   95

/* Data log history. Offline buffer is never deleted by retention policy. */
#define DATA_LOG_ENABLE             1
#define DATA_LOG_MAX_BYTES          (5 * 1024 * 1024)
#define DATA_LOG_MAX_FILES          100
#define DATA_LOG_RETENTION_DAYS     30
#define DATA_LOG_MAINT_INTERVAL_MS  60000

/* AT Command Timeouts (ms) */
#define AT_TIMEOUT_SHORT    2000
#define AT_TIMEOUT_MEDIUM   15000
#define AT_TIMEOUT_LONG     60000

/* Queue / Buffer */
#define PIPELINE_QUEUE_SIZE     100
#define PAYLOAD_MAX_LEN         512

#endif /* SYS_CONFIG_H */
