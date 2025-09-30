#pragma once

#include "esp_err.h"
#include "ConfigurationModule.h"
#include "communication.h"
#include <memory>
#include <vector>
#include <string>

// Forward declare cJSON to keep headers light
struct cJSON;

namespace config {

class WifiConfig;
class TagsConfig;
class DeviceConfig;
class LEDConfig;
class A2DConfig;
class IOConfig;
class MotionConfig;
class I2CConfig;

class ConfigurationManager {
public:
    ConfigurationManager();
    ~ConfigurationManager();

    // Initialize after NVS is ready; loads persisted values and publishes current config
    esp_err_t initialize();

    // Publish full configuration to sensor/$mac/config/current (retained)
    esp_err_t publish_full_configuration();

    // Handle a single update (from console or MQTT)
    esp_err_t handle_update(const char* module_name, const char* key, const char* value_str, bool persist_if_supported);

    // Accessors for modules
    WifiConfig& wifi();
    TagsConfig& tags();
    DeviceConfig& device();
    LEDConfig& led1();
    LEDConfig& led2();
    LEDConfig& led3();
    LEDConfig& led4();
    A2DConfig& a2d1();
    A2DConfig& a2d2();
    A2DConfig& a2d3();
    A2DConfig& a2d4();
    // Motion sensor
    MotionConfig& motion();
    // IO expanders
    IOConfig& io1();
    IOConfig& io2();
    IOConfig& io3();
    IOConfig& io4();
    IOConfig& io5();
    IOConfig& io6();
    IOConfig& io7();
    IOConfig& io8();
    // I2C address->driver mapping
    I2CConfig& i2cmap();

    // Returns all LED configs that are active (dataGPIO is set)
    std::vector<LEDConfig*> active_leds() const;

    // MQTT integration helpers
    std::string get_mqtt_subscription_topic() const; // sensor/$mac/config/+/+
    std::string get_mqtt_reset_subscription_topic() const; // sensor/$mac/config/reset
    esp_err_t handle_mqtt_message(const char* full_topic, const char* payload);

private:
    // Handle full config reset from JSON payload.
    esp_err_t handle_config_reset(const char* payload);

    // Registers all statically known modules
    void register_modules();

    // Finds module by name
    ConfigurationModule* find_module(const char* module_name);

    // Builds a cJSON object with the entire configuration
    cJSON* build_full_config_json() const;

    // Owned module instances
    std::unique_ptr<WifiConfig> wifi_module_;
    std::unique_ptr<TagsConfig> tags_module_;
    std::unique_ptr<DeviceConfig> device_module_;
    std::unique_ptr<LEDConfig> led1_module_;
    std::unique_ptr<LEDConfig> led2_module_;
    std::unique_ptr<LEDConfig> led3_module_;
    std::unique_ptr<LEDConfig> led4_module_;
    std::unique_ptr<A2DConfig> a2d1_module_;
    std::unique_ptr<A2DConfig> a2d2_module_;
    std::unique_ptr<A2DConfig> a2d3_module_;
    std::unique_ptr<A2DConfig> a2d4_module_;
    std::unique_ptr<MotionConfig> motion_module_;
    std::unique_ptr<IOConfig> io1_module_;
    std::unique_ptr<IOConfig> io2_module_;
    std::unique_ptr<IOConfig> io3_module_;
    std::unique_ptr<IOConfig> io4_module_;
    std::unique_ptr<IOConfig> io5_module_;
    std::unique_ptr<IOConfig> io6_module_;
    std::unique_ptr<IOConfig> io7_module_;
    std::unique_ptr<IOConfig> io8_module_;
    std::unique_ptr<I2CConfig> i2cmap_module_;
    std::vector<ConfigurationModule*> modules_;
};

// Global singleton accessor
ConfigurationManager& GetConfigurationManager();

} // namespace config


