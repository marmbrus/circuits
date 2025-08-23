#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize network logging:
// - Installs a vprintf hook to capture ESP-IDF logs
// - Starts a background task that publishes queued log lines to MQTT
//   at sensor/$mac/logs/$level once the network is connected
esp_err_t netlog_init_early(void);

#ifdef __cplusplus
}
#endif


