#include "sd_log.h"
#include "logger.h"
#include "lte_at.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#define TAG "SD"

static sdmmc_card_t      *s_card    = NULL;
static bool               s_mounted = false;
static SemaphoreHandle_t  s_sd_mutex = NULL;

static void prv_path(char *buf, size_t len, const char *fn)
{
    snprintf(buf, len, "%s/%s", SD_MOUNT_POINT, fn);
}

static uint32_t prv_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool prv_is_data_log(const char *fn)
{
    return fn && strcmp(fn, SD_FILE_DATA) == 0;
}

static bool prv_parse_data_log_date(const char *fn, lte_time_t *out)
{
    if (!fn || !out) return false;

    int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0, idx = 0;
    int n = sscanf(fn, "data_%4d%2d%2d_%2d%2d%2d_%2d.log",
                   &yy, &mo, &dd, &hh, &mm, &ss, &idx);
    if (n != 7 || yy < 2024 || mo < 1 || mo > 12 || dd < 1 || dd > 31 ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
        return false;
    }

    out->year = yy;
    out->month = mo;
    out->day = dd;
    out->hour = hh;
    out->min = mm;
    out->sec = ss;
    out->tz_quarters = 0;
    out->valid = true;
    return true;
}

static bool prv_make_rotated_name(char *out, size_t out_len)
{
    lte_time_t t;
    if (lte_time_get(&t)) {
        for (int i = 0; i < 100; i++) {
            snprintf(out, out_len, "data_%04d%02d%02d_%02d%02d%02d_%02d.log",
                     t.year, t.month, t.day, t.hour, t.min, t.sec, i);

            char path[128];
            struct stat st;
            prv_path(path, sizeof(path), out);
            if (stat(path, &st) != 0) return true;
        }
        return false;
    }

    for (int i = DATA_LOG_MAX_FILES - 1; i >= 1; i--) {
        char old_fn[32];
        char new_fn[32];
        char old_path[128];
        char new_path[128];

        snprintf(old_fn, sizeof(old_fn), "data_%d.log", i);
        snprintf(new_fn, sizeof(new_fn), "data_%d.log", i + 1);
        prv_path(old_path, sizeof(old_path), old_fn);
        prv_path(new_path, sizeof(new_path), new_fn);

        if (i == DATA_LOG_MAX_FILES - 1) {
            remove(new_path);
        }
        rename(old_path, new_path);
    }

    snprintf(out, out_len, "data_1.log");
    return true;
}

static void prv_rotate_data_log_locked(void)
{
    char active[128];
    struct stat st;
    prv_path(active, sizeof(active), SD_FILE_DATA);
    if (stat(active, &st) != 0 || st.st_size < DATA_LOG_MAX_BYTES) return;

    char rotated_fn[64];
    char rotated_path[128];
    if (!prv_make_rotated_name(rotated_fn, sizeof(rotated_fn))) {
        LOG_W(TAG, "data log rotate skipped: no free rotated name");
        return;
    }

    prv_path(rotated_path, sizeof(rotated_path), rotated_fn);
    if (rename(active, rotated_path) == 0) {
        LOG_I(TAG, "Rotated %s -> %s", SD_FILE_DATA, rotated_fn);
    } else {
        LOG_E(TAG, "rotate fail: %s", SD_FILE_DATA);
    }
}

typedef struct {
    char fn[64];
    int  stamp;
    bool keep;
} data_log_entry_t;

static void prv_cleanup_data_logs_locked(void)
{
    static uint32_t s_last_maint_ms = 0;
    uint32_t now = prv_now_ms();
    if (s_last_maint_ms != 0 &&
        now - s_last_maint_ms < DATA_LOG_MAINT_INTERVAL_MS) {
        return;
    }
    s_last_maint_ms = now;

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) return;

    data_log_entry_t entries[DATA_LOG_MAX_FILES + 16];
    int count = 0;
    lte_time_t cur_time;
    bool has_time = lte_time_get(&cur_time);
    int cur_days = has_time ? lte_time_days_since_2000(&cur_time) : -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (count >= (int)(sizeof(entries) / sizeof(entries[0]))) break;
        if (strncmp(ent->d_name, "data_", 5) != 0 ||
            strstr(ent->d_name, ".log") == NULL) {
            continue;
        }

        lte_time_t log_time = {0};
        int stamp = 0;
        if (prv_parse_data_log_date(ent->d_name, &log_time)) {
            int days = lte_time_days_since_2000(&log_time);
            stamp = days * 86400 + log_time.hour * 3600 +
                    log_time.min * 60 + log_time.sec;

            if (cur_days >= 0 && days >= 0 &&
                cur_days - days >= DATA_LOG_RETENTION_DAYS) {
                char path[128];
                prv_path(path, sizeof(path), ent->d_name);
                if (remove(path) == 0) {
                    LOG_I(TAG, "Deleted old data log: %s", ent->d_name);
                }
                continue;
            }
        } else {
            stamp = count;
        }

        strncpy(entries[count].fn, ent->d_name, sizeof(entries[count].fn) - 1);
        entries[count].fn[sizeof(entries[count].fn) - 1] = '\0';
        entries[count].stamp = stamp;
        entries[count].keep = true;
        count++;
    }
    closedir(dir);

    int keep_count = count;
    while (keep_count > DATA_LOG_MAX_FILES) {
        int oldest = -1;
        for (int i = 0; i < count; i++) {
            if (!entries[i].keep) continue;
            if (oldest < 0 || entries[i].stamp < entries[oldest].stamp) {
                oldest = i;
            }
        }
        if (oldest < 0) break;

        char path[128];
        prv_path(path, sizeof(path), entries[oldest].fn);
        if (remove(path) == 0) {
            LOG_I(TAG, "Deleted excess data log: %s", entries[oldest].fn);
        }
        entries[oldest].keep = false;
        keep_count--;
    }
}

esp_err_t sd_log_init(void)
{
    if (s_sd_mutex == NULL) {
        s_sd_mutex = xSemaphoreCreateMutex();
        if (!s_sd_mutex) {
            LOG_E(TAG, "SD mutex create FAILED");
            return ESP_ERR_NO_MEM;
        }
    }

    LOG_I(TAG, "Init SD | CS=%d SCK=%d MOSI=%d MISO=%d",
          SD_PIN_CS, SD_PIN_SCK, SD_PIN_MOSI, SD_PIN_MISO);

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SD_SPI_FREQ_KHZ;
    host.command_timeout_ms = 1000; // Fail-fast timeout: 1 giây thay vì mặc định

    spi_bus_config_t bus = {
        .mosi_io_num     = SD_PIN_MOSI,
        .miso_io_num     = SD_PIN_MISO,
        .sclk_io_num     = SD_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t r = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (r != ESP_OK) {
        LOG_E(TAG, "SPI bus fail: %s", esp_err_to_name(r));
        return r;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_PIN_CS;
    slot.host_id = host.slot;

    r = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (r != ESP_OK) {
        LOG_E(TAG, "Mount FAIL: %s", esp_err_to_name(r));
        return r;
    }

    s_mounted = true;
    LOG_I(TAG, "SD mounted [OK] | %s | %lluMB",
          s_card->cid.name,
          ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024));
    return ESP_OK;
}

void sd_log_deinit(void)
{
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_mounted = false;
    LOG_I(TAG, "SD unmounted");
    if (s_sd_mutex) {
        vSemaphoreDelete(s_sd_mutex);
        s_sd_mutex = NULL;
    }
}

esp_err_t sd_log_reinit(void)
{
    LOG_W(TAG, "=== SD REINIT ===");

    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        LOG_E(TAG, "reinit: mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    /* Unmount neu dang mounted (giai phong SPI device, giu lai SPI bus) */
    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
        s_mounted = false;
        LOG_I(TAG, "reinit: unmounted old");
    }

    /* Cho SD card on dinh */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Remount voi cung config (SPI bus da init tu truoc, khong can init lai) */
    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SD_SPI_FREQ_KHZ;
    host.command_timeout_ms = 1000;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_PIN_CS;
    slot.host_id = host.slot;

    esp_err_t r = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (r == ESP_OK) {
        s_mounted = true;
        LOG_I(TAG, "SD reinit [OK] | %s | %lluMB",
              s_card->cid.name,
              ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024));
    } else {
        LOG_E(TAG, "SD reinit FAIL: %s", esp_err_to_name(r));
    }

    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return r;
}

bool sd_log_is_ready(void) { return s_mounted; }

bool sd_log_append(const char *fn, const char *line)
{
    if (!s_mounted || !fn || !line) return false;
    if (prv_is_data_log(fn) && !DATA_LOG_ENABLE) return true;

    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        LOG_W(TAG, "append: mutex timeout");
        return false;
    }

    if (prv_is_data_log(fn)) {
        prv_rotate_data_log_locked();
        prv_cleanup_data_logs_locked();
    }

    char path[128];
    prv_path(path, sizeof(path), fn);
    FILE *f = fopen(path, "a");
    if (!f) {
        LOG_E(TAG, "open fail: %s", path);
        if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
        return false;
    }
    int w = fprintf(f, "%s\n", line);
    fclose(f);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return (w > 0);
}

bool sd_log_dump(const char *fn)
{
    if (!s_mounted || !fn) return false;
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return false;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
        return false;
    }
    LOG_I(TAG, "== DUMP: %s ==", fn);
    char line[512];
    int cnt = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\n') line[l - 1] = '\0';
        LOG_I(TAG, "[%04d] %s", cnt++, line);
        if (cnt % 20 == 0) vTaskDelay(pdMS_TO_TICKS(10));
    }
    fclose(f);
    LOG_I(TAG, "== TOTAL: %d lines ==", cnt);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return true;
}

long sd_log_file_size(const char *fn)
{
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return -1;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    struct stat st;
    long res = (stat(path, &st) == 0) ? (long)st.st_size : -1;
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return res;
}

bool sd_log_file_exists(const char *fn)
{
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return false;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    struct stat st;
    bool ok = (stat(path, &st) == 0);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ok;
}

bool sd_log_delete(const char *fn)
{
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return false;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    bool ok = (remove(path) == 0);
    if (ok) LOG_I(TAG, "Deleted: %s", fn);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ok;
}

bool sd_log_rename(const char *old_fn, const char *new_fn)
{
    if (!s_mounted || !old_fn || !new_fn) return false;
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return false;
    }

    char old_path[128];
    char new_path[128];
    prv_path(old_path, sizeof(old_path), old_fn);
    prv_path(new_path, sizeof(new_path), new_fn);

    struct stat st;
    if (stat(new_path, &st) == 0) {
        LOG_W(TAG, "rename target exists: %s", new_fn);
        if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
        return false;
    }

    bool ok = (rename(old_path, new_path) == 0);
    if (ok) {
        LOG_I(TAG, "Renamed: %s -> %s", old_fn, new_fn);
    } else {
        LOG_E(TAG, "rename fail: %s -> %s", old_fn, new_fn);
    }

    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ok;
}

int sd_log_count_lines(const char *fn)
{
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return -1;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
        return -1;
    }
    int cnt = 0;
    char buf[16];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (int i = 0; i < (int)nread; i++) {
            if (buf[i] == '\n') cnt++;
        }
    }
    fclose(f);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return cnt;
}

FILE *sd_log_open_read(const char *fn)
{
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return NULL;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_E(TAG, "open_read fail: %s", path);
    }
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return f;
}

void sd_log_close_read(FILE *f)
{
    if (!f) return;
    fclose(f);
}

int sd_log_read_lines(const char *fn, long offset,
                      char lines[][PAYLOAD_MAX_LEN], int max_lines)
{
    if (!s_mounted || !fn || !lines || max_lines <= 0) return 0;
    if (s_sd_mutex && xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return -1;
    }
    char path[128];
    prv_path(path, sizeof(path), fn);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
        return -1;
    }
    if (offset > 0) fseek(f, offset, SEEK_SET);

    int count = 0;
    while (count < max_lines && fgets(lines[count], PAYLOAD_MAX_LEN, f)) {
        size_t l = strlen(lines[count]);
        if (l > 0 && lines[count][l - 1] == '\n') lines[count][l - 1] = '\0';
        if (strlen(lines[count]) < 2) continue;
        count++;
    }
    fclose(f);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return count;
}
