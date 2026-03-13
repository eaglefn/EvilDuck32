#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Settings keys (NVS)
#define NVS_NAMESPACE "settings"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_CHANNEL "channel"
#define NVS_KEY_IP "ip_addr"
#define NVS_KEY_GATEWAY "gateway"
#define NVS_KEY_NETMASK "netmask"
#define NVS_KEY_AUTORUN "autorun"
#define NVS_KEY_BUTTONSCRIPT "buttonscript"

// Default values
#define SETTINGS_DEFAULT_SSID "EvilDuck32"
#define SETTINGS_DEFAULT_PASSWORD "EvilDuck32"
#define SETTINGS_DEFAULT_CHANNEL 1
#define SETTINGS_DEFAULT_IP "192.168.4.1"
#define SETTINGS_DEFAULT_GATEWAY "192.168.4.1"
#define SETTINGS_DEFAULT_NETMASK "255.255.255.0"

// Settings structure
typedef struct {
    char ssid[33];
    char password[65];
    int channel;
    char ip_addr[16];
    char gateway[16];
    char netmask[16];
    char autorun[65];
    char buttonscript[65];
} settings_t;

// Initialize settings module (load from NVS or set defaults)
esp_err_t settings_init(void);

// Save all settings to NVS
esp_err_t settings_save(void);

// Load settings from NVS
esp_err_t settings_load(void);

// Reset settings to default values
esp_err_t settings_reset(void);

// Get settings structure
const settings_t* settings_get(void);

// Individual getters/setters
esp_err_t settings_set_ssid(const char *ssid);
esp_err_t settings_set_password(const char *password);
esp_err_t settings_set_channel(int channel);
esp_err_t settings_set_ip(const char *ip);
esp_err_t settings_set_gateway(const char *gateway);
esp_err_t settings_set_netmask(const char *netmask);
esp_err_t settings_set_autorun(const char *autorun);
esp_err_t settings_set_buttonscript(const char *buttonscript);

const char* settings_get_ssid(void);
const char* settings_get_password(void);
int settings_get_channel(void);
const char* settings_get_ip(void);
const char* settings_get_gateway(void);
const char* settings_get_netmask(void);
const char* settings_get_autorun(void);
const char* settings_get_buttonscript(void);

// Export settings as JSON string (for web API)
char* settings_to_json(char *buf, size_t buf_size);

// Parse settings from JSON string (for web API)
esp_err_t settings_parse_json(const char *json);

#ifdef __cplusplus
}
#endif
