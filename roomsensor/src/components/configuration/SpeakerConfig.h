#pragma once

#include "ConfigurationModule.h"
#include "configuration_types.h"
#include <string>
#include <vector>

// Forward declare to avoid heavy include in header
struct cJSON;

namespace config {

class SpeakerConfig : public ConfigurationModule {
public:
	SpeakerConfig();
	~SpeakerConfig() override = default;

	const char* name() const override;
	const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
	esp_err_t apply_update(const char* key, const char* value_str) override;
	esp_err_t to_json(struct cJSON* root_object) const override;

	bool has_sdin() const { return sdin_set_; }
	bool has_sclk() const { return sclk_set_; }
	bool has_lrclk() const { return lrclk_set_; }
	uint32_t sdin() const { return sdin_gpio_; }
	uint32_t sclk() const { return sclk_gpio_; }
	uint32_t lrclk() const { return lrclk_gpio_; }

	// Dynamic playback controls (non-persistent)
	bool has_sine() const { return sine_set_; }
	int32_t sine_hz() const { return sine_hz_; }
	bool has_url() const { return url_set_; }
	const std::string& url() const { return url_; }
	bool has_volume() const { return volume_set_; }
	int32_t volume() const { return volume_; }

private:
	std::vector<ConfigurationValueDescriptor> descriptors_;
	uint32_t sdin_gpio_ = 0;
	uint32_t sclk_gpio_ = 0;
	uint32_t lrclk_gpio_ = 0;
	bool sdin_set_ = false;
	bool sclk_set_ = false;
	bool lrclk_set_ = false;

	// Non-persistent runtime controls
	int32_t sine_hz_ = 0; bool sine_set_ = false;
	std::string url_; bool url_set_ = false;
	int32_t volume_ = -1; bool volume_set_ = false;
};

} // namespace config
