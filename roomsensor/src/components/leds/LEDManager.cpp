#include "LEDManager.h"
#include "LEDStripRmt.h"
#include "LEDPattern.h"
#include "OffPattern.h"
#include "SolidPattern.h"
#include "FadePattern.h"
#include "RainbowPattern.h"
#include "StatusPattern.h"
#include "ConfigurationManager.h"
#include "LEDConfig.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

namespace leds {

using config::LEDConfig;
static const char* TAG = "LEDManager";

LEDManager::LEDManager() = default;
LEDManager::~LEDManager() = default;

static leds::LEDChip ToLEDChip(config::LEDConfig::Chip c) {
    switch (c) {
        case config::LEDConfig::Chip::WS2812: return leds::LEDChip::WS2812;
        case config::LEDConfig::Chip::SK6812: return leds::LEDChip::SK6812;
        default: return leds::LEDChip::WS2812;
    }
}

static const char* pattern_name_for_config(config::LEDConfig::Pattern p) {
    using P = config::LEDConfig::Pattern;
    switch (p) {
        case P::OFF: return "OFF";
        case P::SOLID: return "SOLID";
        case P::FADE: return "FADE";
        case P::STATUS: return "STATUS";
        case P::RAINBOW: return "RAINBOW";
        case P::INVALID: default: return "OFF";
    }
}

esp_err_t LEDManager::init(config::ConfigurationManager& cfg_manager) {
    cfg_manager_ = &cfg_manager;
    ESP_LOGI(TAG, "Initializing LEDManager");
    refresh_configuration(cfg_manager);
    // create update task pinned to APP core
    // Give the task a small stack and low priority so it never starves IDLE1 on CPU1 (APP core)
    BaseType_t ok = xTaskCreatePinnedToCore(
        &LEDManager::UpdateTaskEntry, "led-update", 6144, this, update_task_priority_, &update_task_, update_task_core_);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED update task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void LEDManager::refresh_configuration(config::ConfigurationManager& cfg_manager) {
    // Build strips array from active LED configs (clean slate)
    std::vector<LEDConfig*> active = cfg_manager.active_leds();
    strips_.clear();
    patterns_.clear();
    frames_tx_counts_.assign(active.size(), 0);
    strips_.reserve(active.size());
    patterns_.reserve(active.size());

    ESP_LOGI(TAG, "Config refresh: %zu active strips", active.size());
    for (LEDConfig* c : active) {
        ESP_LOGI(TAG, "Strip config: name=%s gpio=%d enable_gpio=%d chip=%s size=%dx%d dma=%s pattern=%s", c->name(),
                 c->has_data_gpio() ? c->data_gpio() : -1,
                 c->has_enabled_gpio() ? c->enabled_gpio() : -1,
                 c->chip().c_str(), c->num_columns(), c->num_rows(),
                 c->has_dma() ? (c->dma() ? "true" : "false") : "unset",
                 c->has_pattern() ? c->pattern().c_str() : "<unset>");
    }

    // Determine DMA strip (longest by default, or explicit) and build in optimal order.
    // First, pick index of DMA candidate in 'active'
    size_t dma_idx = 0;
    size_t longest_len = 0;
    for (size_t i = 0; i < active.size(); ++i) {
        size_t len = static_cast<size_t>(active[i]->num_columns() * active[i]->num_rows());
        if (len > longest_len) { longest_len = len; dma_idx = i; }
    }

    // Create DMA strip first with larger buffer; then create others
    auto create_one = [&](LEDConfig* c, bool use_dma) {
        LEDStripRmt::CreateParams p;
        p.gpio = c->data_gpio();
        p.enable_gpio = c->has_enabled_gpio() ? c->enabled_gpio() : -1;
        p.length = static_cast<size_t>(c->num_columns() * c->num_rows());
        p.chip = ToLEDChip(c->chip_enum());
        p.use_dma = use_dma;
        // For DMA, use larger buffer to minimize underflows
        p.mem_block_symbols = use_dma ? 256 : 48;
        auto s = LEDStripRmt::Create(p);
        if (!s) {
            ESP_LOGE(TAG, "Failed to create strip on GPIO %d (dma=%d)", p.gpio, (int)use_dma);
        }
        return s;
    };

    // Clear previous (already cleared above); build in optimal order
    std::vector<LEDConfig*> built_cfgs;
    if (!active.empty()) {
        // DMA first
        if (auto s = create_one(active[dma_idx], true)) {
            strips_.push_back(std::move(s));
            patterns_.push_back(create_pattern_from_config(*active[dma_idx]));
            built_cfgs.push_back(active[dma_idx]);
        }
        // Others
        for (size_t i = 0; i < active.size(); ++i) {
            if (i == dma_idx) continue;
            if (auto s = create_one(active[i], false)) {
                strips_.push_back(std::move(s));
                patterns_.push_back(create_pattern_from_config(*active[i]));
                built_cfgs.push_back(active[i]);
            }
        }
    }

    // Initial pattern application
    uint64_t now = esp_timer_get_time();
    for (size_t i = 0; i < strips_.size(); ++i) {
        if (i < built_cfgs.size()) apply_pattern_updates_from_config(i, *built_cfgs[i], now);
    }
}

std::unique_ptr<LEDStrip> LEDManager::create_strip_from_config(const config::LEDConfig& cfg) {
    LEDStripRmt::CreateParams p;
    p.gpio = cfg.data_gpio();
    p.enable_gpio = cfg.has_enabled_gpio() ? cfg.enabled_gpio() : -1;
    p.length = static_cast<size_t>(cfg.num_columns() * cfg.num_rows());
    p.chip = ToLEDChip(cfg.chip_enum());
    p.use_dma = cfg.has_dma() ? cfg.dma() : false; // manager may override in assign_dma_channels
    auto s = LEDStripRmt::Create(p);
    if (!s) ESP_LOGE(TAG, "Failed to create strip on GPIO %d", p.gpio);
    return s;
}

// For initial implementation, return nullptr; concrete patterns will be added in a later step.
std::unique_ptr<LEDPattern> LEDManager::create_pattern_from_config(const config::LEDConfig& cfg) {
    using P = config::LEDConfig::Pattern;
    P pat = cfg.pattern_enum();
    std::unique_ptr<LEDPattern> p;
    switch (pat) {
        case P::OFF: p.reset(new OffPattern()); break;
        case P::SOLID: p.reset(new SolidPattern()); break;
        case P::FADE: p.reset(new FadePattern()); break;
        case P::STATUS: p.reset(new StatusPattern()); break;
        case P::RAINBOW: p.reset(new RainbowPattern()); break;
        case P::INVALID: default: p.reset(new OffPattern()); break;
    }
    return p;
}

// assign_dma_channels removed; DMA is decided at build time in refresh_configuration

void LEDManager::UpdateTaskEntry(void* arg) {
    static_cast<LEDManager*>(arg)->run_update_loop();
}

void LEDManager::run_update_loop() {
    uint64_t last_cfg_check = 0;
    const uint64_t cfg_check_period_us = cfg_poll_period_us_;
    TickType_t tick_delay = pdMS_TO_TICKS(update_interval_us_ / 1000);
    if (tick_delay == 0) tick_delay = 1; // ensure at least one tick to yield CPU
    while (true) {
        uint64_t now = esp_timer_get_time();
        // Periodically log tick rate and yield behavior to diagnose WDT issues
        static uint32_t loop_count = 0;
        if ((loop_count++ % 2000u) == 0u) {
            ESP_LOGD(TAG, "update loop tick; delay_ticks=%u, strips=%u", (unsigned)tick_delay, (unsigned)strips_.size());
        }
        if (now - last_cfg_check > cfg_check_period_us) {
            last_cfg_check = now;
            if (cfg_manager_) {
                reconcile_with_config(*cfg_manager_);
            }
        }

        // Update patterns and flush strips. Skip pattern update if strip is transmitting.
        for (size_t i = 0; i < strips_.size(); ++i) {
            LEDStrip* s = strips_[i].get();
            LEDPattern* p = (i < patterns_.size()) ? patterns_[i].get() : nullptr;
            if (!s) continue;
            if (!s->is_transmitting() && p) p->update(*s, now);
            if (s->flush_if_dirty(now)) {
                if (i < frames_tx_counts_.size()) frames_tx_counts_[i]++;
            }
        }

        // Periodic telemetry/logging: once per minute
        if (now - last_telemetry_log_us_ > 60ull * 1000 * 1000) {
            last_telemetry_log_us_ = now;
            for (size_t i = 0; i < strips_.size(); ++i) {
                unsigned idx = static_cast<unsigned>(i);
                unsigned frames = (i < frames_tx_counts_.size()) ? static_cast<unsigned>(frames_tx_counts_[i]) : 0u;
                ESP_LOGI(TAG, "Frames TX (last minute window) strip %u: %u", idx, frames);
            }
            std::fill(frames_tx_counts_.begin(), frames_tx_counts_.end(), 0);
        }

        // Sleep until next tick
        vTaskDelay(tick_delay);
    }
}

void LEDManager::apply_pattern_updates_from_config(size_t idx, const config::LEDConfig& cfg, uint64_t now_us) {
    if (idx >= strips_.size()) return;
    LEDStrip* strip = strips_[idx].get();
    LEDPattern* pat = (idx < patterns_.size()) ? patterns_[idx].get() : nullptr;
    if (!strip) return;

    // Update existing pattern or (re)create if type changed
    bool type_mismatch = false;
    if (pat) {
        // Compare by reported name; simple and sufficient
        const char* desired_name = pattern_name_for_config(cfg.pattern_enum());
        if (strcmp(pat->name(), desired_name) != 0) type_mismatch = true;
    } else {
        type_mismatch = true;
    }

    if (type_mismatch) {
        patterns_[idx] = create_pattern_from_config(cfg);
        pat = patterns_[idx].get();
        if (pat) {
            // Apply all dynamic knobs regardless of pattern type; patterns may ignore what they don't use
            pat->set_speed_percent(cfg.has_speed() ? cfg.speed() : 50);
            pat->set_solid_color(cfg.has_r() ? cfg.r() : 0,
                                 cfg.has_g() ? cfg.g() : 0,
                                 cfg.has_b() ? cfg.b() : 0,
                                 cfg.has_w() ? cfg.w() : 0);
            if (cfg.has_brightness()) pat->set_brightness_percent(cfg.brightness());
            pat->reset(*strip, now_us);
            // Render first frame with correct parameters
            pat->update(*strip, now_us);
        }
        ESP_LOGI(TAG, "Pattern swapped for strip %u -> %s", (unsigned)idx, pat ? pat->name() : "<null>");
    } else if (pat) {
        // Apply runtime knobs
        pat->set_speed_percent(cfg.has_speed() ? cfg.speed() : 50);
        pat->set_solid_color(cfg.has_r() ? cfg.r() : 0,
                             cfg.has_g() ? cfg.g() : 0,
                             cfg.has_b() ? cfg.b() : 0,
                             cfg.has_w() ? cfg.w() : 0);
        if (cfg.has_brightness()) pat->set_brightness_percent(cfg.brightness());
    }
}

void LEDManager::reconcile_with_config(config::ConfigurationManager& cfg_manager) {
    auto active = cfg_manager.active_leds();
    // Big change: number of active strips or any hardware mismatch (gpio, enable gpio, chip, size)
    bool big_change = (active.size() != strips_.size());
    if (!big_change) {
        for (size_t i = 0; i < active.size(); ++i) {
            auto* c = active[i];
            LEDStrip* s = strips_[i].get();
            if (!c || !s) { big_change = true; break; }
            size_t desired_len = static_cast<size_t>(c->num_columns() * c->num_rows());
            if (s->pin() != (c->has_data_gpio() ? c->data_gpio() : -1) ||
                s->length() != desired_len) { big_change = true; break; }
        }
    }

    if (big_change) {
        ESP_LOGI(TAG, "Detected hardware-level config change; rebuilding strips");
        refresh_configuration(cfg_manager);
        return;
    }

    // Small change: pattern/speed/colors. Apply in place.
    uint64_t now = esp_timer_get_time();
    for (size_t i = 0; i < active.size(); ++i) {
        apply_pattern_updates_from_config(i, *active[i], now);
    }
}

} // namespace leds


