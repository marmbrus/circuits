#include "led_control.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <led_strip.h>

static const char *TAG = "led_control";
static led_strip_handle_t led_strip;
static bool button_led_status[NUM_BUTTON_LEDS] = {false};

esp_err_t led_control_init(void)
{
    ESP_LOGI(TAG, "Initializing LED Control");

    // Initialize GPIOs for the button LEDs
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    // Initialize all button LEDs at once with a single bitmask
    uint64_t button_led_mask = 0;
    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        button_led_mask |= (1ULL << button_led_pins[i]);
    }
    io_conf.pin_bit_mask = button_led_mask;
    gpio_config(&io_conf);

    // Initialize all button LEDs to low (off)
    for (int i = 0; i < NUM_BUTTON_LEDS; ++i) {
        gpio_set_level(button_led_pins[i], 0);  // Set low (off) initially
        button_led_status[i] = false;
    }

    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_NUM_PIXELS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    // RMT configuration
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        }
    };

    // Create LED strip
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    // Clear LED strip initially
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    return ESP_OK;
}

esp_err_t led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    return ESP_OK;
}