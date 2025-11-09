#include "LEDManager.h"
#include "LEDStripRmt.h"
#include "LEDPattern.h"
#include "LEDStripSurfaceAdapter.h"
#include "LEDWireEncoderWS2812.h"
#include "LEDWireEncoderSK6812.h"
#include "LEDWireEncoderWS2814.h"
#include "LEDWireEncoderFlipdot.h"
#include "LEDCoordinateMapperRowMajor.h"
#include "LEDCoordinateMapperSerpentineRow.h"
#include "LEDCoordinateMapperSerpentineColumn.h"
#include "LEDCoordinateMapperColumnMajor.h"
#include "LEDCoordinateMapperFlipdotGrid.h"
#include "OffPattern.h"
#include "SolidPattern.h"
#include "FadePattern.h"
#include "RainbowPattern.h"
#include "StatusPattern.h"
#include "GameOfLifePattern.h"
#include "ChasePattern.h"
#include "PositionTestPattern.h"
#include "ClockPattern.h"
#include "CalendarPattern.h"
#include "SummaryPattern.h"
#include "PowerManager.h"
// Calendar pattern forward include added later
#include "ConfigurationManager.h"
#include "LEDConfig.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "debug.h"
#include <algorithm>
#include <cstring>

namespace leds {

using config::LEDConfig;
static const char* TAG = "LEDManager";

LEDManager::LEDManager() = default;
LEDManager::~LEDManager() = default;


// Helper to apply common runtime knobs to a pattern from config
static inline void apply_runtime_knobs(leds::LEDPattern& pat, const config::LEDConfig& cfg) {
    pat.set_speed_percent(cfg.has_speed() ? cfg.speed() : 50);
    if (cfg.has_r() || cfg.has_g() || cfg.has_b() || cfg.has_w()) {
        pat.set_solid_color(cfg.has_r() ? cfg.r() : 0,
                            cfg.has_g() ? cfg.g() : 0,
                            cfg.has_b() ? cfg.b() : 0,
                            cfg.has_w() ? cfg.w() : 0);
    }
    if (cfg.has_brightness()) pat.set_brightness_percent(cfg.brightness());
}


esp_err_t LEDManager::init(config::ConfigurationManager& cfg_manager) {
    cfg_manager_ = &cfg_manager;
    if (cfg_manager_ == nullptr) {
        ESP_LOGE(TAG, "LEDManager init requires a ConfigurationManager instance");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Initializing LEDManager");
    refresh_configuration(cfg_manager);
    // Create update task pinned to APP core with a small stack and low priority
    BaseType_t ok = xTaskCreatePinnedToCore(
        &LEDManager::UpdateTaskEntry, "led-update", 6144, this, update_task_priority_, &update_task_, update_task_core_);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED update task");
        log_memory_snapshot(TAG, "led_update_task_create_failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void LEDManager::refresh_configuration(config::ConfigurationManager& cfg_manager) {
    // Build strips array from active LED configs (clean slate)
    std::vector<LEDConfig*> active = cfg_manager.active_leds();
    strips_.clear();
    patterns_.clear();
    power_mgrs_.clear();
    prev_frames_rgba_.clear();
    scratch_frames_rgba_.clear();
    last_layouts_.clear();
    last_patterns_.clear();
    last_enable_pins_.clear();
    frames_tx_counts_.assign(active.size(), 0);
    last_generations_.assign(active.size(), 0);
    last_power_enabled_.assign(active.size(), false);
    power_on_hold_until_us_.assign(active.size(), 0);
    strips_.reserve(active.size());
    patterns_.reserve(active.size());

    ESP_LOGI(TAG, "Config refresh: %zu active strips", active.size());
    for (LEDConfig* c : active) {
        // Build enable list string
        std::string en_str;
        auto pins = c->all_enabled_gpios();
        if (!pins.empty()) {
            en_str.push_back('[');
            for (size_t i = 0; i < pins.size(); ++i) {
                if (i) en_str.push_back(',');
                en_str += std::to_string(pins[i]);
            }
            en_str.push_back(']');
        } else {
            en_str = "[]";
        }
        ESP_LOGI(TAG, "Strip config: name=%s gpio=%d enable_gpios=%s chip=%s size=%dx%d dma=%s pattern=%s", c->name(),
                 c->has_data_gpio() ? c->data_gpio() : -1,
                 en_str.c_str(),
                 c->chip().c_str(), c->num_columns(), c->num_rows(),
                 c->has_dma() ? (c->dma() ? "true" : "false") : "unset",
                 c->has_pattern() ? c->pattern().c_str() : "<unset>");
    }

    // Determine which strip should use DMA without reordering strips.
    // Priority: explicit config (first with dma=true), otherwise the longest strip.
    size_t selected_dma_idx = static_cast<size_t>(-1);
    for (size_t i = 0; i < active.size(); ++i) {
        if (active[i]->has_dma() && active[i]->dma()) { selected_dma_idx = i; break; }
    }
    if (selected_dma_idx == static_cast<size_t>(-1)) {
        size_t longest_len = 0;
        for (size_t i = 0; i < active.size(); ++i) {
            size_t len = static_cast<size_t>(active[i]->num_columns() * active[i]->num_rows());
            if (len > longest_len) { longest_len = len; selected_dma_idx = i; }
        }
    }

    // Build strips in the same order as provided by the configuration
    std::vector<LEDConfig*> built_cfgs;
    for (size_t i = 0; i < active.size(); ++i) {
        LEDConfig* c = active[i];
        bool use_dma = (i == selected_dma_idx);
        size_t rows = static_cast<size_t>(c->num_rows());
        size_t cols = static_cast<size_t>(c->num_columns());
        auto chip = c->chip_enum();
        std::unique_ptr<LEDStrip> s = create_strip(*c, use_dma);
        if (!s) {
            ESP_LOGE(TAG, "Failed to create strip on GPIO %d (dma=%d)", c->data_gpio(), (int)use_dma);
            continue;
        }
        // Prime hardware with a clear frame to establish known state
        s->clear();
        s->flush_if_dirty(esp_timer_get_time(), 0);
        // Install power manager by chip type
        if (chip == config::LEDConfig::Chip::FLIPDOT) power_mgrs_.push_back(std::unique_ptr<PowerManager>(new FlipDotPower()));
        else power_mgrs_.push_back(std::unique_ptr<PowerManager>(new LedPower()));
        prev_frames_rgba_.emplace_back(rows * cols * 4, 0);
        scratch_frames_rgba_.emplace_back(rows * cols * 4, 0);
        strips_.push_back(std::move(s));
        patterns_.push_back(create_pattern_from_config(*c));
        last_layouts_.push_back(c->layout_enum());
        last_patterns_.push_back(c->pattern_enum());
        last_enable_pins_.push_back(c->all_enabled_gpios());
        built_cfgs.push_back(c);
    }

    // Initial pattern application
    uint64_t now = esp_timer_get_time();
    for (size_t i = 0; i < strips_.size(); ++i) {
        if (i < built_cfgs.size()) apply_pattern_updates_from_config(i, *built_cfgs[i], now);
    }
}

std::unique_ptr<LEDStrip> LEDManager::create_strip(const config::LEDConfig& cfg, bool use_dma) {
    size_t rows = static_cast<size_t>(cfg.num_rows());
    size_t cols = static_cast<size_t>(cfg.num_columns());
    auto chip = cfg.chip_enum();
    std::unique_ptr<LEDStrip> s;
    std::unique_ptr<leds::internal::LEDCoordinateMapper> mapper;
    switch (cfg.layout_enum()) {
        case config::LEDConfig::Layout::SERPENTINE_ROW:
            mapper.reset(new leds::internal::SerpentineRowMapper(rows, cols));
            break;
        case config::LEDConfig::Layout::SERPENTINE_COLUMN:
            mapper.reset(new leds::internal::SerpentineColumnMapper(rows, cols, static_cast<size_t>(cfg.has_segment_rows() ? cfg.segment_rows() : 0)));
            break;
        case config::LEDConfig::Layout::COLUMN_MAJOR:
            mapper.reset(new leds::internal::ColumnMajorMapper(rows, cols));
            break;
        case config::LEDConfig::Layout::FLIPDOT_GRID:
            mapper.reset(new leds::internal::FlipdotGridMapper(rows, cols));
            break;
        case config::LEDConfig::Layout::ROW_MAJOR:
        default:
            mapper.reset(new leds::internal::RowMajorMapper(rows, cols));
            break;
    }
    switch (chip) {
        case config::LEDConfig::Chip::WS2812: {
            LEDStripSurfaceAdapter::Params ap{cfg.data_gpio(), cfg.all_enabled_gpios(), rows, cols};
            // Encoder does not manage enable pins; LEDStripSurfaceAdapter handles power control
            auto enc = std::unique_ptr<leds::internal::LEDWireEncoder>(new leds::internal::WireEncoderWS2812(ap.gpio, use_dma, 10 * 1000 * 1000, use_dma ? 256 : 48, rows * cols));
            s.reset(new LEDStripSurfaceAdapter(ap, std::move(mapper), std::move(enc)));
            break;
        }
        case config::LEDConfig::Chip::SK6812: {
            LEDStripSurfaceAdapter::Params ap{cfg.data_gpio(), cfg.all_enabled_gpios(), rows, cols};
            auto enc = std::unique_ptr<leds::internal::LEDWireEncoder>(new leds::internal::WireEncoderSK6812(ap.gpio, use_dma, 10 * 1000 * 1000, use_dma ? 256 : 48, rows * cols));
            s.reset(new LEDStripSurfaceAdapter(ap, std::move(mapper), std::move(enc)));
            break;
        }
        case config::LEDConfig::Chip::WS2814: {
            LEDStripSurfaceAdapter::Params ap{cfg.data_gpio(), cfg.all_enabled_gpios(), rows, cols};
            auto enc = std::unique_ptr<leds::internal::LEDWireEncoder>(new leds::internal::WireEncoderWS2814(ap.gpio, use_dma, 10 * 1000 * 1000, use_dma ? 256 : 48, rows * cols));
            s.reset(new LEDStripSurfaceAdapter(ap, std::move(mapper), std::move(enc)));
            break;
        }
        case config::LEDConfig::Chip::FLIPDOT: {
            LEDStripSurfaceAdapter::Params ap{cfg.data_gpio(), cfg.all_enabled_gpios(), rows, cols};
            auto enc = std::unique_ptr<leds::internal::LEDWireEncoder>(new leds::internal::WireEncoderFlipdot(ap.gpio, use_dma, 10 * 1000 * 1000, use_dma ? 256 : 48, ((rows * cols) + 2) / 3));
            s.reset(new LEDStripSurfaceAdapter(ap, std::move(mapper), std::move(enc)));
            break;
        }
        default:
            ESP_LOGE(TAG, "Unknown LED chip enum");
            return nullptr;
    }
    if (!s) ESP_LOGE(TAG, "Failed to create strip on GPIO %d (dma=%d)", cfg.data_gpio(), (int)use_dma);
    return s;
}

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
        case P::CHASE: p.reset(new ChasePattern()); break;
        case P::LIFE: p.reset(new GameOfLifePattern()); break;
        case P::POSITION: p.reset(new PositionTestPattern()); break;
        case P::CLOCK: p.reset(new ClockPattern()); break;
        case P::CALENDAR: p.reset(new CalendarPattern()); break;
        case P::SUMMARY: p.reset(new SummaryPattern()); break;
        case P::INVALID: default: p.reset(new OffPattern()); break;
    }
    return p;
}

void LEDManager::UpdateTaskEntry(void* arg) {
    static_cast<LEDManager*>(arg)->run_update_loop();
}

void LEDManager::run_update_loop() {
    TickType_t tick_delay = pdMS_TO_TICKS(update_interval_us_ / 1000);
    if (tick_delay == 0) tick_delay = 1; // ensure at least one tick to yield CPU
    while (true) {
        uint64_t now = esp_timer_get_time();
        // Periodically log tick rate and yield behavior to diagnose WDT issues
        static uint32_t loop_count = 0;
        if ((loop_count++ % 2000u) == 0u) {
            ESP_LOGD(TAG, "update loop tick; delay_ticks=%u, strips=%u", (unsigned)tick_delay, (unsigned)strips_.size());
        }
        // Cheap per-tick generation check; if changed, reconcile immediately
        reconcile_with_config(*cfg_manager_);

        // Update patterns and flush strips. Skip pattern update if strip is transmitting.
        for (size_t i = 0; i < strips_.size(); ++i) {
            LEDStrip* s = strips_[i].get();
            LEDPattern* p = (i < patterns_.size()) ? patterns_[i].get() : nullptr;
            if (!s) continue;
            if (!s->is_transmitting() && p) p->update(*s, now);

            // Power + refresh policy
            if (i < power_mgrs_.size()) {
                // Build current frame view from strip's shadow via get_pixel
                size_t rows = s->rows(), cols = s->cols();
                if (i >= scratch_frames_rgba_.size()) scratch_frames_rgba_.resize(i+1);
                auto& current = scratch_frames_rgba_[i];
                current.assign(rows * cols * 4, 0);
                for (size_t idx = 0; idx < rows * cols; ++idx) {
                    uint8_t r=0,g=0,b=0,w=0; s->get_pixel(idx, r, g, b, w);
                    size_t off = idx * 4; current[off]=r; current[off+1]=g; current[off+2]=b; current[off+3]=w;
                }
                FrameView cur{current.data(), rows, cols};
                FrameView prev{prev_frames_rgba_[i].data(), rows, cols};
                (void)power_mgrs_[i]->on_frame(cur, prev, now);
                // Apply power state; log transitions
                if (s->has_enable_pin()) {
                    bool new_state = power_mgrs_[i]->power_enabled();
                    if (i >= last_power_enabled_.size()) last_power_enabled_.resize(i+1, false);
                    if (new_state != last_power_enabled_[i]) {
                        ESP_LOGI(TAG, "LED power %s on strip %u", new_state ? "ENABLED" : "DISABLED", (unsigned)i);
                        // On OFF->ON, start a 10ms hold window to allow downstream circuits to initialize
                        if (new_state && !last_power_enabled_[i]) {
                            if (i >= power_on_hold_until_us_.size()) power_on_hold_until_us_.resize(i+1, 0);
                            power_on_hold_until_us_[i] = now + 10ull * 1000ull; // 10ms
                        }
                        last_power_enabled_[i] = new_state;
                    }
                    s->set_power_enabled(new_state);
                }
                // Save current as previous
                prev_frames_rgba_[i].swap(current);
            }

            bool hold_active = (i < power_on_hold_until_us_.size()) && (now < power_on_hold_until_us_[i]);
            if (!hold_active) {
                if (s->flush_if_dirty(now)) frames_tx_counts_[i]++;
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

    // Decide if pattern type changed using last_patterns_
    size_t ensure = idx + 1;
    if (last_patterns_.size() < ensure) last_patterns_.resize(ensure, config::LEDConfig::Pattern::INVALID);
    bool type_changed = (!pat) || (last_patterns_[idx] != cfg.pattern_enum());

    if (type_changed) {
        patterns_[idx] = create_pattern_from_config(cfg);
        pat = patterns_[idx].get();
        if (pat) { apply_runtime_knobs(*pat, cfg); pat->reset(*strip, now_us); }
        ESP_LOGI(TAG, "Pattern swapped for strip %u -> %s", (unsigned)idx, pat ? pat->name() : "<null>");
    } else if (pat) {
        // Apply runtime knobs
        apply_runtime_knobs(*pat, cfg);
    }
    // Record last applied pattern type
    last_patterns_[idx] = cfg.pattern_enum();
}

void LEDManager::reconcile_with_config(config::ConfigurationManager& cfg_manager) {
    auto active = cfg_manager.active_leds();
    bool big_change = (active.size() != strips_.size());
    // If not obviously big, check per-strip generation first; only then inspect hardware params
    if (!big_change) {
        // Detect any generation change and then decide big vs small
        bool any_gen_changed = false;
        for (size_t i = 0; i < active.size(); ++i) {
            auto* c = active[i];
            if (!c) { big_change = true; break; }
            uint32_t gen = c->generation();
            if (i >= last_generations_.size()) last_generations_.resize(i+1, 0);
            if (gen != last_generations_[i]) { any_gen_changed = true; }
        }
        if (any_gen_changed) {
            for (size_t i = 0; i < active.size(); ++i) {
                auto* c = active[i];
                LEDStrip* s = strips_[i].get();
                if (!c || !s) { big_change = true; break; }
                size_t desired_len = static_cast<size_t>(c->num_columns() * c->num_rows());
                if (s->pin() != (c->has_data_gpio() ? c->data_gpio() : -1) ||
                    s->length() != desired_len ||
                    (i < last_layouts_.size() && last_layouts_[i] != c->layout_enum()) ||
                    (i < last_enable_pins_.size() && last_enable_pins_[i] != c->all_enabled_gpios())) { big_change = true; break; }
            }
        } else {
            // No generation change; nothing to do
            return;
        }
    }

    if (big_change) {
        ESP_LOGI(TAG, "Detected hardware-level config change; rebuilding strips");
        // Update generations snapshot then refresh configuration
        auto act2 = cfg_manager.active_leds();
        for (size_t i = 0; i < act2.size(); ++i) {
            if (i >= last_generations_.size()) last_generations_.resize(i+1, 0);
            last_generations_[i] = act2[i] ? act2[i]->generation() : 0;
        }

        refresh_configuration(cfg_manager);

        return;
    }

    // Apply updates only to strips whose generation changed
    uint64_t now = esp_timer_get_time();
    for (size_t i = 0; i < active.size(); ++i) {
        auto* c = active[i];
        if (!c) continue;
        if (i >= last_generations_.size()) last_generations_.resize(i+1, 0);
        uint32_t current_gen = c->generation();
        if (last_generations_[i] == current_gen) continue; // skip unchanged
        // Conservative: record first to avoid skipping updates if generation advances mid-apply
        last_generations_[i] = current_gen;
        apply_pattern_updates_from_config(i, *c, now);
    }
    // No pattern-specific restarts here; patterns handle their own config changes
}

} // namespace leds


