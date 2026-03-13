#pragma once

#include "esp_err.h"
#include <stdbool.h>

// WiFi Configuration
#define WIFI_AP_SSID       "EvilDuck32"
#define WIFI_AP_PASSWORD   "EvilDuck32"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CONN   4

// IP Configuration
#define WIFI_AP_IP         "192.168.4.1"
#define WIFI_AP_GATEWAY    "192.168.4.1"
#define WIFI_AP_NETMASK    "255.255.255.0"

// Initialize WiFi in Access Point mode
esp_err_t wifi_manager_init(void);

// Start/Stop WiFi
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);

// Get WiFi status
bool wifi_manager_is_started(void);

// Get AP IP address
const char* wifi_manager_get_ip(void);
