#include "LEDTransition.h"
#include "LEDStrip.h"
#include "LEDPattern.h"
#include "LEDBuffer.h"
#include <algorithm>
#include <cstring>

namespace leds {

SweepTransition::SweepTransition() 
    : speed_percent_(50), start_time_us_(0), strip_length_(0), last_transitioned_led_(0) {
}

void SweepTransition::start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    start_time_us_ = now_us;
    strip_length_ = strip.length();
    // Start with no LEDs transitioned (all showing old pattern)
    // We'll sweep from highest index (strip_length_ - 1) down to 0
    last_transitioned_led_ = strip_length_; // One past the highest index means none transitioned yet
    
    // Initialize both patterns
    from_pattern.reset(strip, now_us);
    to_pattern.reset(strip, now_us);
}

bool SweepTransition::update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    if (strip_length_ == 0) return true; // Empty strip, transition complete
    
    uint64_t elapsed_us = now_us - start_time_us_;
    uint64_t current_duration = calculate_duration_us();
    
    // Check if transition is complete
    if (elapsed_us >= current_duration) {
        // Transition complete - all LEDs should show new pattern
        to_pattern.update(strip, now_us);
        return true;
    }
    
    // Calculate how many LEDs should be showing the new pattern based on elapsed time
    // We sweep from highest index (strip_length_ - 1) down to 0
    float progress = static_cast<float>(elapsed_us) / static_cast<float>(current_duration);
    size_t leds_transitioned = static_cast<size_t>(progress * strip_length_);
    
    // Ensure we don't exceed strip length
    if (leds_transitioned > strip_length_) {
        leds_transitioned = strip_length_;
    }
    
    // Create buffers that match the strip properties for pattern rendering
    LEDBuffer old_pattern_buffer(strip);
    LEDBuffer new_pattern_buffer(strip);
    
    // Render patterns to buffers without affecting the actual strip
    from_pattern.update(old_pattern_buffer, now_us);
    to_pattern.update(new_pattern_buffer, now_us);
    
    // Calculate the transition boundary
    size_t transition_boundary = (leds_transitioned < strip_length_) ? 
        (strip_length_ - leds_transitioned) : 0;
    
    // Now compose the final result and write to the strip - this is the ONLY write to the actual strip
    for (size_t i = 0; i < strip_length_; ++i) {
        uint8_t r, g, b, w;
        
        if (i >= transition_boundary) {
            // This LED should show the new pattern
            new_pattern_buffer.get_pixel(i, r, g, b, w);
        } else {
            // This LED should show the old pattern
            old_pattern_buffer.get_pixel(i, r, g, b, w);
        }
        
        strip.set_pixel(i, r, g, b, w);
    }
    
    last_transitioned_led_ = leds_transitioned;
    
    // Transition is not complete
    return false;
}

void SweepTransition::set_speed(int speed_percent) {
    if (speed_percent < 1) speed_percent = 1;   // Minimum speed to prevent division by zero
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

uint64_t SweepTransition::duration_us() const {
    return calculate_duration_us();
}

uint64_t SweepTransition::calculate_duration_us() const {
    // Base duration is 2 seconds (2,000,000 microseconds)
    // Speed 1 = 4 seconds (slowest), Speed 100 = 0.5 seconds (fastest)
    // Formula: duration = base_duration * (101 - speed) / 100
    // This gives us a range from 4 seconds (speed=1) to 0.02 seconds (speed=100)
    const uint64_t base_duration_us = 2000000; // 2 seconds
    return (base_duration_us * (101 - speed_percent_)) / 50; // Adjusted for better range
}

// BacksweepTransition implementation
BacksweepTransition::BacksweepTransition() 
    : speed_percent_(50), start_time_us_(0), strip_length_(0), last_transitioned_led_(0) {
}

void BacksweepTransition::start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    start_time_us_ = now_us;
    strip_length_ = strip.length();
    // Start with no LEDs transitioned (all showing old pattern)
    // We'll sweep from lowest index (0) up to highest (strip_length_ - 1)
    last_transitioned_led_ = 0; // Start from index 0
    
    // Initialize both patterns
    from_pattern.reset(strip, now_us);
    to_pattern.reset(strip, now_us);
}

bool BacksweepTransition::update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    if (strip_length_ == 0) return true; // Empty strip, transition complete
    
    uint64_t elapsed_us = now_us - start_time_us_;
    uint64_t current_duration = calculate_duration_us();
    
    // Check if transition is complete
    if (elapsed_us >= current_duration) {
        // Transition complete - all LEDs should show new pattern
        to_pattern.update(strip, now_us);
        return true;
    }
    
    // Calculate how many LEDs should be showing the new pattern based on elapsed time
    // We sweep from lowest index (0) up to highest (strip_length_ - 1)
    float progress = static_cast<float>(elapsed_us) / static_cast<float>(current_duration);
    size_t leds_transitioned = static_cast<size_t>(progress * strip_length_);
    
    // Ensure we don't exceed strip length
    if (leds_transitioned > strip_length_) {
        leds_transitioned = strip_length_;
    }
    
    // Create buffers that match the strip properties for pattern rendering
    LEDBuffer old_pattern_buffer(strip);
    LEDBuffer new_pattern_buffer(strip);
    
    // Render patterns to buffers without affecting the actual strip
    from_pattern.update(old_pattern_buffer, now_us);
    to_pattern.update(new_pattern_buffer, now_us);
    
    // For backsweep: LEDs from 0 to (leds_transitioned - 1) show new pattern
    // LEDs from leds_transitioned to (strip_length_ - 1) show old pattern
    
    // Now compose the final result and write to the strip - this is the ONLY write to the actual strip
    for (size_t i = 0; i < strip_length_; ++i) {
        uint8_t r, g, b, w;
        
        if (i < leds_transitioned) {
            // This LED should show the new pattern (backsweep: low indices first)
            new_pattern_buffer.get_pixel(i, r, g, b, w);
        } else {
            // This LED should show the old pattern
            old_pattern_buffer.get_pixel(i, r, g, b, w);
        }
        
        strip.set_pixel(i, r, g, b, w);
    }
    
    last_transitioned_led_ = leds_transitioned;
    
    // Transition is not complete
    return false;
}

void BacksweepTransition::set_speed(int speed_percent) {
    if (speed_percent < 1) speed_percent = 1;   // Minimum speed to prevent division by zero
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

uint64_t BacksweepTransition::duration_us() const {
    return calculate_duration_us();
}

uint64_t BacksweepTransition::calculate_duration_us() const {
    // Same speed calculation as SweepTransition
    const uint64_t base_duration_us = 2000000; // 2 seconds
    return (base_duration_us * (101 - speed_percent_)) / 50; // Adjusted for better range
}

// ExpandTransition implementation
ExpandTransition::ExpandTransition() 
    : speed_percent_(50), start_time_us_(0), strip_length_(0), center_index_(0), last_transitioned_radius_(0) {
}

void ExpandTransition::start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    start_time_us_ = now_us;
    strip_length_ = strip.length();
    center_index_ = strip_length_ / 2; // Integer division gives us the center (or center-left for even lengths)
    last_transitioned_radius_ = 0; // Start with no radius (just center pixel)
    
    // Initialize both patterns
    from_pattern.reset(strip, now_us);
    to_pattern.reset(strip, now_us);
}

bool ExpandTransition::update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    if (strip_length_ == 0) return true; // Empty strip, transition complete
    
    uint64_t elapsed_us = now_us - start_time_us_;
    uint64_t current_duration = calculate_duration_us();
    
    // Check if transition is complete
    if (elapsed_us >= current_duration) {
        // Transition complete - all LEDs should show new pattern
        to_pattern.update(strip, now_us);
        return true;
    }
    
    // Calculate how far from center the transition should have reached
    float progress = static_cast<float>(elapsed_us) / static_cast<float>(current_duration);
    // Maximum radius is the distance from center to the farthest edge
    size_t max_radius = (strip_length_ % 2 == 0) ? 
        std::max(center_index_, strip_length_ - center_index_ - 1) :  // Even length
        center_index_;  // Odd length: center to either edge is the same
    
    size_t current_radius = static_cast<size_t>(progress * (max_radius + 1)); // +1 to include the full radius
    
    // Create buffers that match the strip properties for pattern rendering
    LEDBuffer old_pattern_buffer(strip);
    LEDBuffer new_pattern_buffer(strip);
    
    // Render patterns to buffers without affecting the actual strip
    from_pattern.update(old_pattern_buffer, now_us);
    to_pattern.update(new_pattern_buffer, now_us);
    
    // For expand: LEDs within current_radius of center show new pattern
    // LEDs outside current_radius show old pattern
    
    // Now compose the final result and write to the strip - this is the ONLY write to the actual strip
    for (size_t i = 0; i < strip_length_; ++i) {
        uint8_t r, g, b, w;
        
        // Calculate distance from center
        size_t distance_from_center = (i <= center_index_) ? 
            (center_index_ - i) : (i - center_index_);
        
        if (distance_from_center <= current_radius) {
            // This LED should show the new pattern (within expansion radius)
            new_pattern_buffer.get_pixel(i, r, g, b, w);
        } else {
            // This LED should show the old pattern
            old_pattern_buffer.get_pixel(i, r, g, b, w);
        }
        
        strip.set_pixel(i, r, g, b, w);
    }
    
    last_transitioned_radius_ = current_radius;
    
    // Transition is not complete
    return false;
}

void ExpandTransition::set_speed(int speed_percent) {
    if (speed_percent < 1) speed_percent = 1;   // Minimum speed to prevent division by zero
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

uint64_t ExpandTransition::duration_us() const {
    return calculate_duration_us();
}

uint64_t ExpandTransition::calculate_duration_us() const {
    // Same speed calculation as other transitions
    const uint64_t base_duration_us = 2000000; // 2 seconds
    return (base_duration_us * (101 - speed_percent_)) / 50; // Adjusted for better range
}

// ContractTransition implementation
ContractTransition::ContractTransition() 
    : speed_percent_(50), start_time_us_(0), strip_length_(0), center_index_(0), last_transitioned_radius_(0) {
}

void ContractTransition::start(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    start_time_us_ = now_us;
    strip_length_ = strip.length();
    center_index_ = strip_length_ / 2; // Integer division gives us the center (or center-left for even lengths)
    // Start with maximum radius (all LEDs showing old pattern)
    last_transitioned_radius_ = (strip_length_ % 2 == 0) ? 
        std::max(center_index_, strip_length_ - center_index_ - 1) :  // Even length
        center_index_;  // Odd length: center to either edge is the same
    
    // Initialize both patterns
    from_pattern.reset(strip, now_us);
    to_pattern.reset(strip, now_us);
}

bool ContractTransition::update(LEDStrip& strip, LEDPattern& from_pattern, LEDPattern& to_pattern, uint64_t now_us) {
    if (strip_length_ == 0) return true; // Empty strip, transition complete
    
    uint64_t elapsed_us = now_us - start_time_us_;
    uint64_t current_duration = calculate_duration_us();
    
    // Check if transition is complete
    if (elapsed_us >= current_duration) {
        // Transition complete - all LEDs should show new pattern
        to_pattern.update(strip, now_us);
        return true;
    }
    
    // Calculate how far from edges the transition should have reached (contracting inward)
    float progress = static_cast<float>(elapsed_us) / static_cast<float>(current_duration);
    // Maximum radius is the distance from center to the farthest edge
    size_t max_radius = (strip_length_ % 2 == 0) ? 
        std::max(center_index_, strip_length_ - center_index_ - 1) :  // Even length
        center_index_;  // Odd length: center to either edge is the same
    
    // For contract, we start at max_radius and shrink inward
    size_t current_radius = max_radius - static_cast<size_t>(progress * (max_radius + 1));
    
    // Create buffers that match the strip properties for pattern rendering
    LEDBuffer old_pattern_buffer(strip);
    LEDBuffer new_pattern_buffer(strip);
    
    // Render patterns to buffers without affecting the actual strip
    from_pattern.update(old_pattern_buffer, now_us);
    to_pattern.update(new_pattern_buffer, now_us);
    
    // For contract: LEDs outside current_radius from center show new pattern
    // LEDs within current_radius show old pattern
    
    // Now compose the final result and write to the strip - this is the ONLY write to the actual strip
    for (size_t i = 0; i < strip_length_; ++i) {
        uint8_t r, g, b, w;
        
        // Calculate distance from center
        size_t distance_from_center = (i <= center_index_) ? 
            (center_index_ - i) : (i - center_index_);
        
        if (distance_from_center > current_radius) {
            // This LED should show the new pattern (outside contraction radius)
            new_pattern_buffer.get_pixel(i, r, g, b, w);
        } else {
            // This LED should show the old pattern
            old_pattern_buffer.get_pixel(i, r, g, b, w);
        }
        
        strip.set_pixel(i, r, g, b, w);
    }
    
    last_transitioned_radius_ = current_radius;
    
    // Transition is not complete
    return false;
}

void ContractTransition::set_speed(int speed_percent) {
    if (speed_percent < 1) speed_percent = 1;   // Minimum speed to prevent division by zero
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

uint64_t ContractTransition::duration_us() const {
    return calculate_duration_us();
}

uint64_t ContractTransition::calculate_duration_us() const {
    // Same speed calculation as other transitions
    const uint64_t base_duration_us = 2000000; // 2 seconds
    return (base_duration_us * (101 - speed_percent_)) / 50; // Adjusted for better range
}

// Factory functions
std::unique_ptr<LEDTransition> create_transition(TransitionType type) {
    switch (type) {
        case TransitionType::SWEEP:
            return std::make_unique<SweepTransition>();
        case TransitionType::BACKSWEEP:
            return std::make_unique<BacksweepTransition>();
        case TransitionType::EXPAND:
            return std::make_unique<ExpandTransition>();
        case TransitionType::CONTRACT:
            return std::make_unique<ContractTransition>();
        default:
            return std::make_unique<SweepTransition>(); // Default fallback
    }
}

const char* transition_type_to_string(TransitionType type) {
    switch (type) {
        case TransitionType::SWEEP: return "SWEEP";
        case TransitionType::BACKSWEEP: return "BACKSWEEP";
        case TransitionType::EXPAND: return "EXPAND";
        case TransitionType::CONTRACT: return "CONTRACT";
        default: return "SWEEP";
    }
}

TransitionType parse_transition_type(const char* str) {
    if (!str) return TransitionType::SWEEP;
    if (strcmp(str, "SWEEP") == 0) return TransitionType::SWEEP;
    if (strcmp(str, "BACKSWEEP") == 0) return TransitionType::BACKSWEEP;
    if (strcmp(str, "EXPAND") == 0) return TransitionType::EXPAND;
    if (strcmp(str, "CONTRACT") == 0) return TransitionType::CONTRACT;
    return TransitionType::SWEEP; // Default fallback
}

} // namespace leds
