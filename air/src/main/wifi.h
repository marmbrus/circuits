#pragma once

#include "esp_system.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include "system_state.h"

// Function declarations
void wifi_mqtt_init(void);
SystemState get_system_state(void);
esp_mqtt_client_handle_t get_mqtt_client(void);
const uint8_t* get_device_mac(void);
// Block until the retained boot/device message has been published (QoS1 PUBACK)
// timeout_ms <= 0 waits indefinitely
esp_err_t wifi_wait_for_boot_publish(int timeout_ms);
// Block until SNTP has synchronized system time
// timeout_ms <= 0 waits indefinitely
esp_err_t wifi_wait_for_time_sync(int timeout_ms);