#pragma once

#include "ConfigurationModule.h"
#include <string>

namespace config {

class WifiConfig : public ConfigurationModule {
public:
    WifiConfig();

    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // Accessors
    const std::string& ssid() const { return ssid_; }
    const std::string& password() const { return password_; }
    const std::string& mqtt_broker() const { return mqtt_broker_; }
    const std::string& channel() const { return channel_; }
    int loglevel() const { return loglevel_; }

    // Presence helpers (true only if loaded from NVS or set via update and non-empty)
    bool has_ssid() const { return ssid_set_ && !ssid_.empty(); }
    bool has_password() const { return password_set_ && !password_.empty(); }
    bool has_mqtt_broker() const { return mqtt_broker_set_ && !mqtt_broker_.empty(); }
    bool has_channel() const { return channel_set_ && !channel_.empty(); }

private:
    std::string ssid_;
    std::string password_;
    std::string mqtt_broker_;
    std::string channel_;
    bool ssid_set_ = false;
    bool password_set_ = false;
    bool mqtt_broker_set_ = false;
    bool channel_set_ = false;
    int loglevel_ = 2; // default warn (ESP_LOG_WARN)
    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config


