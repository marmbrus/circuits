#include "communication.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "system_state.h"
#include <string.h>
#include "esp_timer.h"
#include <time.h>
#include "freertos/semphr.h"

static const char* TAG = "metrics";

// Queue handle for metric reports
static QueueHandle_t metrics_queue = NULL;
#define METRICS_QUEUE_SIZE 50

// Task handle for the metrics reporting task
static TaskHandle_t metrics_task_handle = NULL;

// Latest metrics storage
#define INITIAL_METRICS_CAPACITY 20
#define MAX_METRICS_CAPACITY 100
static StoredMetric* latest_metrics = NULL;
static int metrics_count = 0;
static int metrics_capacity = 0;
static SemaphoreHandle_t metrics_mutex = NULL;

// Safe string concatenation function
static void safe_strcat(char* dst, size_t dst_size, const char* src) {
    if (dst == NULL || src == NULL || dst_size == 0) {
        return;
    }

    size_t dst_len = strlen(dst);
    if (dst_len >= dst_size - 1) {
        return;  // No space left
    }

    size_t i;
    for (i = 0; i < dst_size - dst_len - 1 && src[i] != '\0'; i++) {
        dst[dst_len + i] = src[i];
    }
    dst[dst_len + i] = '\0';
}

// Function to build the MQTT topic for a metric
static char* build_metric_topic(const char* metric_name, const TagCollection* tags) {
    // Find the necessary tags
    const char* area = "unknown";
    const char* room = "unknown";
    const char* id = "unknown";

    for (int i = 0; i < tags->count; i++) {
        if (strcmp(tags->tags[i].key, "area") == 0) {
            area = tags->tags[i].value;
        } else if (strcmp(tags->tags[i].key, "room") == 0) {
            room = tags->tags[i].value;
        } else if (strcmp(tags->tags[i].key, "id") == 0) {
            id = tags->tags[i].value;
        }
    }

    // Allocate topic buffer with generous size
    char* topic = (char*)malloc(512);
    if (topic == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for topic string");
        return NULL;
    }

    // Use safe string operations with new format: roomsensor/$metric_name/$area/$room/$id
    topic[0] = '\0';  // Initialize to empty string
    safe_strcat(topic, 512, "roomsensor/");
    safe_strcat(topic, 512, metric_name);
    safe_strcat(topic, 512, "/");
    safe_strcat(topic, 512, area);
    safe_strcat(topic, 512, "/");
    safe_strcat(topic, 512, room);
    safe_strcat(topic, 512, "/");
    safe_strcat(topic, 512, id);

    return topic;
}

// Function to create a JSON message from a metric report
static char* create_json_message(const MetricReport* report) {
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return NULL;
    }

    // Add metric name and value
    cJSON_AddStringToObject(root, "metric", report->metric_name);
    cJSON_AddNumberToObject(root, "value", report->value);

    // Add tags as a nested object
    cJSON* tags_obj = cJSON_AddObjectToObject(root, "tags");
    if (tags_obj == NULL) {
        ESP_LOGE(TAG, "Failed to create tags JSON object");
        cJSON_Delete(root);
        return NULL;
    }

    // Add each tag to the tags object
    for (int i = 0; i < report->tags->count; i++) {
        const DeviceTag* tag = &report->tags->tags[i];
        cJSON_AddStringToObject(tags_obj, tag->key, tag->value);
    }

    // Generate the JSON string
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        return NULL;
    }

    return json_str;
}

// Generate a hash key for a metric based on name and tags
static uint32_t hash_metric(const char* metric_name, const TagCollection* tags) {
    // Simple hash function for metric name + tags
    uint32_t hash = 5381;
    
    // Hash the metric name
    for (const char* c = metric_name; *c; c++) {
        hash = ((hash << 5) + hash) + *c; // hash * 33 + c
    }

    // Hash each tag in a consistent order (tags are already sorted alphabetically by key)
    for (int i = 0; i < tags->count; i++) {
        const DeviceTag* tag = &tags->tags[i];
        
        // Hash the key
        for (const char* c = tag->key; *c; c++) {
            hash = ((hash << 5) + hash) + *c;
        }
        
        // Hash the value
        for (const char* c = tag->value; *c; c++) {
            hash = ((hash << 5) + hash) + *c;
        }
    }
    
    return hash;
}

// Find a metric in the latest metrics storage by name and tags
static int find_metric_index(const char* metric_name, const TagCollection* tags) {
    uint32_t target_hash = hash_metric(metric_name, tags);
    
    for (int i = 0; i < metrics_count; i++) {
        uint32_t current_hash = hash_metric(latest_metrics[i].metric_name, &latest_metrics[i].tags);
        if (current_hash == target_hash) {
            // Double-check with actual values to avoid collisions
            if (strcmp(latest_metrics[i].metric_name, metric_name) == 0) {
                // Check if tags match
                bool tags_match = true;
                if (latest_metrics[i].tags.count == tags->count) {
                    for (int t = 0; t < tags->count; t++) {
                        bool tag_found = false;
                        for (int s = 0; s < latest_metrics[i].tags.count; s++) {
                            if (strcmp(latest_metrics[i].tags.tags[s].key, tags->tags[t].key) == 0 && 
                                strcmp(latest_metrics[i].tags.tags[s].value, tags->tags[t].value) == 0) {
                                tag_found = true;
                                break;
                            }
                        }
                        if (!tag_found) {
                            tags_match = false;
                            break;
                        }
                    }
                    if (tags_match) {
                        return i;
                    }
                }
            }
        }
    }
    
    return -1; // Not found
}

// Store a metric in the latest metrics storage
static esp_err_t store_latest_metric(const char* metric_name, float value, const TagCollection* tags) {
    if (metrics_mutex == NULL) {
        ESP_LOGE(TAG, "Metrics mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Get current timestamp 
    // esp_timer_get_time returns microseconds since boot, not since epoch
    int64_t now_microsec = esp_timer_get_time();
    
    // For timestamps we need time since epoch, not since boot
    // Get current time as seconds since epoch
    time_t now_sec;
    time(&now_sec);
    
    // Convert to milliseconds and add the sub-second part from esp_timer
    int64_t timestamp = (now_sec * 1000) + ((now_microsec / 1000) % 1000);
    
    ESP_LOGD(TAG, "Storing metric '%s' with timestamp: %lld ms", metric_name, timestamp);
    
    // Take mutex for the update
    if (xSemaphoreTake(metrics_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take metrics mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // Find if this metric already exists
    int index = find_metric_index(metric_name, tags);
    
    if (index >= 0) {
        // Update existing metric (no need for lock since we're just updating a value)
        latest_metrics[index].value = value;
        latest_metrics[index].timestamp = timestamp;
        xSemaphoreGive(metrics_mutex);
        return ESP_OK;
    }
    
    // This is a new metric, ensure we have capacity
    if (metrics_count >= metrics_capacity) {
        // Need to grow the array
        int new_capacity = metrics_capacity == 0 ? INITIAL_METRICS_CAPACITY : metrics_capacity * 2;
        if (new_capacity > MAX_METRICS_CAPACITY) {
            new_capacity = MAX_METRICS_CAPACITY;
        }
        
        if (metrics_count >= MAX_METRICS_CAPACITY) {
            ESP_LOGE(TAG, "Maximum metrics capacity reached (%d)", MAX_METRICS_CAPACITY);
            xSemaphoreGive(metrics_mutex);
            return ESP_ERR_NO_MEM;
        }
        
        // Allocate new array
        StoredMetric* new_metrics = (StoredMetric*)realloc(latest_metrics, new_capacity * sizeof(StoredMetric));
        if (new_metrics == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for metrics storage");
            xSemaphoreGive(metrics_mutex);
            return ESP_ERR_NO_MEM;
        }
        
        latest_metrics = new_metrics;
        metrics_capacity = new_capacity;
        ESP_LOGI(TAG, "Resized metrics storage to %d entries", metrics_capacity);
    }
    
    // Add the new metric
    StoredMetric* new_metric = &latest_metrics[metrics_count];
    
    // Copy metric name
    strncpy(new_metric->metric_name, metric_name, MAX_METRIC_NAME_LEN - 1);
    new_metric->metric_name[MAX_METRIC_NAME_LEN - 1] = '\0';
    
    // Set value and timestamp
    new_metric->value = value;
    new_metric->timestamp = timestamp;
    
    // Copy tags
    new_metric->tags.count = 0;
    for (int i = 0; i < tags->count && i < MAX_DEVICE_TAGS; i++) {
        strncpy(new_metric->tags.tags[i].key, tags->tags[i].key, MAX_TAG_KEY_LEN - 1);
        new_metric->tags.tags[i].key[MAX_TAG_KEY_LEN - 1] = '\0';
        
        strncpy(new_metric->tags.tags[i].value, tags->tags[i].value, MAX_TAG_VALUE_LEN - 1);
        new_metric->tags.tags[i].value[MAX_TAG_VALUE_LEN - 1] = '\0';
        
        new_metric->tags.count++;
    }
    
    // Increment count
    metrics_count++;
    
    ESP_LOGI(TAG, "Added new metric '%s' (total: %d/%d)", metric_name, metrics_count, metrics_capacity);
    
    xSemaphoreGive(metrics_mutex);
    return ESP_OK;
}

// The metrics reporting task
static void metrics_reporting_task(void* pvParameters) {
    ESP_LOGI(TAG, "Metrics reporting task started");

    MetricReport report;

    while (1) {
        // Wait for a new metric report
        if (xQueueReceive(metrics_queue, &report, portMAX_DELAY) == pdTRUE) {
            // Check system state before trying to publish
            if (get_system_state() != FULLY_CONNECTED) {
                ESP_LOGW(TAG, "System not fully connected, waiting before publishing metric %s", report.metric_name);

                // Wait for system to be fully connected, checking every second
                // Maximum retry count of 30 (30 seconds)
                int retry_count = 0;
                const int max_retries = 30;

                while (get_system_state() != FULLY_CONNECTED && retry_count < max_retries) {
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
                    retry_count++;
                }

                if (get_system_state() != FULLY_CONNECTED) {
                    ESP_LOGW(TAG, "System still not connected after waiting, discarding metric %s", report.metric_name);
                    continue; // Skip this metric
                }

                ESP_LOGI(TAG, "System now connected, proceeding with publishing metric %s", report.metric_name);
            }

            // Build the topic string
            char* topic = build_metric_topic(report.metric_name, report.tags);
            if (topic == NULL) {
                ESP_LOGE(TAG, "Failed to build topic string");
                continue;
            }

            // Create the JSON message
            char* json_str = create_json_message(&report);
            if (json_str == NULL) {
                ESP_LOGE(TAG, "Failed to create JSON message");
                free(topic);
                continue;
            }

            // Publish the message
            esp_err_t err = publish_to_topic(topic, json_str, 1, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to publish metric: %s", esp_err_to_name(err));
            } else {
                ESP_LOGD(TAG, "Published metric to %s: %s", topic, json_str);
            }
            
            // In addition to publishing, also store the latest value
            store_latest_metric(report.metric_name, report.value, report.tags);

            // Clean up dynamically allocated memory
            free(topic);
            free(json_str);

            // Add a small delay between publishing to prevent overwhelming MQTT
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// Initialize metrics system and start the reporting task
esp_err_t initialize_metrics_system(void) {
    // Create the metrics mutex
    metrics_mutex = xSemaphoreCreateMutex();
    if (metrics_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create metrics mutex");
        return ESP_FAIL;
    }
    
    // Initialize the metrics storage
    metrics_capacity = INITIAL_METRICS_CAPACITY;
    latest_metrics = (StoredMetric*)malloc(metrics_capacity * sizeof(StoredMetric));
    if (latest_metrics == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for metrics storage");
        vSemaphoreDelete(metrics_mutex);
        metrics_mutex = NULL;
        return ESP_FAIL;
    }
    metrics_count = 0;
    
    // Create the metrics queue
    metrics_queue = xQueueCreate(METRICS_QUEUE_SIZE, sizeof(MetricReport));
    if (metrics_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create metrics queue");
        free(latest_metrics);
        latest_metrics = NULL;
        vSemaphoreDelete(metrics_mutex);
        metrics_mutex = NULL;
        return ESP_FAIL;
    }

    // Create the reporting task
    BaseType_t ret = xTaskCreate(
        metrics_reporting_task,
        "metrics_task",
        4096,           // Stack size
        NULL,           // Parameters
        5,              // Priority
        &metrics_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create metrics reporting task");
        vQueueDelete(metrics_queue);
        metrics_queue = NULL;
        free(latest_metrics);
        latest_metrics = NULL;
        vSemaphoreDelete(metrics_mutex);
        metrics_mutex = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Metrics system initialized and reporting task started");
    return ESP_OK;
}

// Report a new metric (can be called from any task or interrupt handler)
esp_err_t report_metric(const char* metric_name, float value, TagCollection* tags) {
    if (metrics_queue == NULL) {
        ESP_LOGE(TAG, "Metrics queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (metric_name == NULL || tags == NULL) {
        ESP_LOGE(TAG, "Invalid metric parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Create a metric report with direct pointer assignment
    MetricReport report;
    report.metric_name = metric_name;  // Directly store the pointer to the static string
    report.value = value;
    report.tags = tags;

    // Get queue information for debugging
    UBaseType_t queue_spaces = uxQueueSpacesAvailable(metrics_queue);
    UBaseType_t queue_messages = uxQueueMessagesWaiting(metrics_queue);
    UBaseType_t queue_size = METRICS_QUEUE_SIZE;

    // Send the report to the queue
    if (xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (xQueueSendFromISR(metrics_queue, &report, &xHigherPriorityTaskWoken) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send metric report to queue from ISR - metric: %s, value: %.3f, queue full: %d/%d",
                    metric_name, value, queue_messages, queue_size);
            return ESP_FAIL;
        }

        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (xQueueSend(metrics_queue, &report, 0) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send metric report to queue - metric: %s, value: %.3f, queue spaces: %d/%d",
                    metric_name, value, queue_spaces, queue_size);

            // Add more debugging info about tag collection
            if (tags != NULL) {
                ESP_LOGE(TAG, "Tags collection info - count: %d", tags->count);
                for (int i = 0; i < tags->count && i < MAX_DEVICE_TAGS; i++) {
                    ESP_LOGE(TAG, "  Tag[%d]: %s = %s",
                            i, tags->tags[i].key, tags->tags[i].value);
                }
            }

            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

// Get the latest metrics
StoredMetricCollection* get_latest_metrics(void) {
    if (metrics_mutex == NULL || latest_metrics == NULL) {
        ESP_LOGE(TAG, "Metrics storage not initialized");
        return NULL;
    }
    
    // Take mutex for the read
    if (xSemaphoreTake(metrics_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take metrics mutex for read");
        return NULL;
    }
    
    // Allocate collection structure
    StoredMetricCollection* collection = (StoredMetricCollection*)malloc(sizeof(StoredMetricCollection));
    if (collection == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for metrics collection");
        xSemaphoreGive(metrics_mutex);
        return NULL;
    }
    
    // Set up collection
    collection->count = metrics_count;
    collection->capacity = metrics_count;
    
    if (metrics_count == 0) {
        collection->metrics = NULL;
        xSemaphoreGive(metrics_mutex);
        return collection;
    }
    
    // Allocate metrics array
    collection->metrics = (StoredMetric*)malloc(metrics_count * sizeof(StoredMetric));
    if (collection->metrics == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for metrics array copy");
        free(collection);
        xSemaphoreGive(metrics_mutex);
        return NULL;
    }
    
    // Copy metrics
    memcpy(collection->metrics, latest_metrics, metrics_count * sizeof(StoredMetric));
    
    xSemaphoreGive(metrics_mutex);
    return collection;
}

// Free a metric collection
void free_metric_collection(StoredMetricCollection* collection) {
    if (collection == NULL) {
        return;
    }
    
    if (collection->metrics != NULL) {
        free(collection->metrics);
    }
    
    free(collection);
}