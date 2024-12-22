#pragma once

#include "application.h"
#include "esp_log.h"
#include "led_control.h"

// Forward declare IOManager
class IOManager;

class ButtonsPuzzleApp : public Application {
private:
    static const char* TAG;
    FourColorLights* fourColorLights;
    ChristmasLights* christmasLights;
    ChasingLights* chasingLights;
    RainbowLights* rainbowLights;
    RainbowChasing* rainbowChasing;
    FlashingLights* flashingLights;
    PulsingLights* pulsingLights;
    LEDBehavior* currentBehavior;
    int currentColorIndex;
    int buttonPresses[4];
    int patternIndex;
    ButtonEvent lastOrientation;
    IOManager* ioManager;

    void checkPattern();
    void handleButtonPress(int buttonIndex, uint8_t red, uint8_t green, uint8_t blue);
    void resetState();

public:
    ButtonsPuzzleApp();
    void setIOManager(IOManager* manager) override { ioManager = manager; }
    void onButton1Pressed() override;
    void onButton2Pressed() override;
    void onButton3Pressed() override;
    void onButton4Pressed() override;
    void onMovementDetected() override;
    void onOrientationChanged(ButtonEvent orientation) override;
};