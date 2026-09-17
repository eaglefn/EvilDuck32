#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize the SD card as a raw USB Mass Storage (MSC) device.
//
// Brings up its own SDMMC card handle for RAW block access (sdmmc_read/write_sectors) -
// this is deliberately NOT the FATFS mount from components/sdcard. The two must never be
// active at the same time: either the ESP32 firmware mounts the card via FATFS (normal
// HID/RNDIS boot), or the host PC owns it via MSC (Storage/ARMING boot) - never both, to
// avoid filesystem corruption from concurrent writers.
//
// Also switches the TinyUSB stack into MSC-only mode via usb_hid_init_storage_mode().
// Intended to be called from the dedicated Storage/ARMING boot path (GPIO0 held at
// power-on) instead of sdcard_init()/usb_hid_init().
esp_err_t usb_storage_init(void);

// Returns true once the USB host has mounted the MSC device.
bool usb_storage_is_mounted(void);

// Total SD card capacity in bytes (0 if no card was found).
uint64_t usb_storage_capacity_bytes(void);
