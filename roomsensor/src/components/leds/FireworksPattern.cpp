#include "FireworksPattern.h"
#include "LEDStrip.h"
#include "esp_random.h"
#include <algorithm>
#include <cmath>

namespace leds {

uint64_t FireworksPattern::lifetime_us() const {
    int s = duration_seconds_;
    if (s <= 0) s = 5;
    return static_cast<uint64_t>(s) * 1'000'000ULL;
}

void FireworksPattern::set_speed_percent(int speed_seconds) {
    if (speed_seconds < 0) speed_seconds = 0;
    duration_seconds_ = speed_seconds;
}

void FireworksPattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    brightness_percent_ = brightness_percent;
}

void FireworksPattern::set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    base_r_ = r; base_g_ = g; base_b_ = b; base_w_ = w;
    base_color_set_ = true;
}

void FireworksPattern::update_geometry(LEDStrip& strip) {
    real_rows_ = strip.rows();
    real_cols_ = strip.cols();
    size_t length = strip.length();
    if (real_rows_ == 0) real_rows_ = 1;
    if (real_cols_ == 0) {
        // 1D strip: treat columns as length
        real_cols_ = (length > 0) ? length : 1;
    }

    // Treat the longest axis as "vertical height" for fireworks
    if (real_rows_ >= real_cols_) {
        major_len_ = real_rows_;
        minor_len_ = std::max<size_t>(1, real_cols_);
    } else {
        major_len_ = real_cols_;
        minor_len_ = std::max<size_t>(1, real_rows_);
    }
    if (major_len_ == 0) major_len_ = 1;
}

void FireworksPattern::reset(LEDStrip& strip, uint64_t now_us) {
    update_geometry(strip);
    rocket_.active = false;
    sparks_.clear();
    last_launch_us_ = now_us;
    if (!base_color_set_) {
        base_r_ = 255; base_g_ = 220; base_b_ = 160; base_w_ = 0;
        base_color_set_ = true;
    }
}

void FireworksPattern::ensure_rocket(uint64_t now_us) {
    if (rocket_.active || !sparks_.empty()) return;
    uint64_t interval = lifetime_us() / 3; // spacing between launches
    if (now_us - last_launch_us_ >= interval) {
        spawn_rocket(now_us);
    }
}

void FireworksPattern::spawn_rocket(uint64_t now_us) {
    rocket_.active = true;
    rocket_.start_us = now_us;
    rocket_.last_us = now_us;

    uint32_t r = esp_random();
    float bottom = static_cast<float>(major_len_ - 1);
    rocket_.u = bottom;
    rocket_.minor = static_cast<float>(r % minor_len_);

    // Upward velocity so rocket reaches near top in ~40% of its lifetime
    float life_s = static_cast<float>(lifetime_us()) / 1'000'000.0f;
    float flight_s = life_s * 0.4f;
    if (flight_s <= 0.1f) flight_s = 0.1f;
    rocket_.vu = -bottom / flight_s;

    // Small lateral motion
    float dir = (r & 1u) ? 1.0f : -1.0f;
    rocket_.vminor = dir * (static_cast<float>((r >> 8) & 0xFF) / 255.0f) * (static_cast<float>(minor_len_) / life_s) * 0.1f;

    last_launch_us_ = now_us;
}

void FireworksPattern::explode_rocket(uint64_t now_us) {
    if (!rocket_.active) return;
    // Spawn sparks around the rocket's last position in different patterns:
    // - ring
    // - radiating lines (spokes)
    // - more solid-filled bursts
    // - concentric circles (multiple rings)
    uint32_t rnd = esp_random();
    int mode = static_cast<int>(rnd % 5u); // 0=ring, 1=spokes, 2=solid-ish, 3=concentric, 4=rain
    last_explosion_mode_ = static_cast<uint8_t>(mode);

    int num_sparks;
    switch (mode) {
        case 1: num_sparks = 16; break;           // spokes: fewer, more distinct rays
        case 2: num_sparks = 40; break;           // solid: more points
        case 3: num_sparks = 1;  break;           // concentric: one logical explosion center
        case 4: num_sparks = 32; break;           // rain: dense solid burst
        default: num_sparks = 24; break;          // ring: default density
    }

    float dur_s = static_cast<float>(duration_seconds_ <= 0 ? 5 : duration_seconds_);
    // Randomize explosion size by varying base_speed
    float size_jitter = static_cast<float>((rnd >> 8) & 0xFF) / 255.0f; // 0..1
    float size_scale = 0.4f + size_jitter * 1.2f; // 0.4x .. 1.6x
    float base_speed = (static_cast<float>(major_len_) / (dur_s * 1.5f)) * size_scale;

    // Base lifetime; individual sparks will jitter around this.
    float life_base = dur_s * 0.6f;

    float u0 = rocket_.u;
    float m0 = rocket_.minor;

    for (int i = 0; i < num_sparks; ++i) {
        Spark s;
        s.u = u0;
        s.minor = m0;
        s.origin_u = u0;
        s.origin_minor = m0;
        s.start_us = now_us;
        s.last_us = now_us;
        // Per-spark lifetime jitter for more organic fades
        float lj = static_cast<float>((esp_random() >> 16) & 0xFF) / 255.0f; // 0..1
        s.life_s = life_base * (0.7f + lj * 0.6f); // ~0.7x..1.3x of base

        float angle;
        if (mode == 1) {
            // Spokes: snap to a smaller set of discrete angles so rays are clear
            const int spoke_count = 8;
            angle = static_cast<float>(i % spoke_count) *
                    (2.0f * static_cast<float>(M_PI) / static_cast<float>(spoke_count));
        } else {
            // Ring / solid: uniform distribution
            angle = static_cast<float>(i) *
                    (2.0f * static_cast<float>(M_PI) / static_cast<float>(num_sparks));
        }

        // Solid-ish bursts: vary speed radially for a filled effect
        float speed = base_speed;
        if (mode == 2) {
            float sj = static_cast<float>((esp_random() >> 8) & 0xFF) / 255.0f; // 0..1
            speed *= (0.4f + sj * 1.4f); // wide spread of radii
        }

        s.vu = std::sin(angle) * speed;
        s.vminor = std::cos(angle) * speed;

        // Color and mode
        s.r = base_r_;
        s.g = base_g_;
        s.b = base_b_;
        s.mode = static_cast<uint8_t>(mode);
        sparks_.push_back(s);
    }

    rocket_.active = false;
}

void FireworksPattern::update_rocket(uint64_t now_us) {
    if (!rocket_.active) return;
    float dt = static_cast<float>(now_us - rocket_.last_us) / 1'000'000.0f;
    if (dt < 0.0f) dt = 0.0f;
    rocket_.last_us = now_us;

    rocket_.u += rocket_.vu * dt;
    rocket_.minor += rocket_.vminor * dt;

    float apex = static_cast<float>(major_len_) * 0.3f;
    uint64_t max_flight_us = lifetime_us() / 2;
    if (rocket_.u <= apex || (rocket_.last_us - rocket_.start_us) >= max_flight_us) {
        explode_rocket(now_us);
    }
}

void FireworksPattern::update_sparks(uint64_t now_us) {
    float life_scale = static_cast<float>(brightness_percent_) / 100.0f;
    if (life_scale <= 0.0f) {
        sparks_.clear();
        return;
    }

    for (auto& s : sparks_) {
        if (!s.active && s.start_us == 0) continue;
        float dt = static_cast<float>(now_us - s.last_us) / 1'000'000.0f;
        if (dt < 0.0f) dt = 0.0f;
        s.last_us = now_us;

        // For "rain" explosions, apply a simple gravity so sparks arc up then fall.
        if (s.mode == 4) {
            float dur_s = static_cast<float>(duration_seconds_ <= 0 ? 5 : duration_seconds_);
            // Gravity tuned so that sparks fall back toward the ground over their lifetime.
            float g = (static_cast<float>(major_len_) / (dur_s * dur_s)) * 2.0f;
            s.vu += g * dt;
        }

        s.u += s.vu * dt;
        s.minor += s.vminor * dt;
    }

    // Remove dead sparks
    sparks_.erase(
        std::remove_if(sparks_.begin(), sparks_.end(),
                       [now_us](const Spark& s) {
                           float elapsed_s = static_cast<float>(now_us - s.start_us) / 1'000'000.0f;
                           // Also cull once they've lived their life
                           return elapsed_s >= s.life_s;
                       }),
        sparks_.end());
}

void FireworksPattern::render(LEDStrip& strip) {
    size_t length = strip.length();
    if (length == 0) return;

    // Accumulate contributions additively into a temporary buffer, then write once.
    std::vector<float> acc_r(length, 0.0f);
    std::vector<float> acc_g(length, 0.0f);
    std::vector<float> acc_b(length, 0.0f);

    auto map_to_rc = [&](float u, float minor, size_t& row, size_t& col) -> bool {
        // World is NOT a torus: treat coordinates outside [0, len) as off-screen.
        if (u < 0.0f || u > static_cast<float>(major_len_ - 1)) return false;
        if (minor < 0.0f || minor > static_cast<float>(minor_len_ - 1)) return false;

        size_t ui = static_cast<size_t>(u + 0.5f);
        size_t mi = static_cast<size_t>(minor + 0.5f);
        if (ui >= major_len_) return false;
        if (mi >= minor_len_) return false;

        // Map "vertical-major" coordinates back to actual grid/strip coordinates.
        if (real_rows_ >= real_cols_) {
            // rows is major axis
            row = ui;
            if (real_cols_ == 0) return false;
            col = std::min(mi, real_cols_ - 1);
        } else {
            // cols is major axis; for 1D strips, real_rows_ == 1
            col = ui;
            if (real_rows_ == 0) return false;
            row = std::min(mi, real_rows_ - 1);
        }
        return true;
    };

    float global_scale = static_cast<float>(brightness_percent_) / 100.0f;
    if (global_scale < 0.0f) global_scale = 0.0f;
    if (global_scale > 1.0f) global_scale = 1.0f;

    // Draw rocket as a bright head
    if (rocket_.active) {
        size_t row = 0, col = 0;
        if (map_to_rc(rocket_.u, rocket_.minor, row, col)) {
            size_t idx = strip.index_for_row_col(row, col);
            if (idx < length) {
                float r = 255.0f * global_scale;
                float g = 255.0f * global_scale;
                float b = 255.0f * global_scale;
                acc_r[idx] += r;
                acc_g[idx] += g;
                acc_b[idx] += b;
            }
        }
    }

    // Draw sparks
    for (const auto& s : sparks_) {
        float elapsed_s = static_cast<float>(s.last_us - s.start_us) / 1'000'000.0f;
        if (elapsed_s < 0.0f || elapsed_s >= s.life_s) continue;
        float life_frac = elapsed_s / s.life_s; // 0..1
        float amp = (1.0f - life_frac);
        if (amp <= 0.0f) continue;
        float scale = amp * global_scale;
        float r = static_cast<float>(s.r) * scale;
        float g = static_cast<float>(s.g) * scale;
        float b = static_cast<float>(s.b) * scale;

        if (s.mode == 1) {
            // Radiating line: draw a solid segment from origin to current position.
            float du = s.u - s.origin_u;
            float dm = s.minor - s.origin_minor;
            int steps = static_cast<int>(std::max(std::fabs(du), std::fabs(dm)) + 1.0f);
            if (steps < 1) steps = 1;
            for (int k = 0; k <= steps; ++k) {
                float t = static_cast<float>(k) / static_cast<float>(steps);
                float u = s.origin_u + du * t;
                float m = s.origin_minor + dm * t;
                size_t row = 0, col = 0;
                if (!map_to_rc(u, m, row, col)) continue;
                size_t idx = strip.index_for_row_col(row, col);
                if (idx < length) {
                    acc_r[idx] += r;
                    acc_g[idx] += g;
                    acc_b[idx] += b;
                }
            }
        } else if (s.mode == 3) {
            // Concentric circles: multiple rings around the origin that expand and fade over time.
            const int ring_count = 3;
            const int samples_per_ring = 32;
            float max_radius = static_cast<float>(major_len_) * 0.4f;
            if (max_radius < 1.0f) max_radius = 1.0f;
            // Use life_frac to drive expansion: start near center, grow to max_radius.
            float radial_phase = 1.0f - amp; // 0 at start, 1 near end
            if (radial_phase < 0.0f) radial_phase = 0.0f;
            if (radial_phase > 1.0f) radial_phase = 1.0f;

            for (int ri = 0; ri < ring_count; ++ri) {
                float ring_norm = static_cast<float>(ri + 1) / static_cast<float>(ring_count);
                float radius = max_radius * radial_phase * ring_norm;
                if (radius <= 0.0f) continue;
                for (int j = 0; j < samples_per_ring; ++j) {
                    float ang = 2.0f * static_cast<float>(M_PI) * static_cast<float>(j) / static_cast<float>(samples_per_ring);
                    float u = s.origin_u + std::sin(ang) * radius;
                    float m = s.origin_minor + std::cos(ang) * radius;
                    size_t row = 0, col = 0;
                    if (!map_to_rc(u, m, row, col)) continue;
                    size_t idx = strip.index_for_row_col(row, col);
                    if (idx < length) {
                        acc_r[idx] += r;
                        acc_g[idx] += g;
                        acc_b[idx] += b;
                    }
                }
            }
        } else {
            // Ring / solid / rain: single spark point, with optional "twinkle" for rain.
            size_t row = 0, col = 0;
            if (!map_to_rc(s.u, s.minor, row, col)) continue;
            size_t idx = strip.index_for_row_col(row, col);
            if (idx < length) {
                float rr = r, gg = g, bb = b;
                if (s.mode == 4) {
                    // Twinkle by modulating with a fast temporal sinusoid and a
                    // per-spark spatial phase; this looks good on RGB and degrades
                    // to on/off flicker on flip-dot.
                    float base = (s.origin_u * 13.0f + s.origin_minor * 7.0f);
                    float t = static_cast<float>(s.last_us - s.start_us) / 1'000'000.0f;
                    float tw = 0.5f + 0.5f * std::sin(base * 0.15f + t * 12.0f);
                    rr *= tw; gg *= tw; bb *= tw;
                }
                acc_r[idx] += rr;
                acc_g[idx] += gg;
                acc_b[idx] += bb;
            }
        }
    }

    // Write accumulated buffer to strip with clamping; pixels with no contributions fade smoothly.
    for (size_t i = 0; i < length; ++i) {
        float r = acc_r[i];
        float g = acc_g[i];
        float b = acc_b[i];
        if (r < 0.0f) r = 0.0f;
        if (g < 0.0f) g = 0.0f;
        if (b < 0.0f) b = 0.0f;
        if (r > 255.0f) r = 255.0f;
        if (g > 255.0f) g = 255.0f;
        if (b > 255.0f) b = 255.0f;
        strip.set_pixel(i,
                        static_cast<uint8_t>(r + 0.5f),
                        static_cast<uint8_t>(g + 0.5f),
                        static_cast<uint8_t>(b + 0.5f),
                        0);
    }
}

void FireworksPattern::update(LEDStrip& strip, uint64_t now_us) {
    update_geometry(strip);
    if (major_len_ == 0) return;

    if (brightness_percent_ <= 0) {
        // Hard off, but keep timing so we can resume quickly when brightness returns.
        size_t length = strip.length();
        for (size_t i = 0; i < length; ++i) {
            strip.set_pixel(i, 0, 0, 0, 0);
        }
        return;
    }

    ensure_rocket(now_us);
    update_rocket(now_us);
    update_sparks(now_us);
    render(strip);
}

} // namespace leds



