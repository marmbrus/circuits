#pragma once

#include "credentials.h"

// MQTT Configuration
#define MQTT_RECONNECT_TIMEOUT_MS 5000
#define MQTT_OPERATION_TIMEOUT_MS 10000

// I2C Configuration
#define I2C_MASTER_SCL_IO           47      // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO           21      // GPIO number for I2C master data
#define I2C_MASTER_NUM              I2C_NUM_0   // I2C port number (using enum value)
#define I2C_MASTER_FREQ_HZ          400000  // I2C master clock frequency
#define I2C_MASTER_TIMEOUT_MS       1000

// Button Configuration
#define BUTTON1_GPIO                15
#define BUTTON2_GPIO                16
#define BUTTON_DEBOUNCE_TIME_MS     200

// Task Configuration
#define SENSOR_TASK_STACK_SIZE      4096
#define SENSOR_TASK_PRIORITY        5
#define SENSOR_TASK_INTERVAL_MS     10000

// Queue Configuration
#define IO_QUEUE_SIZE               10

// LED Configuration
#define LED_STRIP_GPIO              GPIO_NUM_48
#define LED_STRIP_NUM_PIXELS        1
#define LED_UPDATE_TASK_STACK_SIZE  2048
#define LED_UPDATE_INTERVAL_MS      50