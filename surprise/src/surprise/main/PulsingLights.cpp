#include "led_control.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "PulsingLights";

PulsingLights::PulsingLights() : brightness(0), increasing(true) {
    color[0] = color[1] = color[2] = 0;
    ESP_LOGI(TAG, "PulsingLights initialized");
}

void PulsingLights::setColor(uint8_t red, uint8_t green, uint8_t blue) {
    color[0] = red;
    color[1] = green;
    color[2] = blue;
    ESP_LOGI(TAG, "Color set to (%d,%d,%d)", red, green, blue);
}

void PulsingLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    static uint64_t last_update = 0;
    uint64_t current_time = esp_timer_get_time();

    if (current_time - last_update >= 2000) {
        if (increasing) {
            brightness += 15;
            if (brightness >= 255) {
                brightness = 255;
                increasing = false;
            }
        } else {
            brightness -= 15;
            if (brightness <= 15) {
                brightness = 0;
                increasing = true;
            }
        }
        last_update = current_time;
    }

    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        led_control_set_pixel(led_strip, i,
            (color[0] * brightness) / 255,
            (color[1] * brightness) / 255,
            (color[2] * brightness) / 255);
    }
}