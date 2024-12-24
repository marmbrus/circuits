#include "SolidLights.h"
#include "config.h"
#include "esp_log.h"

static const char *TAG = "SolidLights";

void SolidLights::setColor(uint8_t r, uint8_t g, uint8_t b) {
    red = r;
    green = g;
    blue = b;
    ESP_LOGI(TAG, "Color set to: R=%d, G=%d, B=%d", r, g, b);
}

void SolidLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        led_control_set_pixel(led_strip, i, red, green, blue);
    }
}