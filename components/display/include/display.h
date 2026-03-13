#pragma once

#include "esp_err.h"
#include "lvgl.h"

// LCD Pin Configuration (Waveshare ESP32-S3 LCD 1.47)
#define PIN_LCD_SCLK    40
#define PIN_LCD_MOSI    45
#define PIN_LCD_DC      41
#define PIN_LCD_RST     39
#define PIN_LCD_CS      42
#define PIN_LCD_BL      48

// Display dimensions
#define LCD_WIDTH       172
#define LCD_HEIGHT      320
#define LCD_OFFSET_X    34
#define LCD_OFFSET_Y    0

// SPI configuration
#define LCD_SPI_HOST    SPI3_HOST
#define LCD_PIXEL_CLK   (12 * 1000 * 1000)

// Backlight PWM
#define LCD_BL_LEDC_CH  LEDC_CHANNEL_0
#define LCD_BL_LEDC_TMR LEDC_TIMER_0

esp_err_t display_init(void);
void display_set_backlight(uint8_t percent);
lv_disp_t *display_get_lvgl_disp(void);

// LVGL task handler - call from main loop or dedicated task
void display_lvgl_handler(void);
