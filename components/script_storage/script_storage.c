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

// Scripts are stored in fixed slots (0..MAX_SCRIPTS-1) instead of name-derived NVS keys.
// NVS keys are capped at 15 chars, which is too short to hold arbitrary filenames (any
// length, any or no extension) directly - a prior design derived the key from a sanitized,
// truncated copy of the name and reconstructed the display name by re-appending ".txt",
// which both collided on longer names and mangled names that were already exactly at the
// truncation boundary (e.g. "module_rot.txt" -> truncated to "module_rot.tx" -> extension
// no longer recognized -> displayed back as "module_rot.tx.txt").
//
// Instead, "s_<slot>" holds the script content and "n_<slot>" holds the exact original
// filename as a separate value (not a key, so MAX_SCRIPT_NAME is the only limit) - the
// filename round-trips exactly as typed, with whatever extension or none at all.
static void slot_content_key(int slot, char *key, size_t key_size) {
    snprintf(key, key_size, "s_%d", slot);
}
static void slot_name_key(int slot, char *key, size_t key_size) {
    snprintf(key, key_size, "n_%d", slot);
}

// Normalize filename to lowercase in-place
static void str_tolower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static bool names_equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

// Finds the slot currently holding `name` (case-insensitive), or -1 if none.
static int find_slot_by_name(const char *name)
{
    for (int slot = 0; slot < MAX_SCRIPTS; slot++) {
        char key[8];
        slot_name_key(slot, key, sizeof(key));
        char stored[MAX_SCRIPT_NAME];
        size_t len = sizeof(stored);
        if (nvs_get_str(s_nvs_handle, key, stored, &len) == ESP_OK &&
            names_equal_ci(stored, name)) {
            return slot;
        }
    }
    return -1;
}

// Finds the first slot with no name entry (free), or -1 if all MAX_SCRIPTS slots are used.
static int find_free_slot(void)
{
    for (int slot = 0; slot < MAX_SCRIPTS; slot++) {
        char key[8];
        slot_name_key(slot, key, sizeof(key));
        size_t len = 0;
        if (nvs_get_str(s_nvs_handle, key, NULL, &len) == ESP_ERR_NVS_NOT_FOUND) {
            return slot;
        }
    }
    return -1;
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
    if (!name || !content || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    int slot = find_slot_by_name(name);
    if (slot < 0) {
        slot = find_free_slot();
        if (slot < 0) {
            ESP_LOGE(TAG, "Script storage full (max %d scripts)", MAX_SCRIPTS);
            return ESP_ERR_NO_MEM;
        }
    }

    char content_key[8], name_key[8];
    slot_content_key(slot, content_key, sizeof(content_key));
    slot_name_key(slot, name_key, sizeof(name_key));

    ESP_LOGI(TAG, "Saving script '%s' to slot %d (%d bytes)", name, slot, (int)strlen(content));

    esp_err_t err = nvs_set_str(s_nvs_handle, content_key, content);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str (content) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(s_nvs_handle, name_key, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str (name) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Script saved: '%s' (slot %d)", name, slot);
    return ESP_OK;
}

esp_err_t script_storage_load(const char *name, char *content, size_t content_size)
{
    if (!name || !content) {
        return ESP_ERR_INVALID_ARG;
    }

    int slot = find_slot_by_name(name);
    if (slot < 0) {
        ESP_LOGW(TAG, "Script not found: '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }

    char key[8];
    slot_content_key(slot, key, sizeof(key));

    ESP_LOGI(TAG, "Loading script '%s' (slot %d)", name, slot);

    size_t required_size = content_size;
    esp_err_t err = nvs_get_str(s_nvs_handle, key, content, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Script not found: '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "Script '%s' too large for buffer (%d bytes available)", name, (int)content_size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Script loaded: '%s' (%d bytes)", name, (int)required_size);
    return ESP_OK;
}

esp_err_t script_storage_delete(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }

    int slot = find_slot_by_name(name);
    if (slot < 0) {
        ESP_LOGW(TAG, "Script not found: '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }

    char content_key[8], name_key[8];
    slot_content_key(slot, content_key, sizeof(content_key));
    slot_name_key(slot, name_key, sizeof(name_key));

    ESP_LOGI(TAG, "Deleting script '%s' (slot %d)", name, slot);

    esp_err_t err = nvs_erase_key(s_nvs_handle, content_key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_erase_key (content) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_erase_key(s_nvs_handle, name_key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_erase_key (name) failed: %s", esp_err_to_name(err));
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
    for (int slot = 0; slot < MAX_SCRIPTS && count < max_count; slot++) {
        char key[8];
        slot_name_key(slot, key, sizeof(key));
        size_t len = MAX_SCRIPT_NAME;
        if (nvs_get_str(s_nvs_handle, key, names[count], &len) == ESP_OK) {
            count++;
        }
    }

    ESP_LOGI(TAG, "Listed %d scripts", count);
    return count;
}

bool script_storage_exists(const char *name)
{
    if (!name) return false;
    return find_slot_by_name(name) >= 0;
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
    int slot = find_slot_by_name(name);
    if (slot < 0) return 0;

    char key[8];
    slot_content_key(slot, key, sizeof(key));
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
            // Any non-hidden file is a script – the NVS slot scheme stores exact
            // filenames with arbitrary or no extension, so sync must treat every
            // file in the scripts directory as a script (not just .txt).
            // Note: do NOT check d_type – FATFS VFS returns DT_UNKNOWN for all entries.
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
