#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "config.h"
#include "system_state.h"
#include "esp_sntp.h"
#include "time.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_netif_ip_addr.h"
#include "esp_flash.h"
#include "ota.h"
#include "telemetry.h"
#include "ConfigurationManager.h"
#include "WifiConfig.h"

#include "communication.h"

static const char *TAG = "wifi";

static volatile SystemState system_state = WIFI_CONNECTING;
static esp_mqtt_client_handle_t mqtt_client;
static uint8_t device_mac[6] = {0};
static bool sntp_initialized = false;

static void wifi_init_sta(void);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

// No tag handling helpers here; telemetry is delegated to telemetry.cpp

void wifi_mqtt_init(void)
{
    // Initialize WiFi
    system_state = WIFI_CONNECTING;

    // Initialize WiFi as station
    wifi_init_sta();

    // Configure MQTT with LWT (Last Will and Testament)
    esp_mqtt_client_config_t mqtt_cfg = {};  // Zero initialize
    // Pull broker from configuration; if missing, skip MQTT init entirely
    bool have_broker = false;
    {
        using namespace config;
        auto& cfg = GetConfigurationManager();
        if (cfg.wifi().has_mqtt_broker()) {
            mqtt_cfg.broker.address.uri = cfg.wifi().mqtt_broker().c_str();
            have_broker = true;
        }
    }
    mqtt_cfg.network.reconnect_timeout_ms = MQTT_RECONNECT_TIMEOUT_MS;
    mqtt_cfg.network.timeout_ms = MQTT_OPERATION_TIMEOUT_MS;
    
    // Configure LWT via telemetry helper (reads TagsConfig directly)
    telemetry_configure_lwt(&mqtt_cfg);
    
    if (have_broker) {
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        if (mqtt_client) {
            esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, event_handler, NULL);
        } else {
            ESP_LOGE(TAG, "Failed to init MQTT client");
        }
    } else {
        mqtt_client = NULL;
        ESP_LOGW(TAG, "MQTT broker not set; skipping MQTT init");
    }
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

// Publishing helpers moved to telemetry.cpp

// Function to publish location information
// Publishing helpers moved to telemetry.cpp

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
                mqtt_error_count = 0; // Reset error count on disconnect
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

            // Publish telemetry (boot + location/connected)
            telemetry_report_connected();
            
            // Report OTA status when MQTT connects
            ota_report_status();
            
            // Notify OTA again when MQTT is connected (fully network ready)
            ota_notify_network_connected();

            // Subscribe to configuration updates for this device
            {
                using namespace config;
                auto& mgr = GetConfigurationManager();
                std::string topic = mgr.get_mqtt_subscription_topic();
                int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic.c_str(), 1);
                ESP_LOGI(TAG, "Subscribed to config topic %s (msg_id=%d)", topic.c_str(), msg_id);
            }

            // Publish current configuration now that we're connected
            {
                using namespace config;
                auto& mgr = GetConfigurationManager();
                mgr.publish_full_configuration();
            }
        }
        else if (mqtt_event == MQTT_EVENT_DISCONNECTED) {
            ESP_LOGI(TAG, "MQTT Disconnected");

            if (!wifi_connected) {
                // WiFi is actually disconnected but we haven't received the WiFi event yet
                // Update the state to be consistent
                ESP_LOGI(TAG, "WiFi appears to be disconnected, updating state");
                system_state = WIFI_CONNECTING;
            } else if (system_state == FULLY_CONNECTED) {
                // Normal MQTT disconnection while WiFi is still connected
                system_state = WIFI_CONNECTED_MQTT_CONNECTING;
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
                    // LED state handled by LEDManager patterns
                }
            }
        }
        else if (mqtt_event == MQTT_EVENT_DATA) {
            // Forward potential config updates to ConfigurationManager
            using namespace config;
            auto& mgr = GetConfigurationManager();
            // Event provides topic and data not null-terminated
            std::string topic(event->topic, event->topic_len);
            std::string payload(event->data, event->data_len);
            mgr.handle_mqtt_message(topic.c_str(), payload.c_str());
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

    // Pull creds from configuration; if missing, log and skip WiFi start
    {
        using namespace config;
        auto& mgr = GetConfigurationManager();
        auto& w = mgr.wifi();
        if (!(w.has_ssid() && w.has_password())) {
            ESP_LOGW(TAG, "WiFi credentials not set; skipping WiFi start");
            return;
        }
        strlcpy((char*)wifi_config.sta.ssid, w.ssid().c_str(), sizeof(wifi_config.sta.ssid));
        strlcpy((char*)wifi_config.sta.password, w.password().c_str(), sizeof(wifi_config.sta.password));
    }
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

