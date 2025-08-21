#include "ota.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "config.h"
#include "system_state.h"
#include "communication.h"

/*
 * OTA Update State Machine
 * ========================
 *
 * Core OTA Decision Logic:
 * 1. The ONLY factor for deciding whether to upgrade is comparing embedded build timestamp
 *    (BUILD_TIMESTAMP) with the timestamp in the server manifest (build_timestamp_epoch).
 * 2. If server timestamp > local timestamp, perform upgrade
 * 3. No stored state/history should influence this decision
 *
 * Behavior by Partition Type:
 * - Factory partition: Always reports status as DEV_BUILD, but follows the same upgrade
 *   rules - upgrades if server version is newer, no special treatment
 * - OTA partition: Reports status based on version comparison (UP_TO_DATE, NEWER, UPGRADING)
 *
 * Status Reporting:
 * - DEV_BUILD: Running from factory partition (regardless of version)
 * - UPGRADING: When an update is in progress
 * - UP_TO_DATE: When running on an OTA partition with same version as server
 * - NEWER: When running on an OTA partition with newer version than server
 *
 * Process Flow:
 * 1. ota_init() starts a background task (ota_update_task)
 * 2. Task waits for network connectivity notification
 * 3. When connected, periodically checks server for manifest
 * 4. Parses manifest and compares timestamps
 * 5. If newer version exists, triggers OTA update
 * 6. Reports status to MQTT at key points (boot, network connection, before update)
 */

// Define build timestamp if not defined by CMake
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP 0
#endif

static const char *TAG = "ota";
static const char *MANIFEST_URL = "https://updates.gaia.bio/manifest.json";
static char current_version[33] = {0}; // Store current firmware version (git hash)
static char extracted_hash[33] = {0};  // Store extracted Git hash
static TaskHandle_t ota_task_handle = NULL;
static bool ota_running = false;

// Event group to signal that network is connected
static EventGroupHandle_t network_event_group;
#define NETWORK_CONNECTED_BIT BIT0

// Get the embedded build timestamp (set at compile time)
static const time_t FIRMWARE_BUILD_TIME = (time_t)BUILD_TIMESTAMP;

// NVS keys (for logging purposes only, not for decision-making)
static const char* NVS_NAMESPACE = "ota";
static const char* NVS_LAST_OTA_TIME = "last_ota_time";
static const char* NVS_LAST_OTA_HASH = "last_ota_hash";

// OTA status types
typedef enum {
    OTA_STATUS_DEV_BUILD,   // Factory partition (development build, flashed manually)
    OTA_STATUS_UPGRADING,   // Currently being upgraded
    OTA_STATUS_UP_TO_DATE,  // Running latest version from server
    OTA_STATUS_NEWER        // Running newer version than on server
} ota_status_t;

// Store remote version info for status reporting
static char remote_version[33] = {0};
static time_t remote_timestamp = 0;

// Forward declarations
static void report_ota_status(ota_status_t status, const char* remote_hash);

// Determine whether system time has been synchronized (via SNTP)
static bool is_time_synchronized(void) {
    // Consider time valid if after 2021-01-01 (1609459200)
    time_t now = time(NULL);
    return (now >= 1609459200);
}

// Set the network connected bit when network is up
void ota_notify_network_connected(void) {
    if (network_event_group != NULL) {
        xEventGroupSetBits(network_event_group, NETWORK_CONNECTED_BIT);
        ESP_LOGD(TAG, "Network connection signaled to OTA task");
    }
}

// Extract the git hash from a version string that might be in format like "rev20241211-75-gb1768e2-dirty"
static const char* extract_git_hash(const char* version) {
    static char hash_buf[33] = {0};
    memset(hash_buf, 0, sizeof(hash_buf));

    if (!version || strlen(version) == 0) {
        ESP_LOGW(TAG, "Empty version string");
        return "";
    }

    // If the version string contains "g" followed by a hash (typical git describe format)
    const char* hash_start = strstr(version, "g");
    if (hash_start && hash_start[1] != '\0') {
        hash_start++; // Skip the 'g'

        // Copy until dash or end of string
        const char* end = strchr(hash_start, '-');
        if (end) {
            // Copy just the hash part
            size_t len = end - hash_start;
            if (len > 32) len = 32; // Limit to 32 chars
            strncpy(hash_buf, hash_start, len);
        } else {
            // No dash, copy to the end
            strncpy(hash_buf, hash_start, 32);
        }

        ESP_LOGI(TAG, "Extracted git hash '%s' from version '%s'", hash_buf, version);
        return hash_buf;
    }

    // If this is already just a hash (alphanumeric, 4-12 chars typically)
    if (strlen(version) >= 4 && strlen(version) <= 12) {
        // Check if it looks like a hash (alphanumeric)
        bool is_hash = true;
        for (int i = 0; version[i] != '\0'; i++) {
            if (!isalnum(version[i])) {
                is_hash = false;
                break;
            }
        }

        if (is_hash) {
            ESP_LOGD(TAG, "Version '%s' appears to be a git hash", version);
            return version;
        }
    }

    // Could not extract hash, return original
    ESP_LOGW(TAG, "Could not extract git hash from '%s', using as-is", version);
    return version;
}

// Save OTA information for logging purposes (does not affect update decision)
static void save_ota_info(time_t timestamp, const char* hash) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return;
    }

    // Save timestamp
    err = nvs_set_i64(nvs_handle, NVS_LAST_OTA_TIME, (int64_t)timestamp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save OTA timestamp: %s", esp_err_to_name(err));
    }

    // Save hash
    if (hash != NULL && strlen(hash) > 0) {
        err = nvs_set_str(nvs_handle, NVS_LAST_OTA_HASH, hash);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save OTA hash: %s", esp_err_to_name(err));
        }
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

// Helper function to get stored OTA version (returns empty string if none)
static void get_current_version() {
    // Get the current running partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return;
    }

    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08lx)",
             running->type, running->subtype, running->address);

    // Get version info from nvs or partition
    esp_app_desc_t app_desc;
    if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
        // Use project version as the git hash
        strncpy(current_version, app_desc.version, sizeof(current_version) - 1);
        ESP_LOGI(TAG, "Current firmware version: %s", current_version);

        // Extract the git hash part
        const char* hash = extract_git_hash(current_version);
        strncpy(extracted_hash, hash, sizeof(extracted_hash) - 1);

        // Display embedded build timestamp
        char build_time_str[64];
        struct tm build_timeinfo;
        gmtime_r(&FIRMWARE_BUILD_TIME, &build_timeinfo);
        strftime(build_time_str, sizeof(build_time_str), "%Y-%m-%d %H:%M:%S UTC", &build_timeinfo);
        ESP_LOGI(TAG, "Current firmware build time: %s (epoch: %ld)",
                build_time_str, (long)FIRMWARE_BUILD_TIME);
    } else {
        ESP_LOGW(TAG, "Failed to get partition description");
    }

    // Check if the current app is marked as valid
    const esp_partition_t *validated = esp_ota_get_boot_partition();
    if (running != validated) {
        ESP_LOGW(TAG, "Running partition is not the boot partition - pending validation");
    }
}

// Mark the current app as valid so we don't roll back
static void mark_app_valid() {
    // Get the running partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return;
    }

    // Check if the running partition is a valid OTA partition
    if (running->type != ESP_PARTITION_TYPE_APP) {
        ESP_LOGW(TAG, "Running partition is not an app partition");
        return;
    }

    // Get OTA state for this partition
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

    if (err == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Marking app as valid and canceling rollback");
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App marked as valid successfully");
            } else {
                ESP_LOGE(TAG, "Failed to mark app as valid");
            }
        } else {
            ESP_LOGI(TAG, "App is already validated");
        }
    } else {
        // This could happen for factory partition which doesn't have OTA data
        // It's normal for the factory app, so we'll make this INFO level
        ESP_LOGD(TAG, "Could not get OTA state: %s (this is normal for factory app)", esp_err_to_name(err));
    }
}

// Parse manifest and check if update is needed
static bool parse_manifest_and_check_update(char *manifest_data) {
    if (manifest_data == NULL || strlen(manifest_data) == 0) {
        ESP_LOGE(TAG, "Empty manifest data");
        return false;
    }

    cJSON *root = cJSON_Parse(manifest_data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON: %s", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown error");
        return false;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url = cJSON_GetObjectItem(root, "url");
    cJSON *build_timestamp_epoch = cJSON_GetObjectItem(root, "build_timestamp_epoch");

    if (!cJSON_IsString(version) || !cJSON_IsString(url)) {
        ESP_LOGE(TAG, "Manifest missing required fields");
        cJSON_Delete(root);
        return false;
    }

    const char *remote_version_str = version->valuestring;
    const char *firmware_url = url->valuestring;

    if (remote_version_str == NULL || firmware_url == NULL) {
        ESP_LOGE(TAG, "Null version or URL in manifest");
        cJSON_Delete(root);
        return false;
    }

    // Store remote version for status reporting
    strncpy(remote_version, remote_version_str, sizeof(remote_version) - 1);
    ESP_LOGD(TAG, "Stored remote version: %s", remote_version);

    // Get remote timestamp for comparison
    remote_timestamp = 0;
    if (build_timestamp_epoch && cJSON_IsNumber(build_timestamp_epoch)) {
        remote_timestamp = (time_t)build_timestamp_epoch->valuedouble;

        // Format remote timestamp for logging
        char remote_time_str[64];
        struct tm remote_tm;
        gmtime_r(&remote_timestamp, &remote_tm);
        strftime(remote_time_str, sizeof(remote_time_str), "%Y-%m-%dT%H:%M:%SZ", &remote_tm);
        // Reduce logging verbosity
        // ESP_LOGI(TAG, "Remote firmware build time: %s (epoch: %ld)", remote_time_str, (long)remote_timestamp);

        // Format local timestamp for comparison
        char local_time_str[64] = {0};
        if (FIRMWARE_BUILD_TIME > 0) {
            struct tm local_tm;
            gmtime_r(&FIRMWARE_BUILD_TIME, &local_tm);
            strftime(local_time_str, sizeof(local_time_str), "%Y-%m-%dT%H:%M:%SZ", &local_tm);
            // Reduce logging verbosity
            // ESP_LOGI(TAG, "Local firmware build time: %s (epoch: %ld)",
            //          local_time_str, (long)FIRMWARE_BUILD_TIME);
        } else {
            ESP_LOGW(TAG, "Local firmware build time is not available");
        }
    } else {
        ESP_LOGW(TAG, "Remote manifest missing build timestamp");
    }

    // Extract the hash part from the remote version
    const char* remote_hash = extract_git_hash(remote_version_str);
    // Reduce logging verbosity
    // ESP_LOGI(TAG, "Local firmware hash: %s, Remote firmware hash: %s",
    //          extracted_hash, remote_hash);

    // Check if we're running from factory partition
    bool is_factory = false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        is_factory = true;
    }

    // PRIMARY CHECK: Compare build timestamps
    if (remote_timestamp > 0 && FIRMWARE_BUILD_TIME > 0) {
        // Log raw timestamp values for debugging - change to DEBUG level
        // ESP_LOGI(TAG, "Raw timestamp values - Remote: %ld, Local: %ld",
        //         (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME);
        ESP_LOGD(TAG, "Raw timestamp values - Remote: %ld, Local: %ld",
                (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME);

        // Only run sanity checks if system time is known
        if (is_time_synchronized()) {
            // Check for unrealistic timestamp values (e.g., more than 10 years in the future)
            time_t current_time = time(NULL);
            if (remote_timestamp > current_time + 315360000) { // 10 years in seconds
                ESP_LOGW(TAG, "Remote timestamp is unrealistically far in the future, may be corrupted");
            }
            if (FIRMWARE_BUILD_TIME > current_time + 315360000) {
                ESP_LOGW(TAG, "Local build timestamp is unrealistically far in the future, may be corrupted");
            }
        } else {
            ESP_LOGD(TAG, "Skipping timestamp sanity checks until SNTP time is set");
        }

        // Directly compare the timestamps
        if (remote_timestamp > FIRMWARE_BUILD_TIME) {
            time_t time_diff = remote_timestamp - FIRMWARE_BUILD_TIME;

            // Keep this log concise
            ESP_LOGI(TAG, "Newer version found (%ld sec newer), starting upgrade...", (long)time_diff);

            // Special message for factory builds
            if (is_factory) {
                ESP_LOGI(TAG, "OTA update available for factory build - will upgrade to: %s", remote_hash);
            }

            // LED animation during OTA will be handled by LEDManager patterns

            // Report OTA status as UPGRADING before starting the update
            report_ota_status(OTA_STATUS_UPGRADING, remote_hash);

            // Perform the OTA update
            ESP_LOGI(TAG, "Starting firmware update from %s", firmware_url);

            esp_http_client_config_t config = {};
            config.url = firmware_url;
            config.crt_bundle_attach = esp_crt_bundle_attach;
            config.skip_cert_common_name_check = false;
            config.buffer_size = 1024;
            config.timeout_ms = 30000; // 30 second timeout for firmware download

            esp_https_ota_config_t ota_config = {};
            ota_config.http_config = &config;
            ota_config.bulk_flash_erase = true;

            esp_err_t ret = esp_https_ota(&ota_config);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "OTA update successful! Saving update info and rebooting...");

                // Save OTA information before reboot
                save_ota_info(remote_timestamp, remote_hash);

                cJSON_Delete(root);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
                return true;
            } else {
                ESP_LOGE(TAG, "OTA update failed with error: %s", esp_err_to_name(ret));
                // LED state handled by LEDManager patterns
                mark_app_valid();
                cJSON_Delete(root);
                return false;
            }
        } else if (remote_timestamp < FIRMWARE_BUILD_TIME) {
            // Local is newer than remote
            if (is_factory) {
                // Report as DEV_BUILD for factory partition
                report_ota_status(OTA_STATUS_DEV_BUILD, remote_hash);
                ESP_LOGI(TAG, "Running factory build with newer version than server");
            } else {
                // Report as NEWER for OTA partition
                report_ota_status(OTA_STATUS_NEWER, remote_hash);
                ESP_LOGI(TAG, "Running newer version than available on server");
            }
        } else {
            // Versions are identical
            if (is_factory) {
                // Report as DEV_BUILD for factory partition
                report_ota_status(OTA_STATUS_DEV_BUILD, remote_hash);
                ESP_LOGI(TAG, "Running factory build with same version as server");
            } else {
                // Report as UP_TO_DATE for OTA partition
                report_ota_status(OTA_STATUS_UP_TO_DATE, remote_hash);
                ESP_LOGI(TAG, "Running the latest version");
            }
        }
    } else {
        ESP_LOGW(TAG, "Cannot compare timestamps: Remote=%ld, Local=%ld. Skipping update.",
                (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME);

        // Always report status even if we can't compare versions
        if (is_factory) {
            report_ota_status(OTA_STATUS_DEV_BUILD, remote_hash);
            ESP_LOGI(TAG, "Running factory build, status unknown (missing timestamp)");
        } else {
            // Default to UP_TO_DATE if we can't determine
            report_ota_status(OTA_STATUS_UP_TO_DATE, remote_hash);
            ESP_LOGI(TAG, "Running OTA partition, status unknown (missing timestamp)");
        }
    }

    // If we reach here, no update is needed
    mark_app_valid();
    cJSON_Delete(root);
    return false;
}

// Callback to handle HTTP response data
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static char *manifest_data = NULL;
    static int data_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Allocate or reallocate buffer for the data
            if (manifest_data == NULL) {
                manifest_data = (char*)malloc(evt->data_len + 1);
                data_len = evt->data_len;
                if (manifest_data == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for manifest data");
                    return ESP_FAIL;
                }
            } else {
                // Calculate new buffer size and reallocate
                char* new_data = (char*)realloc(manifest_data, data_len + evt->data_len + 1);
                if (new_data == NULL) {
                    ESP_LOGE(TAG, "Failed to reallocate memory for manifest data");
                    free(manifest_data);
                    manifest_data = NULL;
                    data_len = 0;
                    return ESP_FAIL;
                }
                manifest_data = new_data;

                // Update buffer position for the new data
                memcpy(manifest_data + data_len, evt->data, evt->data_len);
                data_len += evt->data_len;
            }

            // Copy received data to the buffer
            if (manifest_data != NULL) {
                if (data_len == evt->data_len) {
                    // First chunk of data
                    memcpy(manifest_data, evt->data, evt->data_len);
                }
                manifest_data[data_len] = 0; // Null terminate
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            if (manifest_data != NULL) {
                // Reduce logging verbosity for successful downloads
                // ESP_LOGI(TAG, "Manifest downloaded (%d bytes)", data_len);
                if (data_len > 0) {
                    // Only show preview at DEBUG level
                    char preview[101] = {0}; // Declare preview here
                    strncpy(preview, manifest_data, 100);
                    // ESP_LOGI(TAG, "Manifest preview: %s%s", preview,
                    //          (strlen(manifest_data) > 100) ? "..." : "");
                    ESP_LOGD(TAG, "Manifest downloaded (%d bytes): %s%s", data_len,
                             preview, (strlen(manifest_data) > 100) ? "..." : "");

                    // Process the manifest - remove unused variable assignment
                    // bool update_result = parse_manifest_and_check_update(manifest_data);
                    parse_manifest_and_check_update(manifest_data);
                    // Remove redundant log - the status report is the main output
                    // ESP_LOGI(TAG, "Manifest parse result: %s", update_result ? "Update triggered" : "No update needed");
                } else {
                    ESP_LOGW(TAG, "Empty manifest received");
                }
                free(manifest_data);
                manifest_data = NULL;
                data_len = 0;
            } else {
                ESP_LOGW(TAG, "HTTP_EVENT_ON_FINISH with NULL manifest data");
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            if (manifest_data != NULL) {
                free(manifest_data);
                manifest_data = NULL;
                data_len = 0;
            }
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            if (manifest_data != NULL) {
                free(manifest_data);
                manifest_data = NULL;
                data_len = 0;
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

// OTA update task that runs in the background
static void ota_update_task(void *pvParameter) {
    ESP_LOGI(TAG, "OTA update task started");

    // Validate that network event group exists
    if (network_event_group == NULL) {
        ESP_LOGE(TAG, "Network event group is NULL! OTA task exiting.");
        vTaskDelete(NULL);
        return;
    }

    // Get current version on startup
    get_current_version();

    // Ensure the app is marked valid to prevent rollback
    mark_app_valid();

    uint32_t check_count = 0;
    bool was_connected = false;

    ESP_LOGI(TAG, "Starting OTA monitoring loop");

    // Main OTA loop - runs forever
    while (1) {
        // Check for system readiness
        bool is_connected = false;

        // Use system state to determine readiness
        extern SystemState get_system_state(void);
        SystemState current_state = get_system_state();

        // Only treat as connected when the whole system is up (WiFi + MQTT)
        if (current_state == FULLY_CONNECTED) {
            is_connected = true;
        }

        // Network state change logging
        if (is_connected && !was_connected) {
            ESP_LOGI(TAG, "Network is now connected, proceeding with OTA checks");
            was_connected = true;
        } else if (!is_connected && was_connected) {
            ESP_LOGI(TAG, "Network is now disconnected, pausing OTA checks");
            was_connected = false;
        }

        // Only proceed with OTA check if system is fully connected
        if (is_connected) {
            // Ensure SNTP has set the time before performing HTTPS requests and comparing timestamps
            if (!is_time_synchronized()) {
                ESP_LOGI(TAG, "Waiting for time synchronization (SNTP) before OTA checks");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            // Check for updates
            check_count++;
            ESP_LOGI(TAG, "OTA check #%lu: Checking for updates from %s", check_count, MANIFEST_URL);

            esp_err_t err = ESP_OK;
            esp_http_client_handle_t client = NULL;

            // Initialize HTTP client with proper error checking
            esp_http_client_config_t config = {};
            config.url = MANIFEST_URL;
            config.event_handler = http_event_handler;
            config.crt_bundle_attach = esp_crt_bundle_attach;
            config.skip_cert_common_name_check = false;
            config.timeout_ms = 10000; // Add 10s timeout to avoid hanging

            client = esp_http_client_init(&config);
            if (client == NULL) {
                ESP_LOGE(TAG, "Failed to initialize HTTP client");
                // Skip to delay and try again later
            } else {
                // Perform HTTP request with proper error handling
                err = esp_http_client_perform(client);

                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
                } else {
                    int status_code = esp_http_client_get_status_code(client);
                    if (status_code != 200) {
                        ESP_LOGW(TAG, "OTA check completed with unexpected status code: %d", status_code);
                    }
                }

                // Cleanup
                esp_http_client_cleanup(client);
            }

            // Sleep for the normal interval before checking again when connected
            vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
        } else {
            // Not fully connected - back off before checking again
            ESP_LOGW(TAG, "Waiting for FULLY_CONNECTED state before OTA check...");
            vTaskDelay(pdMS_TO_TICKS(60000));

            // No explicit else branch needed - we'll just loop back to the connectivity check
        }
    }

    // This should never be reached, but just in case
    ESP_LOGW(TAG, "OTA task unexpectedly exited the main loop!");
    vTaskDelete(NULL);
}

// Initialize OTA module and start background task
esp_err_t ota_init(void) {
    ESP_LOGI(TAG, "Initializing OTA module");

    // Create event group for network synchronization if it doesn't exist
    if (network_event_group == NULL) {
        network_event_group = xEventGroupCreate();
        if (network_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create event group");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Created network event group for OTA");
    }

    // Get current firmware version and partition info
    get_current_version();

    // Check if we're running from factory partition
    bool is_factory = false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        is_factory = true;
        // Report DEV_BUILD status - this will be done when status is published
        // No need for extra log here
    }

    // Mark app as valid to prevent rollback
    mark_app_valid();

    // Start OTA task if not already running
    if (!ota_running || ota_task_handle == NULL) {
        ota_running = true;
        BaseType_t ret = xTaskCreate(ota_update_task, "ota_task",
                                    OTA_TASK_STACK_SIZE, NULL,
                                    OTA_TASK_PRIORITY, &ota_task_handle);

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "OTA task created successfully");
    } else {
        ESP_LOGW(TAG, "OTA task already running, skipping creation");
    }


    ESP_LOGI(TAG, "OTA module initialized successfully");
    return ESP_OK;
}

// Check for OTA updates (legacy function, now just for one-time manual checks)
esp_err_t check_for_ota_update(void) {
    // Return immediately if OTA task is already running
    if (ota_running) {
        ESP_LOGI(TAG, "OTA task already running, skipping one-time check");
        return ESP_OK;
    }

    // For backward compatibility, do a one-time check
    get_current_version();

    // Make sure we mark the app valid to prevent rollback
    mark_app_valid();

    ESP_LOGI(TAG, "Checking for OTA updates from %s", MANIFEST_URL);

    esp_http_client_config_t config = {};
    config.url = MANIFEST_URL;
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Report OTA status to MQTT
static void report_ota_status(ota_status_t status, const char* remote_hash) {
    cJSON *ota_json = cJSON_CreateObject();
    if (!ota_json) {
        ESP_LOGE(TAG, "Failed to create OTA status JSON");
        return;
    }

    // Current time for timestamp
    time_t now;
    time(&now);
    char timestamp[64];
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    cJSON_AddStringToObject(ota_json, "timestamp", timestamp);

    // Status as string
    const char* status_str = "UNKNOWN";
    switch (status) {
        case OTA_STATUS_DEV_BUILD:
            status_str = "DEV_BUILD";
            break;
        case OTA_STATUS_UPGRADING:
            status_str = "UPGRADING";
            break;
        case OTA_STATUS_UP_TO_DATE:
            status_str = "UP_TO_DATE";
            break;
        case OTA_STATUS_NEWER:
            status_str = "NEWER";
            break;
    }
    cJSON_AddStringToObject(ota_json, "status", status_str);

    // Partition info
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        cJSON_AddStringToObject(ota_json, "partition",
            running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? "factory" : "ota");
    }

    // Only include git hashes for non-dev builds
    if (status != OTA_STATUS_DEV_BUILD) {
        // Local firmware git hash
        if (strlen(extracted_hash) > 0) {
            cJSON_AddStringToObject(ota_json, "local_version", extracted_hash);
        }
    }

    // Include remote version when available
    if (remote_hash && strlen(remote_hash) > 0) {
        cJSON_AddStringToObject(ota_json, "remote_version", remote_hash);
        ESP_LOGD(TAG, "Adding remote_version to status: %s", remote_hash);
    } else if (strlen(remote_version) > 0) {
        // Use global remote_version as fallback
        const char* hash = extract_git_hash(remote_version);
        cJSON_AddStringToObject(ota_json, "remote_version", hash);
        ESP_LOGD(TAG, "Adding remote_version from global: %s", hash);
    }

    // Format build timestamps in ISO format
    if (FIRMWARE_BUILD_TIME > 0) {
        char build_time_str[64];
        struct tm build_timeinfo;
        gmtime_r(&FIRMWARE_BUILD_TIME, &build_timeinfo);
        strftime(build_time_str, sizeof(build_time_str), "%Y-%m-%dT%H:%M:%SZ", &build_timeinfo);
        cJSON_AddStringToObject(ota_json, "local_build_time", build_time_str);
    }

    if (remote_timestamp > 0) {
        char remote_time_str[64];
        struct tm remote_tm;
        gmtime_r(&remote_timestamp, &remote_tm);
        strftime(remote_time_str, sizeof(remote_time_str), "%Y-%m-%dT%H:%M:%SZ", &remote_tm);
        cJSON_AddStringToObject(ota_json, "remote_build_time", remote_time_str);
        ESP_LOGD(TAG, "Adding remote_build_time to status: %s", remote_time_str);
    }

    // Convert to string and publish
    char *ota_string = cJSON_Print(ota_json);
    if (ota_string) {
        ESP_LOGI(TAG, "Publishing OTA status: %s", ota_string);
        // Publish to ota subtopic - the publish_to_topic function will add the MAC address
        publish_to_topic("ota", ota_string);
        free(ota_string);
    } else {
        ESP_LOGE(TAG, "Failed to convert OTA status to string");
    }

    cJSON_Delete(ota_json);
}

// Public function to report OTA status
void ota_report_status(void) {
    // Skip early reporting if remote version info isn't available yet
    if (remote_timestamp == 0 || strlen(remote_version) == 0) {
        ESP_LOGD(TAG, "Skipping OTA status report - remote version info not available yet");
        return;
    }

    // Determine current status
    ota_status_t status;

    // Check if running from factory partition - ALWAYS a dev build
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        // Always a development build for factory partition
        status = OTA_STATUS_DEV_BUILD;
    } else {
        // Only for OTA partitions - check version status
        if (remote_timestamp > 0) {
            // We have remote version info, can determine exact status
            if (remote_timestamp > FIRMWARE_BUILD_TIME) {
                // Remote is newer (shouldn't happen unless we skipped update)
                status = OTA_STATUS_UPGRADING;
            } else if (remote_timestamp < FIRMWARE_BUILD_TIME) {
                // Local is newer
                status = OTA_STATUS_NEWER;
            } else {
                // Same version
                status = OTA_STATUS_UP_TO_DATE;
            }
        } else {
            // Default to UP_TO_DATE if we can't determine, but only for OTA partitions
            status = OTA_STATUS_UP_TO_DATE;
        }
    }

    // Report the status
    const char* remote_hash = (strlen(remote_version) > 0) ? extract_git_hash(remote_version) : NULL;
    report_ota_status(status, remote_hash);
}