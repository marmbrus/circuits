#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "config.h"
#include "led_control.h"
#include "esp_sntp.h"
#include "time.h"

static const char *TAG = "wifi";

static volatile SystemState system_state = WIFI_CONNECTING;
static esp_mqtt_client_handle_t mqtt_client;

static void log_current_time(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", asctime(&timeinfo));
}

static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGD(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    // Log the current time
    log_current_time();
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

    // Get MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    // Create hostname with last 4 digits of MAC
    char hostname[32];
    snprintf(hostname, sizeof(hostname), "surprise_%02x%02x", mac[4], mac[5]);
    esp_netif_set_hostname(sta_netif, hostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .scan_method = WIFI_FAST_SCAN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_mqtt_init(void)
{
    // Initialize WiFi
    system_state = WIFI_CONNECTING;
    wifi_init_sta();

    // Configure MQTT but don't start it yet
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URL;
    mqtt_cfg.network.reconnect_timeout_ms = MQTT_RECONNECT_TIMEOUT_MS;
    mqtt_cfg.network.timeout_ms = MQTT_OPERATION_TIMEOUT_MS;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, event_handler, NULL);
    // MQTT will be started in event handler once we have an IP.
}

SystemState get_system_state(void)
{
    return system_state;
}

esp_mqtt_client_handle_t get_mqtt_client(void)
{
    return mqtt_client;
}
