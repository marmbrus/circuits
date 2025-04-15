// UNUSED - LEDBehavior is now defined in led_control.h
// Keeping this file for reference only

#pragma once

#include "led_strip.h"

class LEDBehavior {
public:
    virtual ~LEDBehavior() = default;
    virtual void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) = 0;
};