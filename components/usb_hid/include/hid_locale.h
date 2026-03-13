#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "usb_hid_keys.h"

typedef struct {
    const uint8_t *ascii;           // 128 entries x 2 bytes: [modifier, keycode]
    uint8_t        ascii_len;       // always 128
    const uint8_t *utf8;            // N x 6 bytes: [b0,b1,b2,b3, modifier, keycode]
    size_t         utf8_len;        // number of entries
    const uint8_t *combinations;    // N x 8 bytes: [b0,b1,b2,b3, comb_mod, comb_key, mod, key]
    size_t         combinations_len;
    const char    *name;
} hid_locale_t;

// All available locales
extern const hid_locale_t hid_locale_de;
extern const hid_locale_t hid_locale_us;
extern const hid_locale_t hid_locale_gb;
extern const hid_locale_t hid_locale_fr;
extern const hid_locale_t hid_locale_es;
extern const hid_locale_t hid_locale_it;
extern const hid_locale_t hid_locale_pt;
extern const hid_locale_t hid_locale_dk;
extern const hid_locale_t hid_locale_be;
extern const hid_locale_t hid_locale_ru;
extern const hid_locale_t hid_locale_si;
extern const hid_locale_t hid_locale_sk;
extern const hid_locale_t hid_locale_cz;
extern const hid_locale_t hid_locale_hu;
extern const hid_locale_t hid_locale_cafr;
extern const hid_locale_t hid_locale_chde;
extern const hid_locale_t hid_locale_chfr;
extern const hid_locale_t hid_locale_bg;

void hid_locale_set(const hid_locale_t *loc);
const hid_locale_t *hid_locale_get(void);
bool hid_locale_set_by_name(const char *name);
