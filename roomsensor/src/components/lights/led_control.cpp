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
#include <cstdlib>
#include <cstring>
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
static uint64_t strip_last_refresh_us[LED_STRIP_CONFIG_COUNT] = {0};
static uint32_t strip_frame_time_us[LED_STRIP_CONFIG_COUNT] = {0};
static uint8_t strip_base_hue[LED_STRIP_CONFIG_COUNT] = {0};
// Cache previous group values to avoid rewriting unchanged pixels
static uint8_t* group_prev_vals[LED_STRIP_CONFIG_COUNT] = {0};
static uint16_t group_counts[LED_STRIP_CONFIG_COUNT] = {0};
static const uint16_t k_group_size = 8;
// Slowest-ramp state: increment one LED by 1 each frame
static uint8_t* pixel_vals[LED_STRIP_CONFIG_COUNT] = {0};
static uint16_t ramp_strip_idx = 0;
static uint16_t ramp_pixel_idx = 0;
static bool ramp_all_max = false;
static uint64_t fps_last_log_us = 0;
static uint32_t fps_frames = 0;

// Cycle control: on -> fade up -> log/hold -> fade down -> off
typedef enum {
	RAMP_PHASE_UP = 0,
	RAMP_PHASE_AT_MAX_HOLD,
	RAMP_PHASE_DOWN,
	RAMP_PHASE_OFF_HOLD,
} ramp_phase_t;
static ramp_phase_t g_ramp_phase = RAMP_PHASE_UP;
static uint32_t g_hold_elapsed_ms = 0;
static const uint32_t HOLD_AT_MAX_MS = 1000; // 1s hold at full before fading down
static const uint32_t HOLD_OFF_MS = 5000;    // 5s power-off
static uint32_t ramp_total_leds = 0;
static uint32_t ramp_completed_leds = 0;

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
// Post-startup pulse control
static uint32_t post_ripple_elapsed_ms = 0;     // milliseconds elapsed in brightness ramp (0-15000)
static bool in_disable_window = false;          // true when enable pins are held low
static uint32_t disable_window_elapsed_ms = 0;  // milliseconds elapsed in disable window (0-5000)

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
        // Temporary: render rainbow fade across all channels (8 LEDs per strip) as smoothly as possible
#if defined(BOARD_LED_CONTROLLER)
        auto hsv_to_rgb = [](uint8_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
            if (s == 0) { r = g = b = v; return; }
            uint8_t region = h / 43; // 0..5
            uint8_t remainder = (uint8_t)((h - region * 43) * 6);
            uint16_t p = (uint16_t)v * (255 - s) / 255;
            uint16_t q = (uint16_t)v * (255 - ((uint16_t)s * remainder) / 255) / 255;
            uint16_t t = (uint16_t)v * (255 - ((uint16_t)s * (255 - remainder)) / 255) / 255;
            switch (region) {
                default:
                case 0: r = v; g = (uint8_t)t; b = (uint8_t)p; break;
                case 1: r = (uint8_t)q; g = v; b = (uint8_t)p; break;
                case 2: r = (uint8_t)p; g = v; b = (uint8_t)t; break;
                case 3: r = (uint8_t)p; g = (uint8_t)q; b = v; break;
                case 4: r = (uint8_t)t; g = (uint8_t)p; b = v; break;
                case 5: r = v; g = (uint8_t)p; b = (uint8_t)q; break;
            }
        };

        uint64_t now_us_loop = esp_timer_get_time();
        // Advance hue slowly for smooth fade; ~25.6 deg/s at divisor 10
        // We'll increment hue per-strip only when we actually refresh that strip
        const uint8_t sat = 255;
        const uint8_t val = 255; // global brightness scaling is applied in led_control_set_pixel

        for (size_t s = 0; s < num_initialized_strips; ++s) {
            // Gate refresh to avoid overlapping a transmission in progress
            uint64_t last_us = strip_last_refresh_us[s];
            uint32_t frame_us = strip_frame_time_us[s] ? strip_frame_time_us[s] : 1000;
            if (now_us_loop - last_us >= frame_us) {
                // Compute and write this frame only when we're going to refresh
                uint16_t length = strip_lengths[s] ? strip_lengths[s] : 8;
                uint16_t step16 = (length > 0) ? (uint16_t)(65536 / length) : 0;
                uint16_t base16 = ((uint16_t)strip_base_hue[s]) << 8;
                for (uint16_t i = 0; i < length; ++i) {
                    uint8_t hue = (uint8_t)((base16 + (uint32_t)i * step16) >> 8);
                    uint8_t r, g, b;
                    hsv_to_rgb(hue, sat, val, r, g, b);
                    led_control_set_pixel(led_strips[s], i, r, g, b);
                }
                led_strip_refresh(led_strips[s]);
                strip_last_refresh_us[s] = now_us_loop;
                strip_base_hue[s]++; // advance slowly by 1 step per refresh
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_INTERVAL_MS));
        continue;
#endif
        
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
                if (num_initialized_strips > 0) {
                    switch (g_ramp_phase) {
                        case RAMP_PHASE_UP: {
                            // Ensure power enabled
                            for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
                                if (LED_STRIP_CONFIG[i].enable_gpio != GPIO_NUM_NC) gpio_set_level(LED_STRIP_CONFIG[i].enable_gpio, 1);
                            }
                            // Increment current LED by +1 per frame until it reaches 255; only then advance to the next LED
                            if (pixel_vals[ramp_strip_idx]) {
                                uint8_t &pv = pixel_vals[ramp_strip_idx][ramp_pixel_idx];
                                if (pv < 255) {
                                    pv++;
                                    led_control_set_pixel(led_strips[ramp_strip_idx], ramp_pixel_idx, pv, pv, pv);
                                }
                                if (pv >= 255) {
                                    // Current LED completed; move to next
                                    ramp_completed_leds++;
                                    ramp_pixel_idx++;
                                    if (ramp_pixel_idx >= strip_lengths[ramp_strip_idx]) {
                                        ramp_pixel_idx = 0;
                                        ramp_strip_idx++;
                                        if (ramp_strip_idx >= num_initialized_strips) ramp_strip_idx = 0;
                                    }
                                }
                            }
                            // Check if all at max
                            bool any_not_max = false;
                            if (ramp_completed_leds < ramp_total_leds) any_not_max = true;
                            fps_frames++;
                            if (!any_not_max) {
                                // Reached max: log FPS for this cycle, then hold
                                uint64_t now_us = esp_timer_get_time();
                                if (fps_last_log_us == 0) fps_last_log_us = now_us;
                                uint64_t delta_us = now_us - fps_last_log_us;
                                if (delta_us == 0) delta_us = 1;
                                uint32_t fps_x100 = (uint32_t)((fps_frames * 1000000ull * 100ull) / delta_us);
                                ESP_LOGI(TAG, "LED ramp cycle FPS: %u.%02u", (unsigned)(fps_x100 / 100u), (unsigned)(fps_x100 % 100u));
                                fps_last_log_us = now_us;
                                fps_frames = 0;
                                g_ramp_phase = RAMP_PHASE_AT_MAX_HOLD;
                                g_hold_elapsed_ms = 0;
                            }
                            break;
                        }
                        case RAMP_PHASE_AT_MAX_HOLD: {
                            g_hold_elapsed_ms += LED_UPDATE_INTERVAL_MS;
                            if (g_hold_elapsed_ms >= HOLD_AT_MAX_MS) {
                                g_ramp_phase = RAMP_PHASE_DOWN;
                                // Reset pointers for down ramp
                                ramp_strip_idx = 0;
                                ramp_pixel_idx = 0;
                            }
                            break;
                        }
                        case RAMP_PHASE_DOWN: {
                            // Decrement one LED by -1 per frame until all are zero
                            if (pixel_vals[ramp_strip_idx] && pixel_vals[ramp_strip_idx][ramp_pixel_idx] > 0) {
                                pixel_vals[ramp_strip_idx][ramp_pixel_idx]--;
                                uint8_t v = pixel_vals[ramp_strip_idx][ramp_pixel_idx];
                                led_control_set_pixel(led_strips[ramp_strip_idx], ramp_pixel_idx, v, v, v);
                            }
                            ramp_pixel_idx++;
                            if (ramp_pixel_idx >= strip_lengths[ramp_strip_idx]) {
                                ramp_pixel_idx = 0;
                                ramp_strip_idx++;
                                if (ramp_strip_idx >= num_initialized_strips) ramp_strip_idx = 0;
                            }
                            // Check if all zero
                            bool any_non_zero = false;
                            for (size_t s = 0; s < num_initialized_strips && !any_non_zero; ++s) {
                                if (!pixel_vals[s]) continue;
                                uint16_t len = strip_lengths[s];
                                for (uint16_t i = 0; i < len; ++i) { if (pixel_vals[s][i] != 0) { any_non_zero = true; break; } }
                            }
                            if (!any_non_zero) {
                                // Clear and power off
                                for (size_t s = 0; s < num_initialized_strips; ++s) {
                                    uint16_t len = strip_lengths[s];
                                    for (uint16_t i = 0; i < len; ++i) {
                                        led_control_set_pixel(led_strips[s], i, 0, 0, 0);
                                    }
                                }
                                for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
                                    if (LED_STRIP_CONFIG[i].enable_gpio != GPIO_NUM_NC) gpio_set_level(LED_STRIP_CONFIG[i].enable_gpio, 0);
                                }
                                g_ramp_phase = RAMP_PHASE_OFF_HOLD;
                                g_hold_elapsed_ms = 0;
                            }
                            break;
                        }
                        case RAMP_PHASE_OFF_HOLD: {
                            g_hold_elapsed_ms += LED_UPDATE_INTERVAL_MS;
                            if (g_hold_elapsed_ms >= HOLD_OFF_MS) {
                                // Re-enable and restart ramp from zero
                                for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
                                    if (LED_STRIP_CONFIG[i].enable_gpio != GPIO_NUM_NC) gpio_set_level(LED_STRIP_CONFIG[i].enable_gpio, 1);
                                }
                                for (size_t s = 0; s < num_initialized_strips; ++s) {
                                    if (!pixel_vals[s]) continue;
                                    memset(pixel_vals[s], 0x00, strip_lengths[s]);
                                }
                                ramp_strip_idx = 0;
                                ramp_pixel_idx = 0;
                                g_ramp_phase = RAMP_PHASE_UP;
                                // Reset FPS counters for new cycle
                                fps_last_log_us = esp_timer_get_time();
                                fps_frames = 0;
                            }
                            break;
                        }
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

    // Select the longest strip as the DMA target (ESP32-S3: only TX channel 3 supports DMA)
    int dma_target_index = -1;
    uint16_t dma_target_len = 0;
    for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
        const led_strip_config_entry_t &cfg = LED_STRIP_CONFIG[i];
        if (cfg.data_gpio != GPIO_NUM_NC && cfg.num_pixels > dma_target_len) {
            dma_target_len = cfg.num_pixels;
            dma_target_index = (int)i;
        }
    }

    // Helper to init strip, optionally with DMA
    auto init_strip = [&](size_t i, bool want_dma) {
        const led_strip_config_entry_t &cfg = LED_STRIP_CONFIG[i];
        if (cfg.data_gpio == GPIO_NUM_NC || cfg.num_pixels == 0) {
            ESP_LOGI(TAG, "Strip %d skipped (gpio=%d, pixels=%d)", (int)i, (int)cfg.data_gpio, (int)cfg.num_pixels);
            return; // Skip unconfigured entries
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
        // Use 10 MHz tick for stable timing across 4 TX channels
        rmt_config.resolution_hz = 10 * 1000 * 1000;
        if (want_dma) {
            // With DMA, mem_block_symbols controls internal DMA buffer size; larger reduces CPU load
            rmt_config.mem_block_symbols = 1024; // try 1024 symbols buffer
            rmt_config.flags.with_dma = true;
        } else {
            // Without DMA, must be even and >=48; keep one 48-symbol block per channel
            rmt_config.mem_block_symbols = 48;
            rmt_config.flags.with_dma = false;
        }

        led_strip_handle_t handle = nullptr;
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &handle);
        if (err != ESP_OK && want_dma) {
            // Fallback without DMA if channel 3 unavailable
            ESP_LOGW(TAG, "Strip %d: DMA allocation failed (%s), retrying without DMA", (int)i, esp_err_to_name(err));
            rmt_config.flags.with_dma = false;
            rmt_config.mem_block_symbols = 48;
            err = led_strip_new_rmt_device(&strip_config, &rmt_config, &handle);
        }
        if (err == ESP_OK && handle != nullptr) {
            led_strips[num_initialized_strips] = handle;
            strip_lengths[num_initialized_strips] = cfg.num_pixels;
            // Compute per-strip frame time correctly in microseconds:
            // frame_us = num_pixels * bits_per_led * 1.25us + ~80us reset
            // 1.25us = 1250ns => (1250 / 1000) us; do integer math with rounding
            uint32_t bits_per_led = (cfg.chipset == LED_CHIPSET_SK6812_RGBW) ? 32u : 24u;
            uint64_t us = ((uint64_t)cfg.num_pixels * (uint64_t)bits_per_led * 1250ull + 999ull) / 1000ull;
            us += 80ull;
            strip_frame_time_us[num_initialized_strips] = (uint32_t)us;
            strip_last_refresh_us[num_initialized_strips] = 0;
            // Allocate group cache
            uint16_t groups = (cfg.num_pixels + k_group_size - 1) / k_group_size;
            group_counts[num_initialized_strips] = groups;
            group_prev_vals[num_initialized_strips] = (uint8_t*)malloc(groups);
            if (group_prev_vals[num_initialized_strips]) {
                memset(group_prev_vals[num_initialized_strips], 0xFF, groups); // force first write
            }
            // Allocate pixel ramp cache
            pixel_vals[num_initialized_strips] = nullptr;
            if (cfg.num_pixels > 0) {
                pixel_vals[num_initialized_strips] = (uint8_t*)malloc(cfg.num_pixels);
                if (pixel_vals[num_initialized_strips]) {
                    memset(pixel_vals[num_initialized_strips], 0x00, cfg.num_pixels);
                }
            }
            num_initialized_strips++;
            led_strip_clear(handle);
            led_strip_refresh(handle);
            ESP_LOGI(TAG, "Strip %d initialized: gpio=%d, pixels=%d, chipset=%d, DMA=%s", (int)i, (int)cfg.data_gpio, (int)cfg.num_pixels, (int)cfg.chipset, want_dma ? "yes" : "no");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LED strip %d (gpio=%d, pixels=%d): %s", (int)i, (int)cfg.data_gpio, (int)cfg.num_pixels, esp_err_to_name(err));
        }
    };

    // Initialize DMA target first to maximize chance it maps to DMA-capable RMT channel 3
    if (dma_target_index >= 0) init_strip((size_t)dma_target_index, true);
    // Initialize the rest without DMA
    for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
        if ((int)i == dma_target_index) continue;
        init_strip(i, false);
    }

    ESP_LOGI(TAG, "Total initialized strips: %d", (int)num_initialized_strips);

    // No additional pre-task test; steady state logic will continuously refresh
    // Initialize ramp for the first strip only (cycle is per-strip)
    ramp_strip_idx = 0;
    ramp_pixel_idx = 0;
    ramp_completed_leds = 0;
    ramp_total_leds = (num_initialized_strips > 0) ? strip_lengths[0] : 0;
    g_ramp_phase = RAMP_PHASE_UP;
    fps_frames = 0;
    fps_last_log_us = esp_timer_get_time();

    // One-shot pulse test for channel on GPIO14 (strip typically indexed as 3)
    for (size_t i = 0; i < LED_STRIP_CONFIG_COUNT; ++i) {
        if (LED_STRIP_CONFIG[i].data_gpio == GPIO_NUM_14 && LED_STRIP_CONFIG[i].num_pixels > 0 && led_strips[i]) {
            // Ensure enable is high
            if (LED_STRIP_CONFIG[i].enable_gpio != GPIO_NUM_NC) {
                gpio_set_level(LED_STRIP_CONFIG[i].enable_gpio, 1);
            }
            // Brief white pulse on first pixel
            led_strip_set_pixel(led_strips[i], 0, 80, 80, 80);
            led_strip_refresh(led_strips[i]);
            vTaskDelay(pdMS_TO_TICKS(250));
            led_strip_clear(led_strips[i]);
            led_strip_refresh(led_strips[i]);
            ESP_LOGI(TAG, "GPIO14 (channel 4) pulse test complete");
            break;
        }
    }

    // Keep update cadence fixed per config to ensure smooth ramp and predictable FPS

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
    // Free caches
    for (size_t s = 0; s < num_initialized_strips; ++s) {
        if (group_prev_vals[s]) {
            free(group_prev_vals[s]);
            group_prev_vals[s] = nullptr;
            group_counts[s] = 0;
        }
        if (pixel_vals[s]) {
            free(pixel_vals[s]);
            pixel_vals[s] = nullptr;
        }
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