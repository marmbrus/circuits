#pragma once

#include "LEDStrip.h"
#include "LEDSurfaceImpl.h"
#include "LEDCoordinateMapperRowMajor.h"
#include "PixelProcessor.h"
#include "Transmitter.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <memory>
#include <vector>
#include "PsramAllocator.h"

namespace leds {

class LEDStripSurfaceAdapter final : public LEDStrip {
public:
    struct Params {
        int gpio;
        std::vector<int> enable_gpios;
        size_t rows = 1;
        size_t cols = 1;
    };

    LEDStripSurfaceAdapter(const Params& p,
                           std::unique_ptr<internal::LEDCoordinateMapper> mapper,
                           std::unique_ptr<PixelProcessor> processor,
                           std::unique_ptr<Transmitter> transmitter)
        : gpio_(p.gpio), enable_gpios_(p.enable_gpios), rows_(p.rows), cols_(p.cols) {
        surface_.reset(new LEDSurfaceImpl(rows_, cols_, std::move(mapper), std::move(processor), std::move(transmitter)));
        shadow_pixels_.assign(rows_ * cols_, Color());
        
        if (!enable_gpios_.empty()) {
            uint64_t mask = 0;
            for (int pin : enable_gpios_) { if (pin >= 0) mask |= (1ULL << pin); }
            if (mask) {
                gpio_config_t io_conf = {};
                io_conf.intr_type = GPIO_INTR_DISABLE;
                io_conf.mode = GPIO_MODE_OUTPUT;
                io_conf.pin_bit_mask = mask;
                io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
                io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
                gpio_config(&io_conf);
                for (int pin : enable_gpios_) {
                    if (pin >= 0) gpio_set_level((gpio_num_t)pin, 0);
                }
            }
        }
    }

    // LEDStrip interface
    int pin() const override { return gpio_; }
    size_t length() const override { return rows_ * cols_; }
    config::LEDConfig::Chip chip() const override { return config::LEDConfig::Chip::WS2812; } // TODO: Should come from params or processor?
    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }
    size_t index_for_row_col(size_t row, size_t col) const override {
        if (row >= rows_) row = rows_ - 1;
        if (col >= cols_) col = cols_ - 1;
        return row * cols_ + col; // row-major for surface
    }

    bool set_pixel(size_t index, const Color& color) override {
        if (index >= length()) return false;
        size_t row = index / cols_;
        size_t col = index % cols_;
        surface_->set(row, col, color);
        
        if (shadow_pixels_[index] != color) {
            shadow_pixels_[index] = color;
            dirty_ = true;
            return true;
        }
        return false;
    }

    Color get_pixel(size_t index) const override {
        if (index >= shadow_pixels_.size()) return Color();
        return shadow_pixels_[index];
    }

    void clear() override {
        surface_->clear();
        std::fill(shadow_pixels_.begin(), shadow_pixels_.end(), Color());
        dirty_ = true;
    }

    bool flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us) override {
        (void)now_us;
        if (!dirty_) {
            // Only flush if quiescent timeout is specified and expired (not implemented yet)
            // if (max_quiescent_us > 0 && ...)
            return false;
        }
        bool ok = surface_->flush();
        if (ok) dirty_ = false;
        return ok;
    }

    bool is_transmitting() const override { return surface_->is_busy(); }
    void on_transmit_complete(uint64_t now_us) override { (void)now_us; /* no-op */ }

    bool uses_dma() const override { return false; } // TODO: Propagate from transmitter
    bool has_enable_pin() const override { return !enable_gpios_.empty(); }
    void set_power_enabled(bool on) override {
        if (enable_gpios_.empty()) return;
        if (on == power_enabled_) return;
        if (on && enable_gpios_.size() > 1) {
            // Stagger enabling to mitigate inrush current: 500ms between pins
            for (size_t i = 0; i < enable_gpios_.size(); ++i) {
                int pin = enable_gpios_[i];
                if (pin >= 0) gpio_set_level((gpio_num_t)pin, 1);
                if (i + 1 < enable_gpios_.size()) vTaskDelay(pdMS_TO_TICKS(500));
            }
        } else {
            // Single pin, or turning off: apply immediately
            for (int pin : enable_gpios_) {
                if (pin >= 0) gpio_set_level((gpio_num_t)pin, on ? 1 : 0);
            }
        }
        power_enabled_ = on;
    }

private:
    int gpio_ = -1;
    std::vector<int> enable_gpios_;
    size_t rows_ = 1;
    size_t cols_ = 1;
    bool power_enabled_ = false;
    std::unique_ptr<LEDSurfaceImpl> surface_;
    // Shadow buffer
    std::vector<Color, PsramAllocator<Color>> shadow_pixels_;
    bool dirty_ = false;
};

} // namespace leds
