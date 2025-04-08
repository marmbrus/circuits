#include "led_control.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "RainbowChasing";

RainbowChasing::RainbowChasing() : baseHue(0) {
    ESP_LOGI(TAG, "RainbowChasing initialized");
}

void RainbowChasing::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    static uint64_t last_update = 0;
    uint64_t current_time = esp_timer_get_time();

    if (current_time - last_update >= 20000) {
        baseHue = (baseHue + 1) % 255;
        last_update = current_time;
    }

    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        uint8_t r, g, b;
        uint8_t hue = (baseHue + (i * 255 / LED_STRIP_NUM_PIXELS)) % 255;
        hsvToRgb(hue, 255, 255, &r, &g, &b);
        led_control_set_pixel(led_strip, i, r, g, b);
    }
}

void RainbowChasing::hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        *r = *g = *b = v;
        return;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}