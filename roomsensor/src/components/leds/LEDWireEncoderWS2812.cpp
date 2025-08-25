#include "LEDWireEncoderWS2812.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"
#include "driver/gpio.h"

namespace leds { namespace internal {

static const char* TAG_WS = "WireEncoderWS2812";

WireEncoderWS2812::WireEncoderWS2812(int gpio, int enable_gpio, bool with_dma, uint32_t rmt_resolution_hz, size_t mem_block_symbols, size_t max_leds)
    : gpio_(gpio), enable_gpio_(enable_gpio), with_dma_(with_dma), rmt_resolution_hz_(rmt_resolution_hz), mem_block_symbols_(mem_block_symbols), max_leds_(max_leds) {
    // No enable pin handling here; centralized in LEDStripSurfaceAdapter
    led_strip_config_t led_cfg = {
        .strip_gpio_num = gpio_,
        .max_leds = static_cast<uint32_t>(max_leds_ > 0 ? max_leds_ : 1u),
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = 0 },
    };
    led_strip_rmt_config_t rmt_cfg = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0,
#else
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = rmt_resolution_hz_,
#endif
        .mem_block_symbols = mem_block_symbols_,
        .flags = { .with_dma = static_cast<uint32_t>(with_dma_) },
    };
    auto err = led_strip_new_rmt_device(&led_cfg, &rmt_cfg, reinterpret_cast<led_strip_handle_t*>(&handle_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        handle_ = nullptr;
    }
}

WireEncoderWS2812::~WireEncoderWS2812() {
    if (handle_) {
        auto h = reinterpret_cast<led_strip_handle_t>(handle_);
        led_strip_del(h);
        handle_ = nullptr;
    }
}

bool WireEncoderWS2812::transmit_frame(const uint8_t* frame_bytes, size_t frame_size_bytes) {
    if (!handle_ || !frame_bytes || frame_size_bytes % 3 != 0) return false;
    size_t n = frame_size_bytes / 3;
    if (n > max_leds_ && max_leds_ != 0) n = max_leds_;
    auto h = reinterpret_cast<led_strip_handle_t>(handle_);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = frame_bytes + i * 3;
        // API expects (r,g,b)
        led_strip_set_pixel(h, i, p[0], p[1], p[2]);
    }
    auto err = led_strip_refresh(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_WS, "refresh failed: %s", esp_err_to_name(err));
        return false;
    }
    busy_ = false; // no event hookup; rely on caller timing
    return true;
}

bool WireEncoderWS2812::is_busy() const {
    return busy_;
}

} } // namespace leds::internal


