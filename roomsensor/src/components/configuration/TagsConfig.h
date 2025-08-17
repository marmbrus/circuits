#pragma once

#include "ConfigurationModule.h"
#include <string>

namespace config {

// Tags module: area, room, id are configurable and persisted when set.
// mac and sensor (room-id) are computed and never persisted.
class TagsConfig : public ConfigurationModule {
public:
    TagsConfig();

    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // Accessors (always available; may be default-derived)
    const std::string& area() const { return area_.empty() ? default_area_ : area_; }
    const std::string& room() const { return room_.empty() ? default_room_ : room_; }
    const std::string& id() const { return id_.empty() ? default_id_ : id_; }

    // Presence helpers (true if explicitly set via NVS/MQTT/console)
    bool has_area() const { return area_set_ && !area_.empty(); }
    bool has_room() const { return room_set_ && !room_.empty(); }
    bool has_id() const { return id_set_ && !id_.empty(); }
    bool is_fully_configured() const { return has_area() && has_room() && has_id(); }

private:
    void compute_mac_and_defaults();

    std::string area_;
    std::string room_;
    std::string id_;

    bool area_set_ = false;
    bool room_set_ = false;
    bool id_set_ = false;

    // Derived/non-persisted values (internal only)
    std::string mac_;
    std::string default_area_ = "unknown";
    std::string default_room_ = "unknown";
    std::string default_id_; // derived from MAC

    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config


