#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Initialize and start webserver
esp_err_t webserver_init(void);

// Stop webserver
esp_err_t webserver_stop(void);

// Check if webserver is running
bool webserver_is_running(void);
