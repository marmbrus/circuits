#pragma once

#include "LEDGrid.h"

namespace leds { namespace internal {

class SerpentineRowMapper final : public LEDCoordinateMapper {
public:
    SerpentineRowMapper(size_t rows, size_t cols) : rows_(rows), cols_(cols) {}

    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }

    void map(size_t in_row, size_t in_col, size_t& out_row, size_t& out_col) const override {
        if (rows_ == 0 || cols_ == 0) { out_row = 0; out_col = 0; return; }
        if (in_row >= rows_) in_row = rows_ - 1;
        if (in_col >= cols_) in_col = cols_ - 1;
        bool reverse = (in_row % 2u) == 1u;
        out_row = in_row;
        out_col = reverse ? (cols_ - 1 - in_col) : in_col;
    }

private:
    size_t rows_ = 0;
    size_t cols_ = 0;
};

} } // namespace leds::internal


