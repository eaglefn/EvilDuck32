#include "duckyscript.h"
#include "usb_hid.h"
#include "led.h"
#include "sdcard.h"
#include "script_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "DuckyScript";

#define MAX_LINE_LENGTH   256   // max length for command + short args (DELAY, GUI, etc.)
#define MAX_STRING_LENGTH 1024  // max length for STRING argument + raw line buffer
#define MAX_ARGS 8
#define DEFAULT_DELAY_MS 100

// ── VAR support ──────────────────────────────────────────────
// VAR $NAME = value   →  $NAME is substituted in STRING commands
#define MAX_VARS      16
#define MAX_VAR_NAME  32
#define MAX_VAR_VALUE 256

typedef struct {
    char name[MAX_VAR_NAME];   // includes the leading $
    char value[MAX_VAR_VALUE];
    bool used;
} ducky_var_t;

static ducky_var_t s_vars[MAX_VARS];

// Script context
static struct {
    char current_file[64];
    FILE *current_fp;
    int current_line;
    int total_lines;
    ducky_state_t state;
    uint32_t default_delay;
    bool stop_requested;
    bool pause_requested;
} ctx = {
    .default_delay = DEFAULT_DELAY_MS,
    .state = DUCKY_STATE_IDLE,
    .stop_requested = false,
    .pause_requested = false
};

// Command parsing
typedef struct {
    char *cmd;
    char *args[MAX_ARGS];
    int arg_count;
    char raw_args[MAX_STRING_LENGTH]; // original args string (preserves spacing for STRING)
} command_t;

// Storage for REPEAT: copy of last executed command + its backing buffer
static command_t s_last_cmd;
static char      s_last_cmd_buf[MAX_LINE_LENGTH];
static char      s_last_raw_args[MAX_STRING_LENGTH];
static bool      s_has_last_cmd = false;

// ============================================================
// VAR helpers
// ============================================================

// Replace all occurrences of known $VAR names in src, write result to dst.
// Only substitutes names defined via VAR command – PowerShell's own $vars are untouched.
static void var_substitute(const char *src, char *dst, size_t dst_size)
{
    const char *p = src;
    size_t out = 0;

    while (*p && out < dst_size - 1) {
        if (*p == '$') {
            // Try to match a known variable name starting at p
            bool matched = false;
            for (int i = 0; i < MAX_VARS; i++) {
                if (!s_vars[i].used) continue;
                size_t nlen = strlen(s_vars[i].name);
                if (strncmp(p, s_vars[i].name, nlen) == 0) {
                    // Make sure it's not a substring of a longer identifier
                    char next = p[nlen];
                    if (!isalnum((unsigned char)next) && next != '_' && next != ':') {
                        size_t vlen = strlen(s_vars[i].value);
                        if (out + vlen < dst_size - 1) {
                            memcpy(dst + out, s_vars[i].value, vlen);
                            out += vlen;
                        }
                        p += nlen;
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) dst[out++] = *p++;
        } else {
            dst[out++] = *p++;
        }
    }
    dst[out] = '\0';
}

// ============================================================
// String helpers
// ============================================================

static char *trim(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static void to_lower(char *str) {
    for (; *str; ++str) *str = tolower((unsigned char)*str);
}

static void to_upper_copy(const char *src, char *dst, size_t dst_size) {
    size_t i;
    for (i = 0; i < dst_size - 1 && src[i]; i++)
        dst[i] = toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

// ============================================================
// Key name / modifier lookup helpers
// ============================================================

// Map a key name string (case-insensitive) to a HID keycode.
// Returns HID_KEY_NONE if not found.
static uint8_t parse_key_name(const char *name) {
    char up[32];
    to_upper_copy(name, up, sizeof(up));

    // Single letter a-z / A-Z
    if (strlen(up) == 1) {
        char c = up[0];
        if (c >= 'A' && c <= 'Z') return HID_KEY_A + (c - 'A');
        if (c == '0') return HID_KEY_0;
        if (c >= '1' && c <= '9') return HID_KEY_1 + (c - '1');
    }

    // Two-digit number 10-99 (not used as standalone keys, but handle cleanly)
    // Named keys
    if (strcmp(up, "ENTER") == 0 || strcmp(up, "RETURN") == 0) return HID_KEY_ENTER;
    if (strcmp(up, "ESC") == 0 || strcmp(up, "ESCAPE") == 0) return HID_KEY_ESCAPE;
    if (strcmp(up, "TAB") == 0) return HID_KEY_TAB;
    if (strcmp(up, "SPACE") == 0) return HID_KEY_SPACE;
    if (strcmp(up, "BACKSPACE") == 0) return HID_KEY_BACKSPACE;
    if (strcmp(up, "DELETE") == 0 || strcmp(up, "DEL") == 0) return HID_KEY_DELETE;
    if (strcmp(up, "INSERT") == 0) return HID_KEY_INSERT;
    if (strcmp(up, "HOME") == 0) return HID_KEY_HOME;
    if (strcmp(up, "END") == 0) return HID_KEY_END;
    if (strcmp(up, "PAGEUP") == 0 || strcmp(up, "PAGE_UP") == 0) return HID_KEY_PAGE_UP;
    if (strcmp(up, "PAGEDOWN") == 0 || strcmp(up, "PAGE_DOWN") == 0) return HID_KEY_PAGE_DOWN;
    if (strcmp(up, "UP") == 0 || strcmp(up, "UPARROW") == 0) return HID_KEY_ARROW_UP;
    if (strcmp(up, "DOWN") == 0 || strcmp(up, "DOWNARROW") == 0) return HID_KEY_ARROW_DOWN;
    if (strcmp(up, "LEFT") == 0 || strcmp(up, "LEFTARROW") == 0) return HID_KEY_ARROW_LEFT;
    if (strcmp(up, "RIGHT") == 0 || strcmp(up, "RIGHTARROW") == 0) return HID_KEY_ARROW_RIGHT;
    if (strcmp(up, "F1") == 0)  return HID_KEY_F1;
    if (strcmp(up, "F2") == 0)  return HID_KEY_F2;
    if (strcmp(up, "F3") == 0)  return HID_KEY_F3;
    if (strcmp(up, "F4") == 0)  return HID_KEY_F4;
    if (strcmp(up, "F5") == 0)  return HID_KEY_F5;
    if (strcmp(up, "F6") == 0)  return HID_KEY_F6;
    if (strcmp(up, "F7") == 0)  return HID_KEY_F7;
    if (strcmp(up, "F8") == 0)  return HID_KEY_F8;
    if (strcmp(up, "F9") == 0)  return HID_KEY_F9;
    if (strcmp(up, "F10") == 0) return HID_KEY_F10;
    if (strcmp(up, "F11") == 0) return HID_KEY_F11;
    if (strcmp(up, "F12") == 0) return HID_KEY_F12;
    if (strcmp(up, "CAPSLOCK") == 0) return HID_KEY_CAPS_LOCK;
    if (strcmp(up, "NUMLOCK") == 0) return HID_KEY_NUM_LOCK;
    if (strcmp(up, "SCROLLLOCK") == 0) return HID_KEY_SCROLL_LOCK;
    if (strcmp(up, "PRINTSCREEN") == 0) return HID_KEY_PRINT_SCREEN;
    if (strcmp(up, "PAUSE") == 0 || strcmp(up, "BREAK") == 0) return HID_KEY_PAUSE;
    if (strcmp(up, "MENU") == 0 || strcmp(up, "APP") == 0) return HID_KEY_APPLICATION;

    return HID_KEY_NONE;
}

// Map a modifier name string to modifier bits. Returns 0 if not a modifier.
static uint8_t parse_modifier_name(const char *name) {
    char up[32];
    to_upper_copy(name, up, sizeof(up));
    if (strcmp(up, "CTRL") == 0 || strcmp(up, "CONTROL") == 0) return KEY_MOD_LCTRL;
    if (strcmp(up, "ALT") == 0) return KEY_MOD_LALT;
    if (strcmp(up, "SHIFT") == 0) return KEY_MOD_LSHIFT;
    if (strcmp(up, "GUI") == 0 || strcmp(up, "WINDOWS") == 0) return KEY_MOD_LMETA;
    return 0;
}

// ============================================================
// Command parsing
// ============================================================

static bool parse_command(char *line, command_t *cmd) {
    memset(cmd, 0, sizeof(command_t));

    char *trimmed = trim(line);
    if (trimmed[0] == '\0' || trimmed[0] == '#') {
        return false; // empty line or comment
    }

    // Save raw args (everything after the command word, leading whitespace stripped)
    // Must happen before strtok() modifies the buffer.
    char *space = trimmed;
    while (*space && !isspace((unsigned char)*space)) space++;
    if (*space) {
        char *raw = space;
        while (isspace((unsigned char)*raw)) raw++;
        strncpy(cmd->raw_args, raw, MAX_STRING_LENGTH - 1);
        cmd->raw_args[MAX_STRING_LENGTH - 1] = '\0';
    }

    // Tokenise: first token = command name, rest = args
    char *token = strtok(trimmed, " \t");
    if (!token) return false;

    cmd->cmd = token;
    to_lower(cmd->cmd);

    while ((token = strtok(NULL, " \t")) && cmd->arg_count < MAX_ARGS) {
        cmd->args[cmd->arg_count++] = token;
    }

    return true;
}

// ============================================================
// Low-level key helpers
// ============================================================

static void press_and_release(uint8_t keycode, uint8_t modifier) {
    usb_hid_keyboard_press(keycode, modifier);
    vTaskDelay(pdMS_TO_TICKS(50));
    usb_hid_keyboard_release();
}

// Execute a modifier+key combo from a command whose first word is a modifier.
// Handles multi-modifier syntax like: CTRL ALT DELETE  or  GUI r
static esp_err_t execute_modifier_combo(const command_t *cmd) {
    // The command name itself is the first modifier
    uint8_t modifiers = parse_modifier_name(cmd->cmd);
    uint8_t key = HID_KEY_NONE;

    for (int i = 0; i < cmd->arg_count; i++) {
        uint8_t m = parse_modifier_name(cmd->args[i]);
        if (m) {
            modifiers |= m; // additional modifier
        } else {
            key = parse_key_name(cmd->args[i]); // final key
            break;
        }
    }

    if (key != HID_KEY_NONE) {
        press_and_release(key, modifiers);
    } else {
        // Modifier alone (no key argument, or key not found)
        usb_hid_keyboard_raw_report(modifiers, (uint8_t[]){HID_KEY_NONE, 0, 0, 0, 0, 0});
        vTaskDelay(pdMS_TO_TICKS(50));
        usb_hid_keyboard_release();
    }
    return ESP_OK;
}

// ============================================================
// Command execution
// ============================================================

static esp_err_t execute_command(const command_t *cmd);

// Save a copy of the last command so REPEAT can re-execute it.
// Patches arg pointers to point into the copy buffer.
static void save_last_cmd(const command_t *cmd) {
    // Copy the backing raw buffer (trimmed line that args point into)
    // We need the offsets, but we no longer have the original buffer here.
    // Instead we rebuild from cmd->cmd and cmd->args.
    memset(s_last_cmd_buf, 0, sizeof(s_last_cmd_buf));
    memset(&s_last_cmd, 0, sizeof(s_last_cmd));
    memcpy(s_last_raw_args, cmd->raw_args, sizeof(s_last_raw_args));

    // Write "cmd arg0 arg1 ..." into s_last_cmd_buf, then re-tokenise
    int pos = 0;
    int len = snprintf(s_last_cmd_buf + pos, sizeof(s_last_cmd_buf) - pos, "%s", cmd->cmd);
    pos += len;
    for (int i = 0; i < cmd->arg_count && pos < (int)sizeof(s_last_cmd_buf) - 2; i++) {
        s_last_cmd_buf[pos++] = ' ';
        len = snprintf(s_last_cmd_buf + pos, sizeof(s_last_cmd_buf) - pos, "%s", cmd->args[i]);
        pos += len;
    }

    // Re-tokenise
    char *token = strtok(s_last_cmd_buf, " \t");
    if (!token) return;
    s_last_cmd.cmd = token;
    while ((token = strtok(NULL, " \t")) && s_last_cmd.arg_count < MAX_ARGS) {
        s_last_cmd.args[s_last_cmd.arg_count++] = token;
    }
    memcpy(s_last_cmd.raw_args, s_last_raw_args, MAX_STRING_LENGTH);
    s_has_last_cmd = true;
}

static esp_err_t execute_command(const command_t *cmd) {
    ESP_LOGD(TAG, "Exec: %s (args: %d)", cmd->cmd, cmd->arg_count);

    // ── REM ─────────────────────────────────────────────────
    if (strcmp(cmd->cmd, "rem") == 0) {
        return ESP_OK;
    }

    // ── DELAY ───────────────────────────────────────────────
    if (strcmp(cmd->cmd, "delay") == 0) {
        if (cmd->arg_count < 1) return ESP_ERR_INVALID_ARG;
        vTaskDelay(pdMS_TO_TICKS(atoi(cmd->args[0])));
        return ESP_OK;
    }

    // ── DEFAULT_DELAY ────────────────────────────────────────
    if (strcmp(cmd->cmd, "default_delay") == 0 || strcmp(cmd->cmd, "defaultdelay") == 0) {
        if (cmd->arg_count < 1) return ESP_ERR_INVALID_ARG;
        ctx.default_delay = (uint32_t)atoi(cmd->args[0]);
        return ESP_OK;
    }

    // ── VAR ─────────────────────────────────────────────────
    // VAR $NAME = value  →  defines a substitution variable
    if (strcmp(cmd->cmd, "var") == 0) {
        // raw_args = "$NAME = value"  or  "$NAME=value"
        char *ra = cmd->raw_args;
        while (isspace((unsigned char)*ra)) ra++;
        if (*ra != '$') {
            ESP_LOGW(TAG, "VAR: name must start with $");
            return ESP_ERR_INVALID_ARG;
        }
        // Extract name (up to '=' or whitespace)
        char name[MAX_VAR_NAME];
        int ni = 0;
        while (*ra && *ra != '=' && !isspace((unsigned char)*ra) && ni < MAX_VAR_NAME - 1)
            name[ni++] = *ra++;
        name[ni] = '\0';
        // Skip whitespace and '='
        while (isspace((unsigned char)*ra)) ra++;
        if (*ra == '=') ra++;
        while (isspace((unsigned char)*ra)) ra++;

        // Find or create slot
        int slot = -1;
        for (int i = 0; i < MAX_VARS; i++) {
            if (s_vars[i].used && strcmp(s_vars[i].name, name) == 0) { slot = i; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < MAX_VARS; i++) {
                if (!s_vars[i].used) { slot = i; break; }
            }
        }
        if (slot < 0) {
            ESP_LOGW(TAG, "VAR: table full (max %d)", MAX_VARS);
            return ESP_FAIL;
        }
        strncpy(s_vars[slot].name,  name, MAX_VAR_NAME  - 1);
        strncpy(s_vars[slot].value, ra,   MAX_VAR_VALUE - 1);
        s_vars[slot].name[MAX_VAR_NAME   - 1] = '\0';
        s_vars[slot].value[MAX_VAR_VALUE - 1] = '\0';
        s_vars[slot].used = true;
        ESP_LOGI(TAG, "VAR %s = %s", s_vars[slot].name, s_vars[slot].value);
        return ESP_OK;
    }

    // ── STRING ──────────────────────────────────────────────
    // Uses raw_args to preserve original spacing.
    // $VAR names defined with VAR command are substituted before typing.
    if (strcmp(cmd->cmd, "string") == 0) {
        if (cmd->raw_args[0] == '\0') return ESP_ERR_INVALID_ARG;
        static char s_expanded[MAX_STRING_LENGTH];
        var_substitute(cmd->raw_args, s_expanded, sizeof(s_expanded));
        usb_hid_keyboard_type_string(s_expanded);
        save_last_cmd(cmd);
        return ESP_OK;
    }

    // ── LOCALE ──────────────────────────────────────────────
    if (strcmp(cmd->cmd, "locale") == 0) {
        if (cmd->arg_count < 1) return ESP_ERR_INVALID_ARG;
        char up[16];
        to_upper_copy(cmd->args[0], up, sizeof(up));
        if (!hid_locale_set_by_name(up)) {
            ESP_LOGW(TAG, "LOCALE: unknown locale '%s'", up);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "LOCALE set to %s", up);
        save_last_cmd(cmd);
        return ESP_OK;
    }

    // ── LED ─────────────────────────────────────────────────
    if (strcmp(cmd->cmd, "led") == 0) {
        if (cmd->arg_count < 3) return ESP_ERR_INVALID_ARG;
        uint8_t r = (uint8_t)atoi(cmd->args[0]);
        uint8_t g = (uint8_t)atoi(cmd->args[1]);
        uint8_t b = (uint8_t)atoi(cmd->args[2]);
        led_set_color(r, g, b);
        save_last_cmd(cmd);
        return ESP_OK;
    }

    // ── KEYCODE ─────────────────────────────────────────────
    // KEYCODE <modifier> <key1> [key2] [key3] [key4] [key5] [key6]
    if (strcmp(cmd->cmd, "keycode") == 0) {
        if (cmd->arg_count < 1) return ESP_ERR_INVALID_ARG;
        uint8_t mod = (uint8_t)atoi(cmd->args[0]);
        uint8_t keys[6] = {0};
        for (int i = 0; i < 6 && (i + 1) < cmd->arg_count; i++) {
            keys[i] = (uint8_t)atoi(cmd->args[i + 1]);
        }
        usb_hid_keyboard_raw_report(mod, keys);
        vTaskDelay(pdMS_TO_TICKS(50));
        usb_hid_keyboard_release();
        save_last_cmd(cmd);
        return ESP_OK;
    }

    // ── REPEAT ──────────────────────────────────────────────
    // REPEAT <n>  – repeat the last command n more times
    if (strcmp(cmd->cmd, "repeat") == 0 || strcmp(cmd->cmd, "replay") == 0) {
        if (!s_has_last_cmd) {
            ESP_LOGW(TAG, "REPEAT: no previous command");
            return ESP_OK;
        }
        int n = (cmd->arg_count >= 1) ? atoi(cmd->args[0]) : 1;
        for (int i = 0; i < n; i++) {
            execute_command(&s_last_cmd);
            if (ctx.default_delay > 0) vTaskDelay(pdMS_TO_TICKS(ctx.default_delay));
        }
        return ESP_OK;
    }

    // ── ATTACKMODE ──────────────────────────────────────────
    // Bash Bunny-style: payload declares which interfaces it needs.
    //
    // ATTACKMODE HID              – HID-only (default, no-op)
    // ATTACKMODE RNDIS [ms]       – enable RNDIS + wait for host connection
    // ATTACKMODE HID RNDIS [ms]   – enable RNDIS + wait for host connection
    //
    // Default timeout: 30 000 ms (0 = wait forever)
    if (strcmp(cmd->cmd, "attackmode") == 0) {
        bool want_rndis = false;
        uint32_t timeout_ms = 30000;

        // Scan all arguments: collect mode flags and optional numeric timeout
        for (int i = 0; i < cmd->arg_count; i++) {
            char up[32];
            to_upper_copy(cmd->args[i], up, sizeof(up));
            if (strcmp(up, "RNDIS") == 0) {
                want_rndis = true;
            } else if (strcmp(up, "HID") == 0) {
                // HID is always active – no action needed
            } else {
                // Treat numeric arguments as timeout value
                int val = atoi(cmd->args[i]);
                if (val > 0) timeout_ms = (uint32_t)val;
            }
        }

        if (want_rndis) {
            ESP_LOGI(TAG, "ATTACKMODE HID RNDIS – enabling RNDIS (timeout %lu ms)",
                     (unsigned long)timeout_ms);
            usb_hid_enable_rndis();
            led_set_color(0, 0, 50); // blue = waiting for host

            uint32_t elapsed = 0;
            const uint32_t step = 500;
            while (!usb_net_is_connected()) {
                if (timeout_ms > 0 && elapsed >= timeout_ms) {
                    ESP_LOGW(TAG, "ATTACKMODE RNDIS: timeout after %lu ms", (unsigned long)elapsed);
                    led_set_color(50, 0, 0); // red = failed
                    return ESP_ERR_TIMEOUT;
                }
                vTaskDelay(pdMS_TO_TICKS(step));
                elapsed += step;
            }

            ESP_LOGI(TAG, "ATTACKMODE RNDIS: host connected after %lu ms",
                     (unsigned long)elapsed);
            led_set_color(0, 50, 0); // green = ready
        } else {
            ESP_LOGI(TAG, "ATTACKMODE HID – already active");
        }
        return ESP_OK;
    }

    // ── Standalone keys ─────────────────────────────────────
    // These commands ARE their own key name.
    {
        static const struct { const char *name; uint8_t key; } standalone[] = {
            { "enter",       HID_KEY_ENTER       },
            { "return",      HID_KEY_ENTER       },
            { "space",       HID_KEY_SPACE       },
            { "tab",         HID_KEY_TAB         },
            { "esc",         HID_KEY_ESCAPE      },
            { "escape",      HID_KEY_ESCAPE      },
            { "backspace",   HID_KEY_BACKSPACE   },
            { "delete",      HID_KEY_DELETE      },
            { "del",         HID_KEY_DELETE      },
            { "insert",      HID_KEY_INSERT      },
            { "home",        HID_KEY_HOME        },
            { "end",         HID_KEY_END         },
            { "pageup",      HID_KEY_PAGE_UP     },
            { "page_up",     HID_KEY_PAGE_UP     },
            { "pagedown",    HID_KEY_PAGE_DOWN   },
            { "page_down",   HID_KEY_PAGE_DOWN   },
            { "uparrow",     HID_KEY_ARROW_UP    },
            { "up",          HID_KEY_ARROW_UP    },
            { "downarrow",   HID_KEY_ARROW_DOWN  },
            { "down",        HID_KEY_ARROW_DOWN  },
            { "leftarrow",   HID_KEY_ARROW_LEFT  },
            { "left",        HID_KEY_ARROW_LEFT  },
            { "rightarrow",  HID_KEY_ARROW_RIGHT },
            { "right",       HID_KEY_ARROW_RIGHT },
            { "f1",          HID_KEY_F1          },
            { "f2",          HID_KEY_F2          },
            { "f3",          HID_KEY_F3          },
            { "f4",          HID_KEY_F4          },
            { "f5",          HID_KEY_F5          },
            { "f6",          HID_KEY_F6          },
            { "f7",          HID_KEY_F7          },
            { "f8",          HID_KEY_F8          },
            { "f9",          HID_KEY_F9          },
            { "f10",         HID_KEY_F10         },
            { "f11",         HID_KEY_F11         },
            { "f12",         HID_KEY_F12         },
            { "capslock",    HID_KEY_CAPS_LOCK   },
            { "numlock",     HID_KEY_NUM_LOCK    },
            { "scrolllock",  HID_KEY_SCROLL_LOCK },
            { "printscreen", HID_KEY_PRINT_SCREEN},
            { "pause",       HID_KEY_PAUSE       },
            { "break",       HID_KEY_PAUSE       },
            { "menu",        HID_KEY_APPLICATION },
            { "app",         HID_KEY_APPLICATION },
        };
        for (size_t i = 0; i < sizeof(standalone) / sizeof(standalone[0]); i++) {
            if (strcmp(cmd->cmd, standalone[i].name) == 0) {
                press_and_release(standalone[i].key, 0);
                save_last_cmd(cmd);
                return ESP_OK;
            }
        }
    }

    // ── Modifier combos: CTRL, ALT, SHIFT, GUI/WINDOWS ──────
    if (parse_modifier_name(cmd->cmd) != 0) {
        esp_err_t ret = execute_modifier_combo(cmd);
        save_last_cmd(cmd);
        return ret;
    }

    ESP_LOGW(TAG, "Unknown command: %s", cmd->cmd);
    return ESP_ERR_NOT_SUPPORTED;
}

// ============================================================
// Script execution task
// ============================================================

static void script_task(void *arg) {
    char line[MAX_STRING_LENGTH];
    command_t cmd;

    ESP_LOGI(TAG, "Starting script: %s", ctx.current_file);
    ctx.state = DUCKY_STATE_RUNNING;
    ctx.current_line = 0;

    while (fgets(line, sizeof(line), ctx.current_fp)) {
        if (ctx.stop_requested) {
            ESP_LOGI(TAG, "Script stopped at line %d", ctx.current_line);
            ctx.state = DUCKY_STATE_IDLE;
            break;
        }

        while (ctx.pause_requested) {
            ctx.state = DUCKY_STATE_PAUSED;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ctx.state = DUCKY_STATE_RUNNING;

        ctx.current_line++;

        if (parse_command(line, &cmd)) {
            esp_err_t ret = execute_command(&cmd);
            if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGE(TAG, "Error at line %d", ctx.current_line);
            }
        }

        if (ctx.default_delay > 0) {
            vTaskDelay(pdMS_TO_TICKS(ctx.default_delay));
        }
    }

    fclose(ctx.current_fp);
    ctx.current_fp = NULL;
    ctx.state = DUCKY_STATE_FINISHED;

    ESP_LOGI(TAG, "Script finished: %s (%d lines)", ctx.current_file, ctx.current_line);
    vTaskDelete(NULL);
}

// ============================================================
// Public API
// ============================================================

esp_err_t ducky_init(void) {
    ESP_LOGI(TAG, "Initializing DuckyScript engine");
    memset(&ctx, 0, sizeof(ctx));
    ctx.default_delay = DEFAULT_DELAY_MS;
    ctx.state = DUCKY_STATE_IDLE;
    s_has_last_cmd = false;
    memset(s_vars, 0, sizeof(s_vars));
    return ESP_OK;
}

// Static buffer for NVS-loaded scripts. Safe because only one script runs at a time.
static char s_script_buf[MAX_SCRIPT_SIZE + 1];

esp_err_t ducky_run_script(const char *filename) {
    if (ctx.state == DUCKY_STATE_RUNNING || ctx.state == DUCKY_STATE_PAUSED) {
        ESP_LOGW(TAG, "Script already running");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *fp = NULL;

    // Primary: load from NVS script storage
    esp_err_t ret = script_storage_load(filename, s_script_buf, sizeof(s_script_buf));
    if (ret == ESP_OK) {
        fp = fmemopen(s_script_buf, strlen(s_script_buf), "r");
        if (fp) ESP_LOGI(TAG, "Running script from NVS: %s", filename);
    }

    // Fallback: SD card
    if (!fp && sdcard_is_mounted()) {
        char path[128];
        snprintf(path, sizeof(path), "/sdcard/scripts/%s", filename);
        fp = fopen(path, "r");
        if (fp) ESP_LOGI(TAG, "Running script from SD: %s", path);
    }

    if (!fp) {
        ESP_LOGE(TAG, "Script not found: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }

    ctx.current_fp = fp;
    strncpy(ctx.current_file, filename, sizeof(ctx.current_file) - 1);
    ctx.current_file[sizeof(ctx.current_file) - 1] = '\0';
    ctx.stop_requested = false;
    ctx.pause_requested = false;

    // Priority 1 (low) – USB tasks must be able to preempt HID typing
    BaseType_t xret = xTaskCreate(script_task, "ducky_script", 8192, NULL, 1, NULL);
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create script task");
        fclose(ctx.current_fp);
        ctx.current_fp = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t ducky_stop(void) {
    if (ctx.state != DUCKY_STATE_RUNNING && ctx.state != DUCKY_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    ctx.stop_requested = true;
    return ESP_OK;
}

esp_err_t ducky_pause(void) {
    if (ctx.state != DUCKY_STATE_RUNNING) return ESP_ERR_INVALID_STATE;
    ctx.pause_requested = true;
    return ESP_OK;
}

esp_err_t ducky_resume(void) {
    if (ctx.state != DUCKY_STATE_PAUSED) return ESP_ERR_INVALID_STATE;
    ctx.pause_requested = false;
    return ESP_OK;
}

bool ducky_get_info(ducky_script_info_t *info) {
    if (!info) return false;
    strncpy(info->filename, ctx.current_file, sizeof(info->filename) - 1);
    info->current_line = ctx.current_line;
    info->total_lines  = ctx.total_lines;
    info->state        = ctx.state;
    info->delay_ms     = ctx.default_delay;
    return true;
}

bool ducky_is_running(void) {
    return ctx.state == DUCKY_STATE_RUNNING || ctx.state == DUCKY_STATE_PAUSED;
}

void ducky_set_default_delay(uint32_t delay_ms) {
    ctx.default_delay = delay_ms;
}
