#include "led_control.h"

void NoLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        led_strip_set_pixel(led_strip, i, 0, 0, 0);
    }
}