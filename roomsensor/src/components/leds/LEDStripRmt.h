#pragma once

#include "LEDStrip.h"
#include "led_strip.h"
#include "esp_timer.h"
#include <vector>
#include <memory>

namespace leds {

// Concrete LEDStrip implementation backed by espressif led_strip RMT driver.
class LEDStripRmt final : public LEDStrip {
public:
    struct CreateParams {
        int gpio;
        int enable_gpio = -1; // optional power enable pin
        size_t length; // total number of LEDs = rows * cols
        size_t rows = 1; // logical rows (>=1)
        size_t cols = 0; // logical columns; if 0, inferred from length/rows
        LEDChip chip;
        bool use_dma;
        uint32_t rmt_resolution_hz = 10 * 1000 * 1000; // 10 MHz default per component doc
        size_t mem_block_symbols = 48; // default for non-DMA; manager may override for DMA
    };

    static std::unique_ptr<LEDStripRmt> Create(const CreateParams& params);
    ~LEDStripRmt() override;

    // LEDStrip interface
    int pin() const override { return gpio_; }
    size_t length() const override { return length_; }
    LEDChip chip() const override { return chip_; }
    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }
    size_t index_for_row_col(size_t row, size_t col) const override {
        if (rows_ == 0 || cols_ == 0) return 0;
        if (row >= rows_) row = rows_ - 1;
        if (col >= cols_) col = cols_ - 1;
        size_t idx = col * rows_ + row; // column-major
        if (idx >= length_) idx = length_ - 1;
        return idx;
    }

    bool set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;
    bool get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const override;
    void clear() override;

    bool flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us) override;
    bool is_transmitting() const override { return transmitting_; }
    void on_transmit_complete(uint64_t now_us) override;

    bool uses_dma() const override { return with_dma_; }

    bool has_enable_pin() const override { return enable_gpio_ >= 0; }
    void set_power_enabled(bool on) override;

private:
    LEDStripRmt(const CreateParams& params);
    bool init_handle();
    void destroy_handle();
    void estimate_transmission_end(uint64_t now_us);

    int gpio_ = -1;
    int enable_gpio_ = -1;
    size_t length_ = 0;
    size_t rows_ = 1;
    size_t cols_ = 0;
    LEDChip chip_ = LEDChip::WS2812;
    bool with_dma_ = false;
    uint32_t rmt_resolution_hz_ = 10 * 1000 * 1000;
    size_t mem_block_symbols_ = 48;

    // backing store for current values (RGBA order logical RGBW)
    std::vector<uint8_t> pixels_; // 3 or 4 bytes per LED
    bool has_white_ = false;
    bool dirty_ = false;

    // RMT strip handle
    led_strip_handle_t handle_ = nullptr;
    bool transmitting_ = false;
    uint64_t last_flush_us_ = 0;
    uint64_t expected_done_us_ = 0;
};

} // namespace leds


