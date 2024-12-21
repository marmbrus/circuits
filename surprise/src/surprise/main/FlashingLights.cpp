#include "led_control.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "FlashingLights";

FlashingLights::FlashingLights()
    : isRed(true), brightness(0) {
    ESP_LOGI(TAG, "FlashingLights initialized");
}

void FlashingLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    static uint64_t last_update = 0;
    uint64_t current_time = esp_timer_get_time();

    // Update every 2ms for smooth, fast ramping (500 times per second)
    if (current_time - last_update >= 2000) {
        brightness += 15;  // Increase quickly

        // When we reach full brightness, reset and switch colors
        if (brightness >= 255) {
            brightness = 0;
            isRed = !isRed;
        }

        last_update = current_time;
    }

    // Set all LEDs to current color and brightness
    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        if (isRed) {
            led_strip_set_pixel(led_strip, i, brightness, 0, 0);  // Red pulse up
        } else {
            led_strip_set_pixel(led_strip, i, 0, 0, brightness);  // Blue pulse up
        }
    }
}