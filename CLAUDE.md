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
├── sdcard/           ✅ SD-Card Filesystem (SDMMC, FATFS-Mount für Normalbetrieb)
├── duckyscript/      ✅ DuckyScript Interpreter (HID-Befehle)
├── usb_hid/          ✅ TinyUSB HID+RNDIS Composite (+ MSC-Descriptor für Storage-Modus)
├── usb_storage/      ✅ USB Mass Storage (SD-Karte roh als USB-Laufwerk, Storage/ARMING-Modus)
├── tinyusb_config/   ✅ tusb_config.h für TinyUSB-Konfiguration
├── attack_mode/      ⬜  Leer - Attack-Mode-Manager
├── bt_hid/           ⬜  Leer - Bluetooth HID
├── usb_rndis/        ⬜  Leer - RNDIS-Logik liegt in usb_hid/ (kein eigenes Component nötig)
├── webserver/        ⬜  Leer - HTTP Management
└── wifi_manager/     ⬜  Leer - WiFi AP
```

### Hauptanwendung (src/app_main.c)
- Boot-Seqenz: NVS → Event-Loop → LED → SD → Display → USB HID → LVGL
- Status-Screen auf Display zeigt: Modus, SD-Status, HID-Status, DuckyScript-Status
- Layout: Titel "EvilDuck32 vX.Y.Z" oben mittig, darunter mit 24px Abstand
  (`lv_obj_align_to(status_label, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 24)`) der Status-Text –
  bewusst identischer Abstand wie beim Arming-Mode-Screen (`src/storage_mode.c`) für ein
  einheitliches Erscheinungsbild zwischen Normal- und Storage/ARMING-Modus

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

## Storage/ARMING-Modus (USB Mass Storage)

Die SD-Karte kann als normales USB-Laufwerk am PC ausgelesen werden, ohne sie physisch
auszubauen – Auslöser ist die **BOOT-Taste (GPIO0)**.

**⚠️ Wichtig – Timing der Tastenabfrage:** GPIO0 ist gleichzeitig der Boot-Strap-Pin des
Chips. Wird er *während* des eigentlichen Power-On/Reset-Pulses auf LOW gehalten, interpretiert
das der ROM-Bootloader selbst als "UART/USB-Download-Modus" – die App startet dann gar nicht
(schwarzes Display, kein App-Code läuft). Die Taste darf daher **nicht beim Einstecken/Reset
gehalten** werden, sondern muss **kurz nach einem normalen Boot** gedrückt werden:

**Ablauf:**
1. Board normal einschalten/resetten (Taste dabei NICHT gedrückt halten!)
2. `app_main()` initialisiert die LED und ruft dann `storage_mode_requested()`
   (`src/storage_mode.c`) auf – das pollt GPIO0 für ein 2-Sekunden-Fenster, LED blinkt
   währenddessen Blau als Feedback (Display ist zu diesem Zeitpunkt noch nicht initialisiert)
3. Taste innerhalb dieses Fensters drücken → `run_storage_mode()` übernimmt dauerhaft, kein
   Rücksprung in den Normalbetrieb, dafür ist ein Power-Cycle nötig
4. Display zeigt kurz einen großen "ARMING MODE"-Splash (3s, zweizeilig, horizontal –
   LVGL 8.3 rotiert Label-Glyphen nicht über `transform_angle`), danach dauerhaft
   "ARMING MODE" als Kopfzeile; LED wird durchgehend Blau (`LED_STORAGE_MODE`).
   `usb_storage_init()` läuft parallel zum Splash (nicht danach), damit die
   USB-Enumeration die 3s mitnutzt statt sie draufzuschlagen
5. `usb_storage_init()` (Komponente `usb_storage`) initialisiert die SD-Karte **roh**
   (`sdmmc_card_init()`, ohne FATFS-Mount, 4-Bit-SDMMC) und schaltet TinyUSB über
   `usb_hid_init_storage_mode()` auf einen zweiten, MSC-only Descriptor um

**Architekturentscheidung – warum kein gleichzeitiger Zugriff:**
Normalbetrieb (FATFS-Mount durch die Firmware, `components/sdcard`) und Storage-Modus
(roher Blockzugriff für den Host-PC, `components/usb_storage`) sind bewusst **exklusiv**.
Gleichzeitiger Schreibzugriff von Firmware und PC auf dieselbe Karte würde das Dateisystem
korrumpieren. Deshalb ein eigener Boot-Pfad statt eines Laufzeit-Umschaltens im laufenden
Betrieb (TinyUSB-Descriptoren sind ohnehin nur bei der Enumeration fixierbar – ein Wechsel
erfordert immer ein Re-Enumerate, also praktisch einen Reset).

**Wichtige Dateien:**
- `src/storage_mode.c` – GPIO0-Check + Storage-Mode-Bildschirm/-Loop
- `components/usb_storage/usb_storage.c` – rohe SDMMC-Init + TinyUSB-MSC-Callbacks
  (`tud_msc_read10_cb`, `tud_msc_write10_cb`, `tud_msc_capacity_cb`, …)
- `components/usb_hid/usb_hid.c` – zweites Descriptor-Set (`s_config_desc_storage`,
  `s_device_desc_storage`) hinter `s_storage_mode`-Flag, `usb_hid_init_storage_mode()`

**Hardware-verifiziert (2026-09-14):**
- ⚠️ MSC-Endpoint-Größe MUSS 64 Byte sein (`CFG_TUD_MSC_EP_BUFSIZE`), nicht 512 – das Gerät
  läuft nur Full-Speed (max. 64 Byte pro Bulk-Paket laut USB-Spec). Bei 512 enumeriert das
  Gerät zwar, aber kein Host hängt einen Treiber an. SD-Sektoren (512 Byte) werden deshalb
  über mehrere 64-Byte-Callback-Aufrufe gepuffert (`offset`-Parameter in
  `tud_msc_read10_cb`/`write10_cb`)
- ⚠️ Der TinyUSB-Task im Storage-Modus braucht denselben Stack wie im Normalbetrieb
  (10240 Byte) – mit 4096 Byte stürzt er beim ersten echten SD-Zugriff ab
- ✅ Getestet: **Windows 11** (Laufwerk mit Laufwerksbuchstabe, voll funktionsfähig),
  **Linux** (funktioniert)
- ⚠️ **macOS**: Partition wird erkannt (Größe/Typ korrekt), `diskutil mount` schlägt aber
  zuverlässig fehl (kein Absturz, nur Mount-Verweigerung). Vermutlich fehlt ein von macOS'
  strengerem `msdosfs`/Mass-Storage-Stack erwarteter SCSI-Befehl. Nicht weiter verfolgt, da
  Windows/Linux die relevanten Pentest-Zielplattformen abdecken
- ⚠️ **USB-Mux teilt sich Debug-Konsole und App-USB**: Sobald `usb_hid_init_storage_mode()`
  (bzw. `usb_hid_init()`) `usb_new_phy()` mit `USB_PHY_CTRL_OTG` aufruft, schaltet der
  Board-USB-Mux vom eingebauten USB-Serial-JTAG-Debug-Log auf die native OTG-Verbindung um –
  `pio device monitor` über dasselbe Kabel ist ab diesem Zeitpunkt tot. Für Live-Logs
  während Storage-Modus daher entweder ein UART-Adapter an den physischen UART0-Pins nötig,
  oder Diagnosewerte direkt auf dem Display anzeigen (siehe Mount-Delay-Befund unten)
- ⏱️ **Mount-Delay Windows/Linux (2026-09-15, untersucht, als Hardware-Grenze akzeptiert)**:
  Nach Enumeration (Laufwerksbuchstabe da) dauert es 8-10s, bis der Host den Karteninhalt
  anzeigt. Per On-Screen-Diagnose (`tud_msc_read10_cb`-Zähler) verifiziert: der Host liest in
  dieser Zeit kontinuierlich ~7800 Sektoren (≈3,9 MB, unabhängig von Kartengröße – 4GB und
  32GB brauchten gleich lang, vermutlich ein fixer Readahead-Umfang beim Mount). Bei
  Full-Speed-USB (12 MBit/s, 64-Byte-Bulk-Pakete – siehe Eintrag oben) ergibt das nur
  ~440-490 KB/s effektiven Durchsatz (~30% des theoretischen Maximums), da 3,9 MB in
  ~62.000 einzelne 64-Byte-Transaktionen zerfallen. 4-Bit-SDMMC (statt 1-Bit) brachte KEINE
  Verbesserung → Flaschenhals ist die USB-Seite (Full-Speed-Silizium-Grenze des ESP32-S3),
  nicht die SD-Karte. Als inhärente Hardware-Grenze akzeptiert, nicht weiter optimiert.

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
| BOOT-Taste (Storage-Modus) | 0 |
