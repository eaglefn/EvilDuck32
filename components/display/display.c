#include "display.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static const char *TAG = "display";

#define LVGL_BUF_LINES  20
#define LVGL_TICK_MS    2

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;
static lv_disp_t *lvgl_disp = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t lvgl_mutex = NULL;

static lv_color_t buf1[LCD_WIDTH * LVGL_BUF_LINES];
static lv_color_t buf2[LCD_WIDTH * LVGL_BUF_LINES];

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(drv);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t ph = (esp_lcd_panel_handle_t)drv->user_data;
    // With MADCTL=0x30: ST7789 expects CASET for Y, RASET for X+offset
    // Add offset only to X coordinates
    esp_lcd_panel_draw_bitmap(ph,
                              area->x1 + LCD_OFFSET_X, area->y1,
                              area->x2 + LCD_OFFSET_X + 1, area->y2 + 1,
                              color_map);
}

static void lvgl_port_update_cb(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t ph = (esp_lcd_panel_handle_t)drv->user_data;
    switch (drv->rotated) {
        case LV_DISP_ROT_NONE:
            // MADCTL=0x30 already handles the swap, mirror settings off
            esp_lcd_panel_swap_xy(ph, false);
            esp_lcd_panel_mirror(ph, false, false);
            break;
        case LV_DISP_ROT_90:
            esp_lcd_panel_swap_xy(ph, false);
            esp_lcd_panel_mirror(ph, false, false);
            break;
        case LV_DISP_ROT_180:
            esp_lcd_panel_swap_xy(ph, false);
            esp_lcd_panel_mirror(ph, true, true);
            break;
        case LV_DISP_ROT_270:
            esp_lcd_panel_swap_xy(ph, false);
            esp_lcd_panel_mirror(ph, true, true);
            break;
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LCD_BL_LEDC_TMR,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_BL_LEDC_CH,
        .timer_sel = LCD_BL_LEDC_TMR,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel);
}

void display_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t max_duty = (1 << 13) - 1;
    uint32_t duty = (percent == 0) ? 0 : max_duty - (81 * (100 - percent));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BL_LEDC_CH);
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7789 display");

    // SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Panel IO - use LCD_SPI_HOST directly as bus handle
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLK,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    // Panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,  // Try RGB instead of BGR for sharper text
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Fix for Waveshare ESP32-S3 LCD 1.47: set correct MADCTL (0x30)
    // This sets MV=1 which swaps X/Y axes - required for this display
    const uint8_t madctl = 0x30;
    esp_lcd_panel_io_tx_param(io_handle, 0x36, &madctl, 1);

    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Backlight
    backlight_init();
    display_set_backlight(50);

    // LVGL
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_WIDTH * LVGL_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.drv_update_cb = lvgl_port_update_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lvgl_disp = lv_disp_drv_register(&disp_drv);

    // LVGL tick timer
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    lvgl_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

lv_disp_t *display_get_lvgl_disp(void)
{
    return lvgl_disp;
}

void display_lvgl_handler(void)
{
    if (lvgl_mutex && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10))) {
        lv_timer_handler();
        xSemaphoreGive(lvgl_mutex);
    }
}
