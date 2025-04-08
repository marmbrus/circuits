#pragma once

#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "system_state.h"

// Function declarations
void wifi_mqtt_init(void);
SystemState get_system_state(void);
esp_mqtt_client_handle_t get_mqtt_client(void);
const uint8_t* get_device_mac(void);