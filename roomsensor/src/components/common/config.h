#pragma once

#include "credentials.h"
#include <stdint.h>
// Use explicit GPIO types for clarity in strip configuration
#include "driver/gpio.h"

#ifndef BOARD
#  define BOARD LED_CONTROLLER
#endif

#if (BOARD == LED_CONTROLLER)
#  define BOARD_LED_CONTROLLER 1
#elif (BOARD == ROOM_SENSOR)
#  define BOARD_ROOM_SENSOR 1
#else
#  error "Unknown BOARD. Use -D BOARD=ROOM_SENSOR or -D BOARD=LED_CONTROLLER"
#endif

// MQTT Configuration
#define MQTT_RECONNECT_TIMEOUT_MS 5000
#define MQTT_OPERATION_TIMEOUT_MS 10000

// OTA Configuration
#define OTA_CHECK_INTERVAL_MS     1000000   // Check for updates every 1000 seconds
#define OTA_TASK_STACK_SIZE       4096
#define OTA_TASK_PRIORITY         3       // Lower priority than critical tasks

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
// Brightness is applied as a percentage [0-100]
#define LED_STRIP_NUM_BRIGHTNESS    5
#define LED_UPDATE_TASK_STACK_SIZE  4096
#define LED_UPDATE_INTERVAL_MS      53

// Chipset description for LED strips used by different boards
typedef enum {
    LED_CHIPSET_WS2812_GRB = 0,
    LED_CHIPSET_SK6812_RGBW = 1,
} led_chipset_t;

// Per-strip configuration entry
typedef struct {
    gpio_num_t data_gpio;     // Required data pin for the strip
    gpio_num_t enable_gpio;   // Optional enable pin (GPIO_NUM_NC if unused)
    uint16_t grid_width;      // Logical grid width (e.g., 5 for a 5x5 grid or 1 for a strip)
    uint16_t grid_height;     // Logical grid height (e.g., 5 for a 5x5 grid or N for a strip)
    uint16_t num_pixels;      // Total number of LEDs on this strip (grid_width * grid_height)
    led_chipset_t chipset;    // Chipset/pixel format
} led_strip_config_entry_t;

#if defined(BOARD_ROOM_SENSOR)
// Room sensor board: single 5x5 grid on GPIO 11, WS2812 (GRB)
static const led_strip_config_entry_t LED_STRIP_CONFIG[] = {
    {
        .data_gpio = GPIO_NUM_11,
        .enable_gpio = GPIO_NUM_NC, // No dedicated enable pin
        .grid_width = 5,
        .grid_height = 5,
        .num_pixels = 5 * 5,
        .chipset = LED_CHIPSET_WS2812_GRB,
    },
};
#define LED_GRID_WIDTH              5
#define LED_GRID_HEIGHT             5
#define LED_STRIP_NUM_PIXELS        (LED_GRID_WIDTH * LED_GRID_HEIGHT)
#elif defined(BOARD_LED_CONTROLLER)
// LED controller board: up to 4 strips. Defaults below can be overridden at build time if needed.
// By default, enable only one strip on GPIO 11. Others are placeholders (num_pixels = 0 => disabled).
static const led_strip_config_entry_t LED_STRIP_CONFIG[] = {
    {
        .data_gpio = GPIO_NUM_11,
        .enable_gpio = GPIO_NUM_15, // Optional power enable; set to a GPIO to use
        .grid_width = 700,
        .grid_height = 1,
        .num_pixels = 700,
        .chipset = LED_CHIPSET_SK6812_RGBW,
    },
    {
        .data_gpio = GPIO_NUM_12,
        .enable_gpio = GPIO_NUM_16,
        .grid_width = 8,
        .grid_height = 1,
        .num_pixels = 8,
        .chipset = LED_CHIPSET_SK6812_RGBW,
    },
    {
        .data_gpio = GPIO_NUM_13,
        .enable_gpio = GPIO_NUM_17,
        .grid_width = 8,
        .grid_height = 1,
        .num_pixels = 8,
        .chipset = LED_CHIPSET_SK6812_RGBW,
    },
    {
        .data_gpio = GPIO_NUM_14,
        .enable_gpio = GPIO_NUM_18,
        .grid_width = 8,
        .grid_height = 1,
        .num_pixels = 8,
        .chipset = LED_CHIPSET_SK6812_RGBW,
    },
};
// For compatibility with existing effects, define primary logical grid as the first configured strip
#define LED_GRID_WIDTH              8
#define LED_GRID_HEIGHT             1
#define LED_STRIP_NUM_PIXELS        (LED_GRID_WIDTH * LED_GRID_HEIGHT)
#else
#error "Unknown BOARD configuration"
#endif

// Helper macros to access the number of configured strips and check validity
#define LED_STRIP_CONFIG_COUNT (sizeof(LED_STRIP_CONFIG) / sizeof(LED_STRIP_CONFIG[0]))


// Global variable for battery SOC
extern uint8_t g_battery_soc;

// Movement Interrupt Configuration
#define MOVEMENT_INT_GPIO           GPIO_NUM_1  // GPIO number for movement interrupt