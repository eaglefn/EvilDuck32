#include "usb_storage.h"
#include "usb_hid.h"
#include "sdcard.h"        // PIN_SD_* pin definitions only - no FATFS calls used here
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "tusb.h"
#include "class/msc/msc_device.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "usb_storage";

static sdmmc_host_t s_host;
static sdmmc_card_t s_card;
static bool         s_card_ready = false;

// Raw SDMMC init - same pins/slot config as components/sdcard, but WITHOUT
// esp_vfs_fat_sdmmc_mount(): no FATFS involved, host PC becomes the sole filesystem owner.
static esp_err_t sdcard_raw_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card (raw block access, no FATFS)");

    s_host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    // 4-bit mode (all D0-D3 pins are wired, see the GPIO table in CLAUDE.md): raw block
    // reads for the whole SD card go over this bus, so its throughput directly bounds
    // how fast the host can read the FAT tables / directory entries on first mount.
    // components/sdcard still uses 1-bit for the normal FATFS mount - unaffected by this.
    slot_config.width = 4;
    slot_config.clk = PIN_SD_CLK;
    slot_config.cmd = PIN_SD_CMD;
    slot_config.d0  = PIN_SD_D0;
    slot_config.d1  = PIN_SD_D1;
    slot_config.d2  = PIN_SD_D2;
    slot_config.d3  = PIN_SD_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = s_host.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc host init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_host_init_slot(s_host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc slot init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_card_init(&s_host, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card not found / init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Please check if SD card is inserted properly");
        return ret;
    }

    sdmmc_card_print_info(stdout, &s_card);
    s_card_ready = true;
    return ESP_OK;
}

esp_err_t usb_storage_init(void)
{
    esp_err_t ret = sdcard_raw_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card init failed - USB drive will report as not-ready");
    }

    // MSC-only descriptor is selected here; if a normal HID/RNDIS boot already ran
    // usb_hid_init() earlier in this same boot, this call would be a bug in the caller.
    return usb_hid_init_storage_mode();
}

bool usb_storage_is_mounted(void)
{
    return usb_hid_is_mounted();
}

uint64_t usb_storage_capacity_bytes(void)
{
    if (!s_card_ready) return 0;
    return (uint64_t)s_card.csd.capacity * s_card.csd.sector_size;
}

// ============================================================
// TinyUSB MSC (Mass Storage) Callbacks
//
// These are only ever invoked while the TinyUSB stack is enumerated in Storage/ARMING
// mode (usb_hid_init_storage_mode()) - the MSC interface is absent from the descriptor
// during normal HID/RNDIS operation, so the host never reaches these during an attack run.
// ============================================================

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    const char vid[] = "EvilDuck";          // <= 8 chars
    const char pid[] = "SD Card Storage";   // <= 16 chars
    const char rev[] = "1.0";               // <= 4 chars

    memset(vendor_id,   ' ', 8);
    memset(product_id,  ' ', 16);
    memset(product_rev, ' ', 4);
    memcpy(vendor_id,   vid, strlen(vid));
    memcpy(product_id,  pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (!s_card_ready) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);  // Medium not present
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    if (s_card_ready) {
        *block_count = s_card.csd.capacity;
        *block_size  = (uint16_t)s_card.csd.sector_size;
    } else {
        *block_count = 0;
        *block_size  = 512;
    }
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun; (void)power_condition; (void)start; (void)load_eject;
    return true;
}

// CFG_TUD_MSC_EP_BUFSIZE (64, Full-Speed bulk limit) is smaller than the SD card's block
// size (512) - TinyUSB therefore calls this once per 64-byte chunk of a block, with
// `offset` growing 0, 64, 128, ... within that block before moving to the next lba. We
// read the whole block into s_read_buf on the first chunk (offset == 0) and serve slices
// of it on every call for that block - see tud_msc_read10_cb's doc comment in msc_device.h.
static uint8_t  s_read_buf[512];
static uint32_t s_read_buf_lba = UINT32_MAX;

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    if (!s_card_ready) return TUD_MSC_RET_ERROR;

    uint32_t block_size = s_card.csd.sector_size;
    if (block_size > sizeof(s_read_buf)) return TUD_MSC_RET_ERROR;  // shouldn't happen for SD/SDHC/SDXC

    if (offset == 0 || s_read_buf_lba != lba) {
        esp_err_t err = sdmmc_read_sectors(&s_card, s_read_buf, lba, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read10 failed (lba=%lu): %s", (unsigned long)lba, esp_err_to_name(err));
            s_read_buf_lba = UINT32_MAX;
            return TUD_MSC_RET_ERROR;
        }
        s_read_buf_lba = lba;
    }

    if (offset + bufsize > block_size) bufsize = block_size - offset;  // clamp, defensive
    memcpy(buffer, s_read_buf + offset, bufsize);
    return (int32_t)bufsize;
}

// Catch-all for SCSI commands not handled internally by TinyUSB (see msc_device.h for the
// built-in list). We don't support any vendor-specific commands - report "invalid command".
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)scsi_cmd; (void)buffer; (void)bufsize;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);  // Invalid command operation code
    return -1;
}

// Mirror of s_read_buf for writes: accumulate 64-byte chunks until a full block is
// assembled (offset + bufsize reaches block_size), then flush it in one sdmmc_write_sectors().
static uint8_t s_write_buf[512];

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    if (!s_card_ready) return TUD_MSC_RET_ERROR;

    uint32_t block_size = s_card.csd.sector_size;
    if (block_size > sizeof(s_write_buf)) return TUD_MSC_RET_ERROR;  // shouldn't happen for SD/SDHC/SDXC

    if (offset + bufsize > block_size) bufsize = block_size - offset;  // clamp, defensive
    memcpy(s_write_buf + offset, buffer, bufsize);

    if (offset + bufsize >= block_size) {
        esp_err_t err = sdmmc_write_sectors(&s_card, s_write_buf, lba, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "write10 failed (lba=%lu): %s", (unsigned long)lba, esp_err_to_name(err));
            return TUD_MSC_RET_ERROR;
        }
        // Keep the read cache coherent if this block is re-read right after being written.
        if (s_read_buf_lba == lba) memcpy(s_read_buf, s_write_buf, block_size);
    }
    return (int32_t)bufsize;
}
