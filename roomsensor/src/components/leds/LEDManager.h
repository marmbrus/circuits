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

namespace config { class ConfigurationManager; class LEDConfig; }
namespace leds { class LEDStrip; class LEDPattern; }
namespace leds { class PowerManager; }

namespace leds {

// Coordinates multiple LED strips and their animation patterns.
// - Initializes from config::ConfigurationManager (up to four strips)
// - Chooses which strip should use DMA (by default the longest strip), and can reassign at runtime
// - DMA management is centralized: reassigning DMA frees the existing RMT channel and allocates a new one
//   with DMA on the selected strip (and non-DMA on others). The reconfiguration is triggered inside the
//   update loop when needed, not from individual strips.
// - Owns a pinned FreeRTOS task on the APP CPU to periodically update patterns and flush strips
// - Avoids pattern updates while a transmit is in-flight; prioritizes strips that are not backpressured
class LEDManager {
public:
    LEDManager();
    ~LEDManager();

    // Initialize from the provided ConfigurationManager and start the update task.
    // Returns ESP_OK on success. This does not block; the update task runs independently.
    esp_err_t init(config::ConfigurationManager& cfg_manager);

    // Trigger a re-evaluation of configuration (e.g., after MQTT update). The manager may:
    // - Recreate strips when hardware parameters change (pin/chip/length)
    // - Reallocate DMA to a different strip
    // - Swap patterns if pattern name/speed/brightness/colors change
    // Note: There is no push callback from ConfigurationManager; we deliberately poll config at the start
    // of each update tick. Polling is cheap and avoids concurrency issues with callbacks.
    void refresh_configuration(config::ConfigurationManager& cfg_manager);

    // Expose managed strips for diagnostics/tests; ownership remains with manager
    const std::vector<std::unique_ptr<LEDStrip>>& strips() const { return strips_; }

private:
    // Internal helpers
    std::unique_ptr<LEDStrip> create_strip(const config::LEDConfig& cfg, bool use_dma);
    std::unique_ptr<LEDPattern> create_pattern_from_config(const config::LEDConfig& cfg);
    void reconcile_with_config(config::ConfigurationManager& cfg_manager);
    void apply_pattern_updates_from_config(size_t idx, const config::LEDConfig& cfg, uint64_t now_us);

    // Task management
    static void UpdateTaskEntry(void* arg); // FreeRTOS C entry point
    void run_update_loop();                 // instance method executed by the task

    // State
    config::ConfigurationManager* cfg_manager_ = nullptr; // not owned
    std::vector<std::unique_ptr<LEDStrip>> strips_;
    std::vector<std::unique_ptr<LEDPattern>> patterns_; // 1:1 with strips_
    std::vector<std::unique_ptr<PowerManager>> power_mgrs_; // 1:1 with strips_
    // Store per-strip RGBA frames in PSRAM to free internal RAM
    std::vector<std::vector<uint8_t, PsramAllocator<uint8_t>>> prev_frames_rgba_; // rows*cols*4 per strip
    std::vector<std::vector<uint8_t, PsramAllocator<uint8_t>>> scratch_frames_rgba_; // reusable buffer for current frame
    std::vector<bool> last_power_enabled_; // track power pin state to log transitions
    // Per-strip timestamp (us) until which we should hold transmissions after power-on
    std::vector<uint64_t> power_on_hold_until_us_;
    // Track layout used for each strip to detect when grid mapping changes
    std::vector<config::LEDConfig::Layout> last_layouts_;
    // Track last applied pattern to avoid reinstalling the same type repeatedly
    std::vector<config::LEDConfig::Pattern> last_patterns_;
    std::vector<uint32_t> last_generations_; // per-strip generation snapshot
    TaskHandle_t update_task_ = nullptr;
    int update_task_core_ = 1; // APP CPU on ESP32-S3
    int update_task_priority_ = 1; // keep near idle to avoid starving IDLE task on APP CPU
    // Update loop cadence and behavior:
    // - Each tick: poll configuration; if changed, rebuild strips/patterns and reassign DMA centrally
    // - For each strip: if not transmitting, invoke pattern.update(now); then attempt flush_if_dirty(now)
    // - Use RMT events to call on_transmit_complete and to pace patterns (skip updates while busy)
    // - Ensure a forced refresh at least every ~10s to recover from transient glitches
    uint32_t update_interval_us_ = 5'000; // default cadence; pattern may skip if transmitting


    // Per-strip frame counters for periodic telemetry
    std::vector<uint32_t> frames_tx_counts_;
    uint64_t last_telemetry_log_us_ = 0;
};

} // namespace leds


