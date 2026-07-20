#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

/* A7680C LTE MODULE - UART1
 * ESP32 TX(26) -> SIM RX
 * ESP32 RX(27) <- SIM TX */
#define LTE_UART_NUM        UART_NUM_1
#define LTE_TX_PIN          26
#define LTE_RX_PIN          27
#define LTE_UART_BAUD       115200
#define LTE_RESET_PIN       4
#define LTE_RX_BUF_SIZE     4096
#define LTE_TX_BUF_SIZE     1024

/* MICROSD - SPI (VSPI) */
#define SD_PIN_CS           5
#define SD_PIN_SCK          18
#define SD_PIN_MOSI         23
#define SD_PIN_MISO         19
#define SD_SPI_FREQ_KHZ     4000

/* GPS NEO-7M - UART2 */
#define GPS_UART_NUM        UART_NUM_2
#define GPS_TX_PIN          17
#define GPS_RX_PIN          16
#define GPS_UART_BAUD       9600

/* MPU6050 - I2C */
#define MPU_SDA_PIN         32
#define MPU_SCL_PIN         33
#define MPU_I2C_ADDR        0x68

/* LED onboard (GPIO4 used for LTE_RESET) */
#define LED_PIN             2

#endif /* PIN_CONFIG_H */