#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "config.h"

static const char* TAG = "LED_Control";

static led_strip_handle_t led_strip;
static SystemState current_state = WIFI_CONNECTING;
static uint8_t pulse_brightness = 0;
static bool pulse_increasing = true;

static void update_led_task(void* pvParameters) {
    while (1) {
        switch (current_state) {
            case WIFI_CONNECTING:
                // Pulse blue
                if (pulse_increasing) {
                    pulse_brightness = (pulse_brightness + 5) % 100;
                    if (pulse_brightness >= 95) pulse_increasing = false;
                } else {
                    pulse_brightness = (pulse_brightness - 5) % 100;
                    if (pulse_brightness <= 5) pulse_increasing = true;
                }
                led_strip_set_pixel(led_strip, 0, 0, 0, pulse_brightness);
                break;

            case WIFI_CONNECTED_MQTT_CONNECTING:
                // Pulse orange
                if (pulse_increasing) {
                    pulse_brightness = (pulse_brightness + 5) % 100;
                    if (pulse_brightness >= 95) pulse_increasing = false;
                } else {
                    pulse_brightness = (pulse_brightness - 5) % 100;
                    if (pulse_brightness <= 5) pulse_increasing = true;
                }
                led_strip_set_pixel(led_strip, 0, pulse_brightness, pulse_brightness/2, 0);
                break;

            case FULLY_CONNECTED:
                // Solid green
                led_strip_set_pixel(led_strip, 0, 0, 100, 0);
                break;

            case MQTT_ERROR_STATE:
                // Solid red
                led_strip_set_pixel(led_strip, 0, 100, 0, 0);
                break;
        }

        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_INTERVAL_MS));
    }
}

void led_control_init(void) {
    ESP_LOGI(TAG, "Initializing LED Control");

    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_NUM_PIXELS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
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
    led_strip_clear(led_strip);

    // Create LED update task
    xTaskCreate(update_led_task, "led_update_task",
                LED_UPDATE_TASK_STACK_SIZE, NULL, 5, NULL);

    ESP_LOGI(TAG, "LED Control initialized successfully");
}

void led_control_set_state(SystemState state) {
    current_state = state;
}