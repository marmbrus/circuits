#pragma once

#include "LEDStrip.h"
#include "LEDSurfaceImpl.h"
#include "LEDCoordinateMapperRowMajor.h"
#include "LEDWireEncoder.h"
#include "driver/gpio.h"
#include <memory>
#include <vector>

namespace leds {

class LEDStripSurfaceAdapter final : public LEDStrip {
public:
    struct Params {
        int gpio;
        int enable_gpio = -1;
        size_t rows = 1;
        size_t cols = 1;
    };

    LEDStripSurfaceAdapter(const Params& p, std::unique_ptr<internal::LEDWireEncoder> encoder)
        : gpio_(p.gpio), enable_gpio_(p.enable_gpio), rows_(p.rows), cols_(p.cols) {
        using namespace leds::internal;
        auto mapper = std::unique_ptr<LEDCoordinateMapper>(new RowMajorMapper(rows_, cols_));
        surface_.reset(new LEDSurfaceImpl(rows_, cols_, std::move(mapper), std::move(encoder)));
        shadow_rgba_.assign(rows_ * cols_ * 4, 0);
        if (enable_gpio_ >= 0) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = 1ULL << enable_gpio_;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level((gpio_num_t)enable_gpio_, 0);
        }
    }

    // LEDStrip interface
    int pin() const override { return gpio_; }
    size_t length() const override { return rows_ * cols_; }
    LEDChip chip() const override { return LEDChip::WS2812; }
    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }
    size_t index_for_row_col(size_t row, size_t col) const override {
        if (row >= rows_) row = rows_ - 1;
        if (col >= cols_) col = cols_ - 1;
        return row * cols_ + col; // row-major for surface
    }

    bool set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        if (index >= length()) return false;
        size_t row = index / cols_;
        size_t col = index % cols_;
        surface_->set(row, col, r, g, b, w);
        size_t off = index * 4;
        bool changed = (shadow_rgba_[off] != r) || (shadow_rgba_[off+1] != g) || (shadow_rgba_[off+2] != b) || (shadow_rgba_[off+3] != w);
        shadow_rgba_[off] = r; shadow_rgba_[off+1] = g; shadow_rgba_[off+2] = b; shadow_rgba_[off+3] = w;
        if (changed) dirty_ = true;
        return changed;
    }

    bool get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const override {
        if (index >= shadow_rgba_.size() / 4) return false;
        size_t off = index * 4;
        r = shadow_rgba_[off]; g = shadow_rgba_[off+1]; b = shadow_rgba_[off+2]; w = shadow_rgba_[off+3];
        return true;
    }

    void clear() override {
        surface_->clear();
        std::fill(shadow_rgba_.begin(), shadow_rgba_.end(), 0u);
        dirty_ = true;
    }

    bool flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us) override {
        (void)now_us; (void)max_quiescent_us;
        if (!dirty_) return false;
        // Enable power if any pixel is non-black; otherwise disable
        bool any_on = false;
        for (size_t i = 0; i < shadow_rgba_.size(); ++i) { if (shadow_rgba_[i] != 0) { any_on = true; break; } }
        set_power_enabled(any_on);
        bool ok = surface_->flush();
        if (ok) dirty_ = false;
        return ok;
    }

    bool is_transmitting() const override { return surface_->is_busy(); }
    void on_transmit_complete(uint64_t now_us) override { (void)now_us; /* no-op */ }

    bool uses_dma() const override { return false; }
    bool has_enable_pin() const override { return enable_gpio_ >= 0; }
    void set_power_enabled(bool on) override {
        if (enable_gpio_ >= 0) {
            gpio_set_level((gpio_num_t)enable_gpio_, on ? 1 : 0);
        }
    }

private:
    int gpio_ = -1;
    int enable_gpio_ = -1;
    size_t rows_ = 1;
    size_t cols_ = 1;
    std::unique_ptr<LEDSurfaceImpl> surface_;
    std::vector<uint8_t> shadow_rgba_;
    bool dirty_ = false;
};

} // namespace leds


