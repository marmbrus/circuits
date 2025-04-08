#pragma once

#include "esp_err.h"

// Global battery state of charge variable used by led_control.cpp
extern uint8_t g_battery_soc;

typedef enum {
    ORIENTATION_UP,
    ORIENTATION_DOWN,
    ORIENTATION_LEFT,
    ORIENTATION_RIGHT,
    ORIENTATION_TOP,
    ORIENTATION_BOTTOM,
    ORIENTATION_UNKNOWN
} device_orientation_t;

// Define callback function types
typedef void (*movement_callback_t)(void);
typedef void (*orientation_callback_t)(device_orientation_t);

// Function declarations
esp_err_t sensors_init_with_callbacks(movement_callback_t movement_cb, orientation_callback_t orientation_cb);
esp_err_t sensors_process(void);

// Constants
#define SENSOR_TASK_PERIOD_MS 100