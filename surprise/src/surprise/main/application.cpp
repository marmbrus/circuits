#include "application.h"
#include "esp_log.h"

static const char* TAG = "Application";

void Application::onMovementDetected() {
    ESP_LOGI(TAG, "Movement detected!");
    // Implement the logic to handle movement detection
}