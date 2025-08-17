#include "StatusPattern.h"
#include "LEDStrip.h"

namespace leds {

void StatusPattern::update(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    if (strip.has_enable_pin()) strip.set_power_enabled(true);
    SystemState s = get_system_state();
    uint8_t r=0,g=0,b=0;
    switch (s) {
        case WIFI_CONNECTING: g=0; r=0; b=64; break; // blue
        case WIFI_CONNECTED_MQTT_CONNECTING: r=0; g=64; b=64; break; // cyan
        case FULLY_CONNECTED: r=0; g=64; b=0; break; // green
        case MQTT_ERROR_STATE: r=64; g=0; b=0; break; // red
        default: r=0; g=0; b=0; break;
    }
    for (size_t i = 0; i < strip.length(); ++i) {
        strip.set_pixel(i, r, g, b, 0);
    }
}

} // namespace leds


