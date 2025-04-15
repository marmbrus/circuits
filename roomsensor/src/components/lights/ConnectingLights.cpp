#include "led_control.h"
#include "ConnectingLights.h"

ConnectingLights::ConnectingLights() 
    : position(0), direction(true) {
}

void ConnectingLights::update(led_strip_handle_t led_strip, uint8_t pulse_brightness) {
    // Clear all LEDs first
    for (int i = 0; i < LED_STRIP_NUM_PIXELS; ++i) {
        led_control_set_pixel(led_strip, i, 0, 0, 0);
    }
    
    // Calculate row and column in the grid
    int row = position / LED_GRID_WIDTH;
    int col;
    
    // For odd rows, direction is right to left
    if (row % 2 == 1) {
        col = LED_GRID_WIDTH - 1 - (position % LED_GRID_WIDTH);
    } else {
        // For even rows, direction is left to right
        col = position % LED_GRID_WIDTH;
    }
    
    // Convert 2D position to LED index
    int ledIndex = row * LED_GRID_WIDTH + col;
    
    // Set the blue light at the current position
    led_control_set_pixel(led_strip, ledIndex, 0, 0, pulse_brightness);
    
    // Update position for next time
    if (direction) {
        position++;
        // Check if we've reached the end of the grid
        if (position >= LED_GRID_WIDTH * LED_GRID_HEIGHT) {
            direction = false;
            position = LED_GRID_WIDTH * LED_GRID_HEIGHT - 1;
        }
    } else {
        position--;
        // Check if we've reached the start of the grid
        if (position < 0) {
            direction = true;
            position = 0;
        }
    }
} 