#pragma once

// Define system states

enum SystemState {
    WIFI_CONNECTING,
    WIFI_CONNECTED_MQTT_CONNECTING,
    FULLY_CONNECTED,
    MQTT_ERROR_STATE,
    OTA_UPDATING  // New state for when OTA update is in progress
};

// Function declarations
SystemState get_system_state(void); 