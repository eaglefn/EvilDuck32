#pragma once

#include "esp_err.h"
#include <stdint.h>

#define PIN_WS2812_LED  38

typedef enum {
    LED_OFF,
    LED_BOOT,
    LED_READY,
    LED_RUNNING,
    LED_ERROR,
    LED_BLE_CONNECTED,
    LED_WIFI_ACTIVE,
    LED_STORAGE_MODE,
} led_status_t;

esp_err_t led_init(void);
void led_set_status(led_status_t status);
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_off(void);
