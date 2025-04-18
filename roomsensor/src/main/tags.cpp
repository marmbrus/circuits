#include "communication.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "wifi.h"  // For get_device_mac()
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "tags";

// Static array to store the device tags
static DeviceTag s_device_tags[MAX_DEVICE_TAGS];
static int s_tag_count = 0;

// Internal function to add or update a tag
static esp_err_t add_device_tag(const char* key, const char* value) {
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // First check if this key already exists
    for (int i = 0; i < s_tag_count; i++) {
        if (strcmp(s_device_tags[i].key, key) == 0) {
            // Update existing tag
            strncpy(s_device_tags[i].value, value, MAX_TAG_VALUE_LEN - 1);
            s_device_tags[i].value[MAX_TAG_VALUE_LEN - 1] = '\0';
            return ESP_OK;
        }
    }

    // If we got here, it's a new tag - check if we have space
    if (s_tag_count >= MAX_DEVICE_TAGS) {
        return ESP_ERR_NO_MEM;
    }

    // Add new tag
    strncpy(s_device_tags[s_tag_count].key, key, MAX_TAG_KEY_LEN - 1);
    s_device_tags[s_tag_count].key[MAX_TAG_KEY_LEN - 1] = '\0';
    
    strncpy(s_device_tags[s_tag_count].value, value, MAX_TAG_VALUE_LEN - 1);
    s_device_tags[s_tag_count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
    
    s_tag_count++;
    return ESP_OK;
}

// Load a tag from NVS storage
static esp_err_t load_tag_from_nvs(const char* key) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("tags", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for tag '%s': %s", key, esp_err_to_name(err));
        return err;
    }
    
    char value[MAX_TAG_VALUE_LEN];
    size_t required_size = sizeof(value);
    
    err = nvs_get_str(nvs_handle, key, value, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Tag '%s' not found in NVS: %s", key, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = add_device_tag(key, value);
    nvs_close(nvs_handle);
    
    return err;
}

// Save a tag to NVS storage
static esp_err_t save_tag_to_nvs(const char* key, const char* value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("tags", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for saving tag '%s': %s", key, esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save tag '%s' to NVS: %s", key, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes for tag '%s': %s", key, esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return err;
}

// Initialize tag system with basic device info and load settings from NVS
esp_err_t initialize_tag_system(void) {
    // Reset tag count
    s_tag_count = 0;

    // Get MAC address from wifi.h
    const uint8_t* mac = get_device_mac();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Set device-specific tags
    esp_err_t err;
    
    // MAC address (always set from hardware)
    err = add_device_tag("mac_address", mac_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MAC address tag: %s", esp_err_to_name(err));
        return err;
    }
    
    // Try to load device tags from NVS (area, room, id)
    esp_err_t area_err = load_tag_from_nvs("area");
    esp_err_t room_err = load_tag_from_nvs("room");
    esp_err_t id_err = load_tag_from_nvs("id");
    
    if (area_err != ESP_OK || room_err != ESP_OK || id_err != ESP_OK) {
        ESP_LOGW(TAG, "Some device tags not found in NVS. Use set_device_tags_for_testing to configure them.");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Log the tags
    ESP_LOGI(TAG, "Device tags initialized successfully:");
    for (int i = 0; i < s_tag_count; i++) {
        ESP_LOGI(TAG, "  %s: %s", s_device_tags[i].key, s_device_tags[i].value);
    }
    
    return ESP_OK;
}

// Set test tags (updates both memory and NVS storage)
esp_err_t set_device_tags_for_testing(void) {
    esp_err_t err;
    
    // Get MAC address for ID generation
    const uint8_t* mac = get_device_mac();
    char id_value[16];
    snprintf(id_value, sizeof(id_value), "test%02X%02X", mac[4], mac[5]);
    
    // Test values to store
    struct {
        const char* key;
        const char* value;
    } test_tags[] = {
        {"area", "TestArea"},
        {"room", "TestRoom"},
        {"id", id_value}  // Dynamic ID using last 4 digits of MAC
    };
    
    for (size_t i = 0; i < sizeof(test_tags) / sizeof(test_tags[0]); i++) {
        // Update in-memory tag
        err = add_device_tag(test_tags[i].key, test_tags[i].value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add tag %s: %s", test_tags[i].key, esp_err_to_name(err));
            return err;
        }
        
        // Save to NVS for persistence
        err = save_tag_to_nvs(test_tags[i].key, test_tags[i].value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save tag %s to NVS: %s", test_tags[i].key, esp_err_to_name(err));
            return err;
        }
    }
    
    ESP_LOGI(TAG, "Test tags set and saved to NVS:");
    for (int i = 0; i < s_tag_count; i++) {
        ESP_LOGI(TAG, "  %s: %s", s_device_tags[i].key, s_device_tags[i].value);
    }
    
    return ESP_OK;
}

// Create a new tag collection based on the device tags
TagCollection* create_tag_collection(void) {
    TagCollection* collection = (TagCollection*)malloc(sizeof(TagCollection));
    if (collection == NULL) {
        return NULL;
    }
    
    // Copy existing device tags
    collection->count = s_tag_count;
    for (int i = 0; i < s_tag_count; i++) {
        memcpy(&collection->tags[i], &s_device_tags[i], sizeof(DeviceTag));
    }
    
    return collection;
}

// Add a new tag to a tag collection (for sensor-specific tags)
esp_err_t add_tag_to_collection(TagCollection* collection, const char* key, const char* value) {
    if (collection == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // First check if this key already exists in the collection
    for (int i = 0; i < collection->count; i++) {
        if (strcmp(collection->tags[i].key, key) == 0) {
            // Update existing tag
            strncpy(collection->tags[i].value, value, MAX_TAG_VALUE_LEN - 1);
            collection->tags[i].value[MAX_TAG_VALUE_LEN - 1] = '\0';
            return ESP_OK;
        }
    }
    
    // If we got here, it's a new tag - check if we have space
    if (collection->count >= MAX_DEVICE_TAGS) {
        return ESP_ERR_NO_MEM;
    }
    
    // Add new tag
    strncpy(collection->tags[collection->count].key, key, MAX_TAG_KEY_LEN - 1);
    collection->tags[collection->count].key[MAX_TAG_KEY_LEN - 1] = '\0';
    
    strncpy(collection->tags[collection->count].value, value, MAX_TAG_VALUE_LEN - 1);
    collection->tags[collection->count].value[MAX_TAG_VALUE_LEN - 1] = '\0';
    
    collection->count++;
    return ESP_OK;
}

// Free a tag collection when done with it
void free_tag_collection(TagCollection* collection) {
    if (collection != NULL) {
        free(collection);
    }
} 