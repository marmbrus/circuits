#pragma once

#include "LEDPattern.h"
#include "system_state.h"

namespace leds {

// System status visualization with animations:
// - WIFI_CONNECTING: blue ping-pong scan horizontally and vertically
// - WIFI_CONNECTED_MQTT_CONNECTING: solid orange to indicate WiFi up
// - FULLY_CONNECTED: one-shot white ripple expanding from center, then fade to off
// - MQTT_ERROR_STATE: repeating outward ripple (red)
class StatusPattern final : public LEDPattern {
public:
    const char* name() const override { return "STATUS"; }
    void reset(LEDStrip& strip, uint64_t now_us) override {
        (void)strip;
        last_us_ = now_us;
        prev_state_ = get_system_state();
        state_change_us_ = now_us;
        connect_anim_start_us_ = 0;
        ball_motion_epoch_us_ = now_us;
    }
    void update(LEDStrip& strip, uint64_t now_us) override;

private:
    uint64_t last_us_ = 0;
    SystemState prev_state_ = WIFI_CONNECTING;
    uint64_t state_change_us_ = 0;
    uint64_t connect_anim_start_us_ = 0;
    uint64_t ball_motion_epoch_us_ = 0;
};

} // namespace leds


