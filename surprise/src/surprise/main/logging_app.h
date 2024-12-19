#pragma once
#include "application.h"
#include "esp_log.h"

class LoggingApp : public Application {
private:
    static const char* TAG;

public:
    void onButton1Pressed() override {
        ESP_LOGI(TAG, "Button 1 was pressed!");
    }

    void onButton2Pressed() override {
        ESP_LOGI(TAG, "Button 2 was pressed!");
    }

    void onButton3Pressed() override {
        ESP_LOGI(TAG, "Button 3 was pressed!");
    }

    void onButton4Pressed() override {
        ESP_LOGI(TAG, "Button 4 was pressed!");
    }
};