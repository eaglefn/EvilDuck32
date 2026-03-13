idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_lcd esp_timer driver
    PRIV_REQUIRES lvgl
)
