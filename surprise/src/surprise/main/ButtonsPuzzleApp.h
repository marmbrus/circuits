#pragma once

#include "application.h"
#include "esp_log.h"
#include "led_control.h"

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

    void checkPattern();
    void handleButtonPress(int buttonIndex, uint8_t red, uint8_t green, uint8_t blue);
    void resetState();

public:
    ButtonsPuzzleApp();
    void onButton1Pressed() override;
    void onButton2Pressed() override;
    void onButton3Pressed() override;
    void onButton4Pressed() override;
};