#include "ButtonsPuzzleApp.h"
#include "led_control.h"
#include "wifi.h"
#include "cJSON.h"

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
      currentColorIndex(0),
      lastOrientation(ButtonEvent::ORIENTATION_UNKNOWN) {
    currentBehavior = fourColorLights;
    led_control_set_behavior(currentBehavior);
    resetState();
}

void ButtonsPuzzleApp::handleButtonPress(int buttonIndex, uint8_t red, uint8_t green, uint8_t blue) {
    ESP_LOGI(TAG, "Button %d pressed at position %d", buttonIndex + 1, currentColorIndex);

    // Create and publish MQTT message for button press
    cJSON *button_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(button_json, "index", buttonIndex);

    // Determine color name based on RGB values
    const char* colorName;
    if (red == 0 && green == 255 && blue == 0) {
        colorName = "green";
    } else if (red == 0 && green == 0 && blue == 255) {
        colorName = "blue";
    } else if (red == 255 && green == 0 && blue == 0) {
        colorName = "red";
    } else if (red == 255 && green == 255 && blue == 0) {
        colorName = "yellow";
    } else {
        colorName = "unknown";
    }

    cJSON_AddStringToObject(button_json, "color", colorName);

    char *button_string = cJSON_Print(button_json);
    publish_to_topic("buttons", button_string);

    cJSON_free(button_string);
    cJSON_Delete(button_json);

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
    // First, publish the complete pattern
    cJSON *pattern_json = cJSON_CreateObject();
    cJSON *indices_array = cJSON_CreateArray();
    cJSON *colors_array = cJSON_CreateArray();

    // Add each button press to the arrays
    for (int i = 0; i < 4; i++) {
        // Add index to indices array
        cJSON_AddItemToArray(indices_array, cJSON_CreateNumber(buttonPresses[i]));

        // Determine color name based on button index and add to colors array
        const char* colorName;
        switch (buttonPresses[i]) {
            case 0: colorName = "green"; break;
            case 1: colorName = "blue"; break;
            case 2: colorName = "red"; break;
            case 3: colorName = "yellow"; break;
            default: colorName = "unknown"; break;
        }
        cJSON_AddItemToArray(colors_array, cJSON_CreateString(colorName));
    }

    cJSON_AddItemToObject(pattern_json, "indices", indices_array);
    cJSON_AddItemToObject(pattern_json, "colors", colors_array);

    char *pattern_string = cJSON_Print(pattern_json);
    publish_to_topic("patterns", pattern_string);

    cJSON_free(pattern_string);
    cJSON_Delete(pattern_json);

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

void ButtonsPuzzleApp::onMovementDetected() {
    ESP_LOGI(TAG, "Movement detected in ButtonsPuzzleApp");
    // Add your movement detection handling logic here
}

void ButtonsPuzzleApp::onOrientationChanged(ButtonEvent orientation) {
    const char* orientation_str;
    switch (orientation) {
        case ButtonEvent::ORIENTATION_UP:     orientation_str = "Up"; break;
        case ButtonEvent::ORIENTATION_DOWN:   orientation_str = "Down"; break;
        case ButtonEvent::ORIENTATION_LEFT:   orientation_str = "Left"; break;
        case ButtonEvent::ORIENTATION_RIGHT:  orientation_str = "Right"; break;
        case ButtonEvent::ORIENTATION_FRONT:  orientation_str = "Front"; break;
        case ButtonEvent::ORIENTATION_BACK:   orientation_str = "Back"; break;
        case ButtonEvent::ORIENTATION_UNKNOWN: orientation_str = "Unknown"; break;
        default: orientation_str = "Invalid"; break;
    }

    ESP_LOGI(TAG, "Orientation changed to: %s", orientation_str);
    lastOrientation = orientation;

    // // You can add special behaviors based on orientation here
    // // For example:
    // switch (orientation) {
    //     case ButtonEvent::ORIENTATION_UP:
    //         pulsingLights->setColor(0, 255, 0);  // Green when up
    //         currentBehavior = pulsingLights;
    //         break;
    //     case ButtonEvent::ORIENTATION_DOWN:
    //         pulsingLights->setColor(255, 0, 0);  // Red when down
    //         currentBehavior = pulsingLights;
    //         break;
    //     case ButtonEvent::ORIENTATION_UNKNOWN:
    //         // Return to previous behavior or set a default one
    //         currentBehavior = fourColorLights;
    //         break;
    //     default:
    //         // Optional: handle other orientations
    //         break;
    // }

    // led_control_set_behavior(currentBehavior);
}