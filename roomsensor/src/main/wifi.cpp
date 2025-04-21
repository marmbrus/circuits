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
#include "ota.h"

#include "communication.h"

static const char *TAG = "wifi";

static volatile SystemState system_state = WIFI_CONNECTING;
static esp_mqtt_client_handle_t mqtt_client;
static uint8_t device_mac[6] = {0};
static bool sntp_initialized = false;

static void wifi_init_sta(void);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

// Function to check if device tags are properly configured
static bool are_device_tags_configured(const char* area, const char* room, const char* id) {
    // Check if any of the critical tags are set to "unknown"
    return (strcmp(area, "unknown") != 0 && 
            strcmp(room, "unknown") != 0 && 
            strcmp(id, "unknown") != 0);
}

void wifi_mqtt_init(void)
{
    // Initialize WiFi
    system_state = WIFI_CONNECTING;

    // Initialize WiFi as station
    wifi_init_sta();

    // Initialize tag system BEFORE MQTT to ensure we have proper configuration
    // Call initialize_tag_system if not already done elsewhere in the application
    extern esp_err_t initialize_tag_system(void);
    esp_err_t tag_err = initialize_tag_system();
    if (tag_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize tag system: %s", esp_err_to_name(tag_err));
    }

    // Configure MQTT with LWT (Last Will and Testament)
    esp_mqtt_client_config_t mqtt_cfg = {};  // Zero initialize
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URL;
    mqtt_cfg.network.reconnect_timeout_ms = MQTT_RECONNECT_TIMEOUT_MS;
    mqtt_cfg.network.timeout_ms = MQTT_OPERATION_TIMEOUT_MS;
    
    // Set up LWT (Last Will and Testament) only if we have proper configuration
    extern TagCollection* create_tag_collection(void);
    TagCollection* tags = create_tag_collection();
    
    if (tags) {
        // Extract area, room, id from tags - limit to 20 chars each to prevent buffer overflow
        char area[21] = "unknown";
        char room[21] = "unknown";
        char id[21] = "unknown";
        
        for (int i = 0; i < tags->count; i++) {
            if (strcmp(tags->tags[i].key, "area") == 0) {
                strncpy(area, tags->tags[i].value, 20);
                area[20] = '\0'; // Ensure null termination
            } else if (strcmp(tags->tags[i].key, "room") == 0) {
                strncpy(room, tags->tags[i].value, 20);
                room[20] = '\0'; // Ensure null termination
            } else if (strcmp(tags->tags[i].key, "id") == 0) {
                strncpy(id, tags->tags[i].value, 20);
                id[20] = '\0'; // Ensure null termination
            }
        }
        
        // Only set up LWT if we have proper configuration
        if (are_device_tags_configured(area, room, id)) {
            // LWT topic - location/{area}/{room}/{id}/connected
            char lwt_topic[128];
            snprintf(lwt_topic, sizeof(lwt_topic), "location/%s/%s/%s/connected", area, room, id);
            
            // Create a compact LWT message - {"connected":false} without any whitespace
            const char* lwt_message = "{\"connected\":false}";
            
            // Configure LWT
            mqtt_cfg.session.last_will.topic = strdup(lwt_topic);
            mqtt_cfg.session.last_will.msg = (const char*)strdup(lwt_message);
            mqtt_cfg.session.last_will.msg_len = strlen(lwt_message);
            mqtt_cfg.session.last_will.qos = 1;
            mqtt_cfg.session.last_will.retain = 1;
            
            ESP_LOGI(TAG, "LWT configured for topic: %s", lwt_topic);
        } else {
            ESP_LOGW(TAG, "Device tags not properly configured. LWT will not be set up.");
        }
        
        // Free tag collection
        extern void free_tag_collection(TagCollection* collection);
        free_tag_collection(tags);
    } else {
        ESP_LOGE(TAG, "Failed to get device tags for LWT setup");
    }
    
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
    if (sntp_initialized) {
        ESP_LOGI(TAG, "SNTP already initialized, skipping");
        return;
    }

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    sntp_initialized = true;
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

// Function to publish location information
static void publish_location_info(esp_ip4_addr_t ip) {
    // This function requires area, room, and id from tag system
    
    // Use ESP IDF API to get device tags from NVS or other storage
    extern TagCollection* create_tag_collection(void); // Declare external function
    TagCollection* tags = create_tag_collection();
    
    if (!tags) {
        ESP_LOGE(TAG, "Failed to get device tags, can't publish location info");
        return;
    }
    
    // Extract area, room, id from tags - limit to 20 chars each to prevent buffer overflow
    char area[21] = "unknown";
    char room[21] = "unknown";
    char id[21] = "unknown";
    
    for (int i = 0; i < tags->count; i++) {
        if (strcmp(tags->tags[i].key, "area") == 0) {
            strncpy(area, tags->tags[i].value, 20);
            area[20] = '\0'; // Ensure null termination
        } else if (strcmp(tags->tags[i].key, "room") == 0) {
            strncpy(room, tags->tags[i].value, 20);
            room[20] = '\0'; // Ensure null termination
        } else if (strcmp(tags->tags[i].key, "id") == 0) {
            strncpy(id, tags->tags[i].value, 20);
            id[20] = '\0'; // Ensure null termination
        }
    }
    
    // Check if we have proper configuration before publishing
    if (!are_device_tags_configured(area, room, id)) {
        ESP_LOGW(TAG, "Device tags not properly configured. Not publishing location info.");
        free_tag_collection(tags);
        return;
    }
    
    // Create JSON for device info
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
    
    char *device_string = cJSON_Print(device_json);
    
    // Build the topic: location/$area/$room/$id/device
    char topic[128];
    snprintf(topic, sizeof(topic), "location/%s/%s/%s/device", area, room, id);
    
    // Publish with retain flag set to 1 (retained message)
    publish_to_topic(topic, device_string, 1, 1);
    
    // Now publish the connected status as a retained message with LWT
    cJSON *connected_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(connected_json, "connected", true);
    
    // Use PrintUnformatted to generate a compact single-line JSON without indentation
    char *connected_string = cJSON_PrintUnformatted(connected_json);
    
    // Build connected topic: location/$area/$room/$id/connected
    char connected_topic[128];
    snprintf(connected_topic, sizeof(connected_topic), "location/%s/%s/%s/connected", area, room, id);
    
    // Publish with retain flag set to 1
    publish_to_topic(connected_topic, connected_string, 1, 1);
    
    // Cleanup
    cJSON_free(device_string);
    cJSON_Delete(device_json);
    cJSON_free(connected_string);
    cJSON_Delete(connected_json);
    
    // Free tag collection
    extern void free_tag_collection(TagCollection* collection); // Declare external function
    free_tag_collection(tags);
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
                led_control_set_state(WIFI_CONNECTING);
                mqtt_error_count = 0; // Reset error count on disconnect
                esp_wifi_connect();
                break;
        }
    }
    // Handle IP events
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mqtt_error_count = 0;
        system_state = WIFI_CONNECTED_MQTT_CONNECTING;
        led_control_set_state(WIFI_CONNECTED_MQTT_CONNECTING);

        // Start MQTT client only if it hasn't been started yet
        if (!mqtt_started) {
            esp_mqtt_client_start(mqtt_client);
            mqtt_started = true;
        }

        // Initialize SNTP to set time
        initialize_sntp();
        
        // Notify OTA system that network is connected
        ota_notify_network_connected();
    }
    // Handle MQTT events
    else {
        // This section handles all MQTT events
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        esp_mqtt_event_id_t mqtt_event = (esp_mqtt_event_id_t)event->event_id;

        // First check WiFi state - don't process MQTT errors if WiFi is disconnected
        bool wifi_connected = false;
        {
            wifi_ap_record_t ap_info;
            wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
        }

        // Process MQTT events based on event type
        if (mqtt_event == MQTT_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "MQTT Connected");
            mqtt_error_count = 0;
            system_state = FULLY_CONNECTED;
            led_control_set_state(FULLY_CONNECTED);

            // Get IP info for publishing
            ip_event_got_ip_t ip_event;
            esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_event.ip_info);
            
            // Publish device boot info when MQTT is connected
            publish_device_info(ip_event.ip_info.ip);
            
            // Publish location info with retained flag
            publish_location_info(ip_event.ip_info.ip);
            
            // Report OTA status when MQTT connects
            ota_report_status();
            
            // Notify OTA again when MQTT is connected (fully network ready)
            ota_notify_network_connected();
        }
        else if (mqtt_event == MQTT_EVENT_DISCONNECTED) {
            ESP_LOGI(TAG, "MQTT Disconnected");

            if (!wifi_connected) {
                // WiFi is actually disconnected but we haven't received the WiFi event yet
                // Update the state to be consistent
                ESP_LOGI(TAG, "WiFi appears to be disconnected, updating state");
                system_state = WIFI_CONNECTING;
                led_control_set_state(WIFI_CONNECTING);
            } else if (system_state == FULLY_CONNECTED) {
                // Normal MQTT disconnection while WiFi is still connected
                system_state = WIFI_CONNECTED_MQTT_CONNECTING;
                led_control_set_state(WIFI_CONNECTED_MQTT_CONNECTING);
            }

            mqtt_error_count = 0; // Reset error count on disconnect
        }
        else if (mqtt_event == MQTT_EVENT_ERROR) {
            ESP_LOGI(TAG, "MQTT Error");

            // Only count MQTT errors when we're actually connected to WiFi
            // Ignore MQTT errors when WiFi is disconnected - they're expected
            if (!wifi_connected) {
                // MQTT errors during WiFi disconnect are expected and should be ignored
                ESP_LOGI(TAG, "Ignoring MQTT error during WiFi disconnect state");
            }
            else if (system_state == WIFI_CONNECTED_MQTT_CONNECTING ||
                     system_state == FULLY_CONNECTED) {
                // Only count errors when in an appropriate state
                mqtt_error_count++;
                ESP_LOGI(TAG, "MQTT Error count: %lu/3", (unsigned long)mqtt_error_count);

                if (mqtt_error_count >= 3) {
                    system_state = MQTT_ERROR_STATE;
                    mqtt_error_count = 0;
                    led_control_set_state(MQTT_ERROR_STATE);
                }
            }
        }
        // Other MQTT events are not used in this application
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
    
    // Format MAC address string for easier use
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
            device_mac[0], device_mac[1], device_mac[2],
            device_mac[3], device_mac[4], device_mac[5]);

    // Special handling for device boot topic (previously "device")
    if (strcmp(subtopic, "device") == 0) {
        // New format: sensor/{MAC}/device/boot
        snprintf(full_topic, sizeof(full_topic), "sensor/%s/device/boot", mac_str);
    } 
    // Special handling for OTA status topic
    else if (strcmp(subtopic, "ota") == 0) {
        // New format: sensor/{MAC}/device/ota
        snprintf(full_topic, sizeof(full_topic), "sensor/%s/device/ota", mac_str);
    }
    // Special handling for location topic
    else if (strncmp(subtopic, "location/", 9) == 0) {
        // Format already provided: location/{area}/{room}/{id}/device
        strncpy(full_topic, subtopic, sizeof(full_topic) - 1);
        full_topic[sizeof(full_topic) - 1] = '\0'; // Ensure null termination
    }
    // Special handling for LWT topic
    else if (strncmp(subtopic, "location/", 9) == 0 && strstr(subtopic, "/connected") != NULL) {
        // Format already provided: location/{area}/{room}/{id}/connected
        strncpy(full_topic, subtopic, sizeof(full_topic) - 1);
        full_topic[sizeof(full_topic) - 1] = '\0'; // Ensure null termination
    }
    // Handle metrics topics - check if it's coming from the metrics module
    else if (strstr(subtopic, "roomsensor/") == subtopic) {
        // This is from the old metrics format - extract the metric name
        // Old format: roomsensor/$metric_name/$area/$room/$id
        const char* metric_name_start = subtopic + 11; // Skip "roomsensor/"
        const char* metric_name_end = strchr(metric_name_start, '/');
        
        if (metric_name_end != NULL) {
            char metric_name[32];
            size_t name_len = metric_name_end - metric_name_start;
            if (name_len < sizeof(metric_name)) {
                memcpy(metric_name, metric_name_start, name_len);
                metric_name[name_len] = '\0';
                
                // New format: sensor/{MAC}/metrics/{metric_name}
                snprintf(full_topic, sizeof(full_topic), "sensor/%s/metrics/%s", 
                         mac_str, metric_name);
            } else {
                // Fallback if metric name is too long
                ESP_LOGE(TAG, "Metric name too long, using original topic");
                strncpy(full_topic, subtopic, sizeof(full_topic) - 1);
                full_topic[sizeof(full_topic) - 1] = '\0';
            }
        } else {
            // Fallback if we can't parse the metric name
            ESP_LOGE(TAG, "Can't parse metric name, using original topic");
            strncpy(full_topic, subtopic, sizeof(full_topic) - 1);
            full_topic[sizeof(full_topic) - 1] = '\0';
        }
    }
    else {
        // For all other topics, use the provided path directly
        // If subtopic starts with '/', skip the first character
        const char* topic_path = (subtopic[0] == '/') ? subtopic + 1 : subtopic;
        strncpy(full_topic, topic_path, sizeof(full_topic) - 1);
        full_topic[sizeof(full_topic) - 1] = '\0'; // Ensure null termination
    }

    // Log the MQTT message being published (basic info only)
    ESP_LOGD(TAG, "MQTT: %s -> %s", full_topic, message);

    int msg_id = esp_mqtt_client_publish(mqtt_client, full_topic, message, 0, qos, retain);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "MQTT publish failed, error code=%d", msg_id);
        return ESP_FAIL;
    }

    return ESP_OK;
}

