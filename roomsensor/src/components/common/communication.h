#pragma once

#include "esp_err.h"

// Add new MQTT publish helper function
esp_err_t publish_to_topic(const char* subtopic, const char* message, int qos = 1, int retain = 0);
