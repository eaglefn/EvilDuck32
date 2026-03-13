#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "sdcard.h"
#include "led.h"
#include "duckyscript.h"
#include "usb_hid.h"
#include "wifi_manager.h"
#include "webserver.h"
#include "settings.h"
#include "script_storage.h"

static const char *TAG = "EvilDuck32";

static lv_obj_t *status_label = NULL;

// Cached state – update_status_screen() only redraws when a field actually changes
static struct {
    bool     sd_mounted;
    bool     hid_mounted;
    bool     wifi_started;
    char     wifi_ip[16];
    int      ducky_state;
    char     filename[64];
    int      current_line;
} s_last = { .ducky_state = -1 };  // -1 forces first draw

static void update_status_screen(void)
{
    if (!status_label) return;

    ducky_script_info_t info;
    ducky_get_info(&info);

    bool sd     = sdcard_is_mounted();
    bool hid    = usb_hid_is_mounted();
    bool wifi   = wifi_manager_is_started();
    const char *ip_ptr = wifi ? wifi_manager_get_ip() : "";

    // Compare every field individually – no string allocation needed
    bool changed =
        sd              != s_last.sd_mounted      ||
        hid             != s_last.hid_mounted     ||
        wifi            != s_last.wifi_started    ||
        strncmp(ip_ptr, s_last.wifi_ip, sizeof(s_last.wifi_ip)) != 0 ||
        (int)info.state != s_last.ducky_state     ||
        strncmp(info.filename, s_last.filename, sizeof(s_last.filename)) != 0 ||
        (info.state == DUCKY_STATE_RUNNING && info.current_line != s_last.current_line);

    if (!changed) return;

    // Update cache
    s_last.sd_mounted  = sd;
    s_last.hid_mounted = hid;
    s_last.wifi_started = wifi;
    strncpy(s_last.wifi_ip,   ip_ptr,        sizeof(s_last.wifi_ip)   - 1);
    strncpy(s_last.filename,  info.filename, sizeof(s_last.filename)  - 1);
    s_last.ducky_state   = (int)info.state;
    s_last.current_line  = info.current_line;

    const char *state_str = "Idle";
    switch (info.state) {
        case DUCKY_STATE_IDLE:     state_str = "Idle";     break;
        case DUCKY_STATE_RUNNING:  state_str = "Running";  break;
        case DUCKY_STATE_PAUSED:   state_str = "Paused";   break;
        case DUCKY_STATE_ERROR:    state_str = "Error";    break;
        case DUCKY_STATE_FINISHED: state_str = "Finished"; break;
    }

    char buf[300];
    int len = snprintf(buf, sizeof(buf),
             "Mode: HID\n"
             "Storage: %s\n"
             "HID: %s\n"
             "WiFi: %s\n"
             "State: %s",
             sd   ? "SD"  : "--",
             hid  ? "OK"  : "--",
             wifi ? ip_ptr : "Off",
             state_str);

    if (info.state == DUCKY_STATE_RUNNING && info.filename[0] != '\0') {
        snprintf(buf + len, sizeof(buf) - len,
                 "\n\nScript: %s\nLine: %d/%d",
                 info.filename, info.current_line, info.total_lines);
    }

    lv_label_set_text(status_label, buf);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    uint32_t update_counter = 0;
    while (1) {
        display_lvgl_handler();
        vTaskDelay(pdMS_TO_TICKS(10));

        // Update status screen every 2 seconds (reduced from 500ms)
        if (++update_counter >= 200) {
            update_counter = 0;
            update_status_screen();
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== EvilDuck32 Booting ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize TCP/IP stack early so tcpip_task is running before USB enumerates.
    // This ensures tcpip_callback() works when the RNDIS interface is initialized.
    esp_netif_init();

    // Initialize Settings (load from NVS)
    ret = settings_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Settings init failed: %s", esp_err_to_name(ret));
    }

    // Initialize Script Storage (NVS-based)
    ret = script_storage_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Script storage init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Script storage ready (%d scripts available)", script_storage_count());
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Settings init failed: %s", esp_err_to_name(ret));
    }

    // Phase 1: Hardware init
    ESP_ERROR_CHECK(led_init());
    led_set_status(LED_BOOT);

    // Initialize USB HID FIRST (before SD-Card to avoid pin conflicts)
    ret = usb_hid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB HID init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "USB HID initialized");
    }

    ducky_init();   // Initialize DuckyScript engine

    ESP_ERROR_CHECK(display_init());

    // Create status screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "EvilDuck32 v1.0.0");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    status_label = lv_label_create(scr);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 5, 30);
    lv_obj_set_width(status_label, LCD_WIDTH - 10);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);

    update_status_screen();

    // Initialize SD-Card AFTER USB HID (with delay to let USB settle)
    ESP_LOGI(TAG, "Initializing SD-Card after USB HID...");
    vTaskDelay(pdMS_TO_TICKS(200));  // Let USB HID fully initialize
    sdcard_init();  // Non-fatal if SD not present

    // Initialize WiFi Manager (AP mode)
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi Manager init failed: %s", esp_err_to_name(ret));
    } else {
        ret = wifi_manager_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WiFi AP started. Connect to: %s (IP: %s)",
                     WIFI_AP_SSID, wifi_manager_get_ip());
        }
    }

    // Initialize Webserver
    ret = webserver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Webserver init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Webserver ready. Open: http://%s", wifi_manager_get_ip());
    }

    // Start LVGL task on core 1
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 4096, NULL, 5, NULL, 1);

    led_set_status(LED_READY);

    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "=== EvilDuck32 Ready ===");

    bool autorun_triggered = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Trigger autorun script once on first USB mount
        if (!autorun_triggered && usb_hid_is_mounted()) {
            autorun_triggered = true;
            const char *autorun = settings_get_autorun();
            if (autorun && autorun[0] != '\0') {
                ESP_LOGI(TAG, "USB mounted – starting autorun script: %s", autorun);
                ducky_run_script(autorun);
            } else {
                ESP_LOGI(TAG, "USB mounted – no autorun script configured");
            }
        }
    }
}
