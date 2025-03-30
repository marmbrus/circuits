#ifndef __LED_CONTROL_H__
#define __LED_CONTROL_H__

#include <esp_err.h>

// LED GPIO definitions
#define LED_STRIP_GPIO       38
#define LED_STRIP_NUM_PIXELS 1

// Number of button LEDs and their pins
#define NUM_BUTTON_LEDS     1
static const uint8_t button_led_pins[NUM_BUTTON_LEDS] = {LED_STRIP_GPIO};

/**
 * @brief Initialize the LED control system
 *
 * @return esp_err_t ESP_OK if successful
 */
esp_err_t led_control_init(void);

/**
 * @brief Set LED strip pixel color
 *
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 * @return esp_err_t ESP_OK if successful
 */
esp_err_t led_set_color(uint8_t red, uint8_t green, uint8_t blue);

#endif // __LED_CONTROL_H__