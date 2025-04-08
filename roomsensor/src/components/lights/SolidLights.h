#pragma once
#include "led_control.h"

class SolidLights : public LEDBehavior {
private:
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

public:
    SolidLights() = default;
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;
};