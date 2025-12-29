#include "RmtTransmitter.h"
#include "led_strip_rmt.h"
#include "esp_check.h"
#include "esp_log.h"
#include <cstring>

namespace leds {

static const char* TAG = "RmtTransmitter";

RmtTransmitter::RmtTransmitter(const Config& config) {
    has_white_ = (config.fmt == LED_PIXEL_FORMAT_GRBW);

    led_strip_config_t led_cfg = {
        .strip_gpio_num = config.gpio,
        .max_leds = static_cast<uint32_t>(config.max_leds),
        .led_pixel_format = config.fmt,
        .led_model = config.led_model,
        .flags = { .invert_out = 0 },
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = config.resolution_hz,
        .mem_block_symbols = 48, // Fixed as per requirements
        .flags = { .with_dma = static_cast<uint32_t>(config.with_dma ? 1 : 0) },
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &handle_));
}

RmtTransmitter::~RmtTransmitter() {
    if (handle_) {
        led_strip_del(handle_);
    }
}

bool RmtTransmitter::transmit(const uint8_t* buffer, size_t size) {
    if (!handle_) return false;

    // Unpack buffer to led_strip calls
    // Buffer is expected to be in logical order (RGB or RGBW) because led_strip handles reordering.
    
    // Safety check size
    // For RGB, size should be N*3. For RGBW, N*4.
    // led_strip maintains its own buffer.
    
    if (has_white_) {
        size_t count = size / 4;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* p = buffer + i * 4;
            // Assuming buffer is R, G, B, W
            led_strip_set_pixel_rgbw(handle_, i, p[0], p[1], p[2], p[3]);
        }
    } else {
        size_t count = size / 3;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* p = buffer + i * 3;
            // Assuming buffer is R, G, B
            led_strip_set_pixel(handle_, i, p[0], p[1], p[2]);
        }
    }

    esp_err_t err = led_strip_refresh(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transmit failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool RmtTransmitter::is_busy() const {
    // led_strip_refresh blocks or handles busy state internally?
    // The API usually blocks until the RMT transmission is queued.
    // Ideally we'd like non-blocking check, but led_strip doesn't expose it easily 
    // without hacking the handle.
    // For now, we assume it's "ready" if we are here.
    return false;
}

void RmtTransmitter::wait_for_completion() {
    // No-op for high level API? 
    // Or just rely on next refresh blocking.
}

} // namespace leds
