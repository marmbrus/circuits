#pragma once

#include "LEDGrid.h"

namespace leds { namespace internal {

// Serpentine mapping where the physical chain runs column-major inside
// vertical segments of height segment_rows. Segments themselves are arranged
// row-major across the grid of segments, but in a serpentine pattern:
// even segment rows go left->right, odd segment rows go right->left.
//
// If segment_rows <= 0, the entire height is treated as a single segment.
class SerpentineColumnMapper final : public LEDCoordinateMapper {
public:
    SerpentineColumnMapper(size_t rows, size_t cols, size_t segment_rows)
        : rows_(rows), cols_(cols), seg_rows_(segment_rows == 0 ? rows : segment_rows) {}

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void map(size_t in_row, size_t in_col, size_t& out_row, size_t& out_col) const override {
        if (rows_ == 0 || cols_ == 0) { out_row = 0; out_col = 0; return; }
        if (in_row >= rows_) in_row = rows_ - 1;
        if (in_col >= cols_) in_col = cols_ - 1;

        // Compute segment grid coordinates
        size_t seg_rows = seg_rows_ == 0 ? rows_ : seg_rows_;
        if (seg_rows > rows_) seg_rows = rows_;
        size_t seg_row_index = in_row / seg_rows;                 // which segment row
        size_t row_in_segment = in_row % seg_rows;                 // local row inside the segment

        // Number of segment rows (height in segments)
        size_t num_seg_rows = (rows_ + seg_rows - 1) / seg_rows;   // ceil

        (void)num_seg_rows; // not strictly needed, but useful for clarity

        // Serpentine at the segment level across columns
        bool seg_row_forward = ((seg_row_index % 2u) == 0u);
        size_t col_eff = seg_row_forward ? in_col : (cols_ - 1 - in_col);

        // Within each column of a segment, orientation alternates per column and per
        // segment row so the transition between segment rows flips orientation.
        bool topdown = ((col_eff % 2u) == (seg_row_index % 2u));
        size_t s_in_seg = topdown ? row_in_segment : (seg_rows - 1 - row_in_segment);

        // Chain index: segments laid out row-major across segment rows, then column order
        size_t chain = (seg_row_index * cols_ + col_eff) * seg_rows + s_in_seg;

        // Map chain index into framebuffer row-major coordinates so that iterating the
        // framebuffer row-major emits pixels in physical chain order.
        out_row = chain / cols_;
        out_col = chain % cols_;
    }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
    size_t seg_rows_ = 0;
};

} } // namespace leds::internal




