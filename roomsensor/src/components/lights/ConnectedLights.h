#pragma once

#include "led_control.h"

class ConnectedLights : public LEDBehavior {
public:
    ConnectedLights();
    void reset();
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    bool animation_complete;
    uint64_t start_time;
    float getDistance(int x1, int y1, int x2, int y2);
}; 