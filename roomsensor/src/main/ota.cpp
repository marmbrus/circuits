#include "BLEScanner.h"
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
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "config.h"
#include "system_state.h"
#include "communication.h"
#include "build_info.h"
#include "debug.h"
#include "ConfigurationManager.h"
#include "WifiConfig.h"
#include "device_cert.h"
#include <string>

/*
 * OTA Update State Machine
 * ========================
 *
 * Core OTA Decision Logic:
 * The ONLY factor for deciding whether to upgrade is comparing this build's timestamp
 * (BUILD_INFO_TIMESTAMP, stamped by build_info.sh) with the timestamp in the server manifest
 * (build_timestamp_epoch, which deploy_ota.sh takes from the same stamp).
 * If server timestamp > local timestamp, perform upgrade
 *
 * Version Format:
 * - Clean builds: Just git hash (e.g., "9046537")
 * - Dirty builds: Uniform format "revYYYYMMDDHHMMSS-shortHash-dirty"
 *
 * Behavior by Partition Type:
 * - Factory partition: Always reports status as DEV_BUILD, but follows the same upgrade
 *   rules - upgrades if server version is newer, no special treatment
 * - OTA partition: Reports status based on timestamp comparison (UP_TO_DATE, UPGRADING)
 *
 * Status Reporting:
 * - DEV_BUILD: Running from factory partition (regardless of version)
 * - UPGRADING_FIRMWARE: When a firmware update is in progress
 * - UP_TO_DATE: When running latest version available on server
 * - ERROR: When an update fails
 *
 * Process Flow:
 * 1. ota_init() starts a background task (ota_update_task)
 * 2. Task waits for full system connectivity (WiFi + MQTT)
 * 3. When connected, periodically checks server for manifest
 * 4. Parses manifest and compares build timestamps
 * 5. If newer version exists, triggers OTA update
 * 6. Reports status to MQTT at key points (boot, network connection, before/after updates)
 */

static const char *TAG = "ota";
// Brayden's nginx on smll, which only answers boards presenting a house certificate (device_cert)
#define UPDATES_BASE_URL "https://updates.acgt.net"
static const char *MANIFEST_URL = UPDATES_BASE_URL "/manifest.json";
static char current_version[64] = {0}; // Store current firmware version
static TaskHandle_t ota_task_handle = NULL;
static bool ota_running = false;
// Validation delay management is handled within the OTA task loop

// Event group to signal that network is connected
static EventGroupHandle_t network_event_group;

// This build's timestamp (build_info.sh)
static const time_t FIRMWARE_BUILD_TIME = (time_t)BUILD_INFO_TIMESTAMP;

// NVS keys (for logging purposes only, not for decision-making)
static const char* NVS_NAMESPACE = "ota";
static const char* NVS_LAST_OTA_TIME = "last_ota_time";
static const char* NVS_LAST_OTA_HASH = "last_ota_hash";
// The update this board expects to boot into next (ota_state_t)
static const char* NVS_EXPECTED_PARTITION = "exp_partition";
static const char* NVS_EXPECTED_VERSION = "exp_version";
static const char* NVS_EXPECTED_TIMESTAMP = "exp_timestamp";

// Unified OTA status types (single status per device)
typedef enum {
    OTA_STATUS_DEV_BUILD,
    OTA_STATUS_UPGRADING_FIRMWARE,
    OTA_STATUS_AWAITING_VALIDATION,
    OTA_STATUS_ROLLED_BACK,
    OTA_STATUS_UP_TO_DATE,
    OTA_STATUS_ERROR,
} ota_status_t;

// Store remote version info for status reporting
static char remote_version[64] = {0};
static time_t remote_timestamp = 0;
// This build's timestamp, as compared with the manifest's (see load_local_firmware_info)
static time_t local_fw_timestamp = 0;
// Force-OTA overrides (used by console command)
static bool g_force_ota = false;
static char g_force_url[256] = {0};
static char g_force_version[64] = {0};

// The update this board expects to boot into, kept in NVS between writing it and rebooting:
// booting anything else means the update rolled back
typedef struct {
    char expected_partition[16];
    char ota_version[64];
    time_t ota_timestamp;
} ota_state_t;

// Forward declare
static void report_ota_status(ota_status_t status, const char* error_message);
static esp_err_t perform_https_ota_from_url(const char* firmware_url);

static bool read_ota_state(ota_state_t* state) {
    if (!state) return false;
    memset(state, 0, sizeof(ota_state_t));
    nvs_handle_t n;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &n) != ESP_OK) return false;
    size_t len = sizeof(state->expected_partition);
    if (nvs_get_str(n, NVS_EXPECTED_PARTITION, state->expected_partition, &len) != ESP_OK) {
        state->expected_partition[0] = '\0';
    }
    len = sizeof(state->ota_version);
    if (nvs_get_str(n, NVS_EXPECTED_VERSION, state->ota_version, &len) != ESP_OK) {
        state->ota_version[0] = '\0';
    }
    int64_t ts = 0;
    if (nvs_get_i64(n, NVS_EXPECTED_TIMESTAMP, &ts) == ESP_OK) state->ota_timestamp = (time_t)ts;
    nvs_close(n);
    return state->expected_partition[0] != '\0';
}

static bool write_ota_state(const ota_state_t* state) {
    if (!state) return false;
    nvs_handle_t n;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &n) != ESP_OK) return false;
    bool ok = nvs_set_str(n, NVS_EXPECTED_PARTITION, state->expected_partition) == ESP_OK &&
              nvs_set_str(n, NVS_EXPECTED_VERSION, state->ota_version) == ESP_OK &&
              nvs_set_i64(n, NVS_EXPECTED_TIMESTAMP, (int64_t)state->ota_timestamp) == ESP_OK &&
              nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
}

static void clear_ota_state(void) {
    nvs_handle_t n;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &n) != ESP_OK) return;
    nvs_erase_key(n, NVS_EXPECTED_PARTITION);
    nvs_erase_key(n, NVS_EXPECTED_VERSION);
    nvs_erase_key(n, NVS_EXPECTED_TIMESTAMP);
    nvs_commit(n);
    nvs_close(n);
}

// This build's version and timestamp, which the firmware carries itself (build_info.sh)
static void load_local_firmware_info(void) {
    strncpy(current_version, BUILD_INFO_VERSION, sizeof(current_version) - 1);
    local_fw_timestamp = FIRMWARE_BUILD_TIME;
}

// Determine whether system time has been synchronized (via SNTP)
static bool is_time_synchronized(void) {
    // Consider time valid if after 2021-01-01 (1609459200)
    time_t now = time(NULL);
    return (now >= 1609459200);
}

// Set the network connected bit when network is up
void ota_notify_network_connected(void) {
    // Network connectivity is now handled by checking system state directly
    ESP_LOGD(TAG, "Network connection notification received");
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

    load_local_firmware_info();
    time_t effective_ts = local_fw_timestamp;
    char build_time_str[64];
    struct tm build_timeinfo;
    gmtime_r(&effective_ts, &build_timeinfo);
    strftime(build_time_str, sizeof(build_time_str), "%Y-%m-%d %H:%M:%S UTC", &build_timeinfo);
    ESP_LOGI(TAG, "Current firmware: %s, built %s (epoch: %ld)", current_version, build_time_str, (long)effective_ts);

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

// Schedule marking the app valid after a delay if we're in PENDING_VERIFY
// No separate validation task; OTA task manages the 5-minute window.

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

        // Timestamp validation (no detailed logging needed - raw values logged below)
        if (FIRMWARE_BUILD_TIME <= 0) {
            ESP_LOGW(TAG, "Local firmware build time is not available");
        }
    } else {
        ESP_LOGW(TAG, "Remote manifest missing build timestamp");
    }

    // Store the remote version for comparison and reporting

    // Check if we're running from factory partition
    bool is_factory = false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        is_factory = true;
    }

    time_t local_effective_ts = local_fw_timestamp;

    // Handle forced OTA path first: bypass timestamp comparison
    if (g_force_ota) {
        // Prefer forced URL/version if provided
        if (g_force_url[0]) firmware_url = g_force_url;
        if (g_force_version[0]) remote_version_str = g_force_version;

        report_ota_status(OTA_STATUS_UPGRADING_FIRMWARE, NULL);
        ESP_LOGI(TAG, "Force-updating firmware from %s", firmware_url);
        esp_err_t ret = perform_https_ota_from_url(firmware_url);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Forced OTA successful; saving info and rebooting");
            time_t now_ts = time(NULL);
            if (now_ts <= 0) now_ts = remote_timestamp > 0 ? remote_timestamp : 0;
            // Record the attempt in NVS
            save_ota_info((now_ts > 0 ? now_ts : FIRMWARE_BUILD_TIME), remote_version_str);
            g_force_ota = false; g_force_url[0]=0; g_force_version[0]=0;
            cJSON_Delete(root);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return true;
        } else {
            ESP_LOGE(TAG, "Forced OTA failed: %s", esp_err_to_name(ret));
            log_memory_snapshot(TAG, "forced_ota_failed");
            mark_app_valid();
            report_ota_status(OTA_STATUS_ERROR, esp_err_to_name(ret));
            g_force_ota = false; g_force_url[0]=0; g_force_version[0]=0;
            cJSON_Delete(root);
            return false;
        }
    }

    // Before deciding, skip retrying the exact same manifest version we last attempted
    bool skip_firmware = false;
    if (cJSON_IsString(version)) {
        char last_hash[64] = {0}; size_t hash_len = sizeof(last_hash);
        nvs_handle_t n;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &n) == ESP_OK) {
            if (nvs_get_str(n, NVS_LAST_OTA_HASH, last_hash, &hash_len) == ESP_OK) {
                if (last_hash[0] != '\0' && strcmp(last_hash, version->valuestring) == 0) {
                    ESP_LOGD(TAG, "Skipping firmware OTA: manifest version matches last attempted (%s)", last_hash);
                    // Always skip firmware for same-version attempts
                    skip_firmware = true;
                }
            }
            nvs_close(n);
        }
    }

    // PRIMARY CHECK: Compare build timestamps
    if (!skip_firmware && remote_timestamp > 0 && local_effective_ts > 0) {
        ESP_LOGD(TAG, "Raw timestamp values - Remote: %ld, Local(eff): %ld",
                (long)remote_timestamp, (long)local_effective_ts);

        // Only run sanity checks if system time is known
        if (is_time_synchronized()) {
            // Check for unrealistic timestamp values (e.g., more than 10 years in the future)
            time_t current_time = time(NULL);
            if (remote_timestamp > current_time + 315360000) { // 10 years in seconds
                ESP_LOGW(TAG, "Remote timestamp is unrealistically far in the future, may be corrupted");
            }
            if (local_effective_ts > current_time + 315360000) {
                ESP_LOGW(TAG, "Local build timestamp is unrealistically far in the future, may be corrupted");
            }
        } else {
            ESP_LOGD(TAG, "Skipping timestamp sanity checks until SNTP time is set");
        }

        // Directly compare the timestamps
        if (remote_timestamp > local_effective_ts) {
            time_t time_diff = remote_timestamp - local_effective_ts;

            // Keep this log concise
            ESP_LOGI(TAG, "Newer version found (%ld sec newer), starting upgrade...", (long)time_diff);

            // Special message for factory builds
            if (is_factory) {
                ESP_LOGI(TAG, "OTA update available for factory build - will upgrade to: %s", remote_version_str);
            }

            // LED animation during OTA will be handled by LEDManager patterns

            // Report OTA status as UPGRADING_FIRMWARE before starting the update
            report_ota_status(OTA_STATUS_UPGRADING_FIRMWARE, NULL);

            // Perform the OTA update
            ESP_LOGI(TAG, "Starting firmware update from %s", firmware_url);

            esp_err_t ret = perform_https_ota_from_url(firmware_url);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "OTA update successful! Saving update info and rebooting...");

                // Set the boot partition to the new OTA partition
                const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
                if (update_partition == NULL) {
                    ESP_LOGE(TAG, "Could not find OTA partition to boot from");
                    report_ota_status(OTA_STATUS_ERROR, "No OTA partition found");
                    cJSON_Delete(root);
                    return false;
                }
                ESP_LOGI(TAG, "Next boot partition: %s", update_partition->label);

                // CRITICAL: Write OTA state BEFORE rebooting
                ota_state_t next_state = {0};
                strncpy(next_state.expected_partition, update_partition->label, sizeof(next_state.expected_partition) - 1);
                strncpy(next_state.ota_version, remote_version_str, sizeof(next_state.ota_version) - 1);
                next_state.ota_timestamp = remote_timestamp;
                write_ota_state(&next_state);

                // Save OTA info to NVS for redundancy/logging
                save_ota_info(remote_timestamp, remote_version_str);

                cJSON_Delete(root);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
                return true;
            } else {
                ESP_LOGE(TAG, "OTA update failed with error: %s", esp_err_to_name(ret));
                // Capture memory state to diagnose TLS/HTTP allocation failures
                log_memory_snapshot(TAG, "ota_https_ota_failed");
                // LED state handled by LEDManager patterns; validation handled in OTA loop
                cJSON_Delete(root);
                report_ota_status(OTA_STATUS_ERROR, esp_err_to_name(ret));
                return false;
            }
        } else if (remote_timestamp < local_effective_ts) {
            // Local is newer than remote; final status will be decided later
            ESP_LOGI(TAG, "Running newer version than available on server");
        } else {
            // Versions are identical; final status will be decided later
            ESP_LOGI(TAG, "Running the latest version (timestamps equal)");
        }
    } else if (!skip_firmware) {
        ESP_LOGD(TAG, "Cannot compare timestamps: Remote=%ld, Local(eff)=%ld. Deferring classification.",
                (long)remote_timestamp, (long)local_effective_ts);
    }

    // If we reach here, no firmware update is needed; classify and report

    // Compute predicates early for linear classification
    char running_ver[64] = {0};
    esp_app_desc_t running_desc = {};
    if (running && esp_ota_get_partition_description(running, &running_desc) == ESP_OK) {
        strncpy(running_ver, running_desc.version, sizeof(running_ver) - 1);
    }

    // Read last attempted OTA hash from NVS
    char last_hash_pre[64] = {0}; size_t last_hash_pre_len = sizeof(last_hash_pre);
    nvs_handle_t nvs_tmp_pre;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_tmp_pre) == ESP_OK) {
        (void)nvs_get_str(nvs_tmp_pre, NVS_LAST_OTA_HASH, last_hash_pre, &last_hash_pre_len);
        nvs_close(nvs_tmp_pre);
    }

    bool running_matches_remote = (running_ver[0] != '\0' && remote_version_str && strcmp(running_ver, remote_version_str) == 0);
    bool same_as_last_attempt_pre = (last_hash_pre[0] != '\0' && remote_version_str && strcmp(last_hash_pre, remote_version_str) == 0);

    esp_ota_img_states_t img_state_pre = ESP_OTA_IMG_UNDEFINED;
    (void)esp_ota_get_state_partition(running, &img_state_pre);
    bool pending_verify_pre = (img_state_pre == ESP_OTA_IMG_PENDING_VERIFY);

    // HIGHEST PRIORITY: Check for rollback by comparing expected vs actual partition.
    ota_state_t ota_state = {0};
    if (read_ota_state(&ota_state)) {
        if (strcmp(ota_state.expected_partition, running->label) != 0) {
            ESP_LOGW(TAG, "State: ROLLED_BACK (expected partition '%s' but running from '%s')", ota_state.expected_partition, running->label);
            clear_ota_state(); // Clear state after detecting rollback
            // Fall through to standard ROLLED_BACK reporting
        }
    }

    // If the running partition is pending verification, we are awaiting validation.
    if (pending_verify_pre) {
        ESP_LOGI(TAG, "State: AWAITING_VALIDATION (partition is pending verification)");
        report_ota_status(OTA_STATUS_AWAITING_VALIDATION, NULL);
        cJSON_Delete(root);
        return false;
    }

    // Linear classification
    bool remote_newer = (remote_timestamp > 0 && local_fw_timestamp > 0 && remote_timestamp > local_fw_timestamp);
    bool remote_older = (remote_timestamp > 0 && local_fw_timestamp > 0 && remote_timestamp < local_fw_timestamp);

    // ROLLED_BACK if server newer and we already tried this manifest version
    if (remote_newer && same_as_last_attempt_pre) {
        ESP_LOGI(TAG, "State: ROLLED_BACK (remote_newer && same_as_last_attempt)");
        report_ota_status(OTA_STATUS_ROLLED_BACK, NULL);
        cJSON_Delete(root);
        return false;
    }

    // DEV_BUILD (factory newer than server)
    if (is_factory && remote_older) {
        ESP_LOGI(TAG, "State: DEV_BUILD (factory && remote_older)");
        report_ota_status(OTA_STATUS_DEV_BUILD, NULL);
        cJSON_Delete(root);
        return false;
    }

    report_ota_status(OTA_STATUS_UP_TO_DATE, NULL);
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
                if (data_len > 0) {
                    char preview[101] = {0};
                    strncpy(preview, manifest_data, 100);
                    ESP_LOGD(TAG, "Manifest downloaded (%d bytes): %s%s", data_len,
                             preview, (strlen(manifest_data) > 100) ? "..." : "");

                    // Process the manifest
                    parse_manifest_and_check_update(manifest_data);
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

static esp_err_t perform_https_ota_from_url(const char* firmware_url) {
    esp_http_client_config_t config = {};
    config.url = firmware_url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    device_cert_configure_http(&config);
    config.skip_cert_common_name_check = false;
    config.buffer_size = 1024;      // reduce RX buffer to save internal RAM
    config.buffer_size_tx = 512;    // reduce TX buffer
    config.keep_alive_enable = false; // ensure server closes promptly
    config.timeout_ms = 30000; // 30 second timeout for firmware download

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    ota_config.bulk_flash_erase = true;
    // Add init callback to tweak headers before transfer
    ota_config.http_client_init_cb = [](esp_http_client_handle_t client) -> esp_err_t {
        esp_http_client_set_header(client, "User-Agent", "roomsensor-ota/1.0");
        esp_http_client_set_header(client, "Connection", "close");
        return ESP_OK;
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    // How close the download came to the end of this task's stack (in bytes; on success the board
    // reboots, so this is the only chance to see it)
    ESP_LOGI(TAG, "OTA download done (%s); OTA task stack never had less than %u bytes free",
             esp_err_to_name(ret), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    return ret;
}

extern "C" esp_err_t ota_force_update(const char* version_hash) {
    // Build URL based on provided hash or manifest
    if (version_hash && version_hash[0] != '\0') {
        snprintf(g_force_url, sizeof(g_force_url), UPDATES_BASE_URL "/firmware-%s.bin", version_hash);
        // Track a friendly version string for status
        snprintf(g_force_version, sizeof(g_force_version), "%s", version_hash);
    } else {
        // No hash: use manifest URL as-is; set force flag only
        g_force_url[0] = 0; // Will use manifest-provided URL inside parse_manifest_and_check_update
        g_force_version[0] = 0;
    }
    g_force_ota = true;
    ESP_LOGI(TAG, "Force OTA armed (hash=%s)", (version_hash && version_hash[0]) ? version_hash : "<manifest>");
    return ESP_OK;
}

// OTA update task that runs in the background
static void ota_update_task(void *pvParameter) {
    ESP_LOGI(TAG, "OTA update task started - task handle: %p", xTaskGetCurrentTaskHandle());
    
    // Minimal startup diagnostics
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGD(TAG, "OTA task stack high water: %u bytes", stack_high_water);

    // Validate that network event group exists
    if (network_event_group == NULL) {
        ESP_LOGE(TAG, "Network event group is NULL! OTA task exiting.");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGD(TAG, "Network event group validated: %p", network_event_group);

    // Get current version on startup
    // Major transition: capture current version
    get_current_version();

    // Track a 5-minute validation window for PENDING_VERIFY images
    TickType_t validation_deadline = 0;
    {
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running) {
            esp_ota_img_states_t ota_state;
            if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
                ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                validation_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5 * 60 * 1000);
                ESP_LOGI(TAG, "Pending verify image detected; will mark valid in 5 minutes");
            }
        }
    }

    uint32_t check_count = 0;
    bool was_connected = false;
    TickType_t last_network_check_time = 0;

    ESP_LOGI(TAG, "OTA monitoring loop started");

    // Main OTA loop - runs forever
    while (1) {
        // This loop now runs on a short, consistent interval (e.g., 1 second)
        // to allow for responsive state checks, like validation deadlines.

        // Check for system readiness
        bool is_connected = false;

        // Use system state to determine readiness
        extern SystemState get_system_state(void);
        SystemState current_state = get_system_state();
        
        // Debug: Log system state periodically or on changes
        static SystemState last_logged_state = (SystemState)-1;
        if (current_state != last_logged_state) {
            const char* state_names[] = {"STARTING", "WIFI_CONNECTING", "WIFI_CONNECTED", "MQTT_CONNECTING", "FULLY_CONNECTED"};
            const char* state_name = (current_state >= 0 && current_state <= 4) ? state_names[current_state] : "UNKNOWN";
            ESP_LOGI(TAG, "System state: %s", state_name);
            last_logged_state = current_state;
        }

        // Only treat as connected when the whole system is up (WiFi + MQTT)
        if (current_state == FULLY_CONNECTED) {
            is_connected = true;
        }

        // Network state change logging
        if (is_connected && !was_connected) {
            ESP_LOGI(TAG, "Network connected; OTA checks enabled");
            was_connected = true;
        } else if (!is_connected && was_connected) {
            ESP_LOGI(TAG, "Network disconnected; OTA checks paused");
            was_connected = false;
        }

        // If pending verify and deadline reached, mark valid once and sync version file
        if (validation_deadline != 0 && (int32_t)(xTaskGetTickCount() - validation_deadline) >= 0) {
            validation_deadline = 0; // single-shot
            mark_app_valid();
            clear_ota_state(); // OTA is complete and validated
            // Force an immediate re-check of the manifest to update status to UP_TO_DATE
            last_network_check_time = 0;
        }

        // Only proceed with OTA check if system is fully connected
        if (is_connected) {
            // Ensure SNTP has set the time before proceeding
            if (!is_time_synchronized()) {
                ESP_LOGD(TAG, "Waiting for SNTP before OTA checks");
            } else {
                // Check if it's time for the less frequent network check
                if (last_network_check_time == 0 || (xTaskGetTickCount() - last_network_check_time) >= pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS)) {
                    last_network_check_time = xTaskGetTickCount(); // Reset timer

                    // Check for updates
                    check_count++;
                    ESP_LOGI(TAG, "Checking for updates");

                    // A check is rare (every OTA_CHECK_INTERVAL_MS) and short, but its TLS handshake
                    // takes about 11 KB of internal DMA-capable RAM, and any download it starts takes
                    // more, along with the radio. So Bluetooth, whose controller holds about 30 KB of
                    // that RAM, shuts down for the check and whatever it leads to. After a firmware
                    // update the board reboots before it would start again.
                    ble::suspend(pdMS_TO_TICKS(5000));

                    // Debug: Memory before HTTP request
                    size_t heap_before_http = esp_get_free_heap_size();
                    (void)heap_before_http;

                    esp_err_t err = ESP_OK;
                    esp_http_client_handle_t client = NULL;

                    // Initialize HTTP client with channel-aware manifest selection
                    char url_buf[256];
                    const char* url_to_use = MANIFEST_URL;
                    {
                        using namespace config;
                        auto& cfg = GetConfigurationManager();
                        if (cfg.wifi().has_channel()) {
                            const std::string& ch = cfg.wifi().channel();
                            if (!ch.empty()) {
                                // manifest-<channel>.json
                                snprintf(url_buf, sizeof(url_buf), UPDATES_BASE_URL "/manifest-%s.json", ch.c_str());
                                url_buf[sizeof(url_buf)-1] = '\0';
                                url_to_use = url_buf;
                                ESP_LOGI(TAG, "Using channel manifest: %s", url_to_use);
                            }
                        }
                    }
                    esp_http_client_config_t config = {};
                    config.url = url_to_use;
                    config.event_handler = http_event_handler;
                    config.crt_bundle_attach = esp_crt_bundle_attach;
                    device_cert_configure_http(&config);
                    config.skip_cert_common_name_check = false;
                    config.timeout_ms = 10000; // Add 10s timeout to avoid hanging


                    client = esp_http_client_init(&config);
                    if (client == NULL) {
                        ESP_LOGE(TAG, "Failed to initialize HTTP client - insufficient memory?");
                        size_t heap_after_fail = esp_get_free_heap_size();
                        ESP_LOGE(TAG, "Free heap after HTTP client init failure: %zu bytes", heap_after_fail);
                        // Skip to delay and try again later
                    } else {


                        // Perform HTTP request with proper error handling

                        err = esp_http_client_perform(client);

                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "HTTP GET request failed: %s (0x%x)", esp_err_to_name(err), err);

                            // Additional error details
                            int status_code = esp_http_client_get_status_code(client);
                            int64_t content_length = esp_http_client_get_content_length(client);
                            ESP_LOGE(TAG, "HTTP error details - Status: %d, Content-Length: %lld",
                                     status_code, (long long)content_length);
                        } else {
                            int status_code = esp_http_client_get_status_code(client);
                            int64_t content_length = esp_http_client_get_content_length(client);
                            (void)status_code; (void)content_length;

                            if (status_code != 200) {
                                ESP_LOGW(TAG, "OTA check completed with unexpected status code: %d", status_code);
                            }
                        }
                        esp_http_client_cleanup(client);
                    }
                    ble::resume();
                }
            }
        } else {
            // Not fully connected, reset the network check timer so we check immediately on reconnect
            last_network_check_time = 0;
        }

        // Short, consistent delay for the main loop
        vTaskDelay(pdMS_TO_TICKS(1000));
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
        ESP_LOGI(TAG, "Creating network event group for OTA");
        network_event_group = xEventGroupCreate();
        if (network_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create event group - insufficient memory?");
            size_t free_heap = esp_get_free_heap_size();
            ESP_LOGE(TAG, "Free heap after event group creation failure: %zu bytes", free_heap);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Created network event group for OTA successfully");
    } else {
        ESP_LOGI(TAG, "Network event group already exists");
    }

    // Get current firmware version and partition info
    ESP_LOGI(TAG, "Getting current firmware version");
    get_current_version();

    // Check if we're running from factory partition (for future use)
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        // Report DEV_BUILD status - this will be done when status is published
        ESP_LOGI(TAG, "Running from factory partition (DEV_BUILD mode)");
    } else {
        ESP_LOGI(TAG, "Running from OTA partition");
    }

    // Validation handled by OTA task loop

    // Debug: Check memory before task creation
    size_t free_heap_mid = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap before task creation: %zu bytes", free_heap_mid);
    ESP_LOGI(TAG, "OTA task stack size: %d bytes, priority: %d", OTA_TASK_STACK_SIZE, OTA_TASK_PRIORITY);

    // Start OTA task if not already running
    if (!ota_running || ota_task_handle == NULL) {
        ESP_LOGI(TAG, "Creating OTA background task");
        ota_running = true;
        
        // Debug: Validate stack size requirements and memory fragmentation
        if (OTA_TASK_STACK_SIZE < 4096) {
            ESP_LOGW(TAG, "OTA task stack size may be too small: %d bytes", OTA_TASK_STACK_SIZE);
        }
        
        // Check for heap fragmentation by trying to allocate the required stack size
        void* test_alloc = malloc(OTA_TASK_STACK_SIZE);
        if (test_alloc) {
            ESP_LOGI(TAG, "Memory test: Successfully allocated %d bytes for stack test", OTA_TASK_STACK_SIZE);
            free(test_alloc);
        } else {
            ESP_LOGE(TAG, "Memory test: Failed to allocate %d bytes - heap fragmentation likely", OTA_TASK_STACK_SIZE);
            
            // Try smaller allocations to see what's available
            size_t test_sizes[] = {8192, 4096, 2048, 1024};
            for (int i = 0; i < 4; i++) {
                void* smaller_test = malloc(test_sizes[i]);
                if (smaller_test) {
                    ESP_LOGI(TAG, "Memory test: %zu bytes allocation succeeded", test_sizes[i]);
                    free(smaller_test);
                } else {
                    ESP_LOGE(TAG, "Memory test: %zu bytes allocation failed", test_sizes[i]);
                }
            }
        }
        
        // Check largest free block
        size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        ESP_LOGI(TAG, "Largest contiguous free block: %zu bytes (need %d bytes)", 
                 largest_free_block, OTA_TASK_STACK_SIZE);
        
        if (largest_free_block < OTA_TASK_STACK_SIZE) {
            ESP_LOGE(TAG, "Insufficient contiguous memory for OTA task stack!");
        }
        
        // Create task in internal RAM (stack must be in internal RAM for OTA when flash cache may be disabled)
        BaseType_t ret = xTaskCreate(
            ota_update_task,
            "ota_task",
            OTA_TASK_STACK_SIZE,
            NULL,
            OTA_TASK_PRIORITY,
            &ota_task_handle
        );

        if (ret != pdPASS) {
            ota_running = false; // Reset flag on failure
            ESP_LOGE(TAG, "Failed to create OTA task - xTaskCreate returned %d", ret);
            log_memory_snapshot(TAG, "ota_task_create_failed");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "OTA task created");
        
    } else {
        ESP_LOGW(TAG, "OTA task already running (handle: %p), skipping creation", ota_task_handle);
    }

    // Final: success

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

    // Validation handled by OTA task loop

    ESP_LOGI(TAG, "Checking for OTA updates from %s", MANIFEST_URL);

    esp_http_client_config_t config = {};
    config.url = MANIFEST_URL;
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    device_cert_configure_http(&config);
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
static void report_ota_status(ota_status_t status, const char* error_message) {
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
        case OTA_STATUS_DEV_BUILD: status_str = "DEV_BUILD"; break;
        case OTA_STATUS_UPGRADING_FIRMWARE: status_str = "UPGRADING_FIRMWARE"; break;
        case OTA_STATUS_AWAITING_VALIDATION: status_str = "AWAITING_VALIDATION"; break;
        case OTA_STATUS_ROLLED_BACK: status_str = "ROLLED_BACK"; break;
        case OTA_STATUS_UP_TO_DATE: status_str = "UP_TO_DATE"; break;
        case OTA_STATUS_ERROR: status_str = "ERROR"; break;
    }
    cJSON_AddStringToObject(ota_json, "status", status_str);

    // Release channel (default to "prod" when not configured)
    {
        using namespace config;
        ConfigurationManager& cfg = GetConfigurationManager();
        const char* ch = "prod";
        if (cfg.wifi().has_channel()) {
            const std::string& s = cfg.wifi().channel();
            if (!s.empty()) ch = s.c_str();
        }
        cJSON_AddStringToObject(ota_json, "channel", ch);
    }

    // Partition info
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        cJSON_AddStringToObject(ota_json, "partition",
            running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? "factory" : "ota");
    }

    // Firmware versions/times
    if (strlen(current_version) > 0) {
        cJSON_AddStringToObject(ota_json, "firmware_local_version", current_version);
    }
    if (strlen(remote_version) > 0) {
        cJSON_AddStringToObject(ota_json, "firmware_remote_version", remote_version);
    }

    // Format build timestamps in ISO format
    {
        time_t local_effective_ts_json = local_fw_timestamp;
        if (local_effective_ts_json > 0) {
        char build_time_str[64];
        struct tm build_timeinfo;
        gmtime_r(&local_effective_ts_json, &build_timeinfo);
        strftime(build_time_str, sizeof(build_time_str), "%Y-%m-%dT%H:%M:%SZ", &build_timeinfo);
        cJSON_AddStringToObject(ota_json, "local_build_time", build_time_str);
        }
    }

    if (remote_timestamp > 0) {
        char remote_time_str[64];
        struct tm remote_tm;
        gmtime_r(&remote_timestamp, &remote_tm);
        strftime(remote_time_str, sizeof(remote_time_str), "%Y-%m-%dT%H:%M:%SZ", &remote_tm);
        cJSON_AddStringToObject(ota_json, "remote_build_time", remote_time_str);
        ESP_LOGD(TAG, "Adding remote_build_time to status: %s", remote_time_str);
    }

    if (status == OTA_STATUS_ERROR && error_message && strlen(error_message) > 0) {
        cJSON_AddStringToObject(ota_json, "error", error_message);
    }

    // Convert to string and publish
    char *ota_string = cJSON_Print(ota_json);
    if (ota_string) {
        ESP_LOGI(TAG, "Publishing OTA status: %s", ota_string);
        // Centralized mapping: publish_to_topic("ota", ...) -> sensor/<mac>/device/ota (retained)
        publish_to_topic("ota", ota_string, 1, 1);
        free(ota_string);
    } else {
        ESP_LOGE(TAG, "Failed to convert OTA status to string");
    }

    cJSON_Delete(ota_json);
}

// Public function to report OTA status
void ota_report_status(void) {
    // Determine current status using same invariants as the linear flow
    const esp_partition_t *running = esp_ota_get_running_partition();
    bool is_factory = (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);

    load_local_firmware_info();

    // last attempt hash
    char last_hash[64] = {0}; size_t hash_len = sizeof(last_hash);
    nvs_handle_t n;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &n) == ESP_OK) {
        (void)nvs_get_str(n, NVS_LAST_OTA_HASH, last_hash, &hash_len);
        nvs_close(n);
    }
    bool same_as_last_attempt = (last_hash[0] && strlen(remote_version) && strcmp(last_hash, remote_version) == 0);

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    (void)esp_ota_get_state_partition(running, &state);
    bool pending_verify = (state == ESP_OTA_IMG_PENDING_VERIFY);

    bool remote_newer = (remote_timestamp > 0 && local_fw_timestamp > 0 && remote_timestamp > local_fw_timestamp);
    bool remote_equal = (remote_timestamp > 0 && local_fw_timestamp > 0 && remote_timestamp == local_fw_timestamp);
    bool remote_older = (remote_timestamp > 0 && local_fw_timestamp > 0 && remote_timestamp < local_fw_timestamp);

    ota_status_t status = OTA_STATUS_UP_TO_DATE;
    if (remote_newer && same_as_last_attempt) status = OTA_STATUS_ROLLED_BACK;
    else if (remote_equal && same_as_last_attempt && pending_verify) status = OTA_STATUS_AWAITING_VALIDATION;
    else if (is_factory && remote_older) status = OTA_STATUS_DEV_BUILD;
    else if (remote_newer && !same_as_last_attempt) status = OTA_STATUS_UPGRADING_FIRMWARE;
    else status = OTA_STATUS_UP_TO_DATE;

    report_ota_status(status, NULL);
}