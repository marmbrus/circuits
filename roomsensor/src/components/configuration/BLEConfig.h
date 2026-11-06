#pragma once

#include "ConfigurationModule.h"
#include "ble_scan_logic.h"
#include <mutex>
#include <string>
#include <vector>

// Forward declare to avoid adding heavy dependency to all includers
struct cJSON;

namespace config {

// Bluetooth LE scanning. Nothing runs (Bluetooth isn't even started) until scan is set or
// track names a device.
//   scan     discovery: report every device heard, each interval, on sensor/<mac>/ble/devices
//   track    devices to report on sensor/<mac>/metrics/ble_rssi each interval they're heard:
//            "aa:bb:cc:dd:ee:ff=Mittens,11:22:33:44:55:66=Tom" (names optional)
//   interval seconds per report (and RSSI averaging window), default 10
//   duty     percent of the time the radio listens, default 50 (Wi-Fi shares the radio)
//   minRssi  discovery leaves out devices never heard stronger than this (dBm)
class BLEConfig : public ConfigurationModule {
public:
    static constexpr int kDefaultIntervalSec = 10;
    static constexpr int kDefaultDutyPercent = 50;

    // Everything the scanner needs, read together
    struct Settings {
        bool scan = false;
        std::vector<ble::TrackedDevice> tracked;
        int interval_sec = kDefaultIntervalSec;
        int duty_percent = kDefaultDutyPercent;
        bool has_min_rssi = false;
        int min_rssi = 0;

        bool enabled() const { return scan || !tracked.empty(); }
    };

    BLEConfig();
    ~BLEConfig() override = default;

    // ConfigurationModule API
    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // A consistent copy; safe to call from any task while updates arrive over MQTT
    Settings settings() const;
    bool enabled() const;

private:
    mutable std::mutex mutex_;
    Settings settings_;
    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config
