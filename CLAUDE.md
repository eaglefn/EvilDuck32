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

**Versionierung:** Einzige Quelle der Wahrheit ist `EVILDUCK32_VERSION` in
`components/settings/include/settings.h` – wird sowohl im Display-Titel (`src/app_main.c`)
als auch in der Web-UI (`/api/status`, Nav-Leiste) verwendet, damit beide nie auseinanderlaufen.
Fertige `firmware.bin`-Builds werden zusätzlich manuell unter `firmware/v<X.Y.Z>/firmware.bin`
abgelegt (lokales Archiv, nicht in Git getrackt) – bei einer neuen Version: Konstante bumpen,
`pio run`, Ergebnis in einen neuen `firmware/vX.Y.Z/`-Unterordner kopieren.

---

## Architektur

### Komponenten-Struktur
```
components/
├── display/          ✅ LVGL ST7789 Display-Treiber (MADCTL=0x30 korrigiert)
├── led/              ✅ WS2812 RGB-Status-LED (GPIO 38)
├── sdcard/           ✅ SD-Card Filesystem (SDMMC, FATFS-Mount für Normalbetrieb)
├── duckyscript/      ✅ DuckyScript Interpreter (HID-Befehle, inkl. RUN-Skriptverkettung)
├── script_storage/   ✅ Skript-Speicherung in NVS (Slot-Schema, siehe eigene Sektion unten)
├── settings/         ✅ Einstellungen in NVS + EVILDUCK32_VERSION-Konstante
├── usb_hid/          ✅ TinyUSB HID+RNDIS Composite (+ MSC-Descriptor für Storage-Modus)
├── usb_storage/      ✅ USB Mass Storage (SD-Karte roh als USB-Laufwerk, Storage/ARMING-Modus)
├── tinyusb_config/   ✅ tusb_config.h für TinyUSB-Konfiguration
├── wifi_manager/     ✅ WiFi AP (SSID/Passwort in settings, Default-Gateway 192.168.4.1)
├── webserver/        ✅ HTTP-Server + Web-UI (Scripts, Sync, Settings, OTA-Update, Commands-Doku, SD-Browser mit View/Download/Delete)
├── attack_mode/      ⬜  Leer - Attack-Mode-Manager
├── bt_hid/           ⬜  Leer - Bluetooth HID
└── usb_rndis/        ⬜  Leer - RNDIS-Logik liegt in usb_hid/ (kein eigenes Component nötig)
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

### OTA-Update: "Malformed upload" / Firmware-Datei korrumpiert
**Ursache:** Beim Streaming-Multipart-Parser (`handle_api_update_stream()` in `webserver.c`)
war das Trailing-Window, das die schließende Multipart-Boundary (`\r\n--<boundary>--\r\n`)
zurückhält, exakt so groß wie der gesuchte Marker `\r\n--<boundary>` selbst
(`window_len = strlen(boundary) + 4`). Dadurch wurde der Marker schon ins Flash geschrieben,
bevor das abschließende `--\r\n` überhaupt ankam – am Ende fehlte der Marker im Fenster.
**Lösung:** Fenster großzügiger bemessen (`strlen(boundary) + 16`), damit die komplette
schließende Sequenz inklusive `--\r\n` sicher im Fenster bleibt (`webserver.c`,
`handle_api_update_stream()`).

### Web-UI: Button zeigt "Erfolg", obwohl der Request fehlgeschlagen ist
**Wiederkehrendes Muster** (bereits einmal bei `saveScript()` aufgetreten, dann erneut bei
`uploadFirmware()`): JS-`fetch()`-Aufrufe, die nur `.then(...)` ohne `response.ok`-Prüfung
verwenden, zeigen eine Erfolgsmeldung selbst bei HTTP 400/500. **Beim Hinzufügen neuer
Web-UI-Aktionen immer `r.ok`/`r.status` prüfen** (Vorlage: `saveScript()`/`deleteScript()` in
`webserver.c`).

### SD-Card-Browser: Dritte Action "Delete" (v1.4.2)
Der SD-Browser zeigt pro Datei drei Buttons: view, download (⬇) und delete (🗑). Die Delete-
Action basiert auf `confirm()` + `fetch('/api/sdcard/delete?path=...', {method:'DELETE'})`
(mit `r.ok`-Prüfung nach dem Muster oben) und ruft danach `refreshSDCard()`. Server-seitig
löst `handle_api_sdcard_delete()` (Route `DELETE /api/sdcard/delete`): Mount-Check (503),
`path`-Parsing + URL-Decode, leeren Pfad/`..`-Traversal → 400, dann `sdcard_delete_file()`
(`components/sdcard`), Erfolg → 200 `{"status":"deleted"}`. Nur Dateien, keine Ordner.

### Web-UI: Eingebettete HTML/JS-Strings – fehlende C-String-Quotes (Build-Fehler)
Die komplette Web-UI liegt als C-String-Literale in `webserver.c`. Beim Verlängern solcher
String-Muster (z.B. ein weiterer Action-Button in der `actions`-Zeile des SD-Browsers) darf
das **eröffnende oder schließende `"` des C-Literals** nie fehlen. Das Muster pro Segment:
`"…(\\\"'"` – C-String auf, dann `\\\"` (C-Escape: `\\` → Backslash, `\"` → literales `"` im JS),
dann `'` (JS-String-Ende), dann `"` (C-String zu).
Fehlt eines davon, bricht der Build mit `error: missing terminating " character` sofort
und mit Zeilenangabe – die Segmente werden dann wie Multi-Character-Literale interpretiert.
Erkennt man gut, wenn eine neue Zeile mit `'\\"` statt `"'\\"` beginnt oder mit `\\\"'` statt
`\\\"'"` endet. (2026-09-25 bei v1.4.2 zweimal passiert und gefixt.)

### Skript-Dateiname wird beim Speichern verstümmelt (z.B. "module_rot.tx.txt")
**Ursache (behoben 2026-09-17):** Die alte NVS-Speicherung leitete den Key direkt aus dem
Dateinamen ab (`s_<name>`, auf 13 Zeichen gekürzt wegen des 15-Zeichen-NVS-Key-Limits) und
hängte beim Anzeigen immer `.txt` wieder an. Bei Namen nahe der 13-Zeichen-Grenze wurde die
`.txt`-Endung selbst mit abgeschnitten (z.B. zu `.tx`), die Endungs-Erkennung griff dann nicht
mehr, und beim Anzeigen kam ein zweites `.txt` obendrauf.
**Lösung:** Komplett neues Speicherschema, siehe [Script Storage (NVS)](#script-storage-nvs)
unten.

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

## OTA Firmware-Update (Web-UI) – implementiert (Stand 2026-09-17)

Die Web-UI hat einen "Firmware Update"-Bereich (Datei-Upload + "Upload & Reboot"-Button,
`components/webserver/webserver.c` HTML/JS `uploadFirmware()`), der die `.bin`-Datei per
`multipart/form-data` an `POST /api/update` schickt. Der Server-Endpunkt ist voll
implementiert (`handle_api_update_stream()` in `webserver.c`).

**Architektur:**
- `partitions.csv` hat jetzt `ota_0`/`ota_1` (je 4 MB) + `otadata` statt der einzelnen
  `factory`-Partition. War ein Breaking Change am Partition-Layout – bereits geflasht,
  inkl. dem dafür nötigen Full-Chip-Erase.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.evilduck32`: bootet die neue
  Firmware nicht durch, rollt der Bootloader automatisch auf den vorherigen Slot zurück.
  Dafür markiert `app_main()` (`src/app_main.c`, Ende der Boot-Sequenz) den aktuellen Slot
  per `esp_ota_mark_app_valid_cancel_rollback()` als gültig, sobald der Bootvorgang normal
  durchgelaufen ist.
- `handle_client()` (`webserver.c`) fängt `POST /api/update` ab, **bevor** der generische
  16-KB-Static-Buffer versucht den kompletten Body zu puffern (der reicht nicht für eine
  ~1,3-MB-Firmware-Datei). `handle_api_update_stream()` liest den Rest des Sockets danach
  selbst in 2-KB-Chunks (`OTA_RECV_CHUNK`) und schreibt sie direkt per `esp_ota_write()` in
  die inaktive OTA-Partition (`esp_ota_get_next_update_partition()`).
- Die abschließende Multipart-Boundary-Markierung (`\r\n--<boundary>--\r\n`) darf nicht mit
  in die Firmware geschrieben werden. Da der Body nie komplett im Speicher liegt, hält
  `ota_stream_feed()` immer ein kleines Fenster (Boundary-Länge + 4 Byte, `OTA_TAIL_MAX`)
  der zuletzt empfangenen Bytes zurück und schreibt nur ältere Bytes weg – am Ende steht die
  Boundary-Markierung in diesem Fenster und wird dort erkannt/abgeschnitten statt geflasht.
- Ablauf: `esp_ota_begin()` → gestreamtes `esp_ota_write()` → `esp_ota_end()` (validiert
  Image-Header/Checksumme) → `esp_ota_set_boot_partition()` → Response senden → 1s warten
  (damit die Antwort den Client noch erreicht) → `esp_restart()`.

**Wichtige Dateien:**
- `partitions.csv` – `ota_0`/`ota_1`/`otadata` statt `factory`
- `components/webserver/webserver.c` – `handle_api_update_stream()`, `ota_stream_feed()`
- `src/app_main.c` – `esp_ota_mark_app_valid_cancel_rollback()`

---

## SD Card Config (v1.4.4)

Die Datei `/config/settings.conf` auf der SD-Karte überschreibt beim Boot `g_settings` **nur im RAM**, nicht in NVS. Wird die Datei gelöscht, greifen beim nächsten Boot die alten NVS-Werte.

**Format:**
```
ssid=MeinAP
password=MeinPasswort
channel=1
ip=192.168.4.1
autorun=start.txt
```

- Keys: ssid, password, channel, ip, gateway, netmask, autorun, buttonscript (case-insensitiv)
- `#`-Kommentare und Leerzeilen werden ignoriert
- Validation direkt in `settings_load_from_sd()` (keine NVS-Setter)

**Wichtige Änderung zu v1.4.4:** `wifi_manager_start()` liest jetzt `settings_get_ssid()` etc. statt Compile-Time-Macros (`WIFI_AP_SSID`). Dadurch wirkt sich die SD-Config auch auf den WLAN-AP aus.

---

## Script Storage (NVS)

**Implementiert 2026-09-17** (löst das oben dokumentierte Dateinamen-Verstümmelungsproblem):
Skripte liegen in NVS in **festen Slots** (`components/script_storage/script_storage.c`),
nicht mehr in namensbasierten Keys:

- `s_<slot>` (Slot 0 bis `MAX_SCRIPTS-1` = 19) – der eigentliche Skript-Inhalt
- `n_<slot>` – der **exakte** Original-Dateiname als NVS-*Value* (nicht Key!), beliebige Länge
  bis `MAX_SCRIPT_NAME` (32 Zeichen), beliebige oder gar keine Datei-Endung

**Warum Slots statt namensbasierter Keys:** NVS-Keys sind auf 15 Zeichen begrenzt – zu kurz,
um beliebige Dateinamen direkt als Key zu codieren. Der Name landet stattdessen als Value
unter einem kurzen, namensunabhängigen Key (`n_0` .. `n_19`), wodurch weder Kürzungs- noch
Kollisionsprobleme bei langen Namen auftreten. Ein früherer Versuch, den Namen zusätzlich zu
einem namensbasierten Key zu speichern (`s_<name>_name`), scheiterte historisch am selben
15-Zeichen-Limit (siehe Git-Historie) – der Slot-Ansatz umgeht das komplett, da der Key nie
vom Namen abhängt.

**Lookup:** `find_slot_by_name()` iteriert alle 20 Slots und vergleicht `n_<slot>` case-
insensitiv mit dem gesuchten Namen. `find_free_slot()` liefert den ersten Slot ohne
`n_<slot>`-Eintrag für neue Skripte. Die öffentliche API (`script_storage_save/load/delete/
exists/list`, `components/script_storage/include/script_storage.h`) ist unverändert
namensbasiert – Aufrufer (Web-UI, DuckyScript-Interpreter) merken von der Umstellung nichts.

**⚠️ Migration:** Skripte, die unter dem alten Schema gespeichert wurden, werden vom neuen
Code nicht mehr gefunden (andere Key-Struktur) – sie bleiben als verwaiste NVS-Einträge
liegen (kein Datenverlust, aber unsichtbar). Vor einem Firmware-Update auf diesen Stand ggf.
erst über die Web-UI unter "Sync" → "Backup" auf die SD-Karte sichern und danach per "Sync"
wieder importieren.

**Web-UI:** Erzwingt beim Anlegen (`createScript()` in `webserver.c`) keine `.txt`-Endung
mehr – der eingegebene Name wird 1:1 übernommen.

---

## DuckyScript-Interpreter

Unterstützte Befehle (siehe `components/duckyscript/duckyscript.c`):
- `REM`, `DELAY`, `DEFAULT_DELAY`, `STRING`, `ENTER`
- `GUI`, `CTRL`, `ALT`, `SHIFT`, `SPACE`, `TAB`, `ESC`
- `LOCALE`, `LED`, `VAR`, `REPEAT`, `KEYCODE`, `RUN` (siehe unten)
- `ATTACKMODE` (siehe unten)

Skripte aus NVS (primär) oder SD-Karte `/sdcard/scripts/*.txt` (Fallback)

### RUN – Skript-Verkettung

`RUN <filename>` lädt beim Erreichen dieser Zeile nahtlos ein weiteres Skript nach und führt
es im selben Task weiter aus, statt zu terminieren – üblicherweise als letzte Zeile eines
Skripts eingesetzt, um eine Kette von Skripten ohne erneuten Start über die Web-UI
auszuführen. Restliche Zeilen nach `RUN` im aktuellen Skript werden übersprungen
(`script_task()` bricht die Zeilenschleife ab, sobald `ctx.run_next` gesetzt ist).
`STOP` funktioniert weiterhin auch mitten in einer Kette. Keine Schutzmaßnahme gegen
Endlosketten (A ruft B ruft A...) – `STOP` bleibt dafür der Ausweg.

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
## Script Logging (v1.4.4)

Jede Skript-Ausführung erzeugt eine Logdatei in `/logs/<scriptname>_<boot_s>.log`.

**Inhalt:**
```
= EvilDuck32 Script Log =
Script: loot_windows.txt
Start: boot +5123ms
---
[+00000] LINE    1 | REM Windows Loot
[+15230] LINE    3 | ATTACKMODE RNDIS 15000
---
End: boot +18345ms
Status: FINISHED
```

**Auto-Cleanup:** Wenn freier Speicher < 512 KB, werden alte Logs gelöscht (5 neueste bleiben).

**⚠️ Stack-Overflow-Gefahr:** `sdcard_dir_list_t` (~41 KB) ist zu groß für den 8-KB-`script_task`-Stack. Wird per `malloc()` auf dem Heap allokiert (`ducky_log_cleanup()` in `duckyscript.c`).

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
- Firmware: ~1,3 MB (aktuelle Größe)
- Frei: ~6,9 MB (ungenutzt hinter `storage`)

**Partition Table (partitions.csv):** – seit OTA-Implementierung zwei App-Slots statt
einer einzelnen `factory`-Partition (siehe [OTA Firmware-Update](#ota-firmware-update-web-ui--implementiert-stand-2026-09-17))
- `nvs`: 24 KB (Non-Volatile Storage für Einstellungen)
- `otadata`: 8 KB (aktiver OTA-Slot, von `esp_ota_set_boot_partition()` verwaltet)
- `phy_init`: 4 KB (PHY-Initialisierungsdaten)
- `ota_0` / `ota_1`: je 4 MB (zwei App-Slots, Firmware-Updates wechseln zwischen beiden)
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
