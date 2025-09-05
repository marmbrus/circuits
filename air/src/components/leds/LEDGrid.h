#pragma once

#include <cstddef>

namespace leds { namespace internal {

// Minimal coordinate mapper: translate logical row-major (row, col)
// to the physical row-major (row, col). Patterns always write to logical.
class LEDCoordinateMapper {
public:
    virtual ~LEDCoordinateMapper() = default;

    virtual size_t rows() const = 0;
    virtual size_t cols() const = 0;

    // Map input (row,col) -> output (row,col) in row-major.
    virtual void map(size_t in_row, size_t in_col, size_t& out_row, size_t& out_col) const = 0;
protected:
    LEDCoordinateMapper() = default;

private:
    LEDCoordinateMapper(const LEDCoordinateMapper&) = delete;
    LEDCoordinateMapper& operator=(const LEDCoordinateMapper&) = delete;
};

} } // namespace leds::internal
