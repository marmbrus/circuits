#include "gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "communication.h"
#include "ConfigurationManager.h"
#include "MotionConfig.h"
#include "TagsConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/portmacro.h"

static const char* TAG = "gpio_init";

// Pre-allocated tags used by ISR to avoid dynamic memory in interrupt context
static TagCollection* s_motion_tags = nullptr;
static TimerHandle_t s_motion_publish_timer = nullptr; // periodic 10s publisher
static TimerHandle_t s_motion_idle_timer = nullptr;    // one-shot 10s idle detector
static int s_motion_pin = -1;

// Motion aggregation window state (10s window)
static volatile int s_in_motion_session = 0; // 0 = idle, 1 = in active motion session
static volatile uint32_t s_motion_count = 0; // counts additional edges after first within session
static portMUX_TYPE s_motion_mux = portMUX_INITIALIZER_UNLOCKED;

// ISR handler needs C linkage per ESP-IDF requirements
extern "C" void IRAM_ATTR motion_isr_handler(void* arg) {
    (void)arg;
    BaseType_t task_woken = pdFALSE;

    // Determine if this is the first motion after idle
    bool send_immediate = false;
    portENTER_CRITICAL_ISR(&s_motion_mux);
    if (s_in_motion_session == 0) {
        s_in_motion_session = 1;
        s_motion_count = 0; // do not count this first edge
        send_immediate = true;
        // Start periodic publisher
        if (s_motion_publish_timer) {
            xTimerStartFromISR(s_motion_publish_timer, &task_woken);
        }
    } else {
        // Within session: count additional edges
        s_motion_count++;
    }
    portEXIT_CRITICAL_ISR(&s_motion_mux);

    // Reset/Start idle timer for 10s since last motion
    if (s_motion_idle_timer) {
        xTimerResetFromISR(s_motion_idle_timer, &task_woken);
    }

    // For the first event, enqueue immediate metric (ISR-safe)
    if (send_immediate && s_motion_tags) {
        report_metric("motion", 1.0f, s_motion_tags);
    }

    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void motion_publish_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    // Publish aggregated count every 10s while session is active
    uint32_t count = 0;
    portENTER_CRITICAL(&s_motion_mux);
    count = s_motion_count;
    s_motion_count = 0;
    portEXIT_CRITICAL(&s_motion_mux);

    if (count > 0 && s_motion_tags) {
        report_metric("motion", (float)count, s_motion_tags);
    }
}

static void motion_idle_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    // 10s without motion => end session; stop publisher
    portENTER_CRITICAL(&s_motion_mux);
    s_in_motion_session = 0;
    s_motion_count = 0;
    portEXIT_CRITICAL(&s_motion_mux);
    if (s_motion_publish_timer) {
        xTimerStop(s_motion_publish_timer, 0);
    }
}

static esp_err_t setup_motion_gpio() {
    using namespace config;
    MotionConfig& motion = GetConfigurationManager().motion();
    if (!motion.has_gpio()) {
        ESP_LOGI(TAG, "Motion GPIO not configured; skipping setup");
        return ESP_OK;
    }

    int pin = motion.gpio();
    s_motion_pin = pin;
    // Prepare tags once (no allocation in ISR)
    if (s_motion_tags == nullptr) {
        s_motion_tags = create_tag_collection();
        if (s_motion_tags == nullptr) {
            ESP_LOGE(TAG, "Failed to create tag collection for motion");
            return ESP_ERR_NO_MEM;
        }
    }
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE; // interrupt on rising edge (going high)
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << pin);
    // Respect external pull resistors; do not enable internal pulls
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for motion pin %d: %s", pin, esp_err_to_name(err));
        return err;
    }

    err = gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_POSEDGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_intr_type failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add((gpio_num_t)pin, motion_isr_handler, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(err));
        return err;
    }

    // Create periodic publish timer (10s) and idle timer (10s one-shot)
    if (s_motion_publish_timer == nullptr) {
        s_motion_publish_timer = xTimerCreate("motion_pub", pdMS_TO_TICKS(10000), pdTRUE, nullptr, motion_publish_timer_cb);
        if (s_motion_publish_timer == nullptr) {
            ESP_LOGE(TAG, "Failed to create motion publish timer");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_motion_idle_timer == nullptr) {
        s_motion_idle_timer = xTimerCreate("motion_idle", pdMS_TO_TICKS(10000), pdFALSE, nullptr, motion_idle_timer_cb);
        if (s_motion_idle_timer == nullptr) {
            ESP_LOGE(TAG, "Failed to create motion idle timer");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "Motion GPIO %d configured with rising-edge interrupt", pin);
    return ESP_OK;
}

esp_err_t init_gpio(void) {
    // Extendable: call specific setup functions for device features
    esp_err_t err = setup_motion_gpio();
    if (err != ESP_OK) return err;
    return ESP_OK;
}


