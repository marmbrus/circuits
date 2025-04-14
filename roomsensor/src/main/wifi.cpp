#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "config.h"
#include "led_control.h"
#include "esp_sntp.h"
#include "time.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_netif_ip_addr.h"
#include "esp_flash.h"
#include "cJSON.h"

#include "communication.h"

static const char *TAG = "wifi";

static volatile SystemState system_state = WIFI_CONNECTING;
static esp_mqtt_client_handle_t mqtt_client;
static uint8_t device_mac[6] = {0};

static void wifi_init_sta(void);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void wifi_mqtt_init(void)
{
    // Initialize WiFi
    system_state = WIFI_CONNECTING;
    
    // Initialize WiFi as station
    wifi_init_sta();
    
    // Configure MQTT
    esp_mqtt_client_config_t mqtt_cfg = {};  // Zero initialize
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URL;
    mqtt_cfg.network.reconnect_timeout_ms = MQTT_RECONNECT_TIMEOUT_MS;
    mqtt_cfg.network.timeout_ms = MQTT_OPERATION_TIMEOUT_MS;
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, event_handler, NULL);
}

static void time_sync_notification_cb(struct timeval *tv) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char* time_str = asctime(&timeinfo);
    time_str[strcspn(time_str, "\n")] = '\0';  // Trim newline
    ESP_LOGI("sntp", "System time updated: %s", time_str);
}

static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

static void publish_device_info(esp_ip4_addr_t ip) {
    cJSON *device_json = cJSON_CreateObject();

    // MAC Address
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             device_mac[0], device_mac[1], device_mac[2],
             device_mac[3], device_mac[4], device_mac[5]);
    cJSON_AddStringToObject(device_json, "mac", mac_str);

    // IP Address
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
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

    // Boot Info
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char* reset_reasons[] = {
        "UNKNOWN",
        "POWERON",
        "EXT",
        "SW",
        "PANIC",
        "INT_WDT",
        "TASK_WDT",
        "WDT",
        "DEEPSLEEP",
        "BROWNOUT",
        "SDIO"
    };
    cJSON_AddStringToObject(device_json, "reset_reason",
        reset_reason < sizeof(reset_reasons)/sizeof(reset_reasons[0])
            ? reset_reasons[reset_reason]
            : "UNKNOWN");

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

    // Convert to string and publish
    char *device_string = cJSON_Print(device_json);
    publish_to_topic("device", device_string);

    // Cleanup
    cJSON_free(device_string);
    cJSON_Delete(device_json);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    static uint32_t mqtt_error_count = 0;
    static bool mqtt_started = false;
    // Handle WiFi events
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                system_state = WIFI_CONNECTING;
                esp_wifi_connect();
                break;
        }
    }
    // Handle IP events
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mqtt_error_count = 0;
        system_state = WIFI_CONNECTED_MQTT_CONNECTING;

        // Start MQTT client only if it hasn't been started yet
        if (!mqtt_started) {
            esp_mqtt_client_start(mqtt_client);
            mqtt_started = true;
        }

        // Initialize SNTP to set time
        initialize_sntp();
    }
    // Handle MQTT events
    else {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        switch ((esp_mqtt_event_id_t)event->event_id) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT Connected");
                mqtt_error_count = 0;
                system_state = FULLY_CONNECTED;
                led_control_set_state(FULLY_CONNECTED);

                // Publish device info when MQTT is connected
                ip_event_got_ip_t ip_event;
                esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_event.ip_info);
                publish_device_info(ip_event.ip_info.ip);
                break;

            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT Disconnected");
                if (system_state == FULLY_CONNECTED) {
                    system_state = WIFI_CONNECTED_MQTT_CONNECTING;
                    led_control_set_state(WIFI_CONNECTED_MQTT_CONNECTING);
                }
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGI(TAG, "MQTT Error");
                mqtt_error_count++;
                if (mqtt_error_count >= 3) {
                    system_state = MQTT_ERROR_STATE;
                    mqtt_error_count = 0;
                    led_control_set_state(MQTT_ERROR_STATE);
                }
                break;
            default:
                break;
        }
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    // Use the correct initialization structure
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    // Move MAC address retrieval here, after WiFi is initialized
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, device_mac));

    // Create hostname with last 4 digits of MAC - update to roomsensor
    char hostname[32];
    snprintf(hostname, sizeof(hostname), "roomsensor_%02x%02x", device_mac[4], device_mac[5]);
    esp_netif_set_hostname(sta_netif, hostname);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_got_ip));

    // Initialize only the STA config part explicitly to avoid warnings
    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    
    strlcpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.bssid_set = false;
    wifi_config.sta.channel = 0;
    wifi_config.sta.listen_interval = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

SystemState get_system_state(void)
{
    return system_state;
}

esp_mqtt_client_handle_t get_mqtt_client(void)
{
    return mqtt_client;
}

const uint8_t* get_device_mac(void)
{
    return device_mac;
}
esp_err_t publish_to_topic(const char* subtopic, const char* message, int qos, int retain) {
    if (!mqtt_client || system_state != FULLY_CONNECTED) {
        ESP_LOGE(TAG, "MQTT publish failed: client not connected (state: %d)", system_state);
        return ESP_ERR_INVALID_STATE;
    }

    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "roomsensor/%02x%02x%02x%02x%02x%02x/%s",
             device_mac[0], device_mac[1], device_mac[2],
             device_mac[3], device_mac[4], device_mac[5],
             subtopic);

    // Log the MQTT message being published (basic info only)
    ESP_LOGI(TAG, "MQTT: %s -> %s", full_topic, message);
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, full_topic, message, 0, qos, retain);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "MQTT publish failed, error code=%d", msg_id);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

