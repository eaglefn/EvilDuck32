#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

// SD Card Pin Configuration (Waveshare ESP32-S3 LCD 1.47, SDMMC)
#define PIN_SD_CLK  14
#define PIN_SD_CMD  15
#define PIN_SD_D0   16
#define PIN_SD_D1   18
#define PIN_SD_D2   17
#define PIN_SD_D3   21

#define SD_MOUNT_POINT "/sdcard"

// Max depth for recursive directory listing
#define SDCARD_MAX_LIST_DEPTH 3
// Max entries per directory listing
#define SDCARD_MAX_LIST_ENTRIES 200

// File entry structure for directory listings
typedef struct {
    char name[64];
    char path[128];        // Relative path from mount point
    bool is_dir;
    size_t size;           // File size in bytes (0 for directories)
    time_t mtime;          // Modification time
} sdcard_file_entry_t;

// Directory listing result
typedef struct {
    sdcard_file_entry_t entries[SDCARD_MAX_LIST_ENTRIES];
    int count;
    char path[128];        // Path that was listed
    uint64_t total_bytes;  // Total SD card capacity
    uint64_t used_bytes;   // Used bytes (approximate)
    uint64_t free_bytes;   // Free bytes (approximate)
} sdcard_dir_list_t;

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

// List directory contents (recursive, up to SDCARD_MAX_LIST_DEPTH)
esp_err_t sdcard_list_dir(const char *rel_path, sdcard_dir_list_t *result);

// Get SD card info (total, used, free space)
esp_err_t sdcard_get_info(uint64_t *total_bytes, uint64_t *used_bytes, uint64_t *free_bytes);
