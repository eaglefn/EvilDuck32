#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Script execution states
typedef enum {
    DUCKY_STATE_IDLE,
    DUCKY_STATE_RUNNING,
    DUCKY_STATE_PAUSED,
    DUCKY_STATE_ERROR,
    DUCKY_STATE_FINISHED
} ducky_state_t;

// Script info
typedef struct {
    char filename[64];
    int current_line;
    int total_lines;
    ducky_state_t state;
    uint32_t delay_ms;
} ducky_script_info_t;

// Initialize the ducky script engine
esp_err_t ducky_init(void);

// Load and run a script from SD card
esp_err_t ducky_run_script(const char *filename);

// Stop current script execution
esp_err_t ducky_stop(void);

// Pause/resume execution
esp_err_t ducky_pause(void);
esp_err_t ducky_resume(void);

// Get current script info
bool ducky_get_info(ducky_script_info_t *info);

// Check if script is running
bool ducky_is_running(void);

// Set default delay between commands
void ducky_set_default_delay(uint32_t delay_ms);
