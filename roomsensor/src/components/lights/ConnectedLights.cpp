#include "led_control.h"
#include "ConnectedLights.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>
#include <algorithm> // Add for std::max

static const char *TAG = "ConnectedLights";

ConnectedLights::ConnectedLights() 
    : animation_complete(false), start_time(0) {
}

void ConnectedLights::reset() {
    // Reset the animation state to start a new ripple
    animation_complete = false;
    start_time = 0;
    ESP_LOGD(TAG, "ConnectedLights animation reset");
}

float ConnectedLights::getDistance(int x1, int y1, int x2, int y2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

void ConnectedLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    // Initialize start time on first call
    if (start_time == 0) {
        start_time = esp_timer_get_time();
        ESP_LOGD(TAG, "Starting connected animation");
    }

    uint64_t current_time = esp_timer_get_time();
    uint64_t elapsed_time = current_time - start_time;
    
    // Animation lasts for 3 seconds (3,000,000 microseconds) - extended from 2s
    if (elapsed_time > 3000000 || animation_complete) {
        // Animation complete, turn off all LEDs
        if (!animation_complete) {
            ESP_LOGD(TAG, "Animation complete");
            animation_complete = true;
        }
        
        for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
            led_control_set_pixel(led_strip, i, 0, 0, 0);
        }
        return;
    }
    
    // Center of the grid
    float center_x = (LED_GRID_WIDTH - 1) / 2.0;
    float center_y = (LED_GRID_HEIGHT - 1) / 2.0;
    
    // Calculate the wave position based on elapsed time
    // Scale to complete in 3 seconds
    float max_distance = getDistance(0, 0, center_x, center_y);
    float wave_position = (elapsed_time / 3000000.0) * (max_distance * 2); 
    
    // For each LED, calculate its distance from center and set brightness
    for (int y = 0; y < LED_GRID_HEIGHT; y++) {
        for (int x = 0; x < LED_GRID_WIDTH; x++) {
            int pixel_index = y * LED_GRID_WIDTH + x;
            
            // Calculate distance from center
            float distance = getDistance(x, y, center_x, center_y);
            
            // Calculate intensity based on distance from wave position
            float wave_width = 2.5; // Width of the wave pulse - increased from 2.0
            float raw_intensity = 1.0f - fabs(distance - wave_position) / wave_width;
            float intensity = (raw_intensity > 0.0f) ? raw_intensity : 0.0f;
            
            // Apply smooth falloff
            intensity = intensity * intensity; // Square for smoother falloff
            
            // Set white color with calculated intensity - increased brightness
            uint8_t brightness = (uint8_t)(intensity * 100);
            led_control_set_pixel(led_strip, pixel_index, brightness, brightness, brightness);
        }
    }
} 