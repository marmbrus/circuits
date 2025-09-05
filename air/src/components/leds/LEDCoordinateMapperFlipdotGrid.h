#pragma once

#include "LEDGrid.h"
#include <cstddef>

namespace leds { namespace internal {

// FlipDotGrid mapping: the display is composed of 8x8 boxes (no partial boxes).
// Boxes are arranged row-major across the full surface.
// Within each box, pixel order runs in columns from right to left; within each
// column, from bottom to top.
// I.e., box-local (rb, cb) where rb=0..7 is row from top, cb=0..7 is col from left.
// Physical index increments as: col = 7..0, row = 7..0.
class FlipdotGridMapper final : public LEDCoordinateMapper {
public:
    FlipdotGridMapper(size_t rows, size_t cols) : rows_(rows), cols_(cols) {}

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void map(size_t in_row, size_t in_col, size_t& out_row, size_t& out_col) const override {
        // Constraints: rows and cols are multiples of 8
        if (rows_ == 0 || cols_ == 0) { out_row = 0; out_col = 0; return; }
        if (in_row >= rows_) in_row = rows_ - 1;
        if (in_col >= cols_) in_col = cols_ - 1;

        // Inverse mapping using explicit chain order:
        // Boxes ordered row-major across the surface.
        // Within each 8x8 box, chain is reverse column-major: bottom->top, right->left.
        // Compute the chain index for the desired logical coordinate, then place it
        // into the frame's row-major grid so encoding iterates in chain order.

        size_t box_cols = cols_ / 8;
        size_t br = in_row / 8;
        size_t bc = in_col / 8;
        size_t rb = in_row % 8;
        size_t cb = in_col % 8;

        size_t s = (7 - cb) * 8 + (7 - rb);                 // index within box
        size_t chain = ((br * box_cols) + bc) * 64 + s;      // global chain index

        // Map chain index into row-major coordinates of the frame buffer
        out_row = chain / cols_;
        out_col = chain % cols_;
    }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
};

} } // namespace leds::internal


