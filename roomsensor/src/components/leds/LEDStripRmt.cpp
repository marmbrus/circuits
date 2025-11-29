#include "LEDStripRmt.h"
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
    has_white_ = (chip_ == config::LEDConfig::Chip::SK6812 || chip_ == config::LEDConfig::Chip::WS2814);
    size_t bytes_per = (chip_ == config::LEDConfig::Chip::FLIPDOT) ? 1 : (has_white_ ? 4 : 3);
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
    // Select encoder model per chip to ensure correct timing on the wire.
    led_model_t led_model = LED_MODEL_WS2812;
    bytes_per_pixel_ = 3;
    switch (chip_) {
        case config::LEDConfig::Chip::WS2812:
            led_model = LED_MODEL_WS2812;
            break;
        case config::LEDConfig::Chip::SK6812:
            // SK6812 RGBW strips use GRBW ordering
            led_model = LED_MODEL_SK6812;
            bytes_per_pixel_ = 4;
            break;
        case config::LEDConfig::Chip::WS2814:
            // Use WS2812-like timings; we'll remap channels in software to achieve WRGB on the wire
            led_model = LED_MODEL_WS2812; // timings compatible
            bytes_per_pixel_ = 4;
            break;
        case config::LEDConfig::Chip::FLIPDOT:
            // Use WS2812 timings; we will map logical on/off to a single color channel
            led_model = LED_MODEL_WS2812;
            break;
        default:
            ESP_LOGE(TAG, "Unknown LED chip enum in RMT init");
            return false;
    }

    // For FLIPDOT, three logical dots are packed into one physical WS2812 LED.
    size_t physical_leds = (chip_ == config::LEDConfig::Chip::FLIPDOT)
                               ? ((length_ + 2) / 3)
                               : length_;

    // Configure RMT TX channel
    rmt_tx_channel_config_t rmt_chan_config = {};
    rmt_chan_config.gpio_num = static_cast<gpio_num_t>(gpio_);
    rmt_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_chan_config.mem_block_symbols = mem_block_symbols_;   // carefully tuned; caller chooses per strip
    rmt_chan_config.resolution_hz = rmt_resolution_hz_;
    rmt_chan_config.trans_queue_depth = 1;                    // we only ever keep a single in-flight frame
    rmt_chan_config.intr_priority = 0;                        // default priority
    rmt_chan_config.flags.with_dma = with_dma_ ? 1u : 0u;
    rmt_chan_config.flags.invert_out = 0;
    esp_err_t err = rmt_new_tx_channel(&rmt_chan_config, &rmt_chan_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        rmt_chan_ = nullptr;
        return false;
    }

    led_strip_encoder_config_t encoder_conf = {
        .resolution = rmt_resolution_hz_,
        .led_model = led_model,
    };
    err = rmt_new_led_strip_encoder(&encoder_conf, &strip_encoder_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_led_strip_encoder failed: %s", esp_err_to_name(err));
        rmt_del_channel(rmt_chan_);
        rmt_chan_ = nullptr;
        strip_encoder_ = nullptr;
        return false;
    }

    // Register TX-done callback to drive non-blocking completion.
    rmt_tx_event_callbacks_t cbs = {};
    cbs.on_trans_done = [](rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void* user) noexcept -> bool {
        auto* self = static_cast<LEDStripRmt*>(user);
        uint64_t now = esp_timer_get_time();
        self->on_transmit_complete(now);
        // No need to yield from ISR; LED updates are paced by the manager task
        return false;
    };
    err = rmt_tx_register_event_callbacks(rmt_chan_, &cbs, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_register_event_callbacks failed: %s", esp_err_to_name(err));
        rmt_del_encoder(strip_encoder_);
        rmt_del_channel(rmt_chan_);
        strip_encoder_ = nullptr;
        rmt_chan_ = nullptr;
        return false;
    }

    err = rmt_enable(rmt_chan_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        rmt_del_encoder(strip_encoder_);
        rmt_del_channel(rmt_chan_);
        strip_encoder_ = nullptr;
        rmt_chan_ = nullptr;
        return false;
    }

    // Pre-size TX buffer for the configured number of physical LEDs.
    tx_buf_.assign(physical_leds * bytes_per_pixel_, 0);

    ESP_LOGI(TAG,
             "Created RMT strip: gpio=%d enable_gpio=%d len=%zu rows=%zu cols=%zu dma=%s mem=%zu res=%luHz "
             "phys_leds=%zu bytes_per_pixel=%u",
             gpio_, enable_gpio_, length_, rows_, cols_, with_dma_ ? "true" : "false", mem_block_symbols_,
             (unsigned long)rmt_resolution_hz_, physical_leds, static_cast<unsigned>(bytes_per_pixel_));
    return true;
}

void LEDStripRmt::destroy_handle() {
    if (rmt_chan_) {
        // Best-effort disable before deleting; ignore errors in teardown.
        (void)rmt_disable(rmt_chan_);
        rmt_del_channel(rmt_chan_);
        rmt_chan_ = nullptr;
    }
    if (strip_encoder_) {
        rmt_del_encoder(strip_encoder_);
        strip_encoder_ = nullptr;
    }
}

bool LEDStripRmt::set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index >= length_) return false;
    size_t bytes_per = (chip_ == config::LEDConfig::Chip::FLIPDOT) ? 1 : (has_white_ ? 4 : 3);
    size_t off = index * bytes_per;
    bool changed = false;
    // For FLIPDOT, store a single byte (0 or 255) per logical dot
    if (chip_ == config::LEDConfig::Chip::FLIPDOT) {
        uint8_t on = ((r | g | b | w) != 0) ? 255 : 0;
        if (pixels_[off] != on) { pixels_[off] = on; changed = true; }
    } else {
        // store RGBA order internally
        if (pixels_[off+0] != r) { pixels_[off+0] = r; changed = true; }
        if (pixels_[off+1] != g) { pixels_[off+1] = g; changed = true; }
        if (pixels_[off+2] != b) { pixels_[off+2] = b; changed = true; }
        if (has_white_ && pixels_[off+3] != w) { pixels_[off+3] = w; changed = true; }
    }
    if (changed) dirty_ = true;
    return changed;
}

bool LEDStripRmt::get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const {
    if (index >= length_) return false;
    size_t bytes_per = (chip_ == config::LEDConfig::Chip::FLIPDOT) ? 1 : (has_white_ ? 4 : 3);
    size_t off = index * bytes_per;
    if (chip_ == config::LEDConfig::Chip::FLIPDOT) {
        uint8_t on = pixels_[off];
        r = on; g = on; b = on; w = 0;
    } else {
        r = pixels_[off+0];
        g = pixels_[off+1];
        b = pixels_[off+2];
        w = has_white_ ? pixels_[off+3] : 0;
    }
    return true;
}

void LEDStripRmt::clear() {
    bool any = false;
    for (auto& v : pixels_) { if (v != 0) { v = 0; any = true; } }
    if (any) dirty_ = true;
}

void LEDStripRmt::estimate_transmission_end(uint64_t now_us) {
    // conservative estimate: each physical LED ~30 bits (RGB) or 40 bits (RGBW). 1.25us per bit at 800kHz.
    size_t bits_per_led = has_white_ ? 32 : 24; // plus reset; we add margin below
    size_t physical_leds = (chip_ == config::LEDConfig::Chip::FLIPDOT) ? ((length_ + 2) / 3) : length_;
    uint64_t strip_time_us = static_cast<uint64_t>(bits_per_led) * physical_leds * 1250ULL / 1000ULL; // 1.25us/bit
    uint64_t reset_us = 80; // typical >50us
    uint64_t expected_duration = strip_time_us + reset_us;
    expected_done_us_ = now_us + expected_duration;

    stats_.expected_duration_us_last = static_cast<uint32_t>(expected_duration);
    if (stats_.expected_duration_us_last > stats_.expected_duration_us_max_window) {
        stats_.expected_duration_us_max_window = stats_.expected_duration_us_last;
    }
}

bool LEDStripRmt::flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us) {
    if (!rmt_chan_ || !strip_encoder_) return false;
    if (transmitting_) {
        // A frame is still in-flight; let the TX-done callback clear this.
        return false;
    }
    if (!dirty_ && (now_us - last_flush_us_) < max_quiescent_us) return false;

    // Build GRB/GRBW staging buffer from the logical RGBA pixel shadow.
    const bool is_flipdot = (chip_ == config::LEDConfig::Chip::FLIPDOT);
    const size_t logical_bytes_per = is_flipdot ? 1 : (has_white_ ? 4 : 3);

    if (is_flipdot) {
        // Pack three logical dots into one physical WS2812 pixel's RGB channels.
        const size_t physical_leds = (length_ + 2) / 3;
        const size_t required = physical_leds * bytes_per_pixel_;
        if (tx_buf_.size() < required) tx_buf_.assign(required, 0);
        for (size_t pi = 0; pi < physical_leds; ++pi) {
            size_t li0 = 3 * pi + 0;
            size_t li1 = 3 * pi + 1;
            size_t li2 = 3 * pi + 2;
            uint8_t on0 = (li0 < length_) ? pixels_[li0 * logical_bytes_per] : 0;
            uint8_t on1 = (li1 < length_) ? pixels_[li1 * logical_bytes_per] : 0;
            uint8_t on2 = (li2 < length_) ? pixels_[li2 * logical_bytes_per] : 0;
            // Channel order to encoder is GRB.
            size_t off = pi * bytes_per_pixel_;
            tx_buf_[off + 0] = on0; // G
            tx_buf_[off + 1] = on1; // R
            tx_buf_[off + 2] = on2; // B
            if (bytes_per_pixel_ > 3) {
                tx_buf_[off + 3] = 0;
            }
        }
    } else {
        const size_t physical_leds = length_;
        const size_t required = physical_leds * bytes_per_pixel_;
        if (tx_buf_.size() < required) tx_buf_.assign(required, 0);
        for (size_t i = 0; i < length_; ++i) {
            size_t src_off = i * logical_bytes_per;
            uint8_t r = pixels_[src_off + 0];
            uint8_t g = (logical_bytes_per > 1) ? pixels_[src_off + 1] : 0;
            uint8_t b = (logical_bytes_per > 2) ? pixels_[src_off + 2] : 0;
            uint8_t w = (has_white_ && logical_bytes_per > 3) ? pixels_[src_off + 3] : 0;

            size_t dst_off = i * bytes_per_pixel_;
            if (has_white_) {
                if (chip_ == config::LEDConfig::Chip::WS2814) {
                    // Driver expects GRBW on the wire but WS2814 physical ordering is WRGB.
                    uint8_t desired_r = r;
                    uint8_t desired_g = g;
                    uint8_t desired_b = b;
                    uint8_t desired_w = w;
                    uint8_t arg_g = desired_w;
                    uint8_t arg_r = desired_r;
                    uint8_t arg_b = desired_g;
                    uint8_t arg_w = desired_b;
                    // Encode as GRBW into the staging buffer.
                    tx_buf_[dst_off + 0] = arg_g;
                    tx_buf_[dst_off + 1] = arg_r;
                    tx_buf_[dst_off + 2] = arg_b;
                    tx_buf_[dst_off + 3] = arg_w;
                } else {
                    // GRBW for SK6812-style strips.
                    tx_buf_[dst_off + 0] = g;
                    tx_buf_[dst_off + 1] = r;
                    tx_buf_[dst_off + 2] = b;
                    tx_buf_[dst_off + 3] = w;
                }
            } else {
                // GRB for RGB-only strips.
                tx_buf_[dst_off + 0] = g;
                tx_buf_[dst_off + 1] = r;
                tx_buf_[dst_off + 2] = b;
            }
        }
    }

    rmt_transmit_config_t tx_conf = {};
    tx_conf.loop_count = 0;
    esp_err_t err = rmt_transmit(rmt_chan_, strip_encoder_, tx_buf_.data(),
                                 tx_buf_.size(), &tx_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        stats_.tx_error_count_total++;
        stats_.tx_error_count_window++;
        stats_.tx_error_last_code = err;
        return false;
    }

    // Record timing expectations and counters.
    stats_.tx_count_total++;
    stats_.tx_count_window++;
    stats_.last_start_us = now_us;

    transmitting_ = true;
    last_flush_us_ = now_us;
    dirty_ = false;
    estimate_transmission_end(now_us);
    return true;
}

void LEDStripRmt::on_transmit_complete(uint64_t now_us) {
    transmitting_ = false;
    if (now_us > expected_done_us_) expected_done_us_ = now_us;

    // Compute actual duration and track max in the current window.
    if (stats_.last_start_us != 0 && now_us >= stats_.last_start_us) {
        uint64_t dur = now_us - stats_.last_start_us;
        if (dur > UINT32_MAX) dur = UINT32_MAX;
        stats_.last_duration_us = static_cast<uint32_t>(dur);
        if (stats_.last_duration_us > stats_.max_duration_us_window) {
            stats_.max_duration_us_window = stats_.last_duration_us;
        }
    }

    // If actual completion is significantly later than our conservative estimate,
    // record a "late" transmit as a potential sign of ISR/DMA latency issues.
    const uint64_t margin_us = 200; // small absolute margin
    if (expected_done_us_ != 0 && now_us > expected_done_us_ + margin_us) {
        stats_.tx_late_count_total++;
        stats_.tx_late_count_window++;
    }
}

void LEDStripRmt::reset_window_stats() {
    stats_.tx_count_window = 0;
    stats_.tx_error_count_window = 0;
    stats_.tx_late_count_window = 0;
    stats_.backpressure_ticks_window = 0;
    stats_.max_duration_us_window = 0;
    stats_.expected_duration_us_max_window = 0;
}

void LEDStripRmt::on_backpressure_tick() {
    stats_.backpressure_ticks_window++;
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


