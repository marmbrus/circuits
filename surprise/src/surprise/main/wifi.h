#pragma once

#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

// Define system states
enum SystemState {
    WIFI_CONNECTING,
    WIFI_CONNECTED_MQTT_CONNECTING,
    FULLY_CONNECTED,
    MQTT_ERROR_STATE
};

// Function declarations
void wifi_mqtt_init(void);
SystemState get_system_state(void);
esp_mqtt_client_handle_t get_mqtt_client(void);
const uint8_t* get_device_mac(void);

// Add new MQTT publish helper function
esp_err_t publish_to_topic(const char* subtopic, const char* message, int qos = 1, int retain = 0);
