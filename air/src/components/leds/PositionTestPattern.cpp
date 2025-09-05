#include "PositionTestPattern.h"
#include "LEDStrip.h"

namespace leds {

void PositionTestPattern::update(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    size_t rows = strip.rows();
    size_t cols = strip.cols();
    size_t idx = strip.index_for_row_col(r_ < rows ? r_ : (rows ? rows - 1 : 0),
                                         g_ < cols ? g_ : (cols ? cols - 1 : 0));
    // Clear all; light selected
    for (size_t i = 0; i < strip.length(); ++i) {
        if (i == idx) strip.set_pixel(i, 255, 255, 255, 255);
        else strip.set_pixel(i, 0, 0, 0, 0);
    }
}

} // namespace leds


