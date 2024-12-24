#include "led_control.h"

void ChristmasLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    for (int i = 3; i < LED_STRIP_NUM_PIXELS; ++i) {
        if (i % 2 == 0) {
            led_control_set_pixel(led_strip, i, pulse_brightness, 0, 0);
        } else {
            led_control_set_pixel(led_strip, i, 0, pulse_brightness, 0);
        }
    }
}