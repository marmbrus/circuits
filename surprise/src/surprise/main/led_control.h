#pragma once

#include "esp_system.h"
#include "wifi.h"
#include "led_strip.h"
#include "config.h"

#ifdef __cplusplus

// Base LED Behavior class
class LEDBehavior {
public:
    virtual ~LEDBehavior() = default;
    virtual void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) = 0;

protected:
    // Helper function to scale brightness according to LED_STRIP_NUM_BRIGHTNESS
    static uint8_t scale_brightness(uint8_t value) {
        return (value * LED_STRIP_NUM_BRIGHTNESS) / 100;
    }
};

// NoLights behavior
class NoLights : public LEDBehavior {
public:
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;
};

// FourColorLights behavior
class FourColorLights : public LEDBehavior {
public:
    FourColorLights();
    void setColor(int index, uint8_t red, uint8_t green, uint8_t blue);
    void clearColors();
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    uint8_t colors[4][3]; // Array to store RGB values for four colors
};

// ChristmasLights behavior
class ChristmasLights : public LEDBehavior {
public:
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;
};

// ChasingLights behavior
class ChasingLights : public LEDBehavior {
public:
    ChasingLights();
    void setColors(uint8_t color1_r, uint8_t color1_g, uint8_t color1_b,
                  uint8_t color2_r, uint8_t color2_g, uint8_t color2_b);
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    uint8_t color1[3];  // First color (RGB)
    uint8_t color2[3];  // Second color (RGB)
    bool phase;         // Alternating phase for the chase effect
};

// RainbowLights behavior
class RainbowLights : public LEDBehavior {
public:
    RainbowLights();
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    uint8_t hue;  // Current hue value (0-255)
    void hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b);
};

// RainbowChasing behavior
class RainbowChasing : public LEDBehavior {
public:
    RainbowChasing();
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    uint8_t baseHue;  // Starting hue that advances over time
    void hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b);
};

// FlashingLights behavior
class FlashingLights : public LEDBehavior {
public:
    FlashingLights();
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    bool isRed;         // true for red, false for blue
    uint8_t brightness; // Current brightness level (0-255)
};

// PulsingLights behavior
class PulsingLights : public LEDBehavior {
public:
    PulsingLights();
    void setColor(uint8_t red, uint8_t green, uint8_t blue);
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    uint8_t color[3];    // RGB values for the color to pulse
    uint8_t brightness;  // Current brightness level (0-255)
    bool increasing;     // Whether brightness is increasing or decreasing
};

#else
typedef struct LEDBehavior LEDBehavior;  // Opaque type for C code
#endif

#ifdef __cplusplus
extern "C" {
#endif

void led_control_init(void);
void led_control_set_state(SystemState state);
void led_control_clear(void);
void led_control_stop(void);
void led_control_set_button_led_status(int index, bool status);

// Add this helper that wraps led_strip_set_pixel
esp_err_t led_control_set_pixel(led_strip_handle_t led_strip, uint32_t index, uint8_t red, uint8_t green, uint8_t blue);

#ifdef __cplusplus
void led_control_set_behavior(LEDBehavior* behavior);
#endif

#ifdef __cplusplus
}
#endif