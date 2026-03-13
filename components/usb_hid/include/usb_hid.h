#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "hid_keycodes.h"
#include "hid_locale.h"

// Initialize USB HID (installs TinyUSB driver, starts USB enumeration)
esp_err_t usb_hid_init(void);

// Returns true when the USB host has enumerated and mounted the device
bool usb_hid_is_mounted(void);

// Block until host mounts the device or timeout_ms elapses (0 = forever)
void usb_hid_wait_mounted(uint32_t timeout_ms);

// Send a key press (keycode + optional modifier bits)
void usb_hid_keyboard_press(uint8_t keycode, uint8_t modifiers);

// Release all keys
void usb_hid_keyboard_release(void);

// Type a single UTF-8 character using the active locale
void usb_hid_keyboard_type_char(char c);

// Type a null-terminated string using the active locale
void usb_hid_keyboard_type_string(const char *str);

// Send a raw 8-byte HID keyboard report (modifiers + 6-key rollover)
void usb_hid_keyboard_raw_report(uint8_t modifiers, const uint8_t keys[6]);

// Mouse stubs (implemented in Phase 2 / attack_mode)
void usb_hid_mouse_move(int8_t x, int8_t y);
void usb_hid_mouse_click(uint8_t buttons);
void usb_hid_mouse_scroll(int8_t amount);

// USB Network (RNDIS) – ATTACKMODE HID RNDIS
// Enable RNDIS network interface (called by DuckyScript ATTACKMODE HID RNDIS).
// RNDIS is inactive by default; the device presents as HID-only until this is called.
esp_err_t usb_hid_enable_rndis(void);

// Returns true once the USB host obtained an IP via DHCP and the network is ready
bool usb_net_is_connected(void);
