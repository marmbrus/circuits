#include "telemetry.h"
#include "ConfigurationManager.h"
#include "wifi.h"
#include "communication.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "telemetry";

// Format MAC as lowercase hex without separators (e.g., 28372f8115f8)
static void format_mac_nosep_lower(char* out, size_t out_len) {
    const uint8_t* device_mac = get_device_mac();
    snprintf(out, out_len, "%02x%02x%02x%02x%02x%02x",
             device_mac[0], device_mac[1], device_mac[2],
             device_mac[3], device_mac[4], device_mac[5]);
}

static TaskHandle_t s_status_task = nullptr;
static TaskHandle_t s_boot_task = nullptr;

static void publish_device_status_once(void) {
    // Gather cheap stats
    uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_ms", (double)uptime_ms);
    cJSON_AddNumberToObject(root, "free_heap", (double)free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", (double)min_free_heap);
    cJSON_AddNumberToObject(root, "num_tasks", (double)num_tasks);

    // Absolute UTC timestamp in ISO 8601 format
    time_t now_secs = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now_secs, &tm_utc);
    char iso_ts[32];
    strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    cJSON_AddStringToObject(root, "heartbeat_ts", iso_ts);

    char mac_nosep[13] = {0};
    format_mac_nosep_lower(mac_nosep, sizeof(mac_nosep));

    char topic[64];
    snprintf(topic, sizeof(topic), "sensor/%s/device/status", mac_nosep);

    char* json = cJSON_PrintUnformatted(root);
    publish_to_topic(topic, json, 0, 0);
    cJSON_free(json);
    cJSON_Delete(root);
}

static void status_task_entry(void* arg) {
    ESP_LOGI(TAG, "Starting periodic device status publisher");
    const TickType_t period_ticks = pdMS_TO_TICKS(10000);
    while (true) {
        publish_device_status_once();
        vTaskDelay(period_ticks);
    }
}

static void publish_device_info(bool include_timestamp) {
    cJSON *device_json = cJSON_CreateObject();

    // MAC Address
    const uint8_t* device_mac = get_device_mac();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             device_mac[0], device_mac[1], device_mac[2],
             device_mac[3], device_mac[4], device_mac[5]);
    cJSON_AddStringToObject(device_json, "mac", mac_str);

    // IP Address
    ip_event_got_ip_t ip_event;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_event.ip_info);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_event.ip_info.ip));
    cJSON_AddStringToObject(device_json, "ip", ip_str);

    // Chip Info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const char* chip_model;
    switch (chip_info.model) {
        case CHIP_ESP32:   chip_model = "ESP32"; break;
        case CHIP_ESP32S2: chip_model = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_model = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_model = "ESP32-C3"; break;
        default:           chip_model = "Unknown"; break;
    }
    cJSON_AddStringToObject(device_json, "chip_model", chip_model);
    cJSON_AddNumberToObject(device_json, "chip_revision", chip_info.revision);
    cJSON_AddNumberToObject(device_json, "cpu_cores", chip_info.cores);
    cJSON_AddBoolToObject(device_json, "features_wifi", (chip_info.features & CHIP_FEATURE_WIFI_BGN) != 0);
    cJSON_AddBoolToObject(device_json, "features_bt", (chip_info.features & CHIP_FEATURE_BT) != 0);
    cJSON_AddBoolToObject(device_json, "features_ble", (chip_info.features & CHIP_FEATURE_BLE) != 0);

    // App Info
    const esp_app_desc_t* app_desc = esp_app_get_description();
    cJSON_AddStringToObject(device_json, "app_version", app_desc->version);
    cJSON_AddStringToObject(device_json, "app_name", app_desc->project_name);
    cJSON_AddStringToObject(device_json, "compile_time", app_desc->time);
    cJSON_AddStringToObject(device_json, "compile_date", app_desc->date);
    cJSON_AddStringToObject(device_json, "idf_version", app_desc->idf_ver);

    // System Info
    uint32_t heap_size = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    cJSON_AddNumberToObject(device_json, "free_heap", heap_size);
    cJSON_AddNumberToObject(device_json, "min_free_heap", min_heap);

    // Flash Size
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    cJSON_AddNumberToObject(device_json, "flash_size", flash_size);

    // Optional boot timestamp in ISO 8601 if SNTP time is available
    if (include_timestamp) {
        time_t now_secs = time(nullptr);
        struct tm tm_utc;
        gmtime_r(&now_secs, &tm_utc);
        char iso_ts[32];
        strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        cJSON_AddStringToObject(device_json, "boot_ts", iso_ts);
    }

    // Convert to string and publish
    char *device_string = cJSON_Print(device_json);
    // Retained boot info
    publish_to_topic("device", device_string, 1, 1);

    // Cleanup
    cJSON_free(device_string);
    cJSON_Delete(device_json);
}


esp_err_t telemetry_configure_lwt(esp_mqtt_client_config_t* mqtt_cfg) {
    if (!mqtt_cfg) return ESP_ERR_INVALID_ARG;

    char mac_nosep[13] = {0};
    format_mac_nosep_lower(mac_nosep, sizeof(mac_nosep));

    char lwt_topic[128];
    snprintf(lwt_topic, sizeof(lwt_topic), "sensor/%s/device/connected", mac_nosep);

    const char* lwt_message = "{\"connected\":false}";
    // Configure LWT using ESP-IDF v5.3 nested session.last_will fields
    mqtt_cfg->session.last_will.topic = strdup(lwt_topic);
    mqtt_cfg->session.last_will.msg = (const char*)strdup(lwt_message);
    mqtt_cfg->session.last_will.msg_len = strlen(lwt_message);
    mqtt_cfg->session.last_will.qos = 1;
    mqtt_cfg->session.last_will.retain = 1;

    ESP_LOGI(TAG, "LWT configured for topic: %s", lwt_topic);
    return ESP_OK;
}

static void boot_publish_task_entry(void* arg) {
    (void)arg;
    // Wait up to 60s for time sync; if it doesn't arrive, proceed without timestamp
    esp_err_t time_ok = wifi_wait_for_time_sync(60000);
    bool include_ts = (time_ok == ESP_OK);
    if (!include_ts) {
        ESP_LOGW(TAG, "SNTP time not available within timeout; publishing boot without timestamp");
    }

    publish_device_info(include_ts);

    // Start status task once
    if (s_status_task == nullptr) {
        BaseType_t ok = xTaskCreatePinnedToCore(&status_task_entry, "status-pub", 4096, nullptr, tskIDLE_PRIORITY + 1, &s_status_task, tskNO_AFFINITY);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create status task");
            s_status_task = nullptr;
        }
    }

    vTaskDelete(nullptr);
}

void telemetry_report_connected(void) {
    if (s_boot_task == nullptr) {
        BaseType_t ok = xTaskCreatePinnedToCore(&boot_publish_task_entry, "boot-pub", 4096, nullptr, tskIDLE_PRIORITY + 2, &s_boot_task, tskNO_AFFINITY);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create boot publish task");
            s_boot_task = nullptr;
            // Fallback: publish synchronously without timestamp wait
            publish_device_info(false);
        }
    }
}


