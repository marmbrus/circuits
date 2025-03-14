#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char* TAG = "LED_Control";

// NoLights implementation
void NoLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        led_control_set_pixel(led_strip, i, 0, 0, 0);
    }
}

// FourColorLights implementation
FourColorLights::FourColorLights() {
    clearColors();
}

void FourColorLights::setColor(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= 0 && index < 4) {
        colors[index][0] = red;
        colors[index][1] = green;
        colors[index][2] = blue;
    }
}

void FourColorLights::clearColors() {
    for (int i = 0; i < 4; ++i) {
        colors[i][0] = 0;
        colors[i][1] = 0;
        colors[i][2] = 0;
    }
}

void FourColorLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        int colorIndex = i % 4;
        led_control_set_pixel(led_strip, i,
                          colors[colorIndex][0],
                          colors[colorIndex][1],
                          colors[colorIndex][2]);
    }
}

// ChristmasLights implementation
void ChristmasLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    static bool phase = false;
    static uint64_t last_update = 0;
    uint64_t current_time = esp_timer_get_time(); // Get current time in microseconds

    // Update phase every 500ms (2 times per second)
    if (current_time - last_update >= 500000) {  // 500000 microseconds = 500ms
        phase = !phase;
        last_update = current_time;
    }

    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        if ((i % 2 == 0) == phase) {  // XOR logic to alternate colors
            led_control_set_pixel(led_strip, i, pulse_brightness, 0, 0);  // Red
        } else {
            led_control_set_pixel(led_strip, i, 0, pulse_brightness, 0);  // Green
        }
    }
}

static led_strip_handle_t led_strip;
static SystemState current_state = WIFI_CONNECTING;
static uint8_t pulse_brightness = 0;
static bool pulse_increasing = true;
static TaskHandle_t led_update_task_handle = NULL;

static const gpio_num_t button_led_pins[] = BUTTON_LED_PINS;
static bool button_led_status[NUM_BUTTON_LEDS] = {true, true, true, true};

extern uint8_t g_battery_soc;

// Create static instances of LED behaviors
static NoLights no_lights;
static ChristmasLights christmas_lights;
static FourColorLights four_color_lights;
static LEDBehavior* current_behavior = &no_lights;

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
            led_control_set_pixel(led_strip, 0, 0, 0, pulse_brightness);
            break;
        case WIFI_CONNECTED_MQTT_CONNECTING:
            led_control_set_pixel(led_strip, 0, pulse_brightness, pulse_brightness/2, 0);
            break;
        case FULLY_CONNECTED:
            led_control_set_pixel(led_strip, 0, 0, 100, 0);
            break;
        case MQTT_ERROR_STATE:
            led_control_set_pixel(led_strip, 0, 100, 0, 0);
            break;
    }
}

static void update_battery_leds() {
    uint8_t capped_brightness = (pulse_brightness * g_battery_soc) / 100;
    for (int i = 1; i <= 2; ++i) {
        led_control_set_pixel(led_strip, i, capped_brightness, capped_brightness, capped_brightness);
    }
}

static void update_other_leds() {
    if (current_behavior) {
        current_behavior->update(led_strip, pulse_brightness);
    }
}

static void update_button_leds() {
    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        // Set high when LED should be on, low when it should be off
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

static void count_leds_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting LED counting test...");

    // Wait for a moment to ensure system is stable
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Temporarily disable the regular LED update task
    if (led_update_task_handle != NULL) {
        vTaskSuspend(led_update_task_handle);
    }

    // Turn all LEDs off first and ensure RMT is in good state
    esp_err_t err = led_strip_clear(led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = led_strip_refresh(led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to refresh LED strip: %s", esp_err_to_name(err));
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Turn on LEDs one by one
    for (int i = 0; i < 1024; i++) {
        ESP_LOGI(TAG, "Turning on LED %d", i);

        // Set single LED
        err = led_strip_set_pixel(led_strip, i, 20, 20, 20);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set LED %d: %s", i, esp_err_to_name(err));
            break;
        }

        err = led_strip_refresh(led_strip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to refresh strip at LED %d: %s", i, esp_err_to_name(err));
            break;
        }

        // Every 10 LEDs, log a summary
        if (i % 10 == 9) {
            ESP_LOGI(TAG, "LEDs 0-%d are now on", i);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

cleanup:
    ESP_LOGI(TAG, "LED counting test complete");

    // Clear all LEDs before resuming normal operation
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Resume the LED update task
    if (led_update_task_handle != NULL) {
        vTaskResume(led_update_task_handle);
    }

    vTaskDelete(NULL);
}

void led_control_init(void) {
    ESP_LOGI(TAG, "Initializing LED Control");

    // Initialize GPIOs for the button LEDs
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    // Initialize all button LEDs at once with a single bitmask
    uint64_t button_led_mask = 0;
    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        button_led_mask |= (1ULL << button_led_pins[i]);
    }
    io_conf.pin_bit_mask = button_led_mask;
    gpio_config(&io_conf);

    // Initialize all button LEDs to low (off)
    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        gpio_set_level(button_led_pins[i], 0);  // Set low (off) initially
        button_led_status[i] = false;
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
    led_strip_refresh(led_strip);

    // LED counting test code - run once to determine number of LEDs (found 42)
    if (false) {
        ESP_LOGI(TAG, "Starting LED counting test...");
        for (int i = 0; i < 1024; i++) {
            ESP_LOGI(TAG, "Testing LED %d", i);
            led_strip_set_pixel(led_strip, i, 20, 20, 20);  // Dim white
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(500));  // Half second delay

            if (i % 10 == 9) {
                ESP_LOGI(TAG, "LEDs 0-%d tested", i);
            }
        }
        ESP_LOGI(TAG, "LED counting test complete");

        // Clear LEDs after test
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
    }

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
        // Set high when LED should be on, low when it should be off
        gpio_set_level(button_led_pins[index], status ? 1 : 0);
    }
}

void led_control_set_behavior(LEDBehavior* behavior) {
    if (behavior != nullptr) {
        current_behavior = behavior;
    }
}

esp_err_t led_control_set_pixel(led_strip_handle_t led_strip, uint32_t index, uint8_t red, uint8_t green, uint8_t blue) {
    // Scale all color components according to LED_STRIP_NUM_BRIGHTNESS
    red = (red * LED_STRIP_NUM_BRIGHTNESS) / 100;
    green = (green * LED_STRIP_NUM_BRIGHTNESS) / 100;
    blue = (blue * LED_STRIP_NUM_BRIGHTNESS) / 100;

    return led_strip_set_pixel(led_strip, index, red, green, blue);
}