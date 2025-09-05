#include "i2c_sensor.h"
#include "esp_log.h"

static const char *TAG = "I2CSensor";

I2CSensor::I2CSensor(i2c_master_bus_handle_t bus_handle) 
    : _bus_handle(bus_handle), _dev_handle(nullptr) {
    ESP_LOGD(TAG, "I2CSensor base class constructed");
} 