#pragma once

#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configure MQTT LWT topic/payload based on TagsConfig (area/room/id)
// Must be called before esp_mqtt_client_init/start
esp_err_t telemetry_configure_lwt(esp_mqtt_client_config_t* mqtt_cfg);

// Read what the boot message reports about the last crash (the core dump). Call once at boot,
// from a task with an internal stack, before MQTT connects.
void telemetry_capture_boot_info(void);

// Publish all telemetry that should be sent when MQTT connects
// - Device boot info
// - Location info + connected retained
void telemetry_report_connected(void);

#ifdef __cplusplus
}
#endif


