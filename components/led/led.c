#include "led.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "led";
static led_strip_handle_t strip = NULL;

esp_err_t led_init(void)
{
    ESP_LOGI(TAG, "Initializing WS2812 LED on GPIO %d", PIN_WS2812_LED);

    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_WS2812_LED,
        .max_leds = 1,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(strip);
    ESP_LOGI(TAG, "LED initialized");
    return ESP_OK;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!strip) return;
    // The WS2812 on this board uses RGB byte order, but the led_strip library
    // internally swaps to GRB. Passing (g, r, b) compensates for that swap.
    led_strip_set_pixel(strip, 0, g, r, b);
    led_strip_refresh(strip);
}

void led_set_status(led_status_t status)
{
    switch (status) {
        case LED_OFF:            led_set_color(0, 0, 0);       break;
        case LED_BOOT:           led_set_color(255, 165, 0);   break;
        case LED_READY:          led_set_color(255, 255, 255); break;
        case LED_RUNNING:        led_set_color(0, 0, 255);     break;
        case LED_ERROR:          led_set_color(255, 0, 0);     break;
        case LED_BLE_CONNECTED:  led_set_color(0, 255, 255);   break;
        case LED_WIFI_ACTIVE:    led_set_color(128, 0, 255);   break;
        case LED_STORAGE_MODE:   led_set_color(0, 0, 255);     break;
    }
}

void led_off(void)
{
    led_set_color(0, 0, 0);
}
