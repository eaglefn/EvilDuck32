idf_component_register(
    SRCS "app_main.c" "storage_mode.c"
    INCLUDE_DIRS "." ".."
    REQUIRES nvs_flash esp_event display sdcard led duckyscript usb_hid usb_storage settings wifi_manager webserver script_storage driver
)
