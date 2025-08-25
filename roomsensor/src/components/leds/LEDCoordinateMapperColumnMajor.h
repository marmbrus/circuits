#pragma once

#include "LEDGrid.h"

namespace leds { namespace internal {

class ColumnMajorMapper final : public LEDCoordinateMapper {
public:
    ColumnMajorMapper(size_t rows, size_t cols) : rows_(rows), cols_(cols) {}

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void map(size_t in_row, size_t in_col, size_t& out_row, size_t& out_col) const override {
        // Convert (row-major) input to column-major output coordinates
        if (rows_ == 0 || cols_ == 0) { out_row = 0; out_col = 0; return; }
        if (in_row >= rows_) in_row = rows_ - 1;
        if (in_col >= cols_) in_col = cols_ - 1;
        // Column-major physical expects we traverse columns first; we can
        // express this as remapping to an equivalent row-major coordinate
        size_t linear = in_row * cols_ + in_col; // logical row-major index
        size_t phys_row = linear % rows_;
        size_t phys_col = linear / rows_;
        if (phys_col >= cols_) phys_col = cols_ - 1;
        out_row = phys_row;
        out_col = phys_col;
    }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
};

} } // namespace leds::internal


