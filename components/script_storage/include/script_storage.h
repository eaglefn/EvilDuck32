#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Script Storage Configuration
#define MAX_SCRIPTS 20
#define MAX_SCRIPT_SIZE 4096
#define MAX_SCRIPT_NAME 32

// NVS configuration for script storage
// NVS_PART_NAME: the NVS partition name in partitions.csv (type=data, subtype=nvs)
// NVS_SCRIPT_NS: namespace within the NVS partition used for scripts
#define NVS_PART_NAME  "nvs"
#define NVS_SCRIPT_NS  "scripts"
#define NVS_KEY_AUTORUN "autorun"

// Script structure
typedef struct {
    char name[MAX_SCRIPT_NAME];
    char content[MAX_SCRIPT_SIZE + 1];
    bool used;
} script_storage_entry_t;

// Initialize script storage in NVS
esp_err_t script_storage_init(void);

// Save a script to NVS
esp_err_t script_storage_save(const char *name, const char *content);

// Load a script from NVS
esp_err_t script_storage_load(const char *name, char *content, size_t content_size);

// Delete a script from NVS
esp_err_t script_storage_delete(const char *name);

// List all scripts (fills arrays with names and returns count)
int script_storage_list(char names[][MAX_SCRIPT_NAME], int max_count);

// Check if script exists
bool script_storage_exists(const char *name);

// Get autorun script name
const char* script_storage_get_autorun(void);

// Set autorun script
esp_err_t script_storage_set_autorun(const char *name);

// Clear autorun
esp_err_t script_storage_clear_autorun(void);

// Get script count
int script_storage_count(void);

// Get all scripts metadata
esp_err_t script_storage_get_all(script_storage_entry_t *scripts, int max_count, int *actual_count);

// ===== Sync API (NVS <-> SD card) =====

// SD payloads directory (relative to SD mount point)
#define SD_PAYLOADS_DIR "/sdcard/payloads"

typedef enum {
    SYNC_ACTION_EXPORT = 0,   // NVS only → write to SD
    SYNC_ACTION_IMPORT,       // SD only  → write to NVS
    SYNC_ACTION_CONFLICT,     // exists on both, different size
    SYNC_ACTION_EQUAL,        // exists on both, same size (no action needed)
} sync_action_t;

typedef struct {
    char name[MAX_SCRIPT_NAME];
    sync_action_t action;
    size_t nvs_size;    // 0 if not in NVS
    size_t sd_size;     // 0 if not on SD
} sync_entry_t;

typedef struct {
    sync_entry_t entries[MAX_SCRIPTS * 2];
    int count;
    int to_export;    // SYNC_ACTION_EXPORT count
    int to_import;    // SYNC_ACTION_IMPORT count
    int conflicts;    // SYNC_ACTION_CONFLICT count
} sync_preview_t;

typedef enum {
    CONFLICT_SKIP = 0,     // leave both unchanged
    CONFLICT_NVS_WINS,     // overwrite SD with NVS version
    CONFLICT_SD_WINS,      // overwrite NVS with SD version
} conflict_resolve_t;

typedef struct {
    char name[MAX_SCRIPT_NAME];
    conflict_resolve_t resolve;
} conflict_resolution_t;

// Build a sync preview: compares NVS scripts with /sdcard/payloads/*.txt
esp_err_t script_storage_sync_preview(sync_preview_t *preview);

// Execute sync based on preview + conflict resolutions.
// resolutions: array of per-file conflict decisions (only needed for CONFLICT entries).
// res_count: number of elements in resolutions array.
// out_synced: number of files actually copied/written.
// out_skipped: number of files skipped (EQUAL or CONFLICT_SKIP).
esp_err_t script_storage_sync_execute(const sync_preview_t *preview,
                                      const conflict_resolution_t *resolutions,
                                      int res_count,
                                      int *out_synced,
                                      int *out_skipped);

#ifdef __cplusplus
}
#endif
