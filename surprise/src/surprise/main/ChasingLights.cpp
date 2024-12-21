#include "led_control.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ChasingLights";

ChasingLights::ChasingLights() : phase(false) {
    // Initialize with default colors (off)
    color1[0] = color1[1] = color1[2] = 0;
    color2[0] = color2[1] = color2[2] = 0;
    ESP_LOGI(TAG, "ChasingLights initialized");
}

void ChasingLights::setColors(uint8_t color1_r, uint8_t color1_g, uint8_t color1_b,
                            uint8_t color2_r, uint8_t color2_g, uint8_t color2_b) {
    color1[0] = color1_r;
    color1[1] = color1_g;
    color1[2] = color1_b;

    color2[0] = color2_r;
    color2[1] = color2_g;
    color2[2] = color2_b;
    ESP_LOGI(TAG, "Colors set: (%d,%d,%d) and (%d,%d,%d)",
             color1_r, color1_g, color1_b, color2_r, color2_g, color2_b);
}

void ChasingLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    static uint64_t last_update = 0;
    uint64_t current_time = esp_timer_get_time(); // Get current time in microseconds

    // Update phase every 50ms (20 times per second)
    if (current_time - last_update >= 50000) {  // 50000 microseconds = 50ms
        phase = !phase;
        last_update = current_time;
    }

    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        bool is_even = (i % 2) == 0;
        uint8_t* active_color = ((is_even && phase) || (!is_even && !phase)) ? color1 : color2;

        led_strip_set_pixel(led_strip, i,
                          active_color[0],
                          active_color[1],
                          active_color[2]);
    }
}