#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief Force an OTA update immediately.
 *
 * If version_hash is NULL or empty, downloads the manifest and updates to the
 * version specified there, regardless of local version/timestamp.
 * If version_hash is provided, attempts to download
 *   https://updates.gaia.bio/firmware-<hash>.bin
 * and apply it.
 */
esp_err_t ota_force_update(const char* version_hash);

#ifdef __cplusplus
}
#endif