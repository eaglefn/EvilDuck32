idf_component_register(
    SRCS "usb_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES sdmmc sdcard usb_hid tinyusb_config espressif__tinyusb
)
