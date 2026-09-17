#include "storage_mode.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "display.h"
#include "led.h"
#include "usb_storage.h"

static const char *TAG = "storage_mode";

// GPIO0 = BOOT button on the Waveshare board.
//
// IMPORTANT: GPIO0 doubles as the chip's boot-mode strap pin. Holding it low DURING the
// actual power-on/reset pulse is read by the ROM bootloader itself and forces UART/USB
// download mode - our app never starts in that case, so it can never observe the press
// (this is what caused the black screen when testing "hold BOOT while plugging in").
//
// So this check must NOT rely on GPIO0's state during reset. Instead, the app always boots
// normally, and shortly INTO that normal boot - once the ROM's one-time strap read is long
// over - we poll GPIO0 as a plain GPIO for a short window. The LED blinks during the window
// so a press has visible feedback even though the display isn't initialized yet.
#define PIN_BOOT_BUTTON GPIO_NUM_0
#define STORAGE_WINDOW_MS 2000
#define STORAGE_POLL_MS     50
#define ARMING_SPLASH_MS  3000

bool storage_mode_requested(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BUTTON,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);

    ESP_LOGI(TAG, "Hold BOOT button now for Storage Mode (%d ms window)...", STORAGE_WINDOW_MS);

    bool pressed = false;
    for (int elapsed = 0; elapsed < STORAGE_WINDOW_MS; elapsed += STORAGE_POLL_MS) {
        bool blink_on = (elapsed / STORAGE_POLL_MS) % 2 == 0;
        led_set_color(0, 0, blink_on ? 255 : 0);  // blue blink
        if (gpio_get_level(PIN_BOOT_BUTTON) == 0) {  // active-low
            pressed = true;
        }
        vTaskDelay(pdMS_TO_TICKS(STORAGE_POLL_MS));
    }

    if (pressed) {
        ESP_LOGI(TAG, "BOOT button press detected - entering Storage Mode");
    }
    return pressed;
}

void run_storage_mode(void)
{
    ESP_LOGI(TAG, "=== Storage/ARMING mode (BOOT button held) ===");

    ESP_ERROR_CHECK(led_init());
    led_set_status(LED_STORAGE_MODE);

    ESP_ERROR_CHECK(display_init());

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Brief confirmation splash right after the BOOT button press: big text,
    // centered and word-wrapped over two lines.
    //
    // NOTE: style transform_angle/zoom does NOT rotate rendered label glyphs in this
    // LVGL 8.3 build - it's only consumed by lv_obj_pos.c/lv_refr.c for invalidation-
    // area math, never by the actual draw path for lv_label (verified in
    // managed_components/lvgl__lvgl/src). True glyph rotation would need a custom
    // lv_canvas renderer, so this stays horizontal.
    lv_obj_t *arming = lv_label_create(scr);
    lv_obj_set_width(arming, LCD_WIDTH - 10);
    lv_obj_set_style_text_align(arming, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(arming, "ARMING\nMODE");
    lv_obj_set_style_text_font(arming, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(arming, lv_color_hex(0x0000FF), 0);
    lv_obj_center(arming);

    // Start SD-card + USB Mass Storage init now, while the splash is still showing,
    // instead of after it - USB enumeration on the host (Windows/Linux driver
    // negotiation, SCSI INQUIRY/READ CAPACITY) takes a couple of seconds on its own,
    // so overlapping it with the splash avoids adding the two delays together.
    esp_err_t ret = usb_storage_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB Storage init failed: %s", esp_err_to_name(ret));
        led_set_status(LED_ERROR);
    }

    for (int elapsed = 0; elapsed < ARMING_SPLASH_MS; elapsed += 10) {
        display_lvgl_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    lv_obj_del(arming);

    // Permanent header for the rest of Storage/ARMING mode - "ARMING MODE" replaces
    // the previous "STORAGE MODE" wording so the screen keeps reflecting what the
    // BOOT button press just triggered, instead of reverting to older text.
    lv_obj_t *header = lv_label_create(scr);
    lv_label_set_text(header, "ARMING MODE");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0x0000FF), 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 5);

    // Plain auto-wrap (no manual line breaks) so LVGL fills each line with as many
    // words as fit, instead of the previous hand-broken lines leaving orphan words
    // like "a" / "the" alone on a line. Montserrat 18 - the largest of the compiled
    // sizes (14/18/20/36) that still wraps cleanly within the 162px content width.
    lv_obj_t *info = lv_label_create(scr);
    lv_obj_set_style_text_color(info, lv_color_white(), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_set_width(info, LCD_WIDTH - 10);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_label_set_text(info,
        "SD card is now a USB drive on the connected PC.\n\n"
        "Press RST to return to normal operation.");
    // Blank line's worth of gap below the "ARMING MODE" header.
    lv_obj_align_to(info, header, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);

    while (1) {
        display_lvgl_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
