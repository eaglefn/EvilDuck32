idf_component_register(
    SRCS "usb_hid.c" "hid_keycodes.c"
         "locale_de.c" "locale_us.c" "locale_gb.c" "locale_fr.c"
         "locale_es.c" "locale_it.c" "locale_pt.c" "locale_dk.c"
         "locale_be.c" "locale_ru.c" "locale_si.c" "locale_sk.c"
         "locale_cz.c" "locale_hu.c" "locale_cafr.c" "locale_chde.c"
         "locale_chfr.c" "locale_bg.c"
    INCLUDE_DIRS "include"
    REQUIRES tinyusb_config espressif__tinyusb usb lwip
)
