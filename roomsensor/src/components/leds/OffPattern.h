#pragma once

#include "LEDPattern.h"

namespace leds {

class OffPattern final : public LEDPattern {
public:
    const char* name() const override { return "OFF"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;
};

} // namespace leds


