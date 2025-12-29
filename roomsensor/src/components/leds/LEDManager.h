#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "PsramAllocator.h"
#include "LEDConfig.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include "Color.h"

namespace config { class ConfigurationManager; class LEDConfig; }
namespace leds { class LEDStrip; class LEDPattern; }
namespace leds { class PowerManager; }

namespace leds {

// Coordinates multiple LED strips and their animation patterns.
class LEDManager {
public:
    LEDManager();
    ~LEDManager();

    // Initialize from the provided ConfigurationManager and start the update task.
    esp_err_t init(config::ConfigurationManager& cfg_manager);

    // Trigger a re-evaluation of configuration
    void refresh_configuration(config::ConfigurationManager& cfg_manager);

    // Expose managed strips for diagnostics/tests
    const std::vector<std::unique_ptr<LEDStrip>>& strips() const { return strips_; }

private:
    // Internal helpers
    std::unique_ptr<LEDStrip> create_strip(const config::LEDConfig& cfg, bool use_dma);
    std::unique_ptr<LEDPattern> create_pattern_from_config(const config::LEDConfig& cfg);
    void reconcile_with_config(config::ConfigurationManager& cfg_manager);
    void apply_pattern_updates_from_config(size_t idx, const config::LEDConfig& cfg, uint64_t now_us);

    // Task management
    static void UpdateTaskEntry(void* arg);
    void run_update_loop();

    // State
    config::ConfigurationManager* cfg_manager_ = nullptr;
    std::vector<std::unique_ptr<LEDStrip>> strips_;
    std::vector<std::unique_ptr<LEDPattern>> patterns_; 
    std::vector<std::unique_ptr<PowerManager>> power_mgrs_; 
    
    // Store per-strip frames in PSRAM
    std::vector<std::vector<Color, PsramAllocator<Color>>> prev_frames_; 
    std::vector<std::vector<Color, PsramAllocator<Color>>> scratch_frames_; 
    
    std::vector<bool> last_power_enabled_; 
    std::vector<uint64_t> power_on_hold_until_us_;
    std::vector<config::LEDConfig::Layout> last_layouts_;
    std::vector<config::LEDConfig::Pattern> last_patterns_;
    std::vector<uint32_t> last_generations_; 
    std::vector<std::vector<int>> last_enable_pins_;
    TaskHandle_t update_task_ = nullptr;
    int update_task_core_ = 1; 
    int update_task_priority_ = 3;
    uint32_t update_interval_us_ = 5'000; 

    std::vector<uint32_t> frames_tx_counts_;
    uint64_t last_telemetry_log_us_ = 0;
};

} // namespace leds
