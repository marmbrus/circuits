#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
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

// OTAUpdateLights implementation
class OTAUpdateLights : public LEDBehavior {
public:
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override {
        // Pulse all LEDs white during OTA update
        for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
            led_control_set_pixel(led_strip, i, pulse_brightness, pulse_brightness, pulse_brightness);
        }
    }
};

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

// Support multiple LED strips configured via LED_STRIP_CONFIG
static led_strip_handle_t led_strips[LED_STRIP_CONFIG_COUNT] = {0};
static uint16_t strip_lengths[LED_STRIP_CONFIG_COUNT] = {0};
static size_t num_initialized_strips = 0;

static int find_strip_index(led_strip_handle_t handle) {
    for (size_t i = 0; i < num_initialized_strips; ++i) {
        if (led_strips[i] == handle) {
            return (int)i;
        }
    }
    return -1;
}
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
static OTAUpdateLights ota_update_lights;
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
                for (size_t s = 0; s < num_initialized_strips; ++s) {
                    connecting_lights.update(led_strips[s], pulse_brightness);
                }
            } else {
                // After first connection, show disconnected animation after a threshold
                disconnected_time_ms += LED_UPDATE_INTERVAL_MS;
                
                if (disconnected_time_ms < DISCONNECTED_THRESHOLD_MS) {
                    // Brief disconnect, just keep LEDs off
                    for (size_t s = 0; s < num_initialized_strips; ++s) {
                        no_lights.update(led_strips[s], pulse_brightness);
                    }
                } else {
                    // Persistent WiFi disconnect
                    disconnected_lights.setDisconnectType(WIFI_DISCONNECT);
                    for (size_t s = 0; s < num_initialized_strips; ++s) {
                        disconnected_lights.update(led_strips[s], pulse_brightness);
                    }
                }
            }
        } else if (current_state == WIFI_CONNECTED_MQTT_CONNECTING) {
            if (!has_been_connected) {
                // On initial connection, show connecting animation
                for (size_t s = 0; s < num_initialized_strips; ++s) {
                    connecting_lights.update(led_strips[s], pulse_brightness);
                }
            } else if (previous_state == FULLY_CONNECTED) {
                // We were fully connected before but now MQTT is disconnected
                disconnected_time_ms += LED_UPDATE_INTERVAL_MS;
                
                if (disconnected_time_ms < DISCONNECTED_THRESHOLD_MS) {
                    // Brief disconnect, just keep LEDs off
                    for (size_t s = 0; s < num_initialized_strips; ++s) {
                        no_lights.update(led_strips[s], pulse_brightness);
                    }
                } else {
                    // Persistent MQTT disconnect
                    disconnected_lights.setDisconnectType(MQTT_DISCONNECT);
                    for (size_t s = 0; s < num_initialized_strips; ++s) {
                        disconnected_lights.update(led_strips[s], pulse_brightness);
                    }
                }
            } else {
                // For any other state transition in MQTT connecting state
                // Check if this is a reconnection (has been connected before)
                if (has_been_connected) {
                    // This is a reconnection, don't show connecting animation
                    for (size_t s = 0; s < num_initialized_strips; ++s) {
                        no_lights.update(led_strips[s], pulse_brightness);
                    }
                } else {
                    // Initial connection process, show connecting animation
                    for (size_t s = 0; s < num_initialized_strips; ++s) {
                        connecting_lights.update(led_strips[s], pulse_brightness);
                    }
                }
            }
        } else if (current_state == FULLY_CONNECTED) {
            // Reset disconnect timer
            disconnected_time_ms = 0;
            
            if (just_connected && !has_been_connected) {
                // First time connection - show startup ripple
                // This check should never happen since we set has_been_connected above
                // but included for safety
                for (size_t s = 0; s < num_initialized_strips; ++s) {
                    connected_lights.update(led_strips[s], pulse_brightness);
                }
                ripple_time = 0;
            } else if (has_been_connected && ripple_time < 5000) {
                // Currently showing the startup ripple - only log at start and end
                if (ripple_time == 0) {
                    ESP_LOGI(TAG, "Starting startup ripple animation");
                }
                
                for (size_t s = 0; s < num_initialized_strips; ++s) {
                    connected_lights.update(led_strips[s], pulse_brightness);
                }
                ripple_time += LED_UPDATE_INTERVAL_MS;
                
                if (ripple_time >= 5000) {
                    ESP_LOGI(TAG, "Startup ripple display complete");
                }
            } else {
                // Normal connected state
#if defined(BOARD_LED_CONTROLLER)
                // LED controller: keep strips on at full brightness (white)
                for (size_t s = 0; s < num_initialized_strips; ++s) {
                    uint16_t length = strip_lengths[s];
                    for (uint16_t i = 0; i < length; ++i) {
                        led_control_set_pixel(led_strips[s], i, 100, 100, 100);
                    }
                }
#else
                // Room sensor: no lights when fully connected
                for (size_t s = 0; s < num_initialized_strips; ++s) {
                    no_lights.update(led_strips[s], pulse_brightness);
                }
#endif
            }
        } else if (current_state == MQTT_ERROR_STATE) {
            // For error state, use a specific error indication
            for (size_t s = 0; s < num_initialized_strips; ++s) {
                for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
                    led_control_set_pixel(led_strips[s], i, 100, 0, 0); // All red for error
                }
            }
        } else if (current_state == OTA_UPDATING) {
            // For OTA updates, pulse all LEDs white
            for (size_t s = 0; s < num_initialized_strips; ++s) {
                ota_update_lights.update(led_strips[s], pulse_brightness);
            }
        }
        
        // Remember the current state for next time
        previous_state = current_state;

        for (size_t s = 0; s < num_initialized_strips; ++s) {
            led_strip_refresh(led_strips[s]);
        }
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_INTERVAL_MS));
    }
}

void led_control_init(void) {
    ESP_LOGI(TAG, "Initializing LED Control");

    // Initialize each configured strip
    for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
        const led_strip_config_entry_t &cfg = LED_STRIP_CONFIG[i];
        if (cfg.data_gpio == GPIO_NUM_NC || cfg.num_pixels == 0) {
            ESP_LOGI(TAG, "Strip %d skipped (gpio=%d, pixels=%d)", (int)i, (int)cfg.data_gpio, (int)cfg.num_pixels);
            continue; // Skip unconfigured entries
        }

        // Optional power enable pin
        if (cfg.enable_gpio != GPIO_NUM_NC) {
            gpio_reset_pin(cfg.enable_gpio);
            gpio_set_direction(cfg.enable_gpio, GPIO_MODE_OUTPUT);
            gpio_set_level(cfg.enable_gpio, 1);
            ESP_LOGI(TAG, "Strip %d: enable gpio %d set HIGH", (int)i, (int)cfg.enable_gpio);
        }

        // Do not manipulate data GPIOs here; RMT/LED driver will configure pins

        led_strip_config_t strip_config = {};
        strip_config.strip_gpio_num = cfg.data_gpio;
        strip_config.max_leds = cfg.num_pixels;

        // Select pixel format and model based on chipset
        switch (cfg.chipset) {
            case LED_CHIPSET_SK6812_RGBW:
                strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRBW;
                strip_config.led_model = LED_MODEL_SK6812;
                break;
            case LED_CHIPSET_WS2812_GRB:
            default:
                strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
                strip_config.led_model = LED_MODEL_WS2812;
                break;
        }
        strip_config.flags.invert_out = false;

        led_strip_rmt_config_t rmt_config = {};
        rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
        rmt_config.resolution_hz = 10 * 1000 * 1000;
        // Per RMT docs, use an even value >= 48; 64 improves throughput while remaining safe
        rmt_config.mem_block_symbols = 64;
        // Disable DMA to match previously working configuration and keep channel allocation simple
        rmt_config.flags.with_dma = false;

        led_strip_handle_t handle = nullptr;
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &handle);
        if (err == ESP_OK && handle != nullptr) {
            led_strips[num_initialized_strips] = handle;
            strip_lengths[num_initialized_strips] = cfg.num_pixels;
            num_initialized_strips++;
            led_strip_clear(handle);
            led_strip_refresh(handle);
            ESP_LOGI(TAG, "Strip %d initialized: data gpio=%d, pixels=%d, chipset=%d", (int)i, (int)cfg.data_gpio, (int)cfg.num_pixels, (int)cfg.chipset);
        } else {
            ESP_LOGE(TAG, "Failed to initialize LED strip %d (gpio=%d, pixels=%d): %s", (int)i, (int)cfg.data_gpio, (int)cfg.num_pixels, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Total initialized strips: %d", (int)num_initialized_strips);

    // No additional pre-task test; steady state logic will continuously refresh

    // LED counting test code - run once to determine number of LEDs (found 42)
    if (false) {
        ESP_LOGI(TAG, "Starting LED counting test...");
        for (int i = 0; i < 1024; i++) {
            ESP_LOGI(TAG, "Testing LED %d", i);
            for (size_t s = 0; s < num_initialized_strips; ++s) {
                led_strip_set_pixel(led_strips[s], i, 20, 20, 20);  // Dim white
                led_strip_refresh(led_strips[s]);
            }
            vTaskDelay(pdMS_TO_TICKS(500));  // Half second delay

            if (i % 10 == 9) {
                ESP_LOGI(TAG, "LEDs 0-%d tested", i);
            }
        }
        ESP_LOGI(TAG, "LED counting test complete");

        // Clear LEDs after test
        for (size_t s = 0; s < num_initialized_strips; ++s) {
            led_strip_clear(led_strips[s]);
            led_strip_refresh(led_strips[s]);
        }
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
            case OTA_UPDATING: state_str = "OTA_UPDATING"; break;
        }
        
        switch (current_state) {
            case WIFI_CONNECTING: prev_state_str = "WIFI_CONNECTING"; break;
            case WIFI_CONNECTED_MQTT_CONNECTING: prev_state_str = "WIFI_CONNECTED_MQTT_CONNECTING"; break;
            case FULLY_CONNECTED: prev_state_str = "FULLY_CONNECTED"; break;
            case MQTT_ERROR_STATE: prev_state_str = "MQTT_ERROR_STATE"; break;
            case OTA_UPDATING: prev_state_str = "OTA_UPDATING"; break;
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
    for (size_t s = 0; s < num_initialized_strips; ++s) {
        if (led_strips[s]) {
            led_strip_clear(led_strips[s]);
            led_strip_refresh(led_strips[s]);
        }
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
    // Guard against out-of-range pixel access per strip
    int strip_index = find_strip_index(led_strip);
    if (strip_index >= 0) {
        if (index >= strip_lengths[strip_index]) {
            return ESP_OK; // Silently ignore out-of-range to support generic effects
        }
    }

    // Scale all color components according to LED_STRIP_NUM_BRIGHTNESS
    red = (red * LED_STRIP_NUM_BRIGHTNESS) / 100;
    green = (green * LED_STRIP_NUM_BRIGHTNESS) / 100;
    blue = (blue * LED_STRIP_NUM_BRIGHTNESS) / 100;

    return led_strip_set_pixel(led_strip, index, red, green, blue);
}