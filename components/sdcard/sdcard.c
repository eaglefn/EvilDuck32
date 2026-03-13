#include "sdcard.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "sdcard";
static sdmmc_card_t *card = NULL;
static bool mounted = false;

esp_err_t sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card (SDMMC) - AFTER USB HID init");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  // 1-bit mode for better compatibility
    slot_config.clk = PIN_SD_CLK;
    slot_config.cmd = PIN_SD_CMD;
    slot_config.d0 = PIN_SD_D0;
    slot_config.d1 = PIN_SD_D1;
    slot_config.d2 = PIN_SD_D2;
    slot_config.d3 = PIN_SD_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Please check if SD card is inserted properly");
        return ret;
    }

    mounted = true;
    sdmmc_card_print_info(stdout, card);

    // Create directory structure
    ESP_LOGI(TAG, "Creating directories...");

    // /scripts – DuckyScript files (.txt)
    if (mkdir(SD_MOUNT_POINT "/scripts", 0775) == 0) {
        ESP_LOGI(TAG, "Created /scripts directory");
    } else if (errno == EEXIST) {
        ESP_LOGI(TAG, "/scripts directory already exists");
    } else {
        ESP_LOGW(TAG, "Failed to create /scripts: %d", errno);
    }

    // /payloads – binary payload files (.exe, .bin, .ps1, etc.)
    if (mkdir(SD_MOUNT_POINT "/payloads", 0775) == 0) {
        ESP_LOGI(TAG, "Created /payloads directory");
    } else if (errno == EEXIST) {
        ESP_LOGI(TAG, "/payloads directory already exists");
    } else {
        ESP_LOGW(TAG, "Failed to create /payloads: %d", errno);
    }

    if (mkdir(SD_MOUNT_POINT "/loot", 0775) == 0) {
        ESP_LOGI(TAG, "Created /loot directory");
    } else if (errno == EEXIST) {
        ESP_LOGI(TAG, "/loot directory already exists");
    }
    mkdir(SD_MOUNT_POINT "/config", 0775);
    mkdir(SD_MOUNT_POINT "/logs", 0775);

    // Test write access IMPORTANT
    ESP_LOGI(TAG, "Testing write access...");
    FILE *test_f = fopen(SD_MOUNT_POINT "/scripts/test_write.txt", "w");
    if (test_f) {
        fprintf(test_f, "SD Card Write Test - Success!\n");
        fclose(test_f);
        unlink(SD_MOUNT_POINT "/scripts/test_write.txt");
        ESP_LOGI(TAG, "✓ SD Card write test PASSED - SD is writable");
    } else {
        ESP_LOGE(TAG, "✗ SD Card write test FAILED - Cannot write to SD!");
    }

    ESP_LOGI(TAG, "SD card mounted successfully at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

void sdcard_deinit(void)
{
    if (mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        mounted = false;
        card = NULL;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

bool sdcard_is_mounted(void)
{
    return mounted;
}

static int sdcard_list_dir(const char *dirpath, char names[][64], int max_count)
{
    if (!mounted) return 0;

    DIR *dir = opendir(dirpath);
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        if (entry->d_name[0] == '.') continue;
        char full_path[128];
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        strncpy(names[count], entry->d_name, 63);
        names[count][63] = '\0';
        count++;
    }
    closedir(dir);
    return count;
}

// List DuckyScript files from /sdcard/scripts/
int sdcard_list_scripts(char scripts[][64], int max_count)
{
    return sdcard_list_dir(SD_MOUNT_POINT "/scripts", scripts, max_count);
}

// List payload files from /sdcard/payloads/
int sdcard_list_payloads(char payloads[][64], int max_count)
{
    return sdcard_list_dir(SD_MOUNT_POINT "/payloads", payloads, max_count);
}

esp_err_t sdcard_read_file(const char *path, char *buf, size_t buf_size, size_t *bytes_read)
{
    if (!mounted) return ESP_ERR_INVALID_STATE;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);

    FILE *f = fopen(full_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }

    *bytes_read = fread(buf, 1, buf_size - 1, f);
    buf[*bytes_read] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t sdcard_write_file(const char *path, const char *data, size_t len)
{
    if (!mounted) {
        ESP_LOGE(TAG, "SD card not mounted!");
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);

    ESP_LOGI(TAG, "Writing file: %s (%d bytes)", full_path, len);

    FILE *f = fopen(full_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create %s (errno: %d: %s)", full_path, errno, strerror(errno));
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    if (written != len) {
        ESP_LOGE(TAG, "Write incomplete: %d/%d bytes", written, len);
        fclose(f);
        return ESP_FAIL;
    }

    if (fflush(f) != 0) {
        ESP_LOGE(TAG, "Failed to flush file");
        fclose(f);
        return ESP_FAIL;
    }

    // IMPORTANT: fsync to ensure data is written to SD card
    #ifdef ESP_PLATFORM
    int fd = fileno(f);
    if (fsync(fd) != 0) {
        ESP_LOGW(TAG, "fsync failed, but file may still be written");
    }
    #endif

    fclose(f);

    ESP_LOGI(TAG, "✓ File written and synced successfully: %s", full_path);

    // Note: Webserver cache will be invalidated on next read
    return ESP_OK;
}

esp_err_t sdcard_delete_file(const char *path)
{
    if (!mounted) return ESP_ERR_INVALID_STATE;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);

    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete %s", full_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

uint64_t sdcard_total_bytes(void)
{
    if (!mounted || !card) return 0;
    return (uint64_t)card->csd.capacity * card->csd.sector_size;
}

uint64_t sdcard_used_bytes(void)
{
    // FATFS doesn't easily expose used bytes; return 0 as placeholder
    return 0;
}
