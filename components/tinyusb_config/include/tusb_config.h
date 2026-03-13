#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ESP-IDF specific configuration - don't redefine, already set by ESP-IDF
// #define CFG_TUSB_MCU OPT_MCU_ESP32S2

// Port mode: USB Device, Full-Speed (required for tusb_init() without arguments)
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// TinyUSB configuration
#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 0

// EP0 packet size (64 bytes for full-speed USB)
#define CFG_TUD_ENDPOINT0_SIZE 64

// HID Device
#define CFG_TUD_HID 1
#define CFG_TUD_HID_EP_BUFSIZE 64

// MSC (Mass Storage) - for future USB Storage mode
#define CFG_TUD_MSC 0

// CDC (Serial) - not needed currently
#define CFG_TUD_CDC 0

// MIDI - not needed
#define CFG_TUD_MIDI 0

// WebUSB - not needed
#define CFG_TUD_WEBUSB 0

// RNDIS/ECM Network - USB Ethernet for file exfiltration (ATTACKMODE STORAGE)
#define CFG_TUD_ECM_RNDIS 1
#define CFG_TUD_NCM       0
// CFG_TUD_NET_ENDPOINT_SIZE is auto-set by net_device.h (64 for FS, 512 for HS)
// We run Full-Speed, so the effective value is 64 bytes.
#define CFG_TUD_NET_MTU   1514

// Max number of endpoints (note: correct spelling ENDPOINT, not ENDPPOINT)
// EP0 + EP1(RNDIS notif IN) + EP2(RNDIS bulk IN) + EP2(RNDIS bulk OUT) + EP3(HID IN) = 4 numbers
#define CFG_TUD_ENDPOINT_MAX 6

// Device settings
#define CFG_TUD_DESC_AUTO 0

#ifdef __cplusplus
}
#endif
