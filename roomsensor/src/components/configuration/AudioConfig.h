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
// highpassHz, eqHz and eqGainDb tune the output for the board's speaker and are persisted.
// Volume starts at 0, so nothing is audible until it is set.
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
    // Incremented on every url update, including a republish of the same url.
    // Playback follows this rather than generation(), so other updates (a config
    // reset, a pin change) never start or restart playback.
    uint32_t url_requests() const { return url_requests_; }
    // 0..100
    int volume() const { return volume_; }

    // Speaker tuning: a 2nd-order high-pass at highpass_hz() and a one-octave peaking
    // filter of eq_gain_db() at eq_hz(); 0 Hz disables either
    int highpass_hz() const { return highpass_hz_; }
    int eq_hz() const { return eq_hz_; }
    int eq_gain_db() const { return eq_gain_db_; }

private:
    esp_err_t set_gpio(int& field, const char* value_str);
    esp_err_t set_int(int& field, const char* value_str, int min, int max);

    int bclk_gpio_ = -1;
    int ws_gpio_ = -1;
    int dout_gpio_ = -1;
    int din_gpio_ = -1;
    int enable_gpio_ = -1;

    int highpass_hz_ = 0;
    int eq_hz_ = 0;
    int eq_gain_db_ = 0;

    std::string url_;
    uint32_t url_requests_ = 0;
    int volume_ = 0;

    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config
