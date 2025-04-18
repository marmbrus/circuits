#pragma once

#include "esp_err.h"

// Add new MQTT publish helper function
esp_err_t publish_to_topic(const char* subtopic, const char* message, int qos = 1, int retain = 0);

#ifdef __cplusplus
extern "C" {
#endif

// Tag system definitions
#define MAX_DEVICE_TAGS 10
#define MAX_TAG_KEY_LEN 32
#define MAX_TAG_VALUE_LEN 64

// Structure to hold a key-value tag
typedef struct {
    char key[MAX_TAG_KEY_LEN];
    char value[MAX_TAG_VALUE_LEN];
} DeviceTag;

// Structure to hold a collection of tags
typedef struct {
    DeviceTag tags[MAX_DEVICE_TAGS];
    int count;
} TagCollection;

// Initialize tag system with basic device info (call once at startup)
esp_err_t initialize_tag_system(void);

// Set test device tags (area, room, id) - updates both memory and NVS storage
esp_err_t set_device_tags_for_testing(void);

// Tag collection functions for sensor use
TagCollection* create_tag_collection(void);
esp_err_t add_tag_to_collection(TagCollection* collection, const char* key, const char* value);
void free_tag_collection(TagCollection* collection);

#ifdef __cplusplus
}
#endif
