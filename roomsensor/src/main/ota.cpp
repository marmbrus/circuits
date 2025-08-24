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
#include "filesystem.h"


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

// Build timestamp is provided as FIRMWARE_BUILD_TIMESTAMP compile definition
#ifndef FIRMWARE_BUILD_TIMESTAMP
#error "FIRMWARE_BUILD_TIMESTAMP not defined - check CMakeLists.txt"
#endif

static const char *TAG = "ota";
static const char *MANIFEST_URL = "https://updates.gaia.bio/manifest.json";
static char current_version[64] = {0}; // Store current firmware version
static TaskHandle_t ota_task_handle = NULL;
static bool ota_running = false;

// Event group to signal that network is connected
static EventGroupHandle_t network_event_group;

// Get the embedded build timestamp (set at compile time)
static const time_t FIRMWARE_BUILD_TIME = (time_t)FIRMWARE_BUILD_TIMESTAMP;

// NVS keys (for logging purposes only, not for decision-making)
static const char* NVS_NAMESPACE = "ota";
static const char* NVS_LAST_OTA_TIME = "last_ota_time";
static const char* NVS_LAST_OTA_HASH = "last_ota_hash";

// Unified OTA status types (single status per device)
typedef enum {
    OTA_STATUS_DEV_BUILD,
    OTA_STATUS_UPGRADING_FIRMWARE,
    OTA_STATUS_UPGRADING_WEB,
    OTA_STATUS_UP_TO_DATE,
    OTA_STATUS_ERROR,
} ota_status_t;

// Store remote version info for status reporting
static char remote_version[64] = {0};
static time_t remote_timestamp = 0;

static char web_remote_version[33] = {0};
static time_t web_remote_timestamp = 0;
static char web_local_version[33] = {0};
static time_t web_local_timestamp = 0;
static char web_last_error[96] = {0};



// Forward declare file read helper before first use
static char* read_text_file(const char* path);
static bool write_text_file_atomic(const char* path, const char* text);





// Forward declarations
static void report_ota_status(ota_status_t status, const char* error_message);

// Helper: read a small JSON file fully into memory
static char* read_text_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || sz > 64 * 1024) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

// Helper: write buffer to a file atomically via temp path then rename
static bool write_text_file_atomic(const char* path, const char* text) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    if (!ok) { remove(tmp); return false; }
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

// Helper: copy a file
static bool copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    FILE* out = fopen(tmp, "wb");
    if (!out) { fclose(in); return false; }
    uint8_t buf[2048];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    if (rename(tmp, dst) != 0) { remove(tmp); return false; }
    return true;
}

// Load local web app info from /storage/webapp.json
static void load_local_web_info(void) {
    web_local_version[0] = '\0';
    web_local_timestamp = 0;
    const char* path = "/storage/webapp.json";
    char* s = read_text_file(path);
    if (!s) {
        ESP_LOGI(TAG, "No local webapp.json found; assuming no local web info");
        return;
    }
    cJSON* root = cJSON_Parse(s);
    if (!root) { free(s); return; }
    const cJSON* v = cJSON_GetObjectItem(root, "local_version");
    const cJSON* vd = cJSON_GetObjectItem(root, "local_git_describe");
    const cJSON* t = cJSON_GetObjectItem(root, "local_build_timestamp_epoch");
    if (cJSON_IsString(vd)) {
        strncpy(web_local_version, vd->valuestring, sizeof(web_local_version) - 1);
    } else if (cJSON_IsString(v)) {
        strncpy(web_local_version, v->valuestring, sizeof(web_local_version) - 1);
    }
    if (cJSON_IsNumber(t)) {
        web_local_timestamp = (time_t)t->valuedouble;
    }
    cJSON_Delete(root);
    free(s);
}

// Persist local web app info to /storage/webapp.json
static void save_local_web_info(const char* version, time_t ts_epoch) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "local_version", version ? version : "");
    // ISO time
    char iso[64];
    struct tm tm_time;
    gmtime_r(&ts_epoch, &tm_time);
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_time);
    cJSON_AddStringToObject(root, "local_build_timestamp", iso);
    cJSON_AddNumberToObject(root, "local_build_timestamp_epoch", (double)ts_epoch);
    char* txt = cJSON_PrintUnformatted(root);
    if (txt) {
        write_text_file_atomic("/storage/webapp.json", txt);
        free(txt);
    }
    cJSON_Delete(root);
}

// Download URL to a temp file and then copy to the final targets
static esp_err_t download_web_asset(const char* url, const char* version_hash) {
    if (!url || !version_hash || strlen(version_hash) == 0) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Downloading web asset from %s", url);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.skip_cert_common_name_check = false;
    cfg.timeout_ms = 30000;
    cfg.disable_auto_redirect = false; // follow 30x redirects

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;
    // Be explicit about headers
    esp_http_client_set_header(client, "User-Agent", "roomsensor-ota/1.0");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open web URL: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    // Read and validate headers
    int64_t hdrs = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    long long content_length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "Web GET status=%d, content_length=%lld (hdrs=%lld)", status, content_length, (long long)hdrs);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Unexpected HTTP status for web asset: %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    const char* tmp_path = "/storage/.web_download.tmp";
    FILE* f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open temp file for web download");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t buf[2048];
    int read_total = 0;
    while (1) {
        int r = esp_http_client_read(client, (char*)buf, sizeof(buf));
        if (r < 0) {
            ESP_LOGE(TAG, "Error reading web content: %d", r);
            fclose(f);
            remove(tmp_path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (r == 0) break; // connection closed
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            ESP_LOGE(TAG, "Error writing temp web file");
            fclose(f);
            remove(tmp_path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        read_total += r;
    }
    fclose(f);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_total <= 0) {
        ESP_LOGE(TAG, "Downloaded web asset is empty (status=%d, content_length=%lld)", status, content_length);
        remove(tmp_path);
        return ESP_FAIL;
    }

    char versioned_path[128];
    snprintf(versioned_path, sizeof(versioned_path), "/storage/index-%s.html.gz", version_hash);

    // Prune old versions: keep at most current + this new version
    // Remove any existing previous version different from current and new
    // Read local manifest to know the current version filename
    char* cur_info = read_text_file("/storage/webapp.json");
    char cur_ver[64] = {0};
    if (cur_info) {
        cJSON* j = cJSON_Parse(cur_info);
        if (j) {
            const cJSON* v = cJSON_GetObjectItem(j, "local_version");
            if (cJSON_IsString(v)) strncpy(cur_ver, v->valuestring, sizeof(cur_ver)-1);
            cJSON_Delete(j);
        }
        free(cur_info);
    }

    // Copy temp to versioned and current
    if (!copy_file(tmp_path, versioned_path)) {
        ESP_LOGE(TAG, "Failed to write versioned web file");
        remove(tmp_path);
        return ESP_FAIL;
    }
    if (!copy_file(tmp_path, "/storage/index.html.gz")) {
        ESP_LOGE(TAG, "Failed to update current index.html.gz");
        remove(tmp_path);
        return ESP_FAIL;
    }

    // Remove older archived version if it exists and differs from new/current
    if (cur_ver[0] && strcmp(cur_ver, version_hash) != 0) {
        char old_path[128];
        snprintf(old_path, sizeof(old_path), "/storage/index-%s.html.gz", cur_ver);
        remove(old_path);
    }
    remove(tmp_path);
    return ESP_OK;
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

    // Get version info from nvs or partition
    esp_app_desc_t app_desc;
    if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
        // Store the current firmware version
        strncpy(current_version, app_desc.version, sizeof(current_version) - 1);
        ESP_LOGI(TAG, "Current firmware version: %s", current_version);

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
    // Web fields (optional)
    cJSON *web_version = cJSON_GetObjectItem(root, "web_version");
    cJSON *web_url = cJSON_GetObjectItem(root, "web_url");
    cJSON *web_build_timestamp_epoch = cJSON_GetObjectItem(root, "web_build_timestamp_epoch");

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

    // Load local web info from LittleFS
    load_local_web_info();

    // Use embedded build timestamp directly for comparison

    // PRIMARY CHECK: Compare build timestamps
    if (remote_timestamp > 0 && FIRMWARE_BUILD_TIME > 0) {
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
                ESP_LOGI(TAG, "OTA update available for factory build - will upgrade to: %s", remote_version_str);
            }

            // LED animation during OTA will be handled by LEDManager patterns

            // Report OTA status as UPGRADING_FIRMWARE before starting the update
            report_ota_status(OTA_STATUS_UPGRADING_FIRMWARE, NULL);

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
                save_ota_info(remote_timestamp, remote_version_str);

                cJSON_Delete(root);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
                return true;
            } else {
                ESP_LOGE(TAG, "OTA update failed with error: %s", esp_err_to_name(ret));
                // LED state handled by LEDManager patterns
                mark_app_valid();
                cJSON_Delete(root);
                report_ota_status(OTA_STATUS_ERROR, esp_err_to_name(ret));
                return false;
            }
        } else if (remote_timestamp < FIRMWARE_BUILD_TIME) {
            // Local is newer than remote
            if (is_factory) {
                // Report as DEV_BUILD for factory partition
                report_ota_status(OTA_STATUS_DEV_BUILD, NULL);
                ESP_LOGI(TAG, "Running factory build with newer version than server");
            } else {
                // Treat as up to date when local newer than server
                report_ota_status(OTA_STATUS_UP_TO_DATE, NULL);
                ESP_LOGI(TAG, "Running newer version than available on server");
            }
        } else {
            // Versions are identical
            if (is_factory) {
                // Report as DEV_BUILD for factory partition
                report_ota_status(OTA_STATUS_DEV_BUILD, NULL);
                ESP_LOGI(TAG, "Running factory build with same version as server");
            } else {
                // Report as UP_TO_DATE for OTA partition
                report_ota_status(OTA_STATUS_UP_TO_DATE, NULL);
                ESP_LOGI(TAG, "Running the latest version");
            }
        }
    } else {
        ESP_LOGW(TAG, "Cannot compare timestamps: Remote=%ld, Local=%ld. Skipping update.",
                (long)remote_timestamp, (long)FIRMWARE_BUILD_TIME);

        // Always report status even if we can't compare versions
        if (is_factory) {
            report_ota_status(OTA_STATUS_DEV_BUILD, NULL);
            ESP_LOGI(TAG, "Running factory build, status unknown (missing timestamp)");
        } else {
            // Default to UP_TO_DATE if we can't determine
            report_ota_status(OTA_STATUS_UP_TO_DATE, NULL);
            ESP_LOGI(TAG, "Running OTA partition, status unknown (missing timestamp)");
        }
    }

    // If we reach here, no firmware update is needed; proceed to check web app

    bool had_error = false;

    // Reset last error before web check
    web_last_error[0] = '\0';

    // Populate remote web fields if present
    if (cJSON_IsString(web_version)) {
        strncpy(web_remote_version, web_version->valuestring, sizeof(web_remote_version) - 1);
    } else {
        web_remote_version[0] = '\0';
    }
    web_remote_timestamp = 0;
    if (cJSON_IsNumber(web_build_timestamp_epoch)) {
        web_remote_timestamp = (time_t)web_build_timestamp_epoch->valuedouble;
    }

    // Load local web info from file if any
    load_local_web_info();

            // In DEV_BUILD (factory), treat local web as at least as new as the firmware build
        // to avoid overwriting serial-flashed dev images unless the server is strictly newer.
        if (is_factory) {
            if (FIRMWARE_BUILD_TIME > 0 && web_local_timestamp < FIRMWARE_BUILD_TIME) {
                web_local_timestamp = FIRMWARE_BUILD_TIME;
                if (strlen(current_version) > 0) {
                    strncpy(web_local_version, current_version, sizeof(web_local_version) - 1);
                }
            }
        }

    /* unified status: no separate web_status */

    if (cJSON_IsString(web_url) && web_remote_timestamp > 0) {
        const char* remote_web_url = web_url->valuestring;
        const char* remote_web_hash = web_remote_version;

        if (web_local_timestamp > 0 && web_local_timestamp > web_remote_timestamp) {
            // Local dev build is newer; keep it
            /* unified status: no separate web_status */
            ESP_LOGI(TAG, "Local web assets newer than server; skipping web update");
        } else if (web_local_timestamp > 0 && web_local_timestamp == web_remote_timestamp) {
            /* unified status: no separate web_status */
            ESP_LOGI(TAG, "Web assets up to date");
        } else {
            // Need to update web
            report_ota_status(OTA_STATUS_UPGRADING_WEB, NULL);

            esp_err_t wret = download_web_asset(remote_web_url, remote_web_hash);
            if (wret == ESP_OK) {
                // Save local info for future comparisons
                save_local_web_info(remote_web_hash, web_remote_timestamp);
                strncpy(web_local_version, remote_web_hash, sizeof(web_local_version) - 1);
                web_local_timestamp = web_remote_timestamp;
                /* unified status: no separate web_status */
                ESP_LOGI(TAG, "Web assets updated successfully to %s", remote_web_hash);
            } else {
                snprintf(web_last_error, sizeof(web_last_error), "web download failed: %s", esp_err_to_name(wret));
                ESP_LOGE(TAG, "Web update failed: %s", web_last_error);
                report_ota_status(OTA_STATUS_ERROR, web_last_error);
                had_error = true;
            }
        }
    }

    // Firmware remains valid; report final status unless an error was already reported
    mark_app_valid();
    cJSON_Delete(root);
    if (!had_error) {
        // Keep DEV_BUILD final status for factory partition to reflect local dev flash
        if (is_factory) {
            report_ota_status(OTA_STATUS_DEV_BUILD, NULL);
        } else {
            report_ota_status(OTA_STATUS_UP_TO_DATE, NULL);
        }
    }
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

    // Ensure LittleFS is mounted for web OTA reads/writes
    webfs::init("storage", false);

    // Check if we're running from factory partition (for future use)
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        // Report DEV_BUILD status - this will be done when status is published
        ESP_LOGD(TAG, "Running from factory partition");
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
        case OTA_STATUS_UPGRADING_WEB: status_str = "UPGRADING_WEB"; break;
        case OTA_STATUS_UP_TO_DATE: status_str = "UP_TO_DATE"; break;
        case OTA_STATUS_ERROR: status_str = "ERROR"; break;
    }
    cJSON_AddStringToObject(ota_json, "status", status_str);

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

    // Web versions/times
    if (web_local_version[0]) cJSON_AddStringToObject(ota_json, "web_local_version", web_local_version);
    if (web_remote_version[0]) cJSON_AddStringToObject(ota_json, "web_remote_version", web_remote_version);
    if (web_local_timestamp > 0) {
        char t[64]; struct tm tm1; gmtime_r(&web_local_timestamp, &tm1);
        strftime(t, sizeof(t), "%Y-%m-%dT%H:%M:%SZ", &tm1);
        cJSON_AddStringToObject(ota_json, "web_local_build_time", t);
    }
    if (web_remote_timestamp > 0) {
        char t[64]; struct tm tm2; gmtime_r(&web_remote_timestamp, &tm2);
        strftime(t, sizeof(t), "%Y-%m-%dT%H:%M:%SZ", &tm2);
        cJSON_AddStringToObject(ota_json, "web_remote_build_time", t);
    }
    if (status == OTA_STATUS_ERROR) {
        if (error_message && strlen(error_message) > 0) cJSON_AddStringToObject(ota_json, "error", error_message);
        else if (web_last_error[0]) cJSON_AddStringToObject(ota_json, "error", web_last_error);
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
    // Skip early reporting if remote version info isn't available yet
    if (remote_timestamp == 0 || strlen(remote_version) == 0) {
        ESP_LOGD(TAG, "Skipping OTA status report - remote version info not available yet");
        return;
    }

    // Determine current status
    ota_status_t status = OTA_STATUS_UP_TO_DATE;

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
                status = OTA_STATUS_UPGRADING_FIRMWARE;
            }
        } else {
            // Default to UP_TO_DATE if we can't determine, but only for OTA partitions
            status = OTA_STATUS_UP_TO_DATE;
        }
    }

    // Report the status
    report_ota_status(status, NULL);
}