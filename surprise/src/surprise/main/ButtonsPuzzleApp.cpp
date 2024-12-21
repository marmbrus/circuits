#include "ButtonsPuzzleApp.h"
#include "led_control.h"

// Declare global variables for light behaviors
FourColorLights globalFourColorLights;
ChristmasLights globalChristmasLights;
ChasingLights globalChasingLights;
RainbowLights globalRainbowLights;
RainbowChasing globalRainbowChasing;
FlashingLights globalFlashingLights;
PulsingLights globalPulsingLights;

const char* ButtonsPuzzleApp::TAG = "ButtonsPuzzleApp";

ButtonsPuzzleApp::ButtonsPuzzleApp()
    : fourColorLights(&globalFourColorLights),
      christmasLights(&globalChristmasLights),
      chasingLights(&globalChasingLights),
      rainbowLights(&globalRainbowLights),
      rainbowChasing(&globalRainbowChasing),
      flashingLights(&globalFlashingLights),
      pulsingLights(&globalPulsingLights),
      currentColorIndex(0) {
    currentBehavior = fourColorLights;
    led_control_set_behavior(currentBehavior);
    resetState();
}

void ButtonsPuzzleApp::handleButtonPress(int buttonIndex, uint8_t red, uint8_t green, uint8_t blue) {
    ESP_LOGI(TAG, "Button %d pressed at position %d", buttonIndex + 1, currentColorIndex);

    // If this is the first press after a completed sequence, clear everything
    if (buttonPresses[3] != -1) {
        ESP_LOGI(TAG, "Starting new sequence");
        resetState();
    }

    // Record the button press and set the LED
    buttonPresses[currentColorIndex] = buttonIndex;
    fourColorLights->setColor(currentColorIndex, red, green, blue);

    // Make sure we're using the FourColorLights behavior
    currentBehavior = fourColorLights;
    led_control_set_behavior(currentBehavior);

    // Move to next position
    currentColorIndex = (currentColorIndex + 1) % 4;

    // If we just filled the fourth position, check the pattern
    if (currentColorIndex == 0) {
        checkPattern();
    }
}

void ButtonsPuzzleApp::onButton1Pressed() {
    handleButtonPress(0, 0, 255, 0); // Green
}

void ButtonsPuzzleApp::onButton2Pressed() {
    handleButtonPress(1, 0, 0, 255); // Blue
}

void ButtonsPuzzleApp::onButton3Pressed() {
    handleButtonPress(2, 255, 0, 0); // Red
}

void ButtonsPuzzleApp::onButton4Pressed() {
    handleButtonPress(3, 255, 255, 0); // Yellow
}

void ButtonsPuzzleApp::checkPattern() {
    // Check for the Red, Green, Red, Green pattern (original pattern)
    if (buttonPresses[0] == 2 && buttonPresses[1] == 0 &&
        buttonPresses[2] == 2 && buttonPresses[3] == 0) {
        ESP_LOGI(TAG, "Pattern recognized: Red, Green, Red, Green");
        currentBehavior = christmasLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for the Green, Red, Green, Red pattern (new pattern)
    else if (buttonPresses[0] == 0 && buttonPresses[1] == 2 &&
             buttonPresses[2] == 0 && buttonPresses[3] == 2) {
        ESP_LOGI(TAG, "Pattern recognized: Green, Red, Green, Red");
        chasingLights->setColors(
            0, 100, 0,    // Green
            100, 0, 0     // Red
        );
        currentBehavior = chasingLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for Red, Yellow, Green, Blue pattern
    else if (buttonPresses[0] == 2 && buttonPresses[1] == 3 &&
             buttonPresses[2] == 0 && buttonPresses[3] == 1) {
        ESP_LOGI(TAG, "Pattern recognized: Red, Yellow, Green, Blue");
        currentBehavior = rainbowLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for Blue, Green, Yellow, Red pattern
    else if (buttonPresses[0] == 1 && buttonPresses[1] == 0 &&
             buttonPresses[2] == 3 && buttonPresses[3] == 2) {
        ESP_LOGI(TAG, "Pattern recognized: Blue, Green, Yellow, Red");
        currentBehavior = rainbowChasing;
        led_control_set_behavior(currentBehavior);
    }
    // Check for Red, Blue, Red, Blue pattern
    else if (buttonPresses[0] == 2 && buttonPresses[1] == 1 &&
             buttonPresses[2] == 2 && buttonPresses[3] == 1) {
        ESP_LOGI(TAG, "Pattern recognized: Red, Blue, Red, Blue");
        currentBehavior = flashingLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for all Red pattern
    else if (buttonPresses[0] == 2 && buttonPresses[1] == 2 &&
             buttonPresses[2] == 2 && buttonPresses[3] == 2) {
        ESP_LOGI(TAG, "Pattern recognized: All Red");
        pulsingLights->setColor(255, 0, 0);  // Red
        currentBehavior = pulsingLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for all Green pattern
    else if (buttonPresses[0] == 0 && buttonPresses[1] == 0 &&
             buttonPresses[2] == 0 && buttonPresses[3] == 0) {
        ESP_LOGI(TAG, "Pattern recognized: All Green");
        pulsingLights->setColor(0, 255, 0);  // Green
        currentBehavior = pulsingLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for all Blue pattern
    else if (buttonPresses[0] == 1 && buttonPresses[1] == 1 &&
             buttonPresses[2] == 1 && buttonPresses[3] == 1) {
        ESP_LOGI(TAG, "Pattern recognized: All Blue");
        pulsingLights->setColor(0, 0, 255);  // Blue
        currentBehavior = pulsingLights;
        led_control_set_behavior(currentBehavior);
    }
    // Check for all Yellow pattern
    else if (buttonPresses[0] == 3 && buttonPresses[1] == 3 &&
             buttonPresses[2] == 3 && buttonPresses[3] == 3) {
        ESP_LOGI(TAG, "Pattern recognized: All Yellow");
        pulsingLights->setColor(255, 255, 0);  // Yellow
        currentBehavior = pulsingLights;
        led_control_set_behavior(currentBehavior);
    }
    else {
        ESP_LOGI(TAG, "Pattern not recognized, showing entered pattern");
        currentBehavior = fourColorLights;
        led_control_set_behavior(currentBehavior);
    }
}

void ButtonsPuzzleApp::resetState() {
    currentColorIndex = 0;
    for (int i = 0; i < 4; ++i) {
        buttonPresses[i] = -1;
    }
    fourColorLights->clearColors();
    currentBehavior = fourColorLights;
    led_control_set_behavior(currentBehavior);
}