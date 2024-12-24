#pragma once

#include "credentials.h"
#include <stdint.h>

// MQTT Configuration
#define MQTT_RECONNECT_TIMEOUT_MS 5000
#define MQTT_OPERATION_TIMEOUT_MS 10000

// I2C Configuration
#define I2C_MASTER_SCL_IO           ((gpio_num_t)47)      // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO           ((gpio_num_t)21)      // GPIO number for I2C master data
#define I2C_MASTER_NUM              I2C_NUM_0             // I2C port number
#define I2C_MASTER_FREQ_HZ          400000                // I2C master clock frequency
#define I2C_MASTER_TIMEOUT_MS       100

// Button Configuration
#define BUTTON1_GPIO                15
#define BUTTON2_GPIO                16
#define BUTTON3_GPIO                17
#define BUTTON4_GPIO                18
#define BUTTON_DEBOUNCE_TIME_MS     300

// Task Configuration
#define SENSOR_TASK_STACK_SIZE      4096
#define SENSOR_TASK_PRIORITY        5
#define SENSOR_TASK_INTERVAL_MS     10000

// Queue Configuration
#define IO_QUEUE_SIZE               10

// LED Configuration
#define LED_STRIP_GPIO              GPIO_NUM_48
#define LED_STRIP_NUM_PIXELS        43  // Counted with test routine
#define LED_STRIP_NUM_BRIGHTNESS    40  // Maximum brightness (0-255)
#define LED_UPDATE_TASK_STACK_SIZE  4096
#define LED_UPDATE_INTERVAL_MS      50

// Button LED Configuration
#define BUTTON_LED_PINS             {GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12}
#define NUM_BUTTON_LEDS             4

#define INACTIVITY_THRESHOLD_MS 30000  // 5 seconds in milliseconds

// Global variable for battery SOC
extern uint8_t g_battery_soc;

// Movement Interrupt Configuration
#define MOVEMENT_INT_GPIO           GPIO_NUM_1  // GPIO number for movement interrupt