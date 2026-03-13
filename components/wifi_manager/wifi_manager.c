#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/param.h>
#include <lwip/ip_addr.h>

static const char *TAG = "wifi_manager";

static bool wifi_started = false;
static esp_netif_t *netif = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "WiFi AP started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "WiFi AP stopped");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x joined, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x left, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi (AP mode)");

    // Initialize TCP/IP stack (may already be initialized)
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    // Create event loop if not already created
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    // Create AP netif
    netif = esp_netif_create_default_wifi_ap();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &wifi_event_handler,
                                                NULL));

    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (wifi_started) {
        ESP_LOGW(TAG, "WiFi already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting WiFi AP '%s' on channel %d", WIFI_AP_SSID, WIFI_AP_CHANNEL);

    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    strcpy((char*)wifi_config.ap.ssid, WIFI_AP_SSID);
    strcpy((char*)wifi_config.ap.password, WIFI_AP_PASSWORD);

    // Set WiFi mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // Set IP configuration
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ipaddr_addr(WIFI_AP_IP);
    ip_info.gw.addr = ipaddr_addr(WIFI_AP_GATEWAY);
    ip_info.netmask.addr = ipaddr_addr(WIFI_AP_NETMASK);
    esp_netif_dhcps_stop(netif);
    esp_netif_set_ip_info(netif, &ip_info);
    esp_netif_dhcps_start(netif);

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_started = true;
    ESP_LOGI(TAG, "WiFi AP started. IP: %s", WIFI_AP_IP);

    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    if (!wifi_started) {
        ESP_LOGW(TAG, "WiFi not started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping WiFi");
    ESP_ERROR_CHECK(esp_wifi_stop());
    wifi_started = false;

    return ESP_OK;
}

bool wifi_manager_is_started(void)
{
    return wifi_started;
}

const char* wifi_manager_get_ip(void)
{
    return WIFI_AP_IP;
}
