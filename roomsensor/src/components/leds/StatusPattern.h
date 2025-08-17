#pragma once

#include "LEDPattern.h"
#include "system_state.h"

namespace leds {

// Simple system status visualization:
// - WIFI_CONNECTING: slow blue pulse
// - WIFI_CONNECTED_MQTT_CONNECTING: cyan pulse
// - FULLY_CONNECTED: solid green
// - MQTT_ERROR_STATE: red flash at 1Hz
class StatusPattern final : public LEDPattern {
public:
    const char* name() const override { return "STATUS"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { (void)strip; last_us_ = now_us; }
    void update(LEDStrip& strip, uint64_t now_us) override;

private:
    uint64_t last_us_ = 0;
};

} // namespace leds


