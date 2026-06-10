#ifndef SD_LOG_H
#define SD_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pin_config.h"
#include "sys_config.h"
#include <stdio.h>

esp_err_t sd_log_init(void);
esp_err_t sd_log_reinit(void);    /* Unmount + remount khi SD bi loi */
void      sd_log_deinit(void);
bool      sd_log_is_ready(void);
bool      sd_log_append(const char *filename, const char *line);
bool      sd_log_dump(const char *filename);
long      sd_log_file_size(const char *filename);
bool      sd_log_file_exists(const char *filename);
bool      sd_log_delete(const char *filename);
bool      sd_log_rename(const char *old_filename, const char *new_filename);
int       sd_log_count_lines(const char *filename);

/* Đọc từng dòng từ file offline buffer */
FILE*     sd_log_open_read(const char *filename);
void      sd_log_close_read(FILE *f);

/**
 * Doc N dong tu file, bat dau tu offset byte.
 * Mutex chi giu trong luc doc batch, khong giu lau.
 * Return: so dong doc duoc, -1 neu loi.
 */
int       sd_log_read_lines(const char *fn, long offset,
                            char lines[][PAYLOAD_MAX_LEN], int max_lines);

#endif /* SD_LOG_H */
