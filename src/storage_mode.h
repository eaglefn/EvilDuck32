#pragma once

#include <stdbool.h>

// Polls GPIO0 (BOOT button) for a short window early in a NORMAL boot and returns true if
// it was pressed during that window. Deliberately does NOT check GPIO0's state during the
// power-on/reset pulse itself - holding it low right then is read by the chip's ROM
// bootloader as a strap pin and forces UART/USB download mode, so the app would never even
// start. LED blinks magenta during the window as feedback. Call once, early in app_main(),
// after led_init() and before any other subsystem touches GPIO0.
bool storage_mode_requested(void);

// Storage/ARMING mode: presents the SD card as a plain USB drive instead of
// booting the normal HID/RNDIS attack path. Mutually exclusive with the normal
// boot - the SD card is either owned by the ESP32 firmware (FATFS) or passed
// through raw to the host (MSC), never both at once. Never returns; the only
// way out is a power-cycle.
void run_storage_mode(void);
