#pragma once

#include <cstdint>
#include <memory>

namespace leds {

class LEDStrip;
class LEDPattern;

// Abstract base class for LED pattern transitions.
// A transition manages the change from one pattern to another over time.
class LEDTransition {
public:
    virtual ~LEDTransition() = default;

    // A short name for diagnostics
    virtual const char* name() const = 0;

    // Initialize the transition with source and destination patterns
    // Called when the transition begins
    virtual void start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) = 0;

    // Update the transition state and render the current frame
    // Returns true if the transition is complete, false if still in progress
    virtual bool update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) = 0;

    // Set the transition speed (0-100, where 100 is fastest)
    virtual void set_speed(int speed_percent) = 0;

    // Get the current duration of the transition in microseconds (based on speed)
    virtual uint64_t duration_us() const = 0;
};

// Sweep transition: changes patterns one LED at a time from highest to lowest index
class SweepTransition final : public LEDTransition {
public:
    SweepTransition();

    const char* name() const override { return "SWEEP"; }
    void start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    bool update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    void set_speed(int speed_percent) override;
    uint64_t duration_us() const override;

private:
    int speed_percent_ = 50; // 0-100, 50 = medium speed
    uint64_t start_time_us_;
    size_t strip_length_;
    size_t last_transitioned_led_; // Index of the last LED that was transitioned to new pattern
    
    uint64_t calculate_duration_us() const;
};

// Backsweep transition: changes patterns one LED at a time from lowest to highest index
class BacksweepTransition final : public LEDTransition {
public:
    BacksweepTransition();

    const char* name() const override { return "BACKSWEEP"; }
    void start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    bool update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    void set_speed(int speed_percent) override;
    uint64_t duration_us() const override;

private:
    int speed_percent_ = 50; // 0-100, 50 = medium speed
    uint64_t start_time_us_;
    size_t strip_length_;
    size_t last_transitioned_led_; // Index of the last LED that was transitioned to new pattern
    
    uint64_t calculate_duration_us() const;
};

// Expand transition: changes patterns from center outward to both edges simultaneously
class ExpandTransition final : public LEDTransition {
public:
    ExpandTransition();

    const char* name() const override { return "EXPAND"; }
    void start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    bool update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    void set_speed(int speed_percent) override;
    uint64_t duration_us() const override;

private:
    int speed_percent_ = 50; // 0-100, 50 = medium speed
    uint64_t start_time_us_;
    size_t strip_length_;
    size_t center_index_;
    size_t last_transitioned_radius_; // How far from center the transition has reached
    
    uint64_t calculate_duration_us() const;
};

// Contract transition: changes patterns from both edges inward to center simultaneously
class ContractTransition final : public LEDTransition {
public:
    ContractTransition();

    const char* name() const override { return "CONTRACT"; }
    void start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    bool update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) override;
    void set_speed(int speed_percent) override;
    uint64_t duration_us() const override;

private:
    int speed_percent_ = 50; // 0-100, 50 = medium speed
    uint64_t start_time_us_;
    size_t strip_length_;
    size_t center_index_;
    size_t last_transitioned_radius_; // How far from edges the transition has reached
    
    uint64_t calculate_duration_us() const;
};

// Transition factory for creating different transition types
enum class TransitionType {
    SWEEP,
    BACKSWEEP,
    EXPAND,
    CONTRACT
};

std::unique_ptr<LEDTransition> create_transition(TransitionType type);
const char* transition_type_to_string(TransitionType type);
TransitionType parse_transition_type(const char* str);

} // namespace leds
