#include "mcp23088_keypad.h"
#include "IOConfig.h"
#include "esp_log.h"
#include <string.h>
#include <string>

namespace i2c_logic {

static const char* TAG = "MCP23088KeypadLogic";

// Helpers
static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    size_t ls = strlen(s);
    size_t lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    return strncmp(s + (ls - lsuf), suffix, lsuf) == 0;
}

static bool extract_base(const char* pin_name, const char* suffix, std::string &base_out) {
    if (!pin_name) return false;
    size_t nlen = strlen(pin_name);
    size_t slen = strlen(suffix);
    if (nlen < slen) return false;
    if (!ends_with(pin_name, suffix)) return false;
    size_t blen = nlen - slen;
    if (blen == 0) return false; // empty base not allowed
    base_out.assign(pin_name, blen);
    return true;
}

static bool is_override_active_for_base(config::IOConfig &cfg, const std::string &base) {
    std::string expected = base + ".door.override";
    for (int j = 1; j <= 8; ++j) {
        if (cfg.pin_mode(j) != config::IOConfig::PinMode::SENSOR) continue;
        const char* sname = cfg.pin_name(j);
        if (!sname) continue;
        if (expected == sname) {
            if (cfg.contact_state(j)) {
                return true; // closed => active
            }
        }
    }
    return false;
}

bool apply_lock_keypad_logic(config::IOConfig& cfg, const char* module_name) {
    bool any_change = false;

    // For each switch, check if there is a matching base override that is closed
    for (int i = 1; i <= 8; ++i) {
        auto mode = cfg.pin_mode(i);
        if (!(mode == config::IOConfig::PinMode::SWITCH || mode == config::IOConfig::PinMode::SWITCH_HIGH || mode == config::IOConfig::PinMode::SWITCH_LOW)) continue;
        const char* name = cfg.pin_name(i);
        if (!name) continue;

        std::string base;
        bool is_unlock = extract_base(name, ".door.unlock", base);
        bool is_lock = false;
        if (!is_unlock) {
            is_lock = extract_base(name, ".door.lock", base);
        }
        if (!(is_unlock || is_lock)) continue;

        bool active = is_override_active_for_base(cfg, base);
        ESP_LOGD(TAG, "%s override for base '%s' is %s", module_name, base.c_str(), active ? "ACTIVE" : "inactive");

        // Determine desired effective state: override wins while active; otherwise mirror base
        bool desired_on = false;
        bool has_base = cfg.is_base_switch_state_set(i);
        if (is_unlock) {
            desired_on = active ? true : (has_base ? cfg.base_switch_state(i) : false);
        } else if (is_lock) {
            desired_on = active ? false : (has_base ? cfg.base_switch_state(i) : false);
        }

        if (!cfg.is_switch_state_set(i) || cfg.switch_state(i) != desired_on) {
            cfg.set_switch_state(i, desired_on);
            any_change = true;
            ESP_LOGI(TAG, "%s setting '%s' %s", module_name, name, desired_on ? "ON" : "OFF");
        }
    }

    return any_change;
}

}


