#pragma once

#include "LEDSurface.h"
#include "LEDGrid.h"
#include "PixelProcessor.h"
#include "Transmitter.h"
#include <vector>
#include "PsramAllocator.h"
#include <memory>

namespace leds {

class LEDSurfaceImpl final : public LEDSurface {
public:
    LEDSurfaceImpl(size_t rows,
                   size_t cols,
                   std::unique_ptr<internal::LEDCoordinateMapper> mapper,
                   std::unique_ptr<PixelProcessor> processor,
                   std::unique_ptr<Transmitter> transmitter)
        : rows_(rows), cols_(cols), mapper_(std::move(mapper)), 
          processor_(std::move(processor)), transmitter_(std::move(transmitter)) {
        // Keep logical state in PSRAM (large)
        logical_pixels_.assign(rows_ * cols_, Color());
        // Use internal RAM (SRAM) for the wire buffer to ensure stable transmission
        // especially for non-DMA RMT which relies on CPU-driven FIFO filling.
        // PSRAM latency can cause RMT underrun/flicker.
        if (processor_) {
            frame_bytes_.resize(processor_->get_buffer_size(rows_ * cols_));
        }
    }

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void set(size_t row, size_t col, const Color& color) override {
        size_t mr = row, mc = col;
        if (mapper_) mapper_->map(row, col, mr, mc);
        if (mr >= rows_ || mc >= cols_) return;
        size_t idx = mr * cols_ + mc;
        if (idx < logical_pixels_.size()) {
            logical_pixels_[idx] = color;
        }
    }

    void clear() override {
        std::fill(logical_pixels_.begin(), logical_pixels_.end(), Color());
    }

    bool flush() override {
        if (!processor_ || !transmitter_ || transmitter_->is_busy()) return false;
        
        processor_->process(logical_pixels_.data(), logical_pixels_.size(), frame_bytes_.data());
        return transmitter_->transmit(frame_bytes_.data(), frame_bytes_.size());
    }

    bool is_busy() const override { return transmitter_ ? transmitter_->is_busy() : false; }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
    std::unique_ptr<internal::LEDCoordinateMapper> mapper_;
    std::unique_ptr<PixelProcessor> processor_;
    std::unique_ptr<Transmitter> transmitter_;
    
    // Store logical state in PSRAM
    std::vector<Color, PsramAllocator<Color>> logical_pixels_;
    // Store wire buffer in SRAM (default allocator)
    std::vector<uint8_t> frame_bytes_;
};

} // namespace leds
