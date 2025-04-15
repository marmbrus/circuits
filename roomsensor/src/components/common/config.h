#pragma once

#include "credentials.h"
#include <stdint.h>

// MQTT Configuration
#define MQTT_RECONNECT_TIMEOUT_MS 5000
#define MQTT_OPERATION_TIMEOUT_MS 10000

// I2C Configuration
#define I2C_MASTER_SCL_IO           GPIO_NUM_9      // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO           GPIO_NUM_10     // GPIO number for I2C master data
#define I2C_MASTER_NUM              I2C_NUM_0             // I2C port number
#define I2C_MASTER_FREQ_HZ          400000                // I2C master clock frequency
#define I2C_MASTER_TIMEOUT_MS       100

// Task Configuration
#define SENSOR_TASK_STACK_SIZE      4096
#define SENSOR_TASK_PRIORITY        5
#define SENSOR_TASK_INTERVAL_MS     10000

// Queue Configuration
#define IO_QUEUE_SIZE               10

// LED Configuration
#define LED_STRIP_GPIO              GPIO_NUM_3
#define LED_GRID_WIDTH              5
#define LED_GRID_HEIGHT             5
#define LED_STRIP_NUM_PIXELS        (LED_GRID_WIDTH * LED_GRID_HEIGHT)  // 5x5 grid
#define LED_STRIP_NUM_BRIGHTNESS    40  // Maximum brightness (0-255)
#define LED_UPDATE_TASK_STACK_SIZE  4096
#define LED_UPDATE_INTERVAL_MS      50

// Global variable for battery SOC
extern uint8_t g_battery_soc;

// Movement Interrupt Configuration
#define MOVEMENT_INT_GPIO           GPIO_NUM_1  // GPIO number for movement interrupt