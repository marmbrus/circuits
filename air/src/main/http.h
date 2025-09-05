#pragma once

#include "esp_err.h"

// Function to start the webserver
esp_err_t start_webserver(void);

// Function to stop the webserver
void stop_webserver(void); 