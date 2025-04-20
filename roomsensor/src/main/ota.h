#pragma once

#include "esp_err.h"

/**
 * @brief Initialize OTA system and start background update task
 * 
 * This should be called early in the startup process, but the actual OTA checks
 * will wait for network connection notification via ota_notify_network_connected()
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ota_init(void);

/**
 * @brief Perform a one-time check for OTA updates (legacy function)
 * 
 * This function is kept for backward compatibility. For new code,
 * use ota_init() instead which runs checks automatically.
 * 
 * @return ESP_OK on success
 */
esp_err_t check_for_ota_update(void);

/**
 * @brief Notify OTA system that network is connected
 * 
 * IMPORTANT: You MUST call this function when WiFi and MQTT are connected
 * to trigger the OTA checks. Without this call, OTA checking will be on hold.
 */
void ota_notify_network_connected(void);

/**
 * @brief Report current OTA status via MQTT
 * 
 * This function determines the current status and sends a report to
 * the roomsensor/device/{MAC}/ota topic with detailed information.
 */
void ota_report_status(void);