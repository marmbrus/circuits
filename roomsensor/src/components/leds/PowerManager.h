#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace leds {

// Lightweight view of a frame for power decisions
struct FrameView {
    // RGBA buffer (rows*cols*4). For FLIPDOT, use R|G|B|W to indicate on/off intent per logical dot.
    const uint8_t* rgba = nullptr;
    size_t rows = 0;
    size_t cols = 0;
};

// Abstract power/refresh policy
class PowerManager {
public:
    virtual ~PowerManager() = default;

    // Called with current and previous logical frames (before encoding), and current timestamp (us)
    // Returns true if a refresh should be issued now
    virtual bool on_frame(const FrameView& current,
                          const FrameView& previous,
                          uint64_t now_us) = 0;

    // Whether the power enable pin should be high (true) or low (false)
    virtual bool power_enabled() const = 0;
};

// LEDs policy: enable when any pixel non-black; refresh whenever asked by caller (on_frame returns true)
class LedPower final : public PowerManager {
public:
    bool on_frame(const FrameView& current,
                  const FrameView& previous,
                  uint64_t now_us) override {
        (void)previous; (void)now_us;
        enabled_ = any_on(current);
        // For LEDs, pattern/manager cadence governs refresh; do not force here
        return false;
    }

    bool power_enabled() const override { return enabled_; }

private:
    static bool any_on(const FrameView& f) {
        if (!f.rgba) return false;
        size_t total = f.rows * f.cols * 4;
        for (size_t i = 0; i < total; ++i) if (f.rgba[i] != 0) return true;
        return false;
    }
    bool enabled_ = false;
};

// FlipDot policy:
// - enable whenever any pixel state changed between frames
// - refresh only on change or every 5s
// - disable after 30s of no changes
class FlipDotPower final : public PowerManager {
public:
    bool on_frame(const FrameView& current,
                  const FrameView& previous,
                  uint64_t now_us) override {
        bool changed = frame_differs(current, previous);
        if (changed) last_change_us_ = now_us;

        // Power state
        enabled_ = (now_us - last_change_us_) < off_after_us_;

        // Refresh gating
        bool due_heartbeat = (now_us - last_refresh_us_) >= heartbeat_us_;
        bool do_refresh = changed || due_heartbeat;
        if (do_refresh) last_refresh_us_ = now_us;
        return do_refresh;
    }

    bool power_enabled() const override { return enabled_; }

private:
    static bool frame_differs(const FrameView& a, const FrameView& b) {
        if (a.rows != b.rows || a.cols != b.cols || !a.rgba || !b.rgba) return true;
        size_t n = a.rows * a.cols * 4;
        for (size_t i = 0; i < n; ++i) if (a.rgba[i] != b.rgba[i]) return true;
        return false;
    }

    bool enabled_ = false;
    uint64_t last_change_us_ = 0;
    uint64_t last_refresh_us_ = 0;
    static constexpr uint64_t heartbeat_us_ = 5ull * 1000ull * 1000ull;  // 5 seconds
    static constexpr uint64_t off_after_us_ = 30ull * 1000ull * 1000ull; // 30 seconds
};

} // namespace leds


