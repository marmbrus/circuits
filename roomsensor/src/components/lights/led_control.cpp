#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "ConnectingLights.h"
#include "ConnectedLights.h"
#include "DisconnectedLights.h"

static const char* TAG = "LED_Control";

// NoLights implementation
void NoLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
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
    for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
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

    for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
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
// Track if we've been fully connected before in this power cycle
static bool has_been_connected = false;
// Make this static variable accessible to other functions in this file
static uint32_t disconnected_time_ms = 0;
static constexpr uint32_t DISCONNECTED_THRESHOLD_MS = 10000; // 10 seconds threshold

extern uint8_t g_battery_soc;

// Create static instances of LED behaviors
static NoLights no_lights;
static ChristmasLights christmas_lights;
static FourColorLights four_color_lights;
static ConnectingLights connecting_lights;
static ConnectedLights connected_lights;
static DisconnectedLights disconnected_lights;
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

static void update_led_task(void* pvParameters) {
    static SystemState previous_state = WIFI_CONNECTING;
    static uint32_t ripple_time = 0;
    
    while (1) {
        update_pulse_brightness();
        
        // Check if we've just connected for the first time
        bool just_connected = (previous_state != FULLY_CONNECTED && current_state == FULLY_CONNECTED);
        
        if (just_connected && !has_been_connected) {
            // First time connecting, set the flag and show ripple
            has_been_connected = true;
            ripple_time = 0;  // Reset ripple timer
            disconnected_time_ms = 0; // Reset disconnect timer when we connect
            // Reset connected lights animation to ensure ripple shows from the beginning
            connected_lights.reset();
            ESP_LOGI(TAG, "First connection established, showing startup ripple");
        }
        
        // State transition detection
        if (current_state != previous_state) {
            // Log state transitions for debugging
            ESP_LOGI(TAG, "State transition: %d -> %d", previous_state, current_state);
        }
        
        // Update LED based on system state
        if (current_state == WIFI_CONNECTING) {
            if (!has_been_connected) {
                // Only show connecting animation on first boot
                connecting_lights.update(led_strip, pulse_brightness);
            } else {
                // After first connection, show disconnected animation after a threshold
                disconnected_time_ms += LED_UPDATE_INTERVAL_MS;
                
                if (disconnected_time_ms < DISCONNECTED_THRESHOLD_MS) {
                    // Brief disconnect, just keep LEDs off
                    no_lights.update(led_strip, pulse_brightness);
                } else {
                    // Persistent WiFi disconnect
                    disconnected_lights.setDisconnectType(WIFI_DISCONNECT);
                    disconnected_lights.update(led_strip, pulse_brightness);
                }
            }
        } else if (current_state == WIFI_CONNECTED_MQTT_CONNECTING) {
            if (!has_been_connected) {
                // On initial connection, show connecting animation
                connecting_lights.update(led_strip, pulse_brightness);
            } else if (previous_state == FULLY_CONNECTED) {
                // We were fully connected before but now MQTT is disconnected
                disconnected_time_ms += LED_UPDATE_INTERVAL_MS;
                
                if (disconnected_time_ms < DISCONNECTED_THRESHOLD_MS) {
                    // Brief disconnect, just keep LEDs off
                    no_lights.update(led_strip, pulse_brightness);
                } else {
                    // Persistent MQTT disconnect
                    disconnected_lights.setDisconnectType(MQTT_DISCONNECT);
                    disconnected_lights.update(led_strip, pulse_brightness);
                }
            } else {
                // For any other state transition in MQTT connecting state
                // Check if this is a reconnection (has been connected before)
                if (has_been_connected) {
                    // This is a reconnection, don't show connecting animation
                    no_lights.update(led_strip, pulse_brightness);
                } else {
                    // Initial connection process, show connecting animation
                    connecting_lights.update(led_strip, pulse_brightness);
                }
            }
        } else if (current_state == FULLY_CONNECTED) {
            // Reset disconnect timer
            disconnected_time_ms = 0;
            
            if (just_connected && !has_been_connected) {
                // First time connection - show startup ripple
                // This check should never happen since we set has_been_connected above
                // but included for safety
                connected_lights.update(led_strip, pulse_brightness);
                ripple_time = 0;
            } else if (has_been_connected && ripple_time < 5000) {
                // Currently showing the startup ripple - only log at start and end
                if (ripple_time == 0) {
                    ESP_LOGI(TAG, "Starting startup ripple animation");
                }
                
                connected_lights.update(led_strip, pulse_brightness);
                ripple_time += LED_UPDATE_INTERVAL_MS;
                
                if (ripple_time >= 5000) {
                    ESP_LOGI(TAG, "Startup ripple display complete");
                }
            } else {
                // Normal connected state - no animation
                no_lights.update(led_strip, pulse_brightness);
            }
        } else if (current_state == MQTT_ERROR_STATE) {
            // For error state, use a specific error indication
            for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
                led_control_set_pixel(led_strip, i, 100, 0, 0); // All red for error
            }
        }
        
        // Remember the current state for next time
        previous_state = current_state;

        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_INTERVAL_MS));
    }
}

void led_control_init(void) {
    ESP_LOGI(TAG, "Initializing LED Control");

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
    // Only log when state actually changes
    if (current_state != state) {
        // Convert enum to string for better logging
        const char* state_str = "UNKNOWN";
        const char* prev_state_str = "UNKNOWN";
        
        switch (state) {
            case WIFI_CONNECTING: state_str = "WIFI_CONNECTING"; break;
            case WIFI_CONNECTED_MQTT_CONNECTING: state_str = "WIFI_CONNECTED_MQTT_CONNECTING"; break;
            case FULLY_CONNECTED: state_str = "FULLY_CONNECTED"; break;
            case MQTT_ERROR_STATE: state_str = "MQTT_ERROR_STATE"; break;
        }
        
        switch (current_state) {
            case WIFI_CONNECTING: prev_state_str = "WIFI_CONNECTING"; break;
            case WIFI_CONNECTED_MQTT_CONNECTING: prev_state_str = "WIFI_CONNECTED_MQTT_CONNECTING"; break;
            case FULLY_CONNECTED: prev_state_str = "FULLY_CONNECTED"; break;
            case MQTT_ERROR_STATE: prev_state_str = "MQTT_ERROR_STATE"; break;
        }
        
        ESP_LOGI(TAG, "State change: %s -> %s", prev_state_str, state_str);
        
        // Special handling for state transitions
        if (state == FULLY_CONNECTED && current_state != FULLY_CONNECTED) {
            if (!has_been_connected) {
                // First time connection - will show startup ripple
                ESP_LOGD(TAG, "First time connection detected - ripple will be shown");
            } else {
                // Reconnection - no ripple needed
                ESP_LOGD(TAG, "Reconnection detected - no ripple will be shown");
            }
        }
        
        // Reset timers for certain state transitions
        if (state != WIFI_CONNECTING && current_state == WIFI_CONNECTING) {
            // Moving out of WiFi connecting state
            disconnected_time_ms = 0;
        }
    }
    
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
    // Empty function - no button LEDs in this version
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