# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project: EvilDuck32

EvilDuck32 ist ein ESP32-S3-basiertes Security-Tool für USB HID-Angriffe (ähnlich Bash Bunny/USB Rubber Ducky) auf Basis des Waveshare ESP32-S3 LCD 1.47 Boards.

**Hardware:** Waveshare ESP32-S3 LCD 1.47 (172×320 Pixel, PSRAM 8MB, WiFi, Bluetooth)

---

## Build & Development Commands

```bash
# Build (aus Projektverzeichnis)
pio run

# Clean + Build
pio run -t clean && pio run

# Flashen
pio run --target upload

# Serieller Monitor
pio device monitor -b 115200

# Flash + Monitor
pio run --target upload --target monitor
```

**Wichtig:** Nach Änderungen an `sdkconfig.defaults` immer `pio run -t clean` ausführen!

---

## Architektur

### Komponenten-Struktur
```
components/
├── display/          ✅ LVGL ST7789 Display-Treiber (MADCTL=0x30 korrigiert)
├── led/              ✅ WS2812 RGB-Status-LED (GPIO 38)
├── sdcard/           ✅ SD-Card Filesystem (SDMMC)
├── duckyscript/      ✅ DuckyScript Interpreter (HID-Befehle)
├── usb_hid/          ⚠️  STUB - TinyUSB HID (nur Logging, TODO)
├── tinyusb_config/   ✅ tusb_config.h für TinyUSB-Konfiguration
├── attack_mode/      ⬜  Leer - Attack-Mode-Manager
├── bt_hid/           ⬜  Leer - Bluetooth HID
├── usb_rndis/        ⬜  Leer - USB Ethernet (RNDIS)
├── usb_storage/      ⬜  Leer - USB Mass Storage
├── webserver/        ⬜  Leer - HTTP Management
└── wifi_manager/     ⬜  Leer - WiFi AP
```

### Hauptanwendung (src/app_main.c)
- Boot-Seqenz: NVS → Event-Loop → LED → SD → Display → USB HID → LVGL
- Status-Screen auf Display zeigt: Modus, SD-Status, HID-Status, DuckyScript-Status

---

## Wichtige Konfigurationsdateien

| Datei | Zweck |
|-------|--------|
| `platformio.ini` | PlatformIO-Konfiguration (Board: waveshare_esp32s3_lcd147) |
| `sdkconfig.defaults` | ESP-IDF Default-Konfiguration (wird beim ersten Build angewendet) |
| `sdkconfig.evilduck32` | Aktive SDK-Konfiguration (wird automatisch generiert) |
| `partitions.csv` | Partition-Tabelle (Custom Table) |

---

## Bekannte Probleme & Lösungen

### Display: Spiegelverkehr / Verpixelt
**Lösung:** MADCTL=0x30 setzen (siehe `components/display/display.c` Zeile 151)
```c
const uint8_t madctl = 0x30;  // MV=1 für Waveshare Display
esp_lcd_panel_io_tx_param(io_handle, 0x36, &madctl, 1);
```

### Display: Nur 2/3 genutzt
**Lösung:** Offset in Flush-Callback korrigiert (X+34, Y ohne Offset)

### Display: Farben vertauscht (Grün wird rot)
**Ursache:** RGB Endian Konfiguration - `.rgb_endian = LCD_RGB_ENDIAN_RGB` tauscht Rot/Blau
**Workaround:** `lv_color_hex(0x0000FF)` verwenden für grünen Text (Blau im RGB-Format → Grün bei BGR-Anzeige)
**Lösung:** `LCD_RGB_ENDIAN_BGR` in `display.c` Zeile 141 verwenden

### TinyUSB: tusb_config.h nicht gefunden
**Lösung:** `components/tinyusb_config/include/tusb_config.h` mit korrekten Werten:
- `CFG_TUD_ENDPOINT_MAX=6` (nicht 8)
- `CFG_TUSB_MCU` NICHT definieren (wird von ESP-IDF gesetzt)

### Locale-Typenkonflikt (macOS)
**Lösung:** `locale_t` → `hid_locale_t` umbenannt, `locale.h` → `hid_locale.h`

### USB HID Stub
**Aktueller Status:** Vollständig implementiert – Composite HID + RNDIS. RNDIS ist standardmäßig inaktiv (siehe ATTACKMODE-Philosophie).

---

## DuckyScript-Interpreter

Unterstützte Befehle (siehe `components/duckyscript/duckyscript.c`):
- `REM`, `DELAY`, `DEFAULT_DELAY`, `STRING`, `ENTER`
- `GUI`, `CTRL`, `ALT`, `SHIFT`, `SPACE`, `TAB`, `ESC`
- `LOCALE`, `LED`, `VAR`, `REPEAT`, `KEYCODE`
- `ATTACKMODE` (siehe unten)

Skripte aus NVS (primär) oder SD-Karte `/sdcard/scripts/*.txt` (Fallback)

### ATTACKMODE – Bash Bunny Philosophie

Das Gerät startet standardmäßig als **reines HID-Gerät** (Tastatur). RNDIS (USB-Ethernet) ist
erst aktiv, wenn das Script es explizit anfordert – analog zum Bash Bunny.

| Befehl | Verhalten |
|--------|-----------|
| `ATTACKMODE HID` | No-op – HID ist immer aktiv |
| `ATTACKMODE RNDIS [timeout_ms]` | RNDIS aktivieren + warten bis Host verbunden |
| `ATTACKMODE HID RNDIS [timeout_ms]` | Wie oben (explizite Bash Bunny-Syntax) |

**Technische Details:**
- USB-Descriptor enthält immer beide Interfaces (HID + RNDIS) – kein USB-Reconnect nötig
- RNDIS-Netzwerk-Interface (lwIP) wird erst nach `ATTACKMODE ... RNDIS` registriert
- Standard-Timeout: 30 000 ms (0 = unbegrenzt warten)
- LED-Feedback: Blau = wartet, Grün = verbunden, Rot = Timeout

**Beispiel-Script:**
```
ATTACKMODE HID RNDIS 15000
DELAY 2000
STRING powershell -c "..."
ENTER
```

---

## LVGL-Konfiguration

Aktivierte Schriftarten: Montserrat 14, 18, 20 (siehe `sdkconfig.evilduck32`)
- Um Schriftarten zu ändern: `sdkconfig.evilduck32` direkt editieren, NICHT `sdkconfig.defaults`
- Danach `pio run -t clean` ausführen!

**Performance Monitor:**
- Aktiviert durch `CONFIG_LV_USE_PERF_MONITOR=y` in `sdkconfig.evilduck32`
- Zeigt FPS (Frames Per Second) und CPU-Last am unteren Bildschirmrand
- Für Produktion deaktivieren: `# CONFIG_LV_USE_PERF_MONITOR is not set`

---

## Speicher & Partitionen

**Flash-Speicher:** 16 MB (genug für Firmware + Skripte + zukünftige Features)
- Firmware: ~0.5 MB (aktuelle Größe)
- Frei: ~15+ MB

**Partition Table (partitions.csv):**
- `nvs`: 24 KB (Non-Volatile Storage für Einstellungen)
- `phy_init`: 4 KB (PHY-Initialisierungsdaten)
- `factory`: 4 MB (Anwendung)
- `storage`: 1 MB FAT (SD-Card-emuliertes Dateisystem im Flash)

**Hinweis:** Die SD-Card wird in `storage`-Partition gemountet (nicht physikalische SD)

## GPIO-Pins (Waveshare ESP32-S3 LCD 1.47)

| Funktion | Pin |
|----------|-----|
| LCD MOSI  | 45 |
| LCD SCLK  | 40 |
| LCD DC    | 41 |
| LCD RST   | 39 |
| LCD CS    | 42 |
| LCD BL    | 48 |
| LED (WS2812) | 38 |
| SD_CLK   | 14 |
| SD_CMD   | 15 |
| SD_D0    | 16 |
| SD_D1    | 18 |
| SD_D2    | 17 |
| SD_D3    | 21 |
