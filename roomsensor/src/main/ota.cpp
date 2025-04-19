#include "ota.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char *TAG = "ota";
static const char *MANIFEST_URL = "http://gaia.home:3000/manifest.json";
static char current_version[33] = {0}; // Store current firmware version (git hash)
static char extracted_hash[33] = {0};  // Store extracted Git hash
static int current_build_number = 0;   // Current firmware build number

// Extract the git hash from a version string that might be in format like "rev20241211-75-gb1768e2-dirty"
static const char* extract_git_hash(const char* version) {
    static char hash_buf[33] = {0};
    memset(hash_buf, 0, sizeof(hash_buf));

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

// Extract build number from version string like "rev20241211-75-gb1768e2-dirty"
// The number after the first dash is the commit count since the last tag
static int extract_build_number(const char* version) {
    // If the version contains a dash followed by a number
    const char* dash = strchr(version, '-');
    if (dash && dash[1] != '\0') {
        // Try to parse the number after the dash
        int build_num = atoi(dash + 1);
        if (build_num > 0) {
            ESP_LOGI(TAG, "Extracted build number %d from version '%s'", build_num, version);
            return build_num;
        }
    }

    // Default if we can't extract a build number
    ESP_LOGW(TAG, "Could not extract build number from '%s', using 0", version);
    return 0;
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

        // Extract build number if available
        current_build_number = extract_build_number(current_version);
        ESP_LOGI(TAG, "Current build number: %d", current_build_number);
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
    cJSON *root = cJSON_Parse(manifest_data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        return false;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url = cJSON_GetObjectItem(root, "url");
    cJSON *build_timestamp = cJSON_GetObjectItem(root, "build_timestamp");
    cJSON *build_timestamp_epoch = cJSON_GetObjectItem(root, "build_timestamp_epoch");
    cJSON *build_number = cJSON_GetObjectItem(root, "build_number");
    cJSON *git_describe = cJSON_GetObjectItem(root, "git_describe");

    if (!cJSON_IsString(version) || !cJSON_IsString(url)) {
        ESP_LOGE(TAG, "Manifest missing required fields");
        cJSON_Delete(root);
        return false;
    }

    const char *remote_version = version->valuestring;
    const char *firmware_url = url->valuestring;
    const char *timestamp = build_timestamp ? build_timestamp->valuestring : "unknown";
    const char *describe = git_describe ? git_describe->valuestring : "unknown";

    // Get remote build number for comparison
    int remote_build_number = 0;
    if (build_number && cJSON_IsNumber(build_number)) {
        remote_build_number = build_number->valueint;
    }

    ESP_LOGI(TAG, "Manifest info - version: %s, build number: %d, time: %s",
             remote_version, remote_build_number, timestamp);
    ESP_LOGI(TAG, "Git describe: %s", describe);
    ESP_LOGI(TAG, "Firmware URL: %s", firmware_url);

    // Extract the hash part from the remote version too
    const char* remote_hash = extract_git_hash(remote_version);

    ESP_LOGI(TAG, "Remote hash: %s, Local hash: %s", remote_hash, extracted_hash);
    ESP_LOGI(TAG, "Remote build: %d, Local build: %d", remote_build_number, current_build_number);

    // Check if we need to update based on version comparison
    bool update_needed = false;
    bool is_newer_version = false;

    // First check if the server version is newer than our version
    if (remote_build_number > current_build_number) {
        ESP_LOGI(TAG, "Remote build number is higher - this is a newer version");
        is_newer_version = true;
    } else if (remote_build_number < current_build_number) {
        ESP_LOGW(TAG, "Remote build number is lower - this would be a downgrade, skipping update");
        is_newer_version = false;
    } else {
        // If build numbers are the same, check hash
        if (strcmp(remote_hash, extracted_hash) != 0) {
            ESP_LOGW(TAG, "Same build number but different hash - cannot determine which is newer");
            // Consider same if build numbers match but hashes differ - this avoids unexpected downgrades
            is_newer_version = false;
        } else {
            ESP_LOGI(TAG, "Same version (matching hash and build number)");
            is_newer_version = false;  // Same version, not newer
        }
    }

    // Determine if an update is needed based on version and current state
    if (strlen(extracted_hash) == 0) {
        ESP_LOGI(TAG, "No current version hash set, will update regardless of version");
        update_needed = true;
    } else if (is_newer_version) {
        ESP_LOGI(TAG, "New version available, will update");
        update_needed = true;
    } else {
        ESP_LOGI(TAG, "No update needed - current version is same or newer");

        // Make sure we mark our app as valid even if no update is needed
        mark_app_valid();
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
            ESP_LOGI(TAG, "OTA update successful! Rebooting...");
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