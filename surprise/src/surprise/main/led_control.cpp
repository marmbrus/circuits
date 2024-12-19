#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "config.h"
#include "driver/gpio.h"
#include "LEDBehavior.h"
#include "ChristmasLights.cpp" // Include the implementation
#include "NoLights.cpp"        // Include the NoLights implementation
#include "FourColorLights.cpp" // Include the FourColorLights implementation

static const char* TAG = "LED_Control";

static led_strip_handle_t led_strip;
static SystemState current_state = WIFI_CONNECTING;
static uint8_t pulse_brightness = 0;
static bool pulse_increasing = true;
static TaskHandle_t led_update_task_handle = NULL; // Task handle for the LED update task

static const gpio_num_t button_led_pins[] = BUTTON_LED_PINS;
static bool button_led_status[NUM_BUTTON_LEDS] = {true, true, true, true}; // Default all to on

extern uint8_t g_battery_soc; // Declare the global SOC variable

// Create instances of LED behaviors
static ChristmasLights christmas_lights;
static NoLights no_lights;
static FourColorLights four_color_lights;

// Pointer to the current LED behavior
static LEDBehavior* current_led_behavior = &no_lights; // Default to NoLights

static void update_pulse_brightness() {
    if (pulse_increasing) {
        pulse_brightness = (pulse_brightness + 5) % 100;
        if (pulse_brightness >= 95) pulse_increasing = false;
    } else {
        pulse_brightness = (pulse_brightness - 5) % 100;
        if (pulse_brightness <= 5) {
            pulse_increasing = true;
            pulse_brightness = 0; // Ensure it hits zero
        }
    }
}

static void update_status_led() {
    switch (current_state) {
        case WIFI_CONNECTING:
            // Pulse blue on the first LED
            led_strip_set_pixel(led_strip, 0, 0, 0, pulse_brightness);
            break;

        case WIFI_CONNECTED_MQTT_CONNECTING:
            // Pulse orange on the first LED
            led_strip_set_pixel(led_strip, 0, pulse_brightness, pulse_brightness/2, 0);
            break;

        case FULLY_CONNECTED:
            // Solid green on the first LED
            led_strip_set_pixel(led_strip, 0, 0, 100, 0);
            break;

        case MQTT_ERROR_STATE:
            // Solid red on the first LED
            led_strip_set_pixel(led_strip, 0, 100, 0, 0);
            break;
    }
}

static void update_battery_leds() {
    // Cap the brightness based on the battery SOC
    uint8_t capped_brightness = (pulse_brightness * g_battery_soc) / 100;
    for (int i = 1; i <= 2; ++i) {
        led_strip_set_pixel(led_strip, i, capped_brightness, capped_brightness, capped_brightness);
    }
}

static void update_other_leds() {
    // Use the current LED behavior to update the other LEDs
    current_led_behavior->update(led_strip, pulse_brightness);
}

static void update_button_leds() {
    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        gpio_set_level(button_led_pins[i], button_led_status[i] ? 1 : 0);
    }
}

static void update_led_task(void* pvParameters) {
    while (1) {
        update_pulse_brightness();
        update_status_led();
        update_battery_leds(); // Update the battery LEDs
        update_other_leds();   // Update the other LEDs
        update_button_leds();  // Update the button LEDs status

        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_INTERVAL_MS));
    }
}

void led_control_init(void) {
    ESP_LOGI(TAG, "Initializing LED Control");

    // Initialize GPIOs for the button LEDs
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        io_conf.pin_bit_mask = (1ULL << button_led_pins[i]);
        gpio_config(&io_conf);
    }

    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_NUM_PIXELS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        }
    };

    // RMT configuration
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        }
    };

    // Create LED strip
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    // Clear LED strip initially
    led_strip_clear(led_strip);

    // Create LED update task
    xTaskCreate(update_led_task, "led_update_task",
                LED_UPDATE_TASK_STACK_SIZE, NULL, 5, &led_update_task_handle);

    ESP_LOGI(TAG, "LED Control initialized successfully");
}

void led_control_set_state(SystemState state) {
    current_state = state;
}

void led_control_clear() {
    if (led_strip) {
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
    }
}

void led_control_stop() {
    if (led_update_task_handle != NULL) {
        vTaskDelete(led_update_task_handle);
        led_update_task_handle = NULL;
    }
}

void led_control_set_button_led_status(int index, bool status) {
    if (index >= 0 && index < NUM_BUTTON_LEDS) {
        button_led_status[index] = status;
        gpio_set_level(button_led_pins[index], status ? 1 : 0); // Immediately update the LED status
    }
}

void led_control_set_behavior(LEDBehavior* behavior) {
    if (behavior != nullptr) {
        current_led_behavior = behavior;
    }
}