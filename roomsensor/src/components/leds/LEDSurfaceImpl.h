#pragma once

#include "LEDSurface.h"
#include "LEDGrid.h"
#include "LEDWireEncoder.h"
#include <vector>
#include "PsramAllocator.h"
#include <memory>

namespace leds {

class LEDSurfaceImpl final : public LEDSurface {
public:
    LEDSurfaceImpl(size_t rows,
                   size_t cols,
                   std::unique_ptr<internal::LEDCoordinateMapper> mapper,
                   std::unique_ptr<internal::LEDWireEncoder> encoder)
        : rows_(rows), cols_(cols), mapper_(std::move(mapper)), encoder_(std::move(encoder)) {
        logical_rgba_.assign(rows_ * cols_ * 4, 0);
        frame_bytes_.resize(encoder_->frame_size_for(rows_, cols_));
    }

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void set(size_t row, size_t col, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        size_t mr = row, mc = col;
        if (mapper_) mapper_->map(row, col, mr, mc);
        if (mr >= rows_ || mc >= cols_) return;
        size_t idx = (mr * cols_ + mc) * 4;
        logical_rgba_[idx+0] = r;
        logical_rgba_[idx+1] = g;
        logical_rgba_[idx+2] = b;
        logical_rgba_[idx+3] = w;
    }

    void clear() override {
        std::fill(logical_rgba_.begin(), logical_rgba_.end(), 0u);
    }

    bool flush() override {
        if (!encoder_ || encoder_->is_busy()) return false;
        encoder_->encode_frame(logical_rgba_.data(), rows_, cols_, frame_bytes_.data());
        return encoder_->transmit_frame(frame_bytes_.data(), frame_bytes_.size());
    }

    bool is_busy() const override { return encoder_ ? encoder_->is_busy() : false; }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
    std::unique_ptr<internal::LEDCoordinateMapper> mapper_;
    std::unique_ptr<internal::LEDWireEncoder> encoder_;
    // Large frame buffers → store in PSRAM
    std::vector<uint8_t, PsramAllocator<uint8_t>> logical_rgba_;
    std::vector<uint8_t, PsramAllocator<uint8_t>> frame_bytes_;
};

} // namespace leds


