#pragma once

#include "esp_err.h"
#include <stdbool.h>

// SD Card Pin Configuration (Waveshare ESP32-S3 LCD 1.47, SDMMC)
#define PIN_SD_CLK  14
#define PIN_SD_CMD  15
#define PIN_SD_D0   16
#define PIN_SD_D1   18
#define PIN_SD_D2   17
#define PIN_SD_D3   21

#define SD_MOUNT_POINT "/sdcard"

esp_err_t sdcard_init(void);
void sdcard_deinit(void);
bool sdcard_is_mounted(void);
int sdcard_list_scripts(char scripts[][64], int max_count);
int sdcard_list_payloads(char payloads[][64], int max_count);
esp_err_t sdcard_read_file(const char *path, char *buf, size_t buf_size, size_t *bytes_read);
esp_err_t sdcard_write_file(const char *path, const char *data, size_t len);
esp_err_t sdcard_delete_file(const char *path);
uint64_t sdcard_total_bytes(void);
uint64_t sdcard_used_bytes(void);
