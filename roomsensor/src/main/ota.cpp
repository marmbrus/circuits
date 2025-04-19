#include "ota.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Define build timestamp if not defined by CMake
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP 0
#endif

static const char *TAG = "ota";
static const char *MANIFEST_URL = "http://gaia.home:3000/manifest.json";
static char current_version[33] = {0}; // Store current firmware version (git hash)
static char extracted_hash[33] = {0};  // Store extracted Git hash

// Get the embedded build timestamp (set at compile time)
static const time_t FIRMWARE_BUILD_TIME = (time_t)BUILD_TIMESTAMP;

// NVS keys
static const char* NVS_NAMESPACE = "ota";
static const char* NVS_LAST_OTA_TIME = "last_ota_time";
static const char* NVS_LAST_OTA_HASH = "last_ota_hash";

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
            ESP_LOGI(TAG, "Version '%s' already looks like a git hash", version);
            return version;
        }
    }

    // Could not extract hash, return original
    ESP_LOGW(TAG, "Could not extract git hash from '%s', using as-is", version);
    return version;
}

// Load last OTA information from NVS
static time_t load_last_ota_info() {
    time_t last_ota_timestamp = 0;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 0;
    }

    // Load last OTA timestamp
    err = nvs_get_i64(nvs_handle, NVS_LAST_OTA_TIME, (int64_t*)&last_ota_timestamp);
    if (err == ESP_OK) {
        char time_str[64];
        struct tm timeinfo;
        gmtime_r(&last_ota_timestamp, &timeinfo); // Use gmtime for UTC
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
        ESP_LOGI(TAG, "Last OTA timestamp: %s (epoch: %ld)", time_str, (long)last_ota_timestamp);
    } else {
        ESP_LOGW(TAG, "Failed to read last OTA timestamp: %s", esp_err_to_name(err));
    }

    // Load last OTA hash
    char last_hash[33] = {0};
    size_t len = sizeof(last_hash);
    err = nvs_get_str(nvs_handle, NVS_LAST_OTA_HASH, last_hash, &len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Last OTA hash: %s", last_hash);
    } else {
        ESP_LOGW(TAG, "Failed to read last OTA hash: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return last_ota_timestamp;
}

// Save OTA information to NVS
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

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

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
        ESP_LOGI(TAG, "Compiled %s %s", app_desc.date, app_desc.time);
        ESP_LOGI(TAG, "IDF version: %s", app_desc.idf_ver);
        ESP_LOGI(TAG, "Project name: %s", app_desc.project_name);

        // Extract the git hash part
        const char* hash = extract_git_hash(current_version);
        strncpy(extracted_hash, hash, sizeof(extracted_hash) - 1);
        ESP_LOGI(TAG, "Extracted git hash for comparison: %s", extracted_hash);

        // Display embedded build timestamp
        char build_time_str[64];
        struct tm build_timeinfo;
        gmtime_r(&FIRMWARE_BUILD_TIME, &build_timeinfo);
        strftime(build_time_str, sizeof(build_time_str), "%Y-%m-%d %H:%M:%S UTC", &build_timeinfo);
        ESP_LOGI(TAG, "Embedded build timestamp: %s (epoch: %ld)",
                build_time_str, (long)FIRMWARE_BUILD_TIME);
    } else {
        ESP_LOGW(TAG, "Failed to get partition description");
    }

    // Check if the current app is marked as valid
    const esp_partition_t *validated = esp_ota_get_boot_partition();
    if (running != validated) {
        ESP_LOGW(TAG, "Running partition is not the boot partition - pending validation");
        ESP_LOGI(TAG, "Boot partition type %d subtype %d (offset 0x%08lx)",
                 validated->type, validated->subtype, validated->address);
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
        ESP_LOGI(TAG, "Current app state: %d", ota_state);

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
        ESP_LOGI(TAG, "Could not get OTA state: %s (this is normal for factory app)", esp_err_to_name(err));
    }
}

// Parse manifest and check if update is needed
static bool parse_manifest_and_check_update(char *manifest_data) {
    // Load last OTA info first
    time_t last_ota_timestamp = load_last_ota_info();

    cJSON *root = cJSON_Parse(manifest_data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        return false;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url = cJSON_GetObjectItem(root, "url");
    cJSON *build_timestamp = cJSON_GetObjectItem(root, "build_timestamp");
    cJSON *build_timestamp_epoch = cJSON_GetObjectItem(root, "build_timestamp_epoch");
    cJSON *git_describe = cJSON_GetObjectItem(root, "git_describe");

    if (!cJSON_IsString(version) || !cJSON_IsString(url)) {
        ESP_LOGE(TAG, "Manifest missing required fields");
        cJSON_Delete(root);
        return false;
    }

    const char *remote_version = version->valuestring;
    const char *firmware_url = url->valuestring;
    const char *timestamp_str = build_timestamp ? build_timestamp->valuestring : "unknown";
    const char *describe = git_describe ? git_describe->valuestring : "unknown";

    // Get remote timestamp for comparison
    time_t remote_timestamp = 0;
    if (build_timestamp_epoch && cJSON_IsNumber(build_timestamp_epoch)) {
        remote_timestamp = (time_t)build_timestamp_epoch->valueint;

        // Format remote timestamp for logging
        char remote_time_str[64];
        struct tm remote_tm;
        gmtime_r(&remote_timestamp, &remote_tm);
        strftime(remote_time_str, sizeof(remote_time_str), "%Y-%m-%d %H:%M:%S UTC", &remote_tm);
        ESP_LOGI(TAG, "Remote build timestamp: %s (epoch: %ld)", remote_time_str, (long)remote_timestamp);
    }

    ESP_LOGI(TAG, "Manifest info - version: %s, timestamp: %s", remote_version, timestamp_str);
    ESP_LOGI(TAG, "Git describe: %s", describe);
    ESP_LOGI(TAG, "Firmware URL: %s", firmware_url);

    // Extract the hash part from the remote version
    const char* remote_hash = extract_git_hash(remote_version);

    ESP_LOGI(TAG, "Remote hash: %s, Local hash: %s", remote_hash, extracted_hash);
    ESP_LOGI(TAG, "Remote build time: %ld, Firmware build time: %ld",
             (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME);
    ESP_LOGI(TAG, "Last OTA time: %ld", (long)last_ota_timestamp);

    // Already applied check - if the hashes match and we've already seen this timestamp
    if (strcmp(remote_hash, extracted_hash) == 0 &&
        last_ota_timestamp > 0 && remote_timestamp <= last_ota_timestamp) {
        ESP_LOGI(TAG, "This update was already applied (matching hash and timestamp <= last OTA)");
        mark_app_valid();
        cJSON_Delete(root);
        return false;
    }

    // Determine if we need to update
    bool update_needed = false;

    // We have a reliable embedded build timestamp and remote timestamp
    if (remote_timestamp > 0 && FIRMWARE_BUILD_TIME > 0) {
        // Compare timestamps - only update if remote is newer
        if (remote_timestamp > FIRMWARE_BUILD_TIME) {
            ESP_LOGI(TAG, "Remote build is newer than current firmware, will update");
            ESP_LOGI(TAG, "Remote: %ld vs Local: %ld (difference: %ld seconds)",
                    (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME,
                    (long)(remote_timestamp - FIRMWARE_BUILD_TIME));
            update_needed = true;
        } else if (remote_timestamp < FIRMWARE_BUILD_TIME) {
            ESP_LOGW(TAG, "Remote build is OLDER than current firmware, skipping update");
            ESP_LOGW(TAG, "Remote: %ld vs Local: %ld (difference: %ld seconds)",
                    (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME,
                    (long)(FIRMWARE_BUILD_TIME - remote_timestamp));
            mark_app_valid();
            cJSON_Delete(root);
            return false;
        } else if (strcmp(remote_hash, extracted_hash) != 0) {
            // Same timestamp but different hash - can't determine which is newer
            ESP_LOGW(TAG, "Same timestamp but different hash - cannot determine which is newer");
            mark_app_valid();
            cJSON_Delete(root);
            return false;
        } else {
            // Identical version with same hash and timestamp
            ESP_LOGI(TAG, "Already running identical version");
            mark_app_valid();
            cJSON_Delete(root);
            return false;
        }
    } else if (strcmp(remote_hash, extracted_hash) != 0) {
        // Can't compare timestamps, but hashes differ
        ESP_LOGW(TAG, "Cannot compare timestamps, using hash comparison");

        // Only update if we don't have a valid hash locally yet
        if (strlen(extracted_hash) == 0) {
            ESP_LOGI(TAG, "No local hash available, will update");
            update_needed = true;
        } else {
            ESP_LOGW(TAG, "Different hash but can't determine which is newer without timestamps");
            ESP_LOGW(TAG, "Staying with current version for safety");
            mark_app_valid();
            cJSON_Delete(root);
            return false;
        }
    } else {
        // Hashes match, assume same version
        ESP_LOGI(TAG, "Hash comparison indicates same version");
        mark_app_valid();
        cJSON_Delete(root);
        return false;
    }

    // If update needed, perform the update
    if (update_needed && firmware_url) {
        ESP_LOGI(TAG, "Starting firmware update from %s", firmware_url);

        esp_http_client_config_t config = {};
        config.url = firmware_url;
        config.cert_pem = NULL;
        config.skip_cert_common_name_check = true;
        config.transport_type = HTTP_TRANSPORT_OVER_TCP;  // Explicitly use HTTP not HTTPS
        config.buffer_size = 1024;

        esp_https_ota_config_t ota_config = {};
        ota_config.http_config = &config;
        ota_config.bulk_flash_erase = true;

        esp_err_t ret = esp_https_ota(&ota_config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "OTA update successful! Saving OTA info and rebooting...");

            // Save OTA information before reboot
            save_ota_info(remote_timestamp, remote_hash);

            cJSON_Delete(root);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            return true;
        } else {
            ESP_LOGE(TAG, "OTA update failed with error: %s", esp_err_to_name(ret));
        }
    }

    cJSON_Delete(root);
    return false;
}

// Callback to handle HTTP response data
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static char *manifest_data = NULL;
    static int data_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Allocate or reallocate buffer for the data
            if (manifest_data == NULL) {
                manifest_data = (char*)malloc(evt->data_len + 1);
                data_len = evt->data_len;
            } else {
                manifest_data = (char*)realloc(manifest_data, data_len + evt->data_len + 1);
                data_len += evt->data_len;
            }

            if (manifest_data == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for manifest data");
                return ESP_FAIL;
            }

            // Copy received data to the buffer
            memcpy(manifest_data + data_len - evt->data_len, evt->data, evt->data_len);
            manifest_data[data_len] = 0; // Null terminate
            break;

        case HTTP_EVENT_ON_FINISH:
            if (manifest_data != NULL) {
                ESP_LOGI(TAG, "Manifest downloaded (%d bytes)", data_len);
                if (data_len > 0) {
                    ESP_LOGI(TAG, "Manifest content: %s", manifest_data);
                }
                parse_manifest_and_check_update(manifest_data);
                free(manifest_data);
                manifest_data = NULL;
                data_len = 0;
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
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

// Check for OTA updates
esp_err_t check_for_ota_update(void) {
    get_current_version();

    // Make sure we mark the app valid to prevent rollback
    mark_app_valid();

    ESP_LOGI(TAG, "Checking for OTA updates from %s", MANIFEST_URL);

    esp_http_client_config_t config = {};
    config.url = MANIFEST_URL;
    config.event_handler = http_event_handler;
    config.cert_pem = NULL;
    config.skip_cert_common_name_check = true;
    config.transport_type = HTTP_TRANSPORT_OVER_TCP;  // Explicitly use HTTP not HTTPS

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}