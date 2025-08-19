#include "LEDStripRmt.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <memory>

namespace leds {

static const char* TAG = "LEDStripRmt";

std::unique_ptr<LEDStripRmt> LEDStripRmt::Create(const CreateParams& params) {
    std::unique_ptr<LEDStripRmt> s(new LEDStripRmt(params));
    if (!s->init_handle()) return nullptr;
    return s;
}

LEDStripRmt::LEDStripRmt(const CreateParams& params)
    : gpio_(params.gpio), enable_gpio_(params.enable_gpio), length_(params.length), rows_(params.rows),
      cols_(params.cols), chip_(params.chip), with_dma_(params.use_dma),
      rmt_resolution_hz_(params.rmt_resolution_hz), mem_block_symbols_(params.mem_block_symbols) {
    if (rows_ == 0) rows_ = 1;
    if (cols_ == 0) {
        // infer from length and rows
        cols_ = (rows_ == 0) ? params.length : (params.length + rows_ - 1) / rows_;
    }
    // Normalize length to rows * cols to ensure consistency
    length_ = rows_ * cols_;
    has_white_ = (chip_ == LEDChip::SK6812);
    size_t bytes_per = has_white_ ? 4 : 3;
    pixels_.assign(length_ * bytes_per, 0);
}

LEDStripRmt::~LEDStripRmt() { destroy_handle(); }

bool LEDStripRmt::init_handle() {
    // Configure enable pin if present
    if (enable_gpio_ >= 0) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << enable_gpio_;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        // default off until a pattern turns it on
        gpio_set_level((gpio_num_t)enable_gpio_, 0);
    }
    led_strip_config_t led_cfg = {
        .strip_gpio_num = gpio_,
        .max_leds = static_cast<uint32_t>(length_),
        .led_pixel_format = has_white_ ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB,
        .led_model = has_white_ ? LED_MODEL_SK6812 : LED_MODEL_WS2812,
        .flags = { .invert_out = 0 },
    };
    led_strip_rmt_config_t rmt_cfg = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0, // unused on 5.x
#else
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = rmt_resolution_hz_,
#endif
        .mem_block_symbols = mem_block_symbols_,
        .flags = { .with_dma = static_cast<uint32_t>(with_dma_) },
    };
    auto err = led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Created RMT strip: gpio=%d enable_gpio=%d len=%zu rows=%zu cols=%zu dma=%s mem=%zu res=%luHz",
             gpio_, enable_gpio_, length_, rows_, cols_, with_dma_ ? "true" : "false", mem_block_symbols_, (unsigned long)rmt_resolution_hz_);
    return true;
}

void LEDStripRmt::destroy_handle() {
    if (handle_) {
        led_strip_del(handle_);
        handle_ = nullptr;
    }
}

bool LEDStripRmt::set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index >= length_) return false;
    size_t bytes_per = has_white_ ? 4 : 3;
    size_t off = index * bytes_per;
    bool changed = false;
    // store RGBA order internally
    if (pixels_[off+0] != r) { pixels_[off+0] = r; changed = true; }
    if (pixels_[off+1] != g) { pixels_[off+1] = g; changed = true; }
    if (pixels_[off+2] != b) { pixels_[off+2] = b; changed = true; }
    if (has_white_ && pixels_[off+3] != w) { pixels_[off+3] = w; changed = true; }
    if (changed) dirty_ = true;
    return changed;
}

bool LEDStripRmt::get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const {
    if (index >= length_) return false;
    size_t bytes_per = has_white_ ? 4 : 3;
    size_t off = index * bytes_per;
    r = pixels_[off+0];
    g = pixels_[off+1];
    b = pixels_[off+2];
    w = has_white_ ? pixels_[off+3] : 0;
    return true;
}

void LEDStripRmt::clear() {
    bool any = false;
    for (auto& v : pixels_) { if (v != 0) { v = 0; any = true; } }
    if (any) dirty_ = true;
}

void LEDStripRmt::estimate_transmission_end(uint64_t now_us) {
    // conservative estimate: each LED ~30 bits (RGB) or 40 bits (RGBW). 1.25us per bit at 800kHz.
    size_t bits_per_led = has_white_ ? 32 : 24; // plus reset; we add margin below
    uint64_t strip_time_us = static_cast<uint64_t>(bits_per_led) * length_ * 1250ULL / 1000ULL; // 1.25us/bit
    uint64_t reset_us = 80; // typical >50us
    expected_done_us_ = now_us + strip_time_us + reset_us;
}

bool LEDStripRmt::flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us) {
    if (transmitting_) {
        // best-effort check if we passed expected end (in case event lost); don’t enqueue new
        if (now_us > expected_done_us_) transmitting_ = false;
        return false;
    }
    if (!dirty_ && (now_us - last_flush_us_) < max_quiescent_us) return false;

    // write pixels via led_strip API
    size_t bytes_per = has_white_ ? 4 : 3;
    for (size_t i = 0; i < length_; ++i) {
        size_t off = i * bytes_per;
        if (has_white_) {
            led_strip_set_pixel_rgbw(handle_, i, pixels_[off+0], pixels_[off+1], pixels_[off+2], pixels_[off+3]);
        } else {
            led_strip_set_pixel(handle_, i, pixels_[off+0], pixels_[off+1], pixels_[off+2]);
        }
    }
    auto err = led_strip_refresh(handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "refresh failed: %s", esp_err_to_name(err));
        return false;
    }
    transmitting_ = true;
    last_flush_us_ = now_us;
    dirty_ = false;
    estimate_transmission_end(now_us);
    return true;
}

void LEDStripRmt::on_transmit_complete(uint64_t now_us) {
    transmitting_ = false;
    if (now_us > expected_done_us_) expected_done_us_ = now_us;
}

void LEDStripRmt::set_power_enabled(bool on) {
    if (enable_gpio_ >= 0) {
        int lvl = on ? 1 : 0;
        esp_err_t rc = gpio_set_level((gpio_num_t)enable_gpio_, lvl);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "enable_gpio set_level failed gpio=%d err=%s", enable_gpio_, esp_err_to_name(rc));
        } else {
            ESP_LOGD(TAG, "enable_gpio gpio=%d -> %d", enable_gpio_, lvl);
        }
    }
}

} // namespace leds


