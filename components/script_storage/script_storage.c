#include "script_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "script_storage";

// NVS handle opened against NVS_SCRIPT_NS namespace
static nvs_handle_t s_nvs_handle;

// NVS key format: "s_<name>" where name is the sanitized filename without .txt extension.
// Max NVS key length is 15 chars. Prefix "s_" = 2 chars → max 13 chars for the name part.
#define KEY_PREFIX     "s_"
#define KEY_PREFIX_LEN 2
#define KEY_NAME_MAX   13  // 15 - 2

// Normalize filename to lowercase in-place
static void str_tolower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

// Case-insensitive check for .txt / .TXT extension
static bool has_txt_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 5) return false;
    const char *e = name + len - 4;
    return e[0] == '.' &&
           (e[1] == 't' || e[1] == 'T') &&
           (e[2] == 'x' || e[2] == 'X') &&
           (e[3] == 't' || e[3] == 'T');
}

// Sanitize filename into a NVS-safe key suffix (no special chars, no .txt, max KEY_NAME_MAX chars).
static void make_key(const char *filename, char *key, size_t key_size)
{
    // Copy and sanitize
    char name[KEY_NAME_MAX + 1];
    size_t i = 0;
    for (; i < KEY_NAME_MAX && filename[i] != '\0'; i++) {
        char c = filename[i];
        if (c == ' ' || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            name[i] = '_';
        } else {
            name[i] = c;
        }
    }
    name[i] = '\0';

    // Strip .txt / .TXT extension if present (case-insensitive)
    size_t len = strlen(name);
    if (has_txt_extension(name)) {
        name[len - 4] = '\0';
        len -= 4;
    }

    // Truncate to KEY_NAME_MAX chars (already limited above, but be safe)
    if (len > KEY_NAME_MAX) {
        name[KEY_NAME_MAX] = '\0';
    }

    // Lowercase: NVS keys are case-sensitive; always use lowercase for consistency
    str_tolower(name);

    snprintf(key, key_size, KEY_PREFIX "%s", name);
}

// Derive display filename from NVS key (strip "s_" prefix, append ".txt").
static void key_to_filename(const char *key, char *filename, size_t filename_size)
{
    // skip "s_" prefix
    const char *name = key + KEY_PREFIX_LEN;
    snprintf(filename, filename_size, "%s.txt", name);
}

esp_err_t script_storage_init(void)
{
    ESP_LOGI(TAG, "Initializing script storage (namespace: %s)", NVS_SCRIPT_NS);

    // nvs_open opens a namespace within the default NVS partition
    esp_err_t err = nvs_open(NVS_SCRIPT_NS, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", NVS_SCRIPT_NS, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Script storage initialized");
    return ESP_OK;
}

esp_err_t script_storage_save(const char *name, const char *content)
{
    if (!name || !content) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    make_key(name, key, sizeof(key));

    ESP_LOGI(TAG, "Saving script '%s' as key '%s' (%d bytes)", name, key, strlen(content));

    esp_err_t err = nvs_set_str(s_nvs_handle, key, content);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Script saved: '%s'", name);
    return ESP_OK;
}

esp_err_t script_storage_load(const char *name, char *content, size_t content_size)
{
    if (!name || !content) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    make_key(name, key, sizeof(key));

    ESP_LOGI(TAG, "Loading script '%s' (key '%s')", name, key);

    size_t required_size = content_size;
    esp_err_t err = nvs_get_str(s_nvs_handle, key, content, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Script not found: '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "Script '%s' too large for buffer (%d bytes available)", name, content_size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Script loaded: '%s' (%d bytes)", name, required_size);
    return ESP_OK;
}

esp_err_t script_storage_delete(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    make_key(name, key, sizeof(key));

    ESP_LOGI(TAG, "Deleting script '%s' (key '%s')", name, key);

    esp_err_t err = nvs_erase_key(s_nvs_handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Script not found: '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Script deleted: '%s'", name);
    return ESP_OK;
}

int script_storage_list(char names[][MAX_SCRIPT_NAME], int max_count)
{
    if (!names || max_count <= 0) {
        return 0;
    }

    int count = 0;
    nvs_iterator_t it = NULL;

    // Bug fix: first arg is the NVS *partition* name ("nvs"), second is the *namespace* ("scripts").
    // Using NVS_PART_NAME and NVS_SCRIPT_NS avoids the previous confusion where the FAT
    // partition "storage" was accidentally passed as partition name.
    esp_err_t err = nvs_entry_find(NVS_PART_NAME, NVS_SCRIPT_NS, NVS_TYPE_STR, &it);

    while (err == ESP_OK && it != NULL && count < max_count) {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) break;

        // Only collect keys that start with our prefix
        if (strncmp(info.key, KEY_PREFIX, KEY_PREFIX_LEN) == 0) {
            char display_name[MAX_SCRIPT_NAME];
            key_to_filename(info.key, display_name, sizeof(display_name));
            strncpy(names[count], display_name, MAX_SCRIPT_NAME - 1);
            names[count][MAX_SCRIPT_NAME - 1] = '\0';
            count++;
        }

        err = nvs_entry_next(&it);
    }

    if (it != NULL) {
        nvs_release_iterator(it);
    }

    ESP_LOGI(TAG, "Listed %d scripts", count);
    return count;
}

bool script_storage_exists(const char *name)
{
    if (!name) return false;

    char key[16];
    make_key(name, key, sizeof(key));

    size_t required_size = 0;
    esp_err_t err = nvs_get_str(s_nvs_handle, key, NULL, &required_size);
    return (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH);
}

const char* script_storage_get_autorun(void)
{
    static char autorun_script[MAX_SCRIPT_NAME] = "";
    size_t len = sizeof(autorun_script);

    if (nvs_get_str(s_nvs_handle, NVS_KEY_AUTORUN, autorun_script, &len) != ESP_OK) {
        autorun_script[0] = '\0';
    }

    return autorun_script[0] != '\0' ? autorun_script : NULL;
}

esp_err_t script_storage_set_autorun(const char *name)
{
    if (!name) {
        return script_storage_clear_autorun();
    }

    ESP_LOGI(TAG, "Setting autorun: %s", name);

    esp_err_t err = nvs_set_str(s_nvs_handle, NVS_KEY_AUTORUN, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set autorun: %s", esp_err_to_name(err));
        return err;
    }

    return nvs_commit(s_nvs_handle);
}

esp_err_t script_storage_clear_autorun(void)
{
    ESP_LOGI(TAG, "Clearing autorun");

    esp_err_t err = nvs_erase_key(s_nvs_handle, NVS_KEY_AUTORUN);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to clear autorun: %s", esp_err_to_name(err));
        return err;
    }

    return nvs_commit(s_nvs_handle);
}

int script_storage_count(void)
{
    char names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    return script_storage_list(names, MAX_SCRIPTS);
}

esp_err_t script_storage_get_all(script_storage_entry_t *scripts, int max_count, int *actual_count)
{
    if (!scripts || !actual_count) {
        return ESP_ERR_INVALID_ARG;
    }

    char names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    int count = script_storage_list(names, max_count);

    *actual_count = 0;
    for (int i = 0; i < count && i < max_count; i++) {
        strncpy(scripts[i].name, names[i], MAX_SCRIPT_NAME - 1);
        scripts[i].name[MAX_SCRIPT_NAME - 1] = '\0';

        esp_err_t err = script_storage_load(names[i], scripts[i].content, MAX_SCRIPT_SIZE);
        scripts[i].used = (err == ESP_OK);
        if (scripts[i].used) {
            (*actual_count)++;
        }
    }

    return ESP_OK;
}

// ===== Sync Implementation =====

// Returns the byte size of a script in NVS (0 if not found).
static size_t nvs_script_size(const char *name)
{
    char key[16];
    make_key(name, key, sizeof(key));
    size_t sz = 0;
    esp_err_t err = nvs_get_str(s_nvs_handle, key, NULL, &sz);
    if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) {
        // sz includes the null terminator – subtract 1 for content length
        return sz > 0 ? sz - 1 : 0;
    }
    return 0;
}

// Returns the byte size of a file on SD (0 if not found).
static size_t sd_file_size(const char *filename)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SD_PAYLOADS_DIR, filename);
    struct stat st;
    if (stat(path, &st) == 0) {
        return (size_t)st.st_size;
    }
    return 0;
}

esp_err_t script_storage_sync_preview(sync_preview_t *preview)
{
    if (!preview) return ESP_ERR_INVALID_ARG;
    memset(preview, 0, sizeof(sync_preview_t));

    // --- Collect NVS scripts ---
    char nvs_names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    int nvs_count = script_storage_list(nvs_names, MAX_SCRIPTS);

    // --- Collect SD scripts ---
    char sd_names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    int sd_count = 0;

    DIR *dir = opendir(SD_PAYLOADS_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && sd_count < MAX_SCRIPTS) {
            // Skip . and .. and hidden files
            if (entry->d_name[0] == '.') continue;
            // Only consider .txt / .TXT files (case-insensitive).
            // Note: do NOT check d_type – FATFS VFS returns DT_UNKNOWN for all entries.
            if (!has_txt_extension(entry->d_name)) continue;
            // Normalize to lowercase so NVS key comparison works correctly
            // (NVS keys are case-sensitive; SD FAT may return UPPERCASE names)
            strncpy(sd_names[sd_count], entry->d_name, MAX_SCRIPT_NAME - 1);
            sd_names[sd_count][MAX_SCRIPT_NAME - 1] = '\0';
            str_tolower(sd_names[sd_count]);
            sd_count++;
        }
        closedir(dir);
    } else {
        ESP_LOGW(TAG, "sync_preview: cannot open %s (SD not mounted?)", SD_PAYLOADS_DIR);
    }

    // --- NVS scripts: check if each exists on SD ---
    for (int i = 0; i < nvs_count; i++) {
        sync_entry_t *e = &preview->entries[preview->count];
        strncpy(e->name, nvs_names[i], MAX_SCRIPT_NAME - 1);
        e->name[MAX_SCRIPT_NAME - 1] = '\0';
        e->nvs_size = nvs_script_size(nvs_names[i]);

        // Check SD
        e->sd_size = sd_file_size(nvs_names[i]);

        if (e->sd_size == 0) {
            // Not on SD → export
            e->action = SYNC_ACTION_EXPORT;
            preview->to_export++;
        } else if (e->nvs_size == e->sd_size) {
            // Same size → assume equal, no action
            e->action = SYNC_ACTION_EQUAL;
        } else {
            // Different size → conflict
            e->action = SYNC_ACTION_CONFLICT;
            preview->conflicts++;
        }
        preview->count++;
    }

    // --- SD scripts: find those not already in NVS list ---
    for (int i = 0; i < sd_count; i++) {
        // Check if this SD file is already covered by an NVS entry above
        bool already_listed = false;
        for (int j = 0; j < nvs_count; j++) {
            if (strcmp(sd_names[i], nvs_names[j]) == 0) {
                already_listed = true;
                break;
            }
        }
        if (already_listed) continue;

        if (preview->count >= MAX_SCRIPTS * 2) break;

        sync_entry_t *e = &preview->entries[preview->count];
        strncpy(e->name, sd_names[i], MAX_SCRIPT_NAME - 1);
        e->name[MAX_SCRIPT_NAME - 1] = '\0';
        e->nvs_size = 0;
        e->sd_size = sd_file_size(sd_names[i]);
        e->action = SYNC_ACTION_IMPORT;
        preview->to_import++;
        preview->count++;
    }

    ESP_LOGI(TAG, "Sync preview: %d export, %d import, %d conflicts, %d equal",
             preview->to_export, preview->to_import, preview->conflicts,
             preview->count - preview->to_export - preview->to_import - preview->conflicts);
    return ESP_OK;
}

esp_err_t script_storage_sync_execute(const sync_preview_t *preview,
                                      const conflict_resolution_t *resolutions,
                                      int res_count,
                                      int *out_synced,
                                      int *out_skipped)
{
    if (!preview) return ESP_ERR_INVALID_ARG;

    int synced = 0;
    int skipped = 0;
    static char buf[MAX_SCRIPT_SIZE + 1];

    for (int i = 0; i < preview->count; i++) {
        const sync_entry_t *e = &preview->entries[i];

        if (e->action == SYNC_ACTION_EQUAL) {
            skipped++;
            continue;
        }

        if (e->action == SYNC_ACTION_EXPORT) {
            // NVS → SD
            esp_err_t err = script_storage_load(e->name, buf, sizeof(buf));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "sync export: failed to load '%s' from NVS", e->name);
                skipped++;
                continue;
            }
            char path[128];
            snprintf(path, sizeof(path), "%s/%s", SD_PAYLOADS_DIR, e->name);
            FILE *f = fopen(path, "w");
            if (!f) {
                ESP_LOGE(TAG, "sync export: cannot open '%s' for writing", path);
                skipped++;
                continue;
            }
            fwrite(buf, 1, strlen(buf), f);
            fflush(f);
            fclose(f);
            ESP_LOGI(TAG, "sync: exported '%s' to SD", e->name);
            synced++;
            continue;
        }

        if (e->action == SYNC_ACTION_IMPORT) {
            // SD → NVS
            char path[128];
            snprintf(path, sizeof(path), "%s/%s", SD_PAYLOADS_DIR, e->name);
            FILE *f = fopen(path, "r");
            if (!f) {
                ESP_LOGE(TAG, "sync import: cannot open '%s'", path);
                skipped++;
                continue;
            }
            size_t read = fread(buf, 1, MAX_SCRIPT_SIZE, f);
            fclose(f);
            buf[read] = '\0';

            esp_err_t err = script_storage_save(e->name, buf);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "sync import: failed to save '%s' to NVS", e->name);
                skipped++;
                continue;
            }
            ESP_LOGI(TAG, "sync: imported '%s' from SD", e->name);
            synced++;
            continue;
        }

        if (e->action == SYNC_ACTION_CONFLICT) {
            // Find conflict resolution for this file
            conflict_resolve_t resolve = CONFLICT_SKIP;
            for (int r = 0; r < res_count; r++) {
                if (strcmp(resolutions[r].name, e->name) == 0) {
                    resolve = resolutions[r].resolve;
                    break;
                }
            }

            if (resolve == CONFLICT_SKIP) {
                ESP_LOGI(TAG, "sync: skipping conflict '%s'", e->name);
                skipped++;
                continue;
            }

            if (resolve == CONFLICT_NVS_WINS) {
                // NVS → SD (overwrite)
                esp_err_t err = script_storage_load(e->name, buf, sizeof(buf));
                if (err != ESP_OK) { skipped++; continue; }
                char path[128];
                snprintf(path, sizeof(path), "%s/%s", SD_PAYLOADS_DIR, e->name);
                FILE *f = fopen(path, "w");
                if (!f) { skipped++; continue; }
                fwrite(buf, 1, strlen(buf), f);
                fflush(f);
                fclose(f);
                ESP_LOGI(TAG, "sync: conflict resolved (NVS wins) '%s'", e->name);
                synced++;
            } else if (resolve == CONFLICT_SD_WINS) {
                // SD → NVS (overwrite)
                char path[128];
                snprintf(path, sizeof(path), "%s/%s", SD_PAYLOADS_DIR, e->name);
                FILE *f = fopen(path, "r");
                if (!f) { skipped++; continue; }
                size_t read = fread(buf, 1, MAX_SCRIPT_SIZE, f);
                fclose(f);
                buf[read] = '\0';
                esp_err_t err = script_storage_save(e->name, buf);
                if (err != ESP_OK) { skipped++; continue; }
                ESP_LOGI(TAG, "sync: conflict resolved (SD wins) '%s'", e->name);
                synced++;
            }
        }
    }

    if (out_synced)  *out_synced  = synced;
    if (out_skipped) *out_skipped = skipped;

    ESP_LOGI(TAG, "Sync complete: %d synced, %d skipped", synced, skipped);
    return ESP_OK;
}
