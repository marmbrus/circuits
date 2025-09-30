#include "i2c_sensor.h"
#include "lis2dh_sensor.h"
#include "bme280_sensor.h"
#include "sen55_sensor.h"
#include "scd4x_sensor.h"
#include "ads1115_sensor.h"
#include "i2c_master_ext.h"
#include "opt3001_sensor.h"
#include "mcp23008_sensor.h"
#include "lmp91000_sensor.h"
#include "i2c_telemetry.h"
#include "ConfigurationManager.h"
#include "I2CConfig.h"
#include <algorithm>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <vector>

static const char* TAG = "I2C";

// Array of all possible sensors
static I2CSensor* s_sensors[] = {
    new LIS2DHSensor(),
    new BME280Sensor(),
    new SEN55Sensor(),
    new SCD4xSensor(),
    new OPT3001Sensor(), // OPT3001 at default 0x44
    // ADS1115 ADCs at all four possible addresses
    new ADS1115Sensor(0x48),
    new ADS1115Sensor(0x49),
    new ADS1115Sensor(0x4A),
    new ADS1115Sensor(0x4B),
    // LMP91000 potentiostat (default address often 0x48)
    new LMP91000Sensor(0x48),
    // MCP23008 GPIO expanders at all valid addresses (0x20-0x27)
    new MCP23008Sensor(0x20), //Conflicts with PD controller
    new MCP23008Sensor(0x21),
    new MCP23008Sensor(0x22),
    new MCP23008Sensor(0x23),
    new MCP23008Sensor(0x24),
    new MCP23008Sensor(0x25),
    new MCP23008Sensor(0x26),
    new MCP23008Sensor(0x27),
    // Add more sensors here as they are implemented
};

static const int s_sensor_count = sizeof(s_sensors) / sizeof(s_sensors[0]);
static i2c_master_bus_handle_t s_i2c_bus = nullptr;

// Task handle for the sensor polling task
static TaskHandle_t s_polling_task_handle = nullptr;

// Create a semaphore to signal interrupt events
static SemaphoreHandle_t s_sensorInterruptSemaphore = nullptr;

// Sensor polling task function
static void sensor_polling_task(void* pvParameters) {
    const TickType_t polling_interval = pdMS_TO_TICKS(100); // Poll every 100ms
    const uint32_t interrupt_follow_up_ms = 1000; // Follow-up poll 1 second after interrupt

    uint32_t interrupt_follow_up_time = 0;
    bool follow_up_scheduled = false;

    // Track last poll time per sensor (ms)
    std::vector<uint32_t> last_polled_ms(s_sensor_count, 0);

    while (true) {
        // Block until signaled or the short polling interval expires
        bool interrupt_triggered = (xSemaphoreTake(s_sensorInterruptSemaphore, polling_interval) == pdTRUE);
        
        // Get current time
        uint32_t current_time = esp_timer_get_time() / 1000;
        
        // Check if we have a scheduled follow-up poll
        bool do_follow_up = false;
        if (follow_up_scheduled && current_time >= interrupt_follow_up_time) {
            ESP_LOGD(TAG, "Performing interrupt follow-up poll");
            do_follow_up = true;
            follow_up_scheduled = false;
        }
        
        if (interrupt_triggered) {
            ESP_LOGD(TAG, "Woken by interrupt signal, polling sensors with interrupts...");
            
            // Only poll sensors that have triggered interrupts
            for (int i = 0; i < s_sensor_count; i++) {
                if (s_sensors[i]->isInitialized() && s_sensors[i]->hasInterruptTriggered()) {
                    ESP_LOGD(TAG, "Polling sensor with interrupt: %s", s_sensors[i]->name().c_str());
                    s_sensors[i]->poll();
                    s_sensors[i]->clearInterruptFlag();
                    
                    // Schedule a follow-up poll to flush any accumulated data
                    interrupt_follow_up_time = current_time + interrupt_follow_up_ms;
                    follow_up_scheduled = true;
                }
            }
        } else if (do_follow_up) {
            // This is a follow-up poll after an interrupt - only poll interrupt-capable sensors
            ESP_LOGD(TAG, "Performing follow-up poll to flush accumulated data");
            
            for (int i = 0; i < s_sensor_count; i++) {
                if (s_sensors[i]->isInitialized() && s_sensors[i]->hasInterruptTriggered()) {
                    ESP_LOGD(TAG, "Follow-up polling sensor: %s", s_sensors[i]->name().c_str());
                    s_sensors[i]->poll();
                    s_sensors[i]->clearInterruptFlag();
                }
            }
        }

        // Per-sensor periodic polling based on each sensor's desired interval
        for (int i = 0; i < s_sensor_count; i++) {
            if (!s_sensors[i]->isInitialized()) continue;
            uint32_t interval_ms = s_sensors[i]->poll_interval_ms();
            if (interval_ms == 0) continue;
            if (current_time - last_polled_ms[i] >= interval_ms) {
                s_sensors[i]->poll();
                last_polled_ms[i] = current_time;
            }
        }
    }
}

bool init_i2c() {
    ESP_LOGI(TAG, "Initializing I2C bus");

    // Create semaphore for interrupt signaling
    s_sensorInterruptSemaphore = xSemaphoreCreateBinary();
    if (s_sensorInterruptSemaphore == nullptr) {
        ESP_LOGE(TAG, "Failed to create polling semaphore");
        return false;
    }

    // Initialize the I2C bus
    esp_err_t err = i2c_master_init(&s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(err));
        return false;
    }

    // Scan the I2C bus once and try to initialize any recognized sensors
    const uint16_t probe_timeout_ms = 50;
    int found_count = 0;
    int initialized_count = 0;
    // Collect unrecognized device addresses
    uint8_t unrecognized_addrs[32];
    int unrecognized_count = 0;

    ESP_LOGI(TAG, "Scanning I2C bus for devices...");

    // Track which entries in s_sensors were recognized (address matched on bus)
    bool recognized_flags[s_sensor_count];
    for (int i = 0; i < s_sensor_count; ++i) recognized_flags[i] = false;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(s_i2c_bus, addr, probe_timeout_ms);

        if (ret == ESP_OK) {
            found_count++;

            // Check if this address matches any of our known sensors
            bool recognized = false;

            // Consult configuration for explicit driver selection
            config::ConfigurationManager &cfg_mgr = config::GetConfigurationManager();
            const std::string configured_driver = cfg_mgr.i2cmap().get_driver_for_address(addr);
            if (!configured_driver.empty()) {
                std::string dlow; dlow.resize(configured_driver.size());
                std::transform(configured_driver.begin(), configured_driver.end(), dlow.begin(), [](unsigned char c){ return (char)tolower(c); });
                if (dlow == "none") {
                    ESP_LOGI(TAG, "I2C address 0x%02X is explicitly disabled by config", addr);
                    // Skip probing for any drivers at this address
                    continue;
                }
            }

            // Collect candidate indices matching this address
            int first_match_index = -1;
            int match_count = 0;

            for (int i = 0; i < s_sensor_count; i++) {
                if (s_sensors[i]->addr() == addr) {
                    if (first_match_index < 0) first_match_index = i;
                    match_count++;
                }
            }

            if (match_count > 0) {
                recognized = true;
                int chosen_index = -1;

                if (!configured_driver.empty()) {
                    // Prefer the driver whose name().find(configured_driver) case-insensitive matches
                    for (int i = 0; i < s_sensor_count; i++) {
                        if (s_sensors[i]->addr() == addr) {
                            std::string n = s_sensors[i]->name();
                            // Normalize to lowercase
                            std::string nlow; nlow.resize(n.size());
                            std::transform(n.begin(), n.end(), nlow.begin(), [](unsigned char c){ return (char)tolower(c); });
                            std::string dlow; dlow.resize(configured_driver.size());
                            std::transform(configured_driver.begin(), configured_driver.end(), dlow.begin(), [](unsigned char c){ return (char)tolower(c); });
                            if (nlow.find(dlow) != std::string::npos) { chosen_index = i; break; }
                        }
                    }
                    if (chosen_index < 0) {
                        ESP_LOGW(TAG, "I2C address 0x%02X has configured driver '%s' but no candidate matches by name; falling back.", addr, configured_driver.c_str());
                    }
                }

                if (chosen_index < 0) {
                    if (match_count == 1) {
                        chosen_index = first_match_index; // unambiguous
                    } else {
                        ESP_LOGW(TAG, "Multiple drivers match I2C address 0x%02X but no explicit config in i2c.%02x; please configure.", addr, addr);
                        // Do not auto-initialize ambiguous address without config
                    }
                }

                if (chosen_index >= 0) {
                    recognized_flags[chosen_index] = true;
                    ESP_LOGI(TAG, "Found device at address 0x%02X: %s", addr, s_sensors[chosen_index]->name().c_str());

                    if (!s_sensors[chosen_index]->isInitialized()) {
                        bool initialized = s_sensors[chosen_index]->init(s_i2c_bus);
                        if (initialized) {
                            ESP_LOGI(TAG, "Successfully initialized %s", s_sensors[chosen_index]->name().c_str());
                            initialized_count++;
                        } else {
                            ESP_LOGW(TAG, "Failed to initialize %s", s_sensors[chosen_index]->name().c_str());
                        }
                    } else {
                        ESP_LOGI(TAG, "%s already initialized", s_sensors[chosen_index]->name().c_str());
                        initialized_count++;
                    }
                }
            }

            if (!recognized) {
                ESP_LOGW(TAG, "Found unrecognized device at address 0x%02X", addr);
                if (unrecognized_count < (int)(sizeof(unrecognized_addrs)/sizeof(unrecognized_addrs[0]))) {
                    unrecognized_addrs[unrecognized_count++] = addr;
                }
            }
        }
    }

    ESP_LOGI(TAG, "I2C scan complete: %d devices found, %d sensors initialized",
             found_count, initialized_count);

    // Publish I2C topology retained message
    publish_i2c_topology((const I2CSensor* const*)s_sensors,
                         recognized_flags,
                         s_sensor_count,
                         unrecognized_addrs,
                         unrecognized_count);

    // Start the sensor polling task if we have at least one initialized sensor
    if (initialized_count > 0) {
        ESP_LOGI(TAG, "Starting sensor polling task");
        BaseType_t task_created = xTaskCreate(
            sensor_polling_task,  // Task function
            "i2c_polling",        // Task name
            4096,                 // Stack size (bytes)
            nullptr,              // Task parameters
            5,                    // Task priority
            &s_polling_task_handle // Task handle
        );

        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create sensor polling task");
            return false;
        }

        ESP_LOGI(TAG, "Sensor polling task started");
    } else {
        ESP_LOGW(TAG, "No sensors initialized, polling task not started");
    }

    return true;
}

// Public function to signal the sensor polling task
void signalSensorInterrupt() {
    if (s_sensorInterruptSemaphore) {
        xSemaphoreGive(s_sensorInterruptSemaphore);
    }
}