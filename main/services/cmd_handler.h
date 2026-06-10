#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * cmd_handler.h - Xu ly lenh tu server (2 chieu)
 *
 * Server gui JSON len topic "command":
 *   {"cmd":"set_interval","value":5000}
 *   {"cmd":"get_status"}
 *   {"cmd":"reboot"}
 *
 * Device phan hoi len topic "response".
 */

/* Khoi tao module (goi 1 lan trong pipeline) */
void cmd_handler_init(void);

/**
 * Xu ly payload nhan duoc tu topic command.
 * Parse JSON, thuc thi lenh, gui ACK len MQTT.
 * @param payload  chuoi JSON tu server
 * @param pay_len  do dai payload
 */
void cmd_handler_process(const char *payload, int pay_len);

/* Getter: tan suat sensor hien tai (ms) - pipeline dung de dieu chinh */
uint32_t cmd_handler_get_sensor_interval(void);

#endif /* CMD_HANDLER_H */
