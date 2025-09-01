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
#include "telemetry.h"
#include "ConfigurationManager.h"
#include "WifiConfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "communication.h"

static const char *TAG = "wifi";

static volatile SystemState system_state = WIFI_CONNECTING;
static esp_mqtt_client_handle_t mqtt_client;
static uint8_t device_mac[6] = {0};
static bool sntp_initialized = false;
static esp_timer_handle_t s_wifi_retry_timer = nullptr;
static esp_timer_handle_t s_wifi_initial_connect_timer = nullptr;
static int s_retry_delay_ms = 1000; // start at 1s
static const int s_retry_delay_max_ms = 30000; // cap at 30s
// Synchronization for waiting until boot publish is acknowledged
static SemaphoreHandle_t s_boot_pub_sem = nullptr;
static volatile int s_boot_pub_msg_id = -1;
static volatile bool s_boot_pub_acked = false;
// Synchronization for waiting until time is synchronized
static SemaphoreHandle_t s_time_sync_sem = nullptr;
static volatile bool s_time_synced = false;

static void wifi_init_sta(void);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static const char* wifi_reason_to_name(int reason);
static const char* wifi_event_to_name(int32_t event_id);
static const char* ip_event_to_name(int32_t event_id);
static const char* mqtt_event_to_name(int32_t event_id);
static void schedule_initial_connect(int delay_ms);
static void wifi_retry_cb(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "Retrying WiFi connect (delay=%dms)", s_retry_delay_ms);
    esp_err_t e = esp_wifi_connect();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed in retry: %s", esp_err_to_name(e));
    }
}

static void schedule_wifi_retry(int delay_ms) {
    if (delay_ms <= 0) delay_ms = 1;
    if (!s_wifi_retry_timer) {
        const esp_timer_create_args_t targs = {
            .callback = &wifi_retry_cb,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_retry",
            .skip_unhandled_events = true
        };
        esp_err_t c = esp_timer_create(&targs, &s_wifi_retry_timer);
        if (c != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create retry timer: %s", esp_err_to_name(c));
            return;
        }
    }
    if (esp_timer_is_active(s_wifi_retry_timer)) {
        esp_timer_stop(s_wifi_retry_timer);
    }
    esp_err_t s = esp_timer_start_once(s_wifi_retry_timer, (uint64_t)delay_ms * 1000ULL);
    if (s != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start retry timer: %s", esp_err_to_name(s));
    }
}
static void initial_connect_cb(void* arg) {
    (void)arg;
    esp_err_t e = esp_wifi_connect();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect (initial) failed: %s", esp_err_to_name(e));
    }
}

static void schedule_initial_connect(int delay_ms) {
    if (delay_ms <= 0) delay_ms = 1;
    if (!s_wifi_initial_connect_timer) {
        const esp_timer_create_args_t targs = {
            .callback = &initial_connect_cb,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_initial_connect",
            .skip_unhandled_events = true
        };
        esp_err_t c = esp_timer_create(&targs, &s_wifi_initial_connect_timer);
        if (c != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create initial connect timer: %s", esp_err_to_name(c));
            return;
        }
    }
    if (esp_timer_is_active(s_wifi_initial_connect_timer)) {
        esp_timer_stop(s_wifi_initial_connect_timer);
    }
    esp_err_t s = esp_timer_start_once(s_wifi_initial_connect_timer, (uint64_t)delay_ms * 1000ULL);
    if (s != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start initial connect timer: %s", esp_err_to_name(s));
    }
}

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
            esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
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
    s_time_synced = true;
    if (!s_time_sync_sem) {
        s_time_sync_sem = xSemaphoreCreateBinary();
    }
    if (s_time_sync_sem) {
        xSemaphoreGive(s_time_sync_sem);
    }
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
    // Initialize semaphore for time sync waiters
    if (!s_time_sync_sem) {
        s_time_sync_sem = xSemaphoreCreateBinary();
    }
}

// Publishing helpers moved to telemetry.cpp

// Function to publish location information
// Publishing helpers moved to telemetry.cpp

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    static bool mqtt_started = false;

    // Handle WiFi events
    if (event_base == WIFI_EVENT) {
        ESP_LOGD(TAG, "WiFi event: %s (%ld)", wifi_event_to_name(event_id), (long)event_id);
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                // Apply STA config and initiate connection now that WiFi driver and supplicant are up
                using namespace config;
                auto& mgr = GetConfigurationManager();
                auto& w = mgr.wifi();
                if (!(w.has_ssid() && w.has_password())) {
                    ESP_LOGW(TAG, "WiFi credentials not set on START; entering error state");
                    system_state = MQTT_ERROR_STATE;
                    break;
                }

                // Validate SSID/password again (defensive)
                if (w.ssid().empty() || w.ssid().size() > 32) {
                    ESP_LOGE(TAG, "Invalid SSID length on START: %u", (unsigned)w.ssid().size());
                    system_state = MQTT_ERROR_STATE;
                    break;
                }
                size_t pwd_len = w.password().size();
                bool is_hex64 = (pwd_len == 64);
                if (is_hex64) {
                    for (char c : w.password()) {
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) { is_hex64 = false; break; }
                    }
                }
                if (!is_hex64 && (pwd_len < 8 || pwd_len > 63)) {
                    ESP_LOGE(TAG, "Invalid password length on START: %u", (unsigned)pwd_len);
                    system_state = MQTT_ERROR_STATE;
                    break;
                }

                // Configuration was already applied before esp_wifi_start().
                // Delay connect slightly to avoid race with internal start-up and scanning.
                schedule_initial_connect(300);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* dis = (wifi_event_sta_disconnected_t*)event_data;
                int reason = dis ? dis->reason : -1;
                if (dis) {
                    char ssid_buf[33];
                    int len = (dis->ssid_len < 32) ? dis->ssid_len : 32;
                    memcpy(ssid_buf, dis->ssid, len);
                    ssid_buf[len] = '\0';
                    ESP_LOGW(TAG, "WiFi disconnected: reason=%d:%s ssid='%s' bssid=%02x:%02x:%02x:%02x:%02x:%02x",
                             reason, wifi_reason_to_name(reason), ssid_buf,
                             dis->bssid[0], dis->bssid[1], dis->bssid[2],
                             dis->bssid[3], dis->bssid[4], dis->bssid[5]);
                } else {
                    ESP_LOGW(TAG, "WiFi disconnected: reason=%d:%s (no detail)", reason, wifi_reason_to_name(reason));
                }

                // Treat authentication/handshake related reasons as fatal until credentials are fixed
                bool auth_or_handshake_fail =
                    (reason == WIFI_REASON_AUTH_EXPIRE) ||
                    (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) ||
                    (reason == WIFI_REASON_HANDSHAKE_TIMEOUT) ||
                    (reason == WIFI_REASON_INVALID_PMKID) ||
                    (reason == WIFI_REASON_MIC_FAILURE) ||
                    (reason == WIFI_REASON_ASSOC_FAIL) ||
                    (reason == WIFI_REASON_IE_IN_4WAY_DIFFERS) ||
                    (reason == WIFI_REASON_GROUP_CIPHER_INVALID) ||
                    (reason == WIFI_REASON_PAIRWISE_CIPHER_INVALID) ||
                    (reason == WIFI_REASON_AKMP_INVALID) ||
                    (reason == WIFI_REASON_UNSUPP_RSN_IE_VERSION) ||
                    (reason == WIFI_REASON_INVALID_RSN_IE_CAP) ||
                    (reason == WIFI_REASON_802_1X_AUTH_FAILED) ||
                    (reason == WIFI_REASON_BAD_CIPHER_OR_AKM) ||
                    (reason == WIFI_REASON_AUTH_FAIL);

                if (auth_or_handshake_fail) {
                    ESP_LOGE(TAG, "Authentication/handshake failed (reason=%d: %s). Will retry with backoff.", reason, wifi_reason_to_name(reason));
                    system_state = MQTT_ERROR_STATE;
                    // Remain in error state but keep retrying in background
                    schedule_wifi_retry(s_retry_delay_ms);
                    if (s_retry_delay_ms < s_retry_delay_max_ms) {
                        int next = s_retry_delay_ms * 2;
                        s_retry_delay_ms = (next > s_retry_delay_max_ms) ? s_retry_delay_max_ms : next;
                    }
                    break;
                }

                system_state = WIFI_CONNECTING;
                // Schedule a short retry to avoid tight loops for transient reasons
                schedule_wifi_retry(1000);
                break;
            }
        }
    }
    // Handle IP events
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "IP event: %s (%ld)", ip_event_to_name(event_id), (long)event_id);
        // reset any WiFi backoff state on IP acquisition
        system_state = WIFI_CONNECTED_MQTT_CONNECTING;
        // Reset WiFi retry backoff on successful IP acquisition
        s_retry_delay_ms = 1000;
        if (s_wifi_retry_timer && esp_timer_is_active(s_wifi_retry_timer)) {
            esp_timer_stop(s_wifi_retry_timer);
        }

        // Start MQTT client only if it hasn't been started yet
        if (!mqtt_started) {
            esp_mqtt_client_start(mqtt_client);
            mqtt_started = true;
        }

        // Log AP details
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Connected to SSID='%s' BSSID=%02x:%02x:%02x:%02x:%02x:%02x authmode=%d rssi=%d",
                     (const char*)ap_info.ssid,
                     ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                     ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5],
                     (int)ap_info.authmode, (int)ap_info.rssi);
        }

        // Initialize SNTP to set time
        initialize_sntp();
        
        // OTA will poll system readiness; no direct OTA calls here
    }
    // Handle other IP events of interest
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "IP event: %s (%ld)", ip_event_to_name(event_id), (long)event_id);
        if (system_state == FULLY_CONNECTED) {
            system_state = WIFI_CONNECTED_MQTT_CONNECTING;
        }
    }
    else {
        // Unhandled event base; ignore to avoid miscasting event_data
        ESP_LOGD(TAG, "Ignoring non-WIFI/IP event in wifi handler");
    }
}

static void mqtt_event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    (void)arg;
    (void)event_base;
    // This section handles all MQTT events
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_event_id_t mqtt_event = (esp_mqtt_event_id_t)event->event_id;
    ESP_LOGD(TAG, "MQTT event: %s (%d)", mqtt_event_to_name(mqtt_event), (int)mqtt_event);

    // First check WiFi state - don't process MQTT errors if WiFi is disconnected
    bool wifi_connected = false;
    {
        wifi_ap_record_t ap_info;
        wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    }

    // Process MQTT events based on event type
    if (mqtt_event == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT Connected");
        // Reset error count on a successful connect
        // Note: use a local static in wifi handler; here we just update state
        system_state = FULLY_CONNECTED;

        // Publish telemetry (boot + location/connected)
        telemetry_report_connected();

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
    }
    else if (mqtt_event == MQTT_EVENT_PUBLISHED) {
        // QoS1 PUBACK received for a publish we initiated
        if (s_boot_pub_msg_id >= 0 && event->msg_id == s_boot_pub_msg_id) {
            s_boot_pub_acked = true;
            s_boot_pub_msg_id = -1;
            if (s_boot_pub_sem) {
                xSemaphoreGive(s_boot_pub_sem);
            }
            ESP_LOGI(TAG, "Boot/device publish acknowledged by broker");
        }
    }
    else if (mqtt_event == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "MQTT Error");
        // Only count MQTT errors when we're actually connected to WiFi
        if (!wifi_connected) {
            ESP_LOGI(TAG, "Ignoring MQTT error during WiFi disconnect state");
        } else if (system_state == WIFI_CONNECTED_MQTT_CONNECTING ||
                   system_state == FULLY_CONNECTED) {
            static uint32_t mqtt_error_count_local = 0;
            mqtt_error_count_local++;
            ESP_LOGI(TAG, "MQTT Error count: %lu/3", (unsigned long)mqtt_error_count_local);
            if (mqtt_error_count_local >= 3) {
                system_state = MQTT_ERROR_STATE;
                mqtt_error_count_local = 0;
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
    esp_event_handler_instance_t instance_lost_ip;
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
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_LOST_IP,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_lost_ip));

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
            system_state = MQTT_ERROR_STATE;
            return;
        }
        // Validate SSID length (1..32)
        if (w.ssid().size() == 0 || w.ssid().size() > 32) {
            ESP_LOGE(TAG, "Invalid WiFi SSID length: %u (must be 1..32)", (unsigned)w.ssid().size());
            system_state = MQTT_ERROR_STATE;
            return;
        }
        // Validate password: 8..63 ASCII or 64 hex digits PSK
        const size_t pwd_len = w.password().size();
        bool is_hex64 = false;
        if (pwd_len == 64) {
            is_hex64 = true;
            for (char c : w.password()) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    is_hex64 = false;
                    break;
                }
            }
        }
        if (!is_hex64 && (pwd_len < 8 || pwd_len > 63)) {
            ESP_LOGE(TAG, "Invalid WiFi password length: %u (must be 8..63, or 64 hex)", (unsigned)pwd_len);
            system_state = MQTT_ERROR_STATE;
            return;
        }
        strlcpy((char*)wifi_config.sta.ssid, w.ssid().c_str(), sizeof(wifi_config.sta.ssid));
        strlcpy((char*)wifi_config.sta.password, w.password().c_str(), sizeof(wifi_config.sta.password));
    }
    // Use all-channel scan to avoid missing AP on first attempt on some firmwares/APs
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.bssid_set = false;
    wifi_config.sta.channel = 0;
    wifi_config.sta.listen_interval = 0;
    // Explicitly require WPA2 (or better) to avoid internal threshold flips
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    // Explicit PMF configuration to avoid APs that require PMF causing assoc failures
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Ensure we don't auto-connect using stale NVS config; use RAM storage and set config before start
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    const char* scan_name = (wifi_config.sta.scan_method == WIFI_ALL_CHANNEL_SCAN) ? "ALL" : "FAST";
    ESP_LOGI(TAG, "WiFi config applied: SSID='%s' scan=%s bssid_set=%d ch=%d auth>=%d pmf{cap=%d,req=%d}",
             (const char*)wifi_config.sta.ssid,
             scan_name,
             (int)wifi_config.sta.bssid_set,
             (int)wifi_config.sta.channel,
             (int)wifi_config.sta.threshold.authmode,
             (int)wifi_config.sta.pmf_cfg.capable,
             (int)wifi_config.sta.pmf_cfg.required);
    // Start WiFi; we'll connect on WIFI_EVENT_STA_START
    ESP_ERROR_CHECK(esp_wifi_start());

    // Prefer no power save until connected to improve initial association reliability
    esp_wifi_set_ps(WIFI_PS_NONE);
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
    // (location/*) topics are removed
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

    // Track boot publish so callers can wait for PUBACK
    if (subtopic && strcmp(subtopic, "device") == 0 && qos > 0) {
        // Ensure semaphore exists
        if (!s_boot_pub_sem) {
            s_boot_pub_sem = xSemaphoreCreateBinary();
        }
        // Drain any previous signal
        if (s_boot_pub_sem) {
            while (xSemaphoreTake(s_boot_pub_sem, 0) == pdTRUE) {}
        }
        s_boot_pub_acked = false;
        s_boot_pub_msg_id = msg_id;
        ESP_LOGI(TAG, "Tracking boot/device publish (msg_id=%d)", msg_id);
    }

    return ESP_OK;
}

esp_err_t wifi_wait_for_boot_publish(int timeout_ms) {
    // If MQTT isn't initialized, there's nothing to wait for; return immediately
    if (mqtt_client == NULL) {
        return ESP_OK;
    }
    // Fast-path if already acknowledged
    if (s_boot_pub_acked) {
        return ESP_OK;
    }
    // Ensure semaphore exists so we can wait for a future publish
    if (!s_boot_pub_sem) {
        s_boot_pub_sem = xSemaphoreCreateBinary();
    }
    if (s_boot_pub_acked) {
        return ESP_OK;
    }
    TickType_t to_ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t ok = xSemaphoreTake(s_boot_pub_sem, to_ticks);
    return (ok == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_wait_for_time_sync(int timeout_ms) {
    if (s_time_synced) {
        return ESP_OK;
    }
    if (!sntp_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_time_sync_sem) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t to_ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t ok = xSemaphoreTake(s_time_sync_sem, to_ticks);
    return (ok == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}


// ------------------------------
// Human-readable event helpers
// ------------------------------
static const char* wifi_event_to_name(int32_t event_id) {
    switch (event_id) {
        case WIFI_EVENT_WIFI_READY: return "WIFI_EVENT_WIFI_READY";
        case WIFI_EVENT_SCAN_DONE: return "WIFI_EVENT_SCAN_DONE";
        case WIFI_EVENT_STA_START: return "WIFI_EVENT_STA_START";
        case WIFI_EVENT_STA_STOP: return "WIFI_EVENT_STA_STOP";
        case WIFI_EVENT_STA_CONNECTED: return "WIFI_EVENT_STA_CONNECTED";
        case WIFI_EVENT_STA_DISCONNECTED: return "WIFI_EVENT_STA_DISCONNECTED";
        case WIFI_EVENT_STA_AUTHMODE_CHANGE: return "WIFI_EVENT_STA_AUTHMODE_CHANGE";
        default: return "WIFI_EVENT_UNKNOWN";
    }
}

static const char* ip_event_to_name(int32_t event_id) {
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP: return "IP_EVENT_STA_GOT_IP";
        case IP_EVENT_STA_LOST_IP: return "IP_EVENT_STA_LOST_IP";
        default: return "IP_EVENT_UNKNOWN";
    }
}

static const char* mqtt_event_to_name(int32_t event_id) {
    switch (event_id) {
        case MQTT_EVENT_CONNECTED: return "MQTT_EVENT_CONNECTED";
        case MQTT_EVENT_DISCONNECTED: return "MQTT_EVENT_DISCONNECTED";
        case MQTT_EVENT_SUBSCRIBED: return "MQTT_EVENT_SUBSCRIBED";
        case MQTT_EVENT_UNSUBSCRIBED: return "MQTT_EVENT_UNSUBSCRIBED";
        case MQTT_EVENT_PUBLISHED: return "MQTT_EVENT_PUBLISHED";
        case MQTT_EVENT_DATA: return "MQTT_EVENT_DATA";
        case MQTT_EVENT_ERROR: return "MQTT_EVENT_ERROR";
        default: return "MQTT_EVENT_UNKNOWN";
    }
}

static const char* wifi_reason_to_name(int reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
        case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
        case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
        case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "DISASSOC_PWRCAP_BAD";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "DISASSOC_SUPCHAN_BAD";
        case WIFI_REASON_IE_INVALID: return "IE_INVALID";
        case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE_IN_4WAY_DIFFERS";
        case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
        case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
        case WIFI_REASON_INVALID_RSN_IE_CAP: return "INVALID_RSN_IE_CAP";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";
        case WIFI_REASON_INVALID_PMKID: return "INVALID_PMKID";
        case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";              // 200
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";                    // 201
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";                        // 202
        case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";                      // 203
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";        // 204
        case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";            // 205
        case WIFI_REASON_AP_TSF_RESET: return "AP_TSF_RESET";                  // 206
        case WIFI_REASON_ROAMING: return "ROAMING";                            // 207
        case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG: return "ASSOC_COMEBACK_TIME_TOO_LONG"; // 208
        default: return "UNKNOWN_REASON";
    }
}
