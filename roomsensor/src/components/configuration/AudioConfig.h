#pragma once

#include "ConfigurationModule.h"
#include <string>
#include <vector>

// Forward declare to avoid adding heavy dependency to all includers
struct cJSON;

namespace config {

// I2S audio: a speaker amp (e.g. MAX98357A), a microphone (e.g. ICS-43434), and
// playback control. Pins are persisted per board; url and volume are runtime
// controls: setting url plays that MP3 over HTTP(S), and an empty url stops playback.
class AudioConfig : public ConfigurationModule {
public:
    AudioConfig();
    ~AudioConfig() override = default;

    // ConfigurationModule API
    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // The speaker needs the I2S clock, word select, and data out pins; the mic
    // shares the clock and word select pins and needs data in
    bool has_clock_pins() const { return bclk_gpio_ >= 0 && ws_gpio_ >= 0; }
    bool has_output_pins() const { return has_clock_pins() && dout_gpio_ >= 0; }
    bool has_input_pins() const { return has_clock_pins() && din_gpio_ >= 0; }

    int bclk_gpio() const { return bclk_gpio_; }
    int ws_gpio() const { return ws_gpio_; }
    int dout_gpio() const { return dout_gpio_; }
    int din_gpio() const { return din_gpio_; }
    int enable_gpio() const { return enable_gpio_; }

    const std::string& url() const { return url_; }
    // 0..100
    int volume() const { return volume_; }

private:
    esp_err_t set_gpio(int& field, const char* value_str);

    int bclk_gpio_ = -1;
    int ws_gpio_ = -1;
    int dout_gpio_ = -1;
    int din_gpio_ = -1;
    int enable_gpio_ = -1;

    std::string url_;
    int volume_ = 50;

    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config
