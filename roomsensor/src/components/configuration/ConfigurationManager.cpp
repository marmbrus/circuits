#include "ConfigurationManager.h"
#include "WifiConfig.h"
#include "TagsConfig.h"
#include "DeviceConfig.h"
#include "LEDConfig.h"
#include "A2DConfig.h"
#include "IOConfig.h"
#include "MotionConfig.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <strings.h>
#include <algorithm>

namespace config {

static const char* TAG = "ConfigManager";

static std::unique_ptr<ConfigurationManager> g_manager;

ConfigurationManager::ConfigurationManager() {}
ConfigurationManager::~ConfigurationManager() {}

void ConfigurationManager::register_modules() {
    wifi_module_.reset(new WifiConfig());
    modules_.push_back(wifi_module_.get());
    tags_module_.reset(new TagsConfig());
    modules_.push_back(tags_module_.get());
    device_module_.reset(new DeviceConfig());
    modules_.push_back(device_module_.get());
    led1_module_.reset(new LEDConfig("led1"));
    modules_.push_back(led1_module_.get());
    led2_module_.reset(new LEDConfig("led2"));
    modules_.push_back(led2_module_.get());
    led3_module_.reset(new LEDConfig("led3"));
    modules_.push_back(led3_module_.get());
    led4_module_.reset(new LEDConfig("led4"));
    modules_.push_back(led4_module_.get());

    // A2D modules for ADS1115 addresses
    a2d1_module_.reset(new A2DConfig("a2d1"));
    modules_.push_back(a2d1_module_.get());
    a2d2_module_.reset(new A2DConfig("a2d2"));
    modules_.push_back(a2d2_module_.get());
    a2d3_module_.reset(new A2DConfig("a2d3"));
    modules_.push_back(a2d3_module_.get());
    a2d4_module_.reset(new A2DConfig("a2d4"));
    modules_.push_back(a2d4_module_.get());

    // Motion module (nullable gpio configuration)
    motion_module_.reset(new MotionConfig());
    modules_.push_back(motion_module_.get());

    // IO expander modules mapped to MCP23008 addresses 0x20..0x27 => io1..io8
    io1_module_.reset(new IOConfig("io1")); modules_.push_back(io1_module_.get());
    io2_module_.reset(new IOConfig("io2")); modules_.push_back(io2_module_.get());
    io3_module_.reset(new IOConfig("io3")); modules_.push_back(io3_module_.get());
    io4_module_.reset(new IOConfig("io4")); modules_.push_back(io4_module_.get());
    io5_module_.reset(new IOConfig("io5")); modules_.push_back(io5_module_.get());
    io6_module_.reset(new IOConfig("io6")); modules_.push_back(io6_module_.get());
    io7_module_.reset(new IOConfig("io7")); modules_.push_back(io7_module_.get());
    io8_module_.reset(new IOConfig("io8")); modules_.push_back(io8_module_.get());
}

ConfigurationModule* ConfigurationManager::find_module(const char* module_name) {
    if (!module_name) return nullptr;
    for (ConfigurationModule* mod : modules_) {
        if (strcmp(mod->name(), module_name) == 0) return mod;
    }
    return nullptr;
}

WifiConfig& ConfigurationManager::wifi() {
    return *wifi_module_;
}

TagsConfig& ConfigurationManager::tags() {
    return *tags_module_;
}

DeviceConfig& ConfigurationManager::device() {
    return *device_module_;
}

LEDConfig& ConfigurationManager::led1() { return *led1_module_; }
LEDConfig& ConfigurationManager::led2() { return *led2_module_; }
LEDConfig& ConfigurationManager::led3() { return *led3_module_; }
LEDConfig& ConfigurationManager::led4() { return *led4_module_; }

A2DConfig& ConfigurationManager::a2d1() { return *a2d1_module_; }
A2DConfig& ConfigurationManager::a2d2() { return *a2d2_module_; }
A2DConfig& ConfigurationManager::a2d3() { return *a2d3_module_; }
A2DConfig& ConfigurationManager::a2d4() { return *a2d4_module_; }

IOConfig& ConfigurationManager::io1() { return *io1_module_; }
IOConfig& ConfigurationManager::io2() { return *io2_module_; }
IOConfig& ConfigurationManager::io3() { return *io3_module_; }
IOConfig& ConfigurationManager::io4() { return *io4_module_; }
IOConfig& ConfigurationManager::io5() { return *io5_module_; }
IOConfig& ConfigurationManager::io6() { return *io6_module_; }
IOConfig& ConfigurationManager::io7() { return *io7_module_; }
IOConfig& ConfigurationManager::io8() { return *io8_module_; }

MotionConfig& ConfigurationManager::motion() { return *motion_module_; }

std::vector<LEDConfig*> ConfigurationManager::active_leds() const {
    std::vector<LEDConfig*> result;
    if (led1_module_ && led1_module_->has_data_gpio()) result.push_back(led1_module_.get());
    if (led2_module_ && led2_module_->has_data_gpio()) result.push_back(led2_module_.get());
    if (led3_module_ && led3_module_->has_data_gpio()) result.push_back(led3_module_.get());
    if (led4_module_ && led4_module_->has_data_gpio()) result.push_back(led4_module_.get());
    return result;
}

static esp_err_t nvs_load_module(const char* ns_name, ConfigurationModule* module) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; // No values yet
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed for namespace '%s': %s", ns_name, esp_err_to_name(err));
        return err;
    }

    for (const auto& desc : module->descriptors()) {
        switch (desc.type) {
            case ConfigValueType::String: {
                size_t len = 0;
                err = nvs_get_str(handle, desc.name, nullptr, &len);
                if (err == ESP_OK && len > 0) {
                    std::string buf(len, '\0');
                    err = nvs_get_str(handle, desc.name, buf.data(), &len);
                    if (err == ESP_OK) {
                        module->apply_update(desc.name, buf.c_str());
                        ESP_LOGD(TAG, "Loaded persisted config (str): %s.%s", ns_name, desc.name);
                    }
                }
                break;
            }
            case ConfigValueType::Bool: {
                uint8_t v = 0; err = nvs_get_u8(handle, desc.name, &v);
                if (err == ESP_OK) {
                    const char* s = v ? "1" : "0";
                    module->apply_update(desc.name, s);
                    ESP_LOGD(TAG, "Loaded persisted config (bool): %s.%s=%u", ns_name, desc.name, (unsigned)v);
                }
                break;
            }
            case ConfigValueType::I32: {
                int32_t v = 0; err = nvs_get_i32(handle, desc.name, &v);
                if (err == ESP_OK) {
                    std::string s = std::to_string(v);
                    module->apply_update(desc.name, s.c_str());
                    ESP_LOGD(TAG, "Loaded persisted config (i32): %s.%s=%ld", ns_name, desc.name, (long)v);
                }
                break;
            }
            case ConfigValueType::U32: {
                uint32_t v = 0; err = nvs_get_u32(handle, desc.name, &v);
                if (err == ESP_OK) {
                    std::string s = std::to_string(v);
                    module->apply_update(desc.name, s.c_str());
                    ESP_LOGD(TAG, "Loaded persisted config (u32): %s.%s=%lu", ns_name, desc.name, (unsigned long)v);
                }
                break;
            }
            case ConfigValueType::I64: {
                int64_t v = 0; err = nvs_get_i64(handle, desc.name, &v);
                if (err == ESP_OK) {
                    std::string s = std::to_string((long long)v);
                    module->apply_update(desc.name, s.c_str());
                    ESP_LOGD(TAG, "Loaded persisted config (i64): %s.%s", ns_name, desc.name);
                }
                break;
            }
            case ConfigValueType::F32:
            case ConfigValueType::Blob:
                // Not supported for generic load in this project
                break;
        }
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t ConfigurationManager::initialize() {
    register_modules();

    // Load persisted values from NVS
    for (ConfigurationModule* mod : modules_) {
        nvs_load_module(mod->name(), mod);
    }

    // No global log level changes here; UART logging remains controlled by sdkconfig/menuconfig.

    // Log full configuration to console (pretty-printed)
    {
        cJSON* root = build_full_config_json();
        char* pretty = cJSON_Print(root);
        if (pretty) {
            ESP_LOGI(TAG, "Loaded configuration:\n%s", pretty);
            cJSON_free(pretty);
        }
        cJSON_Delete(root);
    }

    return ESP_OK;
}

static std::string mac_to_string() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(mac_str);
}

cJSON* ConfigurationManager::build_full_config_json() const {
    cJSON* root = cJSON_CreateObject();
    for (const ConfigurationModule* mod : modules_) {
        mod->to_json(root);
    }
    return root;
}

esp_err_t ConfigurationManager::publish_full_configuration() {
    cJSON* root = build_full_config_json();
    char* json = cJSON_PrintUnformatted(root);

    std::string topic = "sensor/" + mac_to_string() + "/config/current";
    esp_err_t res = publish_to_topic(topic.c_str(), json, 1, 1);

    if (res == ESP_OK) {
        ESP_LOGD(TAG, "Published current configuration to %s (%zu bytes)", topic.c_str(), strlen(json));
    } else {
        ESP_LOGE(TAG, "Failed to publish current configuration: %s", esp_err_to_name(res));
    }

    cJSON_free(json);
    cJSON_Delete(root);
    return res;
}

esp_err_t ConfigurationManager::handle_update(const char* module_name, const char* key, const char* value_str, bool persist_if_supported) {
    ConfigurationModule* mod = find_module(module_name);
    if (!mod) return ESP_ERR_NOT_FOUND;

    esp_err_t err = mod->apply_update(key, value_str);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config update failed: %s.%s -> %s", module_name, key, esp_err_to_name(err));
        return err;
    }
    // Centralized generation bump on successful update
    mod->mark_updated();

    {
        const char* log_value = (value_str && value_str[0] != '\0') ? value_str : "(unset)";
        ESP_LOGI(TAG, "Config update applied: %s.%s=%s", module_name, key, log_value);
    }

    // Special handling: Only one strip may claim the DMA RMT channel at a time.
    // If dma=true is set on one LED module, clear it (unset) on all other LED modules.
    if (key && strcmp(key, "dma") == 0 && value_str) {
        bool requested_true = (strcasecmp(value_str, "1") == 0 || strcasecmp(value_str, "true") == 0 ||
                               strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "yes") == 0);
        if (requested_true) {
            // Determine which LED module was updated by comparing names
            LEDConfig* updated_led = nullptr;
            if (led1_module_ && strcmp(module_name, led1_module_->name()) == 0) updated_led = led1_module_.get();
            else if (led2_module_ && strcmp(module_name, led2_module_->name()) == 0) updated_led = led2_module_.get();
            else if (led3_module_ && strcmp(module_name, led3_module_->name()) == 0) updated_led = led3_module_.get();
            else if (led4_module_ && strcmp(module_name, led4_module_->name()) == 0) updated_led = led4_module_.get();

            if (updated_led) {
                const char* clear_value = nullptr; // nullptr => unset/auto-assign
                if (led1_module_ && led1_module_.get() != updated_led) led1_module_->apply_update("dma", clear_value);
                if (led2_module_ && led2_module_.get() != updated_led) led2_module_->apply_update("dma", clear_value);
                if (led3_module_ && led3_module_.get() != updated_led) led3_module_->apply_update("dma", clear_value);
                if (led4_module_ && led4_module_.get() != updated_led) led4_module_->apply_update("dma", clear_value);
            }
        }
    }

    if (persist_if_supported) {
        // Check descriptor for persistence
        for (const auto& desc : mod->descriptors()) {
            if (strcmp(desc.name, key) == 0 && desc.persisted) {
                nvs_handle_t handle;
                err = nvs_open(module_name, NVS_READWRITE, &handle);
                if (err == ESP_OK) {
                    // Persist according to declared type
                    switch (desc.type) {
                        case ConfigValueType::String:
                            err = nvs_set_str(handle, key, value_str ? value_str : "");
                            break;
                        case ConfigValueType::Bool: {
                            if (value_str == nullptr || value_str[0] == '\0') {
                                err = nvs_erase_key(handle, key);
                            } else {
                                bool v = (strcasecmp(value_str, "1") == 0 || strcasecmp(value_str, "true") == 0 ||
                                          strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "yes") == 0);
                                err = nvs_set_u8(handle, key, v ? 1 : 0);
                            }
                            break;
                        }
                        case ConfigValueType::I32: {
                            if (value_str == nullptr || value_str[0] == '\0') {
                                err = nvs_erase_key(handle, key);
                            } else {
                                int32_t v = atoi(value_str);
                                err = nvs_set_i32(handle, key, v);
                            }
                            break;
                        }
                        case ConfigValueType::U32: {
                            if (value_str == nullptr || value_str[0] == '\0') {
                                err = nvs_erase_key(handle, key);
                            } else {
                                uint32_t v = (uint32_t)strtoul(value_str, nullptr, 10);
                                err = nvs_set_u32(handle, key, v);
                            }
                            break;
                        }
                        case ConfigValueType::I64: {
                            if (value_str == nullptr || value_str[0] == '\0') {
                                err = nvs_erase_key(handle, key);
                            } else {
                                int64_t v = (int64_t)strtoll(value_str, nullptr, 10);
                                err = nvs_set_i64(handle, key, v);
                            }
                            break;
                        }
                        case ConfigValueType::F32:
                        case ConfigValueType::Blob:
                            // Not supported for generic persist in this project
                            err = ESP_OK; // do nothing
                            break;
                    }
                    if (err == ESP_OK) {
                        esp_err_t cmt = nvs_commit(handle);
                        if (cmt == ESP_OK) {
                            ESP_LOGD(TAG, "Persisted config: %s.%s", module_name, key);
                        } else {
                            ESP_LOGE(TAG, "Failed to commit persisted config %s.%s: %s", module_name, key, esp_err_to_name(cmt));
                        }
                    }
                    else {
                        ESP_LOGE(TAG, "Failed to set NVS value for %s.%s: %s", module_name, key, esp_err_to_name(err));
                    }
                    nvs_close(handle);
                }
                else {
                    ESP_LOGE(TAG, "Failed to open NVS for %s: %s", module_name, esp_err_to_name(err));
                }
                break;
            }
        }
    }

    // Publish full configuration after change
    return publish_full_configuration();
}

std::string ConfigurationManager::get_mqtt_subscription_topic() const {
    return "sensor/" + mac_to_string() + "/config/+/+";
}

std::string ConfigurationManager::get_mqtt_reset_subscription_topic() const {
    return "sensor/" + mac_to_string() + "/config/reset";
}

esp_err_t ConfigurationManager::handle_config_reset(const char* payload) {
    cJSON* root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config reset JSON");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting full configuration reset from MQTT");

    for (ConfigurationModule* mod : modules_) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(mod->name(), NVS_READWRITE, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open NVS for module %s, skipping reset for it.", mod->name());
            continue;
        }

        // Erase all previously persisted values for this module.
        for (const auto& desc : mod->descriptors()) {
            if (desc.persisted) {
                nvs_erase_key(handle, desc.name);
            }
        }

        cJSON* module_json = cJSON_GetObjectItem(root, mod->name());
        if (module_json) {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, module_json) {
                const char* key = item->string;
                char* value_str = cJSON_Print(item); // Note: cJSON_Print returns a string representation of the value.
                if (value_str) {
                    // For string values, cJSON_Print adds quotes, which we need to remove.
                    char* effective_value = value_str;
                    if (item->type == cJSON_String) {
                        size_t len = strlen(value_str);
                        if (len >= 2 && value_str[0] == '"' && value_str[len - 1] == '"') {
                            value_str[len - 1] = '\0';
                            effective_value = value_str + 1;
                        }
                    }
                    
                    err = mod->apply_update(key, effective_value);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "Config reset failed to apply: %s.%s -> %s", mod->name(), key, esp_err_to_name(err));
                    } else {
                        // Persist this value, bypassing the 'persisted' flag.
                        const auto& descriptors = mod->descriptors();
                        auto it = std::find_if(descriptors.begin(), descriptors.end(), [&](const ConfigurationValueDescriptor& d) {
                            return strcmp(d.name, key) == 0;
                        });

                        if (it != descriptors.end()) {
                            const auto& desc = *it;
                            switch (desc.type) {
                                case ConfigValueType::String:
                                    err = nvs_set_str(handle, key, effective_value);
                                    break;
                                case ConfigValueType::Bool: {
                                    bool v = (strcasecmp(effective_value, "1") == 0 || strcasecmp(effective_value, "true") == 0 ||
                                              strcasecmp(effective_value, "on") == 0 || strcasecmp(effective_value, "yes") == 0);
                                    err = nvs_set_u8(handle, key, v ? 1 : 0);
                                    break;
                                }
                                case ConfigValueType::I32:
                                    err = nvs_set_i32(handle, key, atoi(effective_value));
                                    break;
                                case ConfigValueType::U32:
                                    err = nvs_set_u32(handle, key, (uint32_t)strtoul(effective_value, nullptr, 10));
                                    break;
                                case ConfigValueType::I64:
                                    err = nvs_set_i64(handle, key, (int64_t)strtoll(effective_value, nullptr, 10));
                                    break;
                                default:
                                    // Types not supported for persistence.
                                    break;
                            }
                            if (err != ESP_OK) {
                                ESP_LOGE(TAG, "Failed to persist %s.%s during reset: %s", mod->name(), key, esp_err_to_name(err));
                            }
                        }
                    }
                    cJSON_free(value_str);
                }
            }
        }
        
        esp_err_t cmt_err = nvs_commit(handle);
        if (cmt_err == ESP_OK) {
            ESP_LOGD(TAG, "Persisted config for module: %s", mod->name());
        } else {
            ESP_LOGE(TAG, "Failed to commit NVS for %s: %s", mod->name(), esp_err_to_name(cmt_err));
        }

        nvs_close(handle);
        mod->mark_updated();
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Full configuration reset complete.");

    // Publish the new full configuration
    return publish_full_configuration();
}

esp_err_t ConfigurationManager::handle_mqtt_message(const char* full_topic, const char* payload) {
    // Expect topic: sensor/$mac/config/$module/$key
    // Or: sensor/$mac/config/reset
    if (!full_topic) return ESP_ERR_INVALID_ARG;
    ESP_LOGD(TAG, "MQTT config message: topic='%s' payload='%s'", full_topic, payload ? payload : "");

    // Check for reset topic first
    if (strstr(full_topic, "/config/reset")) {
        return handle_config_reset(payload);
    }

    const char* p = strstr(full_topic, "/config/");
    if (!p) {
        ESP_LOGW(TAG, "Ignoring MQTT message without /config/ segment: %s", full_topic);
        return ESP_ERR_INVALID_ARG;
    }
    p += 8; // skip "/config/"

    const char* slash = strchr(p, '/');
    if (!slash) {
        ESP_LOGW(TAG, "Invalid config topic (missing key): %s", full_topic);
        return ESP_ERR_INVALID_ARG;
    }
    std::string module(p, slash - p);
    const char* key_start = slash + 1;
    if (*key_start == '\0') {
        ESP_LOGW(TAG, "Invalid config topic (empty key): %s", full_topic);
        return ESP_ERR_INVALID_ARG;
    }
    std::string key(key_start);

    // Persist only if descriptor allows (true) when coming via MQTT
    esp_err_t res = handle_update(module.c_str(), key.c_str(), payload, true);
    if (res != ESP_OK) {
        ESP_LOGW(TAG, "Config update failed for %s.%s: %s", module.c_str(), key.c_str(), esp_err_to_name(res));
    }
    return res;
}

ConfigurationManager& GetConfigurationManager() {
    if (!g_manager) g_manager.reset(new ConfigurationManager());
    return *g_manager;
}

} // namespace config


