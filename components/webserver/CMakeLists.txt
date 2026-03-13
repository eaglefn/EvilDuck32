idf_component_register(
    SRCS "webserver.c"
    INCLUDE_DIRS "include"
    REQUIRES sdcard duckyscript wifi_manager usb_hid settings script_storage esp_wifi esp_netif esp_event lwip
)
