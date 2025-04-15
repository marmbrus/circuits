#include "led_control.h"
#include "DisconnectedLights.h"
#include "esp_log.h"

static const char* TAG = "DisconnectedLights";

DisconnectedLights::DisconnectedLights() 
    : current_led(0), fade_progress(0), disconnect_type(WIFI_DISCONNECT) {
    ESP_LOGI(TAG, "DisconnectedLights initialized");
}

void DisconnectedLights::setDisconnectType(DisconnectType type) {
    // Only log when the type actually changes
    if (disconnect_type != type) {
        disconnect_type = type;
        ESP_LOGI(TAG, "Disconnect type changed to: %s", 
                (type == WIFI_DISCONNECT) ? "WIFI_DISCONNECT" : "MQTT_DISCONNECT");
    }
}

void DisconnectedLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    // Log much less frequently - only once every ~5 seconds (100 * 50ms = 5s)
    static int call_count = 0;
    if (call_count == 0 || call_count % 100 == 0) {
        ESP_LOGD(TAG, "DisconnectedLights update called (%d), LED: %d, progress: %lu, type: %s", 
                 call_count, current_led, fade_progress, 
                 (disconnect_type == WIFI_DISCONNECT) ? "WIFI" : "MQTT");
    }
    call_count++;
    
    // Clear all LEDs first (not needed, but makes code safer if behavior switched)
    for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
        led_control_set_pixel(led_strip, i, 0, 0, 0);
    }
    
    // Increment our fade progress based on the LED_UPDATE_INTERVAL_MS
    fade_progress += LED_UPDATE_INTERVAL_MS;
    
    // For each LED that's currently being lit
    for (int i = 0; i <= current_led; ++i) {
        // The earlier LEDs are fully lit at MAX_BRIGHTNESS_PCT
        if (i < current_led) {
            // Set color based on disconnect type
            uint8_t brightness = scale_brightness(MAX_BRIGHTNESS_PCT);
            if (disconnect_type == WIFI_DISCONNECT) {
                // Blue for WiFi disconnect
                led_control_set_pixel(led_strip, i, 0, 0, brightness);
            } else {
                // Orange for MQTT disconnect (red + green mix)
                led_control_set_pixel(led_strip, i, brightness, brightness/2, 0);
            }
        } 
        // The current LED is being faded in
        else if (i == current_led) {
            // Calculate the fade percentage (0-100)
            float fade_percent = (float)fade_progress / FADE_TIME_MS * 100.0f;
            
            // Cap at the maximum brightness
            if (fade_percent > MAX_BRIGHTNESS_PCT) {
                fade_percent = MAX_BRIGHTNESS_PCT;
            }
            
            uint8_t brightness = scale_brightness((uint8_t)fade_percent);
            
            if (disconnect_type == WIFI_DISCONNECT) {
                // Blue for WiFi disconnect
                led_control_set_pixel(led_strip, i, 0, 0, brightness);
            } else {
                // Orange for MQTT disconnect (red + green mix)
                led_control_set_pixel(led_strip, i, brightness, brightness/2, 0);
            }
            
            // If this LED has reached full fade-in, move to the next one
            if (fade_progress >= FADE_TIME_MS) {
                ESP_LOGD(TAG, "Moving to next LED: %d -> %d", current_led, current_led + 1);
                current_led++;
                fade_progress = 0;
                
                // Reset to first LED if we've done all of them
                if (current_led >= LED_STRIP_NUM_PIXELS) {
                    ESP_LOGI(TAG, "Completed full LED sequence, resetting to LED 0");
                    current_led = 0;
                }
            }
        }
    }
} 