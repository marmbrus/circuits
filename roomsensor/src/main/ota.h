#pragma once

#include "esp_err.h"

/**
 * @brief Initialize OTA system and start background update task
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ota_init(void);

/**
 * @brief Perform a one-time check for OTA updates (legacy function)
 * 
 * @return ESP_OK on success
 */
esp_err_t check_for_ota_update(void);

/**
 * @brief Notify OTA system that network is connected
 * 
 * Call this function when WiFi and other network services are ready
 */
void ota_notify_network_connected(void);