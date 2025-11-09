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
        (void)num_seg_rows;

        // Module-local serpentine within 32x8 tiles.
        // - Tiles are 32 columns (wide) by seg_rows (tall, usually 8).
        // - Chain order iterates module-columns left->right.
        // - Within each module-column, we iterate modules top->bottom.
        // - For each module, traverse its 32 columns in serpentine fashion,
        //   reversing the column traversal direction for every successive module
        //   down the column (modules alternate IN/OUT orientation).
        const size_t module_cols = 32; // width of one module
        const size_t mod_h = seg_rows;
        const size_t mod_w = module_cols;
        const size_t mod_rows = (rows_ + mod_h - 1) / mod_h;
        const size_t mod_cols = (cols_ + mod_w - 1) / mod_w;

        // Module grid position and local-in-module coords
        size_t mr = in_row / mod_h;     // module row
        size_t mc = in_col / mod_w;     // module column
        size_t rb = in_row % mod_h;     // row inside module
        size_t cb = in_col % mod_w;     // col inside module

        // Alternate module flip per module-row (vertical daisy-chain flip)
        bool module_flipped = ((mr % 2u) == 1u);

        // Step index through module columns (0..31) controls serpentine parity.
        // If module is flipped, we enumerate columns in reverse.
        size_t step_k = module_flipped ? (mod_w - 1 - cb) : cb;
        // Extra flip when moving to the next module vertically to match daisy-chain orientation,
        // so the start of the next module is at the top instead of bottom.
        bool topdown = (((step_k % 2u) == 0u) ^ ((mr % 2u) == 1u));
        size_t s_in_col = topdown ? rb : (mod_h - 1 - rb);
        size_t s_in_module = step_k * mod_h + s_in_col; // 0..(mod_w*mod_h - 1)

        // Chain: modules left->right (mc), within each column top->bottom (mr), then module-local index
        size_t module_index = mc * mod_rows + mr;
        size_t chain = module_index * (mod_w * mod_h) + s_in_module;

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




