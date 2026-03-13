#include "web.h"
#include "esp_log.h"

static const char *TAG = "web";

esp_err_t web_init(void) {
    ESP_LOGI(TAG, "Web init");
    return ESP_OK;
}
