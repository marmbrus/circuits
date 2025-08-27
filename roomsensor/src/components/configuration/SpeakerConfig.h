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

private:
	std::vector<ConfigurationValueDescriptor> descriptors_;
	uint32_t sdin_gpio_ = 0;
	uint32_t sclk_gpio_ = 0;
	uint32_t lrclk_gpio_ = 0;
	bool sdin_set_ = false;
	bool sclk_set_ = false;
	bool lrclk_set_ = false;
};

} // namespace config
