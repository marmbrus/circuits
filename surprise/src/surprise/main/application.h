#pragma once

#include "button_event.h"

class Application {
public:
    virtual ~Application() = default;

    // Abstract methods for button handlers
    virtual void onButton1Pressed() = 0;
    virtual void onButton2Pressed() = 0;
    virtual void onButton3Pressed() = 0;
    virtual void onButton4Pressed() = 0;
    virtual void onMovementDetected() = 0;
    virtual void onOrientationChanged(ButtonEvent orientation) = 0;
};