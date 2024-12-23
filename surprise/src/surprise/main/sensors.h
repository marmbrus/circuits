#pragma once

#include "esp_err.h"
#include "io_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the sensor handling system and start the sensor task
 *
 * @return ESP_OK if successful, appropriate error code otherwise
 */
esp_err_t sensors_init(IOManager* ioManager);

enum class DeviceOrientation {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    UNKNOWN
};

#ifdef __cplusplus
}
#endif