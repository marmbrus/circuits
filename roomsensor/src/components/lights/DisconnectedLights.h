#pragma once

#include "led_control.h"
#include "config.h"

enum DisconnectType {
    WIFI_DISCONNECT,   // Blue fade
    MQTT_DISCONNECT    // Orange fade
};

class DisconnectedLights : public LEDBehavior {
public:
    DisconnectedLights();
    void setDisconnectType(DisconnectType type);
    void update(led_strip_handle_t led_strip, uint8_t pulse_brightness) override;

private:
    int current_led;          // Current LED being faded
    uint32_t fade_progress;   // Progress for the current LED (in milliseconds)
    DisconnectType disconnect_type; // Type of disconnection (WiFi or MQTT)
    
    static constexpr uint32_t FADE_TIME_MS = 30000;  // 30 seconds per LED
    static constexpr uint8_t MAX_BRIGHTNESS_PCT = 20; // 20% maximum brightness
}; 