#include "led_control.h"

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