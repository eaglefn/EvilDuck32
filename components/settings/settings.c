#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdcard.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "settings";

// Settings storage
static settings_t g_settings = {
    .ssid = SETTINGS_DEFAULT_SSID,
    .password = SETTINGS_DEFAULT_PASSWORD,
    .channel = SETTINGS_DEFAULT_CHANNEL,
    .ip_addr = SETTINGS_DEFAULT_IP,
    .gateway = SETTINGS_DEFAULT_GATEWAY,
    .netmask = SETTINGS_DEFAULT_NETMASK,
    .autorun = "",
    .buttonscript = ""
};

// Helper: Save string to NVS
static esp_err_t nvs_save_str(const char *key, const char *value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

// Helper: Load string from NVS
static esp_err_t nvs_load_str(const char *key, char *buf, size_t buf_size, const char *default_val) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        strncpy(buf, default_val, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return ESP_OK; // Return OK, use default
    }

    size_t len = buf_size;
    err = nvs_get_str(handle, key, buf, &len);
    nvs_close(handle);

    if (err != ESP_OK) {
        strncpy(buf, default_val, buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
    return ESP_OK;
}

// Helper: Save int to NVS
static esp_err_t nvs_save_i32(const char *key, int32_t value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

// Helper: Load int from NVS
static int32_t nvs_load_i32(const char *key, int32_t default_val) {
    nvs_handle_t handle;
    int32_t value = default_val;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return default_val;

    err = nvs_get_i32(handle, key, &value);
    nvs_close(handle);

    if (err != ESP_OK) value = default_val;
    return value;
}

// ===== Public API ===== //

esp_err_t settings_init(void) {
    ESP_LOGI(TAG, "Initializing settings module");
    return settings_load();
}

esp_err_t settings_save(void) {
    ESP_LOGI(TAG, "Saving settings to NVS");

    esp_err_t err = nvs_save_str(NVS_KEY_SSID, g_settings.ssid);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save SSID: %s", esp_err_to_name(err));

    err = nvs_save_str(NVS_KEY_PASSWORD, g_settings.password);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save password: %s", esp_err_to_name(err));

    err = nvs_save_i32(NVS_KEY_CHANNEL, g_settings.channel);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save channel: %s", esp_err_to_name(err));

    err = nvs_save_str(NVS_KEY_IP, g_settings.ip_addr);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save IP: %s", esp_err_to_name(err));

    err = nvs_save_str(NVS_KEY_GATEWAY, g_settings.gateway);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save gateway: %s", esp_err_to_name(err));

    err = nvs_save_str(NVS_KEY_NETMASK, g_settings.netmask);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save netmask: %s", esp_err_to_name(err));

    err = nvs_save_str(NVS_KEY_AUTORUN, g_settings.autorun);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save autorun: %s", esp_err_to_name(err));

    err = nvs_save_str(NVS_KEY_BUTTONSCRIPT, g_settings.buttonscript);
    if (err != ESP_OK) ESP_LOGW(TAG, "Failed to save buttonscript: %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Settings saved: SSID=%s, Channel=%d, IP=%s",
             g_settings.ssid, g_settings.channel, g_settings.ip_addr);

    return ESP_OK;
}

esp_err_t settings_load(void) {
    ESP_LOGI(TAG, "Loading settings from NVS");

    nvs_load_str(NVS_KEY_SSID, g_settings.ssid, sizeof(g_settings.ssid), SETTINGS_DEFAULT_SSID);
    nvs_load_str(NVS_KEY_PASSWORD, g_settings.password, sizeof(g_settings.password), SETTINGS_DEFAULT_PASSWORD);
    g_settings.channel = nvs_load_i32(NVS_KEY_CHANNEL, SETTINGS_DEFAULT_CHANNEL);
    nvs_load_str(NVS_KEY_IP, g_settings.ip_addr, sizeof(g_settings.ip_addr), SETTINGS_DEFAULT_IP);
    nvs_load_str(NVS_KEY_GATEWAY, g_settings.gateway, sizeof(g_settings.gateway), SETTINGS_DEFAULT_GATEWAY);
    nvs_load_str(NVS_KEY_NETMASK, g_settings.netmask, sizeof(g_settings.netmask), SETTINGS_DEFAULT_NETMASK);
    nvs_load_str(NVS_KEY_AUTORUN, g_settings.autorun, sizeof(g_settings.autorun), "");
    nvs_load_str(NVS_KEY_BUTTONSCRIPT, g_settings.buttonscript, sizeof(g_settings.buttonscript), "");

    ESP_LOGI(TAG, "Settings loaded: SSID=%s, Channel=%d, IP=%s",
             g_settings.ssid, g_settings.channel, g_settings.ip_addr);

    return ESP_OK;
}

esp_err_t settings_reset(void) {
    ESP_LOGI(TAG, "Resetting settings to defaults");

    strncpy(g_settings.ssid, SETTINGS_DEFAULT_SSID, sizeof(g_settings.ssid) - 1);
    g_settings.ssid[sizeof(g_settings.ssid) - 1] = '\0';

    strncpy(g_settings.password, SETTINGS_DEFAULT_PASSWORD, sizeof(g_settings.password) - 1);
    g_settings.password[sizeof(g_settings.password) - 1] = '\0';

    g_settings.channel = SETTINGS_DEFAULT_CHANNEL;

    strncpy(g_settings.ip_addr, SETTINGS_DEFAULT_IP, sizeof(g_settings.ip_addr) - 1);
    g_settings.ip_addr[sizeof(g_settings.ip_addr) - 1] = '\0';

    strncpy(g_settings.gateway, SETTINGS_DEFAULT_GATEWAY, sizeof(g_settings.gateway) - 1);
    g_settings.gateway[sizeof(g_settings.gateway) - 1] = '\0';

    strncpy(g_settings.netmask, SETTINGS_DEFAULT_NETMASK, sizeof(g_settings.netmask) - 1);
    g_settings.netmask[sizeof(g_settings.netmask) - 1] = '\0';

    g_settings.autorun[0] = '\0';
    g_settings.buttonscript[0] = '\0';

    return settings_save();
}

const settings_t* settings_get(void) {
    return &g_settings;
}

// ===== Individual setters ===== //

esp_err_t settings_set_ssid(const char *ssid) {
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32) {
        ESP_LOGE(TAG, "Invalid SSID length");
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(g_settings.ssid, ssid, 32);
    g_settings.ssid[32] = '\0';
    return nvs_save_str(NVS_KEY_SSID, g_settings.ssid);
}

esp_err_t settings_set_password(const char *password) {
    if (!password) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(password);
    if (len < 8 || len > 64) {
        ESP_LOGE(TAG, "Invalid password length (must be 8-64 chars)");
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(g_settings.password, password, 64);
    g_settings.password[64] = '\0';
    return nvs_save_str(NVS_KEY_PASSWORD, g_settings.password);
}

esp_err_t settings_set_channel(int channel) {
    if (channel < 1 || channel > 13) {
        ESP_LOGE(TAG, "Invalid channel (must be 1-13)");
        return ESP_ERR_INVALID_ARG;
    }
    g_settings.channel = channel;
    return nvs_save_i32(NVS_KEY_CHANNEL, channel);
}

esp_err_t settings_set_ip(const char *ip) {
    if (!ip || strlen(ip) > 15) return ESP_ERR_INVALID_ARG;
    strncpy(g_settings.ip_addr, ip, 15);
    g_settings.ip_addr[15] = '\0';
    return nvs_save_str(NVS_KEY_IP, g_settings.ip_addr);
}

esp_err_t settings_set_gateway(const char *gateway) {
    if (!gateway || strlen(gateway) > 15) return ESP_ERR_INVALID_ARG;
    strncpy(g_settings.gateway, gateway, 15);
    g_settings.gateway[15] = '\0';
    return nvs_save_str(NVS_KEY_GATEWAY, g_settings.gateway);
}

esp_err_t settings_set_netmask(const char *netmask) {
    if (!netmask || strlen(netmask) > 15) return ESP_ERR_INVALID_ARG;
    strncpy(g_settings.netmask, netmask, 15);
    g_settings.netmask[15] = '\0';
    return nvs_save_str(NVS_KEY_NETMASK, g_settings.netmask);
}

esp_err_t settings_set_autorun(const char *autorun) {
    if (!autorun) return ESP_ERR_INVALID_ARG;
    strncpy(g_settings.autorun, autorun, 64);
    g_settings.autorun[64] = '\0';
    return nvs_save_str(NVS_KEY_AUTORUN, g_settings.autorun);
}

esp_err_t settings_set_buttonscript(const char *buttonscript) {
    if (!buttonscript) return ESP_ERR_INVALID_ARG;
    strncpy(g_settings.buttonscript, buttonscript, 64);
    g_settings.buttonscript[64] = '\0';
    return nvs_save_str(NVS_KEY_BUTTONSCRIPT, g_settings.buttonscript);
}

// ===== Individual getters ===== //

const char* settings_get_ssid(void) { return g_settings.ssid; }
const char* settings_get_password(void) { return g_settings.password; }
int settings_get_channel(void) { return g_settings.channel; }
const char* settings_get_ip(void) { return g_settings.ip_addr; }
const char* settings_get_gateway(void) { return g_settings.gateway; }
const char* settings_get_netmask(void) { return g_settings.netmask; }
const char* settings_get_autorun(void) { return g_settings.autorun; }
const char* settings_get_buttonscript(void) { return g_settings.buttonscript; }

// ===== JSON Export/Import ===== //

char* settings_to_json(char *buf, size_t buf_size) {
    snprintf(buf, buf_size,
        "{\"ssid\":\"%s\",\"password\":\"%s\",\"channel\":%d,\"ip\":\"%s\","
        "\"gateway\":\"%s\",\"netmask\":\"%s\",\"autorun\":\"%s\","
        "\"buttonscript\":\"%s\"}",
        g_settings.ssid, g_settings.password, g_settings.channel, g_settings.ip_addr,
        g_settings.gateway, g_settings.netmask, g_settings.autorun,
        g_settings.buttonscript);
    return buf;
}

// Helper: extract string value from JSON
static esp_err_t extract_json_str(const char *json, const char *key, char *buf, size_t buf_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *start = strstr(json, search);
    if (!start) return ESP_ERR_NOT_FOUND;

    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return ESP_ERR_INVALID_SIZE;

    size_t len = end - start;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';

    return ESP_OK;
}

// Helper: extract int value from JSON
static esp_err_t extract_json_int(const char *json, const char *key, int *value) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) return ESP_ERR_NOT_FOUND;

    start += strlen(search);
    *value = atoi(start);

    return ESP_OK;
}

esp_err_t settings_parse_json(const char *json) {
    // Simple JSON parser for key-value pairs
    // Format: {"key":"value","key2":123,...}

    if (!json) return ESP_ERR_INVALID_ARG;

    char buf[65];
    esp_err_t err;

    err = extract_json_str(json, "ssid", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_ssid(buf);

    err = extract_json_str(json, "password", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_password(buf);

    int channel;
    err = extract_json_int(json, "channel", &channel);
    if (err == ESP_OK) settings_set_channel(channel);

    err = extract_json_str(json, "ip", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_ip(buf);

    err = extract_json_str(json, "gateway", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_gateway(buf);

    err = extract_json_str(json, "netmask", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_netmask(buf);

    err = extract_json_str(json, "autorun", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_autorun(buf);

    err = extract_json_str(json, "buttonscript", buf, sizeof(buf));
    if (err == ESP_OK) settings_set_buttonscript(buf);

    return ESP_OK;
}

// ===== SD Card Config ===== //
// Load settings from /sdcard/config/settings.conf (overrides NVS defaults).
// Updates g_settings in RAM only – does NOT write to NVS, so deleting the SD
// file restores the original NVS values on the next boot.
// Format: key=value, one per line. # comments and blank lines are ignored.
// Unknown keys are logged at DEBUG level.
esp_err_t settings_load_from_sd(void)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGI(TAG, "SD not mounted – skipping SD config load");
        return ESP_FAIL;
    }

    FILE *f = fopen(SD_MOUNT_POINT "/config/settings.conf", "r");
    if (!f) {
        ESP_LOGI(TAG, "No /config/settings.conf found – using NVS defaults");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Loading settings from /config/settings.conf");

    char line[128];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        // Trim trailing whitespace
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                           line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = '\0';

        // Skip empty lines and comments
        if (len == 0 || line[0] == '#') continue;

        // Split at first '='
        char *eq = strchr(line, '=');
        if (!eq) continue; // no '=' → skip

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        // Trim whitespace from key and value
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;

        if (*key == '\0' || *value == '\0') continue;

        // Match known keys (case-insensitive) and update g_settings in RAM only
        if (strcasecmp(key, "ssid") == 0) {
            size_t slen = strlen(value);
            if (slen > 0 && slen < sizeof(g_settings.ssid)) {
                strncpy(g_settings.ssid, value, sizeof(g_settings.ssid) - 1);
                g_settings.ssid[sizeof(g_settings.ssid) - 1] = '\0';
                loaded++;
            }
        } else if (strcasecmp(key, "password") == 0) {
            size_t slen = strlen(value);
            if (slen >= 8 && slen < sizeof(g_settings.password)) {
                strncpy(g_settings.password, value, sizeof(g_settings.password) - 1);
                g_settings.password[sizeof(g_settings.password) - 1] = '\0';
                loaded++;
            }
        } else if (strcasecmp(key, "channel") == 0) {
            int ch = atoi(value);
            if (ch >= 1 && ch <= 13) {
                g_settings.channel = ch;
                loaded++;
            }
        } else if (strcasecmp(key, "ip") == 0) {
            size_t slen = strlen(value);
            if (slen > 0 && slen < sizeof(g_settings.ip_addr)) {
                strncpy(g_settings.ip_addr, value, sizeof(g_settings.ip_addr) - 1);
                g_settings.ip_addr[sizeof(g_settings.ip_addr) - 1] = '\0';
                loaded++;
            }
        } else if (strcasecmp(key, "gateway") == 0) {
            size_t slen = strlen(value);
            if (slen > 0 && slen < sizeof(g_settings.gateway)) {
                strncpy(g_settings.gateway, value, sizeof(g_settings.gateway) - 1);
                g_settings.gateway[sizeof(g_settings.gateway) - 1] = '\0';
                loaded++;
            }
        } else if (strcasecmp(key, "netmask") == 0) {
            size_t slen = strlen(value);
            if (slen > 0 && slen < sizeof(g_settings.netmask)) {
                strncpy(g_settings.netmask, value, sizeof(g_settings.netmask) - 1);
                g_settings.netmask[sizeof(g_settings.netmask) - 1] = '\0';
                loaded++;
            }
        } else if (strcasecmp(key, "autorun") == 0) {
            size_t slen = strlen(value);
            if (slen > 0 && slen < sizeof(g_settings.autorun)) {
                strncpy(g_settings.autorun, value, sizeof(g_settings.autorun) - 1);
                g_settings.autorun[sizeof(g_settings.autorun) - 1] = '\0';
                loaded++;
            }
        } else if (strcasecmp(key, "buttonscript") == 0) {
            size_t slen = strlen(value);
            if (slen > 0 && slen < sizeof(g_settings.buttonscript)) {
                strncpy(g_settings.buttonscript, value, sizeof(g_settings.buttonscript) - 1);
                g_settings.buttonscript[sizeof(g_settings.buttonscript) - 1] = '\0';
                loaded++;
            }
        } else {
            ESP_LOGD(TAG, "Unknown config key: %s", key);
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "SD config loaded: %d settings applied (RAM only, not saved to NVS)", loaded);
    return ESP_OK;
}
