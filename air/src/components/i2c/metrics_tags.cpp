#include "communication.h"
#include "ConfigurationManager.h"
#include "TagsConfig.h"
#include <string.h>
#include <string>
#include "esp_mac.h"
#include <stdio.h>

// Implement TagCollection helpers for metrics, seeding from TagsConfig

TagCollection* create_tag_collection(void) {
    TagCollection* collection = (TagCollection*)malloc(sizeof(TagCollection));
    if (collection == NULL) {
        return NULL;
    }
    collection->count = 0;

    // Seed with device tags from configuration
    using namespace config;
    TagsConfig& tags = GetConfigurationManager().tags();

    // area
    if (collection->count < MAX_DEVICE_TAGS) {
        strncpy(collection->tags[collection->count].key, "area", MAX_TAG_KEY_LEN - 1);
        collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
        strncpy(collection->tags[collection->count].value, tags.area().c_str(), MAX_TAG_VALUE_LEN - 1);
        collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
        collection->count++;
    }
    // room
    if (collection->count < MAX_DEVICE_TAGS) {
        strncpy(collection->tags[collection->count].key, "room", MAX_TAG_KEY_LEN - 1);
        collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
        strncpy(collection->tags[collection->count].value, tags.room().c_str(), MAX_TAG_VALUE_LEN - 1);
        collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
        collection->count++;
    }
    // id
    if (collection->count < MAX_DEVICE_TAGS) {
        strncpy(collection->tags[collection->count].key, "id", MAX_TAG_KEY_LEN - 1);
        collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
        strncpy(collection->tags[collection->count].value, tags.id().c_str(), MAX_TAG_VALUE_LEN - 1);
        collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
        collection->count++;
    }
    // Virtual tags: mac and sensor (computed, not part of ConfigModule)
    if (collection->count < MAX_DEVICE_TAGS) {
        strncpy(collection->tags[collection->count].key, "mac", MAX_TAG_KEY_LEN - 1);
        collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
        uint8_t mac_bytes[6] = {0};
        esp_read_mac(mac_bytes, ESP_MAC_WIFI_STA);
        char mac_buf[13];
        snprintf(mac_buf, sizeof(mac_buf), "%02X%02X%02X%02X%02X%02X",
                 mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);
        strncpy(collection->tags[collection->count].value, mac_buf, MAX_TAG_VALUE_LEN - 1);
        collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
        collection->count++;
    }
    // sensor (room-id)
    if (collection->count < MAX_DEVICE_TAGS) {
        strncpy(collection->tags[collection->count].key, "sensor", MAX_TAG_KEY_LEN - 1);
        collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
        std::string sensor = tags.room() + std::string("-") + tags.id();
        strncpy(collection->tags[collection->count].value, sensor.c_str(), MAX_TAG_VALUE_LEN - 1);
        collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
        collection->count++;
    }

    return collection;
}

esp_err_t add_tag_to_collection(TagCollection* collection, const char* key, const char* value) {
    if (collection == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Update if exists
    for (int i = 0; i < collection->count; i++) {
        if (strcmp(collection->tags[i].key, key) == 0) {
            strncpy(collection->tags[i].value, value, MAX_TAG_VALUE_LEN - 1);
            collection->tags[i].value[MAX_TAG_VALUE_LEN - 1] = '\0';
            return ESP_OK;
        }
    }
    // Append
    if (collection->count >= MAX_DEVICE_TAGS) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(collection->tags[collection->count].key, key, MAX_TAG_KEY_LEN - 1);
    collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
    strncpy(collection->tags[collection->count].value, value, MAX_TAG_VALUE_LEN - 1);
    collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
    collection->count++;
    return ESP_OK;
}

esp_err_t remove_tag_from_collection(TagCollection* collection, const char* key) {
    if (collection == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < collection->count; i++) {
        if (strcmp(collection->tags[i].key, key) == 0) {
            // Shift elements down to fill gap
            for (int j = i + 1; j < collection->count; j++) {
                collection->tags[j - 1] = collection->tags[j];
            }
            collection->count--;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void free_tag_collection(TagCollection* collection) {
    if (collection != NULL) {
        free(collection);
    }
}


