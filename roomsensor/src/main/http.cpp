#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_system.h"
#include "communication.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "http_server";

// HTTP server handle
static httpd_handle_t server = NULL;

// Format timestamp to ISO string in UTC
static char* format_timestamp_utc(int64_t timestamp_ms)
{
    // Validate timestamp - if we got a very small value, it might be incorrect
    if (timestamp_ms < 1577836800000) { // Jan 1, 2020 as a sanity check
        ESP_LOGW(TAG, "Invalid timestamp detected: %lld ms", timestamp_ms);
    }
    
    // Convert milliseconds to seconds and microseconds
    time_t timestamp_sec = timestamp_ms / 1000;
    
    // Convert to tm structure using UTC time
    struct tm tm_time;
    gmtime_r(&timestamp_sec, &tm_time);
    
    // Allocate buffer for formatted time
    char* time_str = (char*)malloc(32);
    if (time_str == NULL) {
        return NULL;
    }
    
    // Format as ISO 8601 with UTC (Z) indicator
    strftime(time_str, 32, "%Y-%m-%dT%H:%M:%SZ", &tm_time);
    
    return time_str;
}

// Handler for /ping GET request
static esp_err_t ping_get_handler(httpd_req_t *req)
{
    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "pong", true);
    char *json_response = cJSON_PrintUnformatted(root);
    
    // Set content type header
    httpd_resp_set_type(req, "application/json");
    
    // Send response
    httpd_resp_sendstr(req, json_response);
    
    // Free resources
    cJSON_Delete(root);
    free(json_response);
    
    return ESP_OK;
}

// Handler for /metrics GET request
static esp_err_t metrics_get_handler(httpd_req_t *req)
{
    // Get the latest metrics
    StoredMetricCollection* metrics = get_latest_metrics();
    if (metrics == NULL) {
        // Create empty response if no metrics available
        cJSON *root = cJSON_CreateObject();
        cJSON_AddArrayToObject(root, "metrics");
        
        // Convert to string
        char *json_response = cJSON_PrintUnformatted(root);
        
        // Set content type header
        httpd_resp_set_type(req, "application/json");
        
        // Send response
        httpd_resp_sendstr(req, json_response);
        
        // Free resources
        cJSON_Delete(root);
        free(json_response);
        
        return ESP_OK;
    }
    
    // Create JSON response with metrics array
    cJSON *root = cJSON_CreateObject();
    cJSON *metrics_array = cJSON_AddArrayToObject(root, "metrics");
    
    // Add each metric to the array
    for (int i = 0; i < metrics->count; i++) {
        StoredMetric *metric = &metrics->metrics[i];
        
        // Create metric object
        cJSON *metric_obj = cJSON_CreateObject();
        
        // Format timestamp as ISO string in UTC
        char* time_str = format_timestamp_utc(metric->timestamp);
        
        // Add basic metric info
        cJSON_AddStringToObject(metric_obj, "metric", metric->metric_name);
        cJSON_AddNumberToObject(metric_obj, "value", metric->value);
        cJSON_AddStringToObject(metric_obj, "timestamp", time_str ? time_str : "unknown");
        
        // Also include raw timestamp for debugging
        cJSON_AddNumberToObject(metric_obj, "timestamp_ms", metric->timestamp);
        
        // Free the formatted time string
        if (time_str) {
            free(time_str);
        }
        
        // Add tags object
        cJSON *tags_obj = cJSON_AddObjectToObject(metric_obj, "tags");
        for (int t = 0; t < metric->tags.count; t++) {
            cJSON_AddStringToObject(tags_obj, 
                                   metric->tags.tags[t].key, 
                                   metric->tags.tags[t].value);
        }
        
        // Add to metrics array
        cJSON_AddItemToArray(metrics_array, metric_obj);
    }
    
    // Convert to string
    char *json_response = cJSON_PrintUnformatted(root);
    
    // Set content type header
    httpd_resp_set_type(req, "application/json");
    
    // Send response
    httpd_resp_sendstr(req, json_response);
    
    // Free resources
    cJSON_Delete(root);
    free(json_response);
    free_metric_collection(metrics);
    
    return ESP_OK;
}

// Define the URI handlers
static const httpd_uri_t ping = {
    .uri       = "/ping",
    .method    = HTTP_GET,
    .handler   = ping_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t metrics = {
    .uri       = "/metrics",
    .method    = HTTP_GET,
    .handler   = metrics_get_handler,
    .user_ctx  = NULL
};

// Function to start the webserver
esp_err_t start_webserver(void)
{
    // Default configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &ping);
        httpd_register_uri_handler(server, &metrics);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return ESP_FAIL;
}

// Function to stop the webserver
void stop_webserver(void)
{
    if (server) {
        // Stop the httpd server
        httpd_stop(server);
        server = NULL;
    }
} 