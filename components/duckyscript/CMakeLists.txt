idf_component_register(
    SRCS "duckyscript.c"
    INCLUDE_DIRS "include"
    REQUIRES usb_hid sdcard script_storage led esp_timer
)
