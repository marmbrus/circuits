#pragma once

#include "led_control.h"
#include "config.h"

class ConnectingLights : public LEDBehavior {
public:
    ConnectingLights();
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    int position;
    bool direction; // true = forward, false = backward
}; 