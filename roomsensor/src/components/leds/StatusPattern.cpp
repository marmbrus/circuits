#include "StatusPattern.h"
#include "LEDStrip.h"
#include <cmath>

namespace leds {

static inline uint8_t clamp_u8(int v) { if (v < 0) return 0; if (v > 255) return 255; return static_cast<uint8_t>(v); }

void StatusPattern::update(LEDStrip& strip, uint64_t now_us) {

    SystemState s = get_system_state();
    if (s != prev_state_) {
        prev_state_ = s;
        // Maintain bouncing ball continuity across WIFI_CONNECTING -> WIFI_CONNECTED_MQTT_CONNECTING
        // by not resetting the motion epoch when moving between these two states.
        bool is_ball_state = (s == WIFI_CONNECTING) || (s == WIFI_CONNECTED_MQTT_CONNECTING);
        bool was_ball_state = (prev_state_ == WIFI_CONNECTING) || (prev_state_ == WIFI_CONNECTED_MQTT_CONNECTING);
        if (!(is_ball_state && was_ball_state)) {
            ball_motion_epoch_us_ = now_us;
        }
        state_change_us_ = now_us;
        if (s == FULLY_CONNECTED) {
            connect_anim_start_us_ = now_us;
        }
    }

    const size_t rows = strip.rows();
    const size_t cols = strip.cols();
    const size_t total = strip.length();
    (void)total;

    // Clear by default; patterns will light select pixels
    strip.clear();

    switch (s) {
        case WIFI_CONNECTING:
        case WIFI_CONNECTED_MQTT_CONNECTING: {
            // Single-dot ping-pong with shallow angle and trailing fade
            float t_s = (now_us - ball_motion_epoch_us_) / 1000000.0f;
            auto tri_pos = [](float t, float max_val, float speed_cells_per_s) -> float {
                if (max_val <= 0.0f || speed_cells_per_s <= 0.0f) return 0.0f;
                float L = 2.0f * max_val;
                float pos = fmodf(speed_cells_per_s * t, L);
                if (pos < 0.0f) pos += L;
                return (pos <= max_val) ? pos : (2.0f * max_val - pos);
            };

            // Choose speeds: mostly horizontal motion; slow vertical drift
            float max_col = cols > 0 ? static_cast<float>(cols - 1) : 0.0f;
            float max_row = rows > 0 ? static_cast<float>(rows - 1) : 0.0f;
            float speed_scale = 2.5f; // 2-3x faster
            float vx = fmaxf(0.5f, (max_col / 1.2f) * speed_scale); // traverse width ~1.2s -> ~0.5s
            float vy = fmaxf(0.2f, (max_row / 6.0f) * speed_scale); // traverse height ~6s -> ~2.4s

            // Tail samples (head at i=0)
            const int tail_count = 5;
            const float tail_dt_s = 0.08f; // 80ms spacing
            for (int i = 0; i < tail_count; ++i) {
                float t_i = t_s - i * tail_dt_s;
                float col_f = tri_pos(t_i, max_col, vx);
                float row_f = tri_pos(t_i, max_row, vy);
                size_t col = static_cast<size_t>(col_f + 0.5f);
                size_t row = static_cast<size_t>(row_f + 0.5f);
                size_t idx = strip.index_for_row_col(row, col);
                float falloff = powf(0.6f, static_cast<float>(i));
                if (s == WIFI_CONNECTING) {
                    strip.set_pixel(idx, 0, 0, clamp_u8(static_cast<int>(180.0f * falloff)), 0);
                } else { // WIFI_CONNECTED_MQTT_CONNECTING
                    uint8_t r = clamp_u8(static_cast<int>(200.0f * falloff));
                    uint8_t g = clamp_u8(static_cast<int>(100.0f * falloff));
                    strip.set_pixel(idx, r, g, 0, 0);
                }
            }
            break;
        }
        case FULLY_CONNECTED: {
            // One-shot white ripple (RGB) from center over ~5 seconds, fading to off
            if (connect_anim_start_us_ == 0) connect_anim_start_us_ = now_us;
            uint64_t dt_us = now_us - connect_anim_start_us_;
            float duration_ms = 5000.0f; // 5 seconds
            float dt_ms = dt_us / 1000.0f;
            float progress = fminf(1.0f, dt_ms / duration_ms);
            float center_r = (rows - 1) * 0.5f;
            float center_c = (cols - 1) * 0.5f;
            float max_radius = hypotf(center_r, center_c) + 1.0f;
            float radius = progress * max_radius;
            float thickness = 1.2f;
            float amplitude = 180.0f * (1.0f - progress); // fade as it expands

            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    float dr = (float)r - center_r;
                    float dc = (float)c - center_c;
                    float d = sqrtf(dr*dr + dc*dc);
                    float band = 1.0f - fminf(1.0f, fabsf(d - radius) / thickness);
                    if (band > 0.0f) {
                        uint8_t v = clamp_u8((int)(band * amplitude));
                        size_t idx = strip.index_for_row_col(r, c);
                        strip.set_pixel(idx, v, v, v, 0); // use RGB so it works on WS2812 and SK6812
                    }
                }
            }
            // After completion, keep lights off
            break;
        }
        case MQTT_ERROR_STATE: {
            // Repeating outward ripple in red
            float period_ms = 1200.0f;
            float t_ms = (now_us % (uint64_t)(period_ms * 1000.0f)) / 1000.0f;
            float center_r = (rows - 1) * 0.5f;
            float center_c = (cols - 1) * 0.5f;
            float max_radius = hypotf((rows - 1) * 0.5f, (cols - 1) * 0.5f);
            float radius = (t_ms / period_ms) * (max_radius + 1.0f);
            float thickness = 1.0f;
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    float dr = (float)r - center_r;
                    float dc = (float)c - center_c;
                    float d = sqrtf(dr*dr + dc*dc);
                    float band = 1.0f - fminf(1.0f, fabsf(d - radius) / thickness);
                    if (band > 0.0f) {
                        uint8_t red = clamp_u8((int)(band * 128.0f));
                        size_t idx = strip.index_for_row_col(r, c);
                        strip.set_pixel(idx, red, 0, 0, 0);
                    }
                }
            }
            break;
        }
        default: {
            // off
            break;
        }
    }
}

} // namespace leds


