#pragma once

#include "LEDStrip.h"
#include "esp_timer.h"
#include "PsramAllocator.h"
#include <vector>
#include <memory>
#include "esp_err.h"

// RMT TX driver (IDF 5.x)
#include "driver/rmt_tx.h"
// Internal encoder used by Espressif's led_strip component
#include "led_strip_rmt_encoder.h"

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
        config::LEDConfig::Chip chip;
        bool use_dma;
        uint32_t rmt_resolution_hz = 10 * 1000 * 1000; // 10 MHz default per component doc
        size_t mem_block_symbols = 48; // default for non-DMA; manager may override for DMA
    };

    static std::unique_ptr<LEDStripRmt> Create(const CreateParams& params);
    ~LEDStripRmt() override;

    // Lightweight telemetry about RMT activity, for diagnostics and health checks.
    struct Stats {
        // Counts
        uint32_t tx_count_total = 0;          // all frames ever transmitted
        uint32_t tx_count_window = 0;         // frames in current logging window
        uint32_t tx_error_count_total = 0;    // total transmit errors
        uint32_t tx_error_count_window = 0;   // errors in current window
        esp_err_t tx_error_last_code = ESP_OK;
        uint32_t tx_late_count_total = 0;     // transfers that finished after expected_done_us_ + margin
        uint32_t tx_late_count_window = 0;
        uint32_t backpressure_ticks_window = 0; // update-loop ticks where strip was still transmitting

        // Timing (us)
        uint64_t last_start_us = 0;
        uint64_t last_done_us = 0;
        uint32_t last_duration_us = 0;        // clamped to 32-bit
        uint32_t max_duration_us_window = 0;  // max observed duration in current window
        uint32_t expected_duration_us_last = 0;
        uint32_t expected_duration_us_max_window = 0;
    };

    // LEDStrip interface
    int pin() const override { return gpio_; }
    size_t length() const override { return length_; }
    config::LEDConfig::Chip chip() const override { return chip_; }
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

    // Access to current statistics snapshot. Safe to call from the LED manager task.
    const Stats& stats() const { return stats_; }
    // Reset per-window counters after a telemetry log.
    void reset_window_stats();
    // Record that an update tick was skipped because transmission was still in flight.
    void on_backpressure_tick();

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
    size_t length_ = 0;   // logical LEDs (rows_ * cols_)
    size_t rows_ = 1;
    size_t cols_ = 0;
    config::LEDConfig::Chip chip_ = config::LEDConfig::Chip::WS2812;
    bool with_dma_ = false;
    uint32_t rmt_resolution_hz_ = 10 * 1000 * 1000;
    size_t mem_block_symbols_ = 48;

    // backing store for current values (RGBA order logical RGBW)
    // Pixel shadow buffer can be large
    std::vector<uint8_t, PsramAllocator<uint8_t>> pixels_; // 3 or 4 bytes per LED
    bool has_white_ = false;
    bool dirty_ = false;

    // Asynchronous RMT backing: channel + encoder + staging buffer in host RAM.
    rmt_channel_handle_t rmt_chan_ = nullptr;
    rmt_encoder_handle_t strip_encoder_ = nullptr;
    // Number of bytes per *physical* LED the encoder expects (3 = GRB, 4 = GRBW).
    uint8_t bytes_per_pixel_ = 3;
    // Staging buffer passed to rmt_transmit(), ordered as GRB/GRBW per physical LED.
    std::vector<uint8_t, PsramAllocator<uint8_t>> tx_buf_;

    Stats stats_;

    bool transmitting_ = false;
    uint64_t last_flush_us_ = 0;
    uint64_t expected_done_us_ = 0;
};

} // namespace leds


