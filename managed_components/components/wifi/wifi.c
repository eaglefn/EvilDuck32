#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <string.h>
#include <lwip/ip_addr.h>

static const char *TAG = "wifi";

esp_err_t wifi_init(void) {
    ESP_LOGI(TAG, "WiFi init");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, NULL, NULL));
    return ESP_OK;
}

esp_err_t wifi_start_ap(void) {
    ESP_LOGI(TAG, "WiFi AP start");
    wifi_config_t cfg = {
        .ap = {
            .ssid_len = 10,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strcpy((char*)cfg.ap.ssid, "EvilDuck32");
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    esp_netif_ip_info_t ip;
    ip.ip.addr = ipaddr_addr("192.168.4.1");
    ip.gw.addr = ipaddr_addr("192.168.4.1");
    ip.netmask.addr = ipaddr_addr("255.255.255.0");
    
    return ESP_OK;
}
