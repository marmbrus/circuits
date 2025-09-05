#pragma once

#include "LEDGrid.h"

namespace leds { namespace internal {

class RowMajorMapper final : public LEDCoordinateMapper {
public:
    RowMajorMapper(size_t rows, size_t cols) : rows_(rows), cols_(cols) {}

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void map(size_t in_row, size_t in_col, size_t& out_row, size_t& out_col) const override {
        if (in_row >= rows_) in_row = rows_ ? (rows_ - 1) : 0;
        if (in_col >= cols_) in_col = cols_ ? (cols_ - 1) : 0;
        out_row = in_row;
        out_col = in_col;
    }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
};

} } // namespace leds::internal


