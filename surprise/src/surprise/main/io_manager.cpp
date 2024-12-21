#include "io_manager.h"
#include "application.h"
#include "esp_log.h"
#include "config.h"
#include "esp_sleep.h"
#include "lis2dh.h"

static const char* TAG = "IOManager";

const gpio_num_t IOManager::BUTTON_GPIOS[NUM_BUTTONS] = {
    (gpio_num_t)BUTTON1_GPIO,  // Button 1
    (gpio_num_t)BUTTON2_GPIO,  // Button 2
    (gpio_num_t)BUTTON3_GPIO,  // Button 3
    (gpio_num_t)BUTTON4_GPIO   // Button 4
};

QueueHandle_t IOManager::eventQueue = nullptr;
uint32_t IOManager::last_interrupt_times[NUM_BUTTONS] = {0, 0, 0, 0};
bool IOManager::button_released[NUM_BUTTONS] = {true, true, true, true};

IOManager::IOManager(Application* app) : currentApp(app) {
    eventQueue = xQueueCreate(QUEUE_SIZE, sizeof(ButtonEvent));
    initButtons();
    initMovementInterrupt();

    // Check if waking up from deep sleep
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask & (1ULL << BUTTON1_GPIO)) {
            currentApp->onButton1Pressed();
        } else if (wakeup_pin_mask & (1ULL << BUTTON2_GPIO)) {
            currentApp->onButton2Pressed();
        } else if (wakeup_pin_mask & (1ULL << BUTTON3_GPIO)) {
            currentApp->onButton3Pressed();
        } else if (wakeup_pin_mask & (1ULL << BUTTON4_GPIO)) {
            currentApp->onButton4Pressed();
        }
    }
}

void IRAM_ATTR IOManager::buttonIsrHandler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    int button_idx = -1;

    if (gpio_num == BUTTON1_GPIO) button_idx = 0;
    else if (gpio_num == BUTTON2_GPIO) button_idx = 1;
    else if (gpio_num == BUTTON3_GPIO) button_idx = 2;
    else if (gpio_num == BUTTON4_GPIO) button_idx = 3;

    if (button_idx != -1) {
        uint32_t interrupt_time = xTaskGetTickCountFromISR();
        bool is_pressed = (gpio_get_level((gpio_num_t)gpio_num) == 0);

        if (is_pressed) {
            // Only process press if button was previously released and debounce time has passed
            if (button_released[button_idx] &&
                (interrupt_time - last_interrupt_times[button_idx] > pdMS_TO_TICKS(BUTTON_DEBOUNCE_TIME_MS))) {
                ButtonEvent evt;
                switch (button_idx) {
                    case 0: evt = ButtonEvent::BUTTON1_PRESSED; break;
                    case 1: evt = ButtonEvent::BUTTON2_PRESSED; break;
                    case 2: evt = ButtonEvent::BUTTON3_PRESSED; break;
                    case 3: evt = ButtonEvent::BUTTON4_PRESSED; break;
                }
                xQueueSendFromISR(eventQueue, &evt, NULL);
                button_released[button_idx] = false;
            }
        } else {
            // Button is released, start the debounce timer
            last_interrupt_times[button_idx] = interrupt_time;
            button_released[button_idx] = true;
        }
    }
}

void IOManager::initButtons() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;  // Changed to detect both edges temporarily for debugging
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = 0;

    gpio_install_isr_service(0);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        io_conf.pin_bit_mask = (1ULL << BUTTON_GPIOS[i]);
        gpio_config(&io_conf);
        gpio_isr_handler_add(BUTTON_GPIOS[i], buttonIsrHandler, (void*)BUTTON_GPIOS[i]);

        // Log initial state
        ESP_LOGI(TAG, "Button %d (GPIO %d) initialized, initial state: %d",
                 i + 1, BUTTON_GPIOS[i], gpio_get_level(BUTTON_GPIOS[i]));
    }

    ESP_LOGI(TAG, "Button GPIOs initialized with pull-up resistors enabled");
}

void IRAM_ATTR IOManager::movementIsrHandler(void* arg) {
    static uint32_t last_interrupt_time = 0;
    uint32_t current_time = xTaskGetTickCountFromISR();

    // Basic debouncing
    if ((current_time - last_interrupt_time) < pdMS_TO_TICKS(50)) {
        return;
    }
    last_interrupt_time = current_time;

    ButtonEvent evt = ButtonEvent::MOVEMENT_DETECTED;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xQueueSendFromISR(eventQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void IOManager::initMovementInterrupt() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;  // Change to falling edge
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;  // Keep line low when inactive
    io_conf.pin_bit_mask = (1ULL << MOVEMENT_INT_GPIO);

    gpio_config(&io_conf);
    gpio_isr_handler_add(MOVEMENT_INT_GPIO, movementIsrHandler, NULL);

    ESP_LOGI(TAG, "Movement interrupt initialized on GPIO %d", MOVEMENT_INT_GPIO);
}

bool IOManager::processEvents() {
    ButtonEvent event;
    if (xQueueReceive(eventQueue, &event, 0)) {
        switch (event) {
            case ButtonEvent::BUTTON1_PRESSED:
                ESP_LOGI(TAG, "Button 1 press processed");
                currentApp->onButton1Pressed();
                break;
            case ButtonEvent::BUTTON2_PRESSED:
                ESP_LOGI(TAG, "Button 2 press processed");
                currentApp->onButton2Pressed();
                break;
            case ButtonEvent::BUTTON3_PRESSED:
                ESP_LOGI(TAG, "Button 3 press processed");
                currentApp->onButton3Pressed();
                break;
            case ButtonEvent::BUTTON4_PRESSED:
                ESP_LOGI(TAG, "Button 4 press processed");
                currentApp->onButton4Pressed();
                break;
            case ButtonEvent::MOVEMENT_DETECTED: {
                uint8_t int_source;
                ESP_LOGI(TAG, "Movement detected");
                // Clear the interrupt here, in the main task context
                lis2dh12_get_int1_source(&int_source);
                currentApp->onMovementDetected();
                break;
            }
            case ButtonEvent::ORIENTATION_UP:
            case ButtonEvent::ORIENTATION_DOWN:
            case ButtonEvent::ORIENTATION_LEFT:
            case ButtonEvent::ORIENTATION_RIGHT:
            case ButtonEvent::ORIENTATION_FRONT:
            case ButtonEvent::ORIENTATION_BACK:
            case ButtonEvent::ORIENTATION_UNKNOWN:
                currentApp->onOrientationChanged(event);
                break;
        }
        return true;
    }
    return false;
}