#include "TagsConfig.h"
#include "cJSON.h"
#include "esp_mac.h"
#include <cstring>

namespace config {

static const char* TAG = "TagsConfig";

TagsConfig::TagsConfig() {
    // Persisted descriptors for user-provided values
    descriptors_.push_back({"area", ConfigValueType::String, nullptr, true});
    descriptors_.push_back({"room", ConfigValueType::String, nullptr, true});
    descriptors_.push_back({"id", ConfigValueType::String, nullptr, true});

    compute_mac_and_defaults();
}

const char* TagsConfig::name() const {
    return "tags";
}

const std::vector<ConfigurationValueDescriptor>& TagsConfig::descriptors() const {
    return descriptors_;
}

void TagsConfig::compute_mac_and_defaults() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    mac_.assign(mac_str);

    // Default id uses last 4 hex chars of MAC
    char id_buf[9];
    snprintf(id_buf, sizeof(id_buf), "dev%02X%02X", mac[4], mac[5]);
    default_id_.assign(id_buf);
}

esp_err_t TagsConfig::apply_update(const char* key, const char* value_str) {
    if (key == nullptr) return ESP_ERR_INVALID_ARG;

    if (strcmp(key, "area") == 0) {
        area_.assign(value_str ? value_str : "");
        area_set_ = (value_str != nullptr);
        return ESP_OK;
    }
    if (strcmp(key, "room") == 0) {
        room_.assign(value_str ? value_str : "");
        room_set_ = (value_str != nullptr);
        return ESP_OK;
    }
    if (strcmp(key, "id") == 0) {
        id_.assign(value_str ? value_str : "");
        id_set_ = (value_str != nullptr);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t TagsConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;

    cJSON* tags_obj = cJSON_CreateObject();
    // Only include configurable values in module JSON
    cJSON_AddStringToObject(tags_obj, "area", area().c_str());
    cJSON_AddStringToObject(tags_obj, "room", room().c_str());
    cJSON_AddStringToObject(tags_obj, "id", id().c_str());
    cJSON_AddItemToObject(root_object, name(), tags_obj);
    return ESP_OK;
}

} // namespace config


