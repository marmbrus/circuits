#include "io_manager.h"
#include "application.h"
#include "esp_log.h"
#include "config.h"
#include "esp_sleep.h"
#include "lis2dh.h"
#include "led_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

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
bool IOManager::last_button_states[NUM_BUTTONS] = {true, true, true, true};

IOManager::IOManager(Application* app) : currentApp(app) {
    ESP_LOGI(TAG, "Initializing IOManager");
    eventQueue = xQueueCreate(QUEUE_SIZE, sizeof(ButtonEvent));
    initButtons();
    initMovementInterrupt();

    app->setIOManager(this);

    xTaskCreate(&IOManager::buttonPollingTask, "button_polling_task", 2048, this, 5, NULL);
}

void IOManager::buttonPollingTask(void* arg) {
    IOManager* ioManager = static_cast<IOManager*>(arg);
    const TickType_t xDelay = pdMS_TO_TICKS(10);  // Poll every 10ms

    while (true) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            bool is_pressed = (gpio_get_level(BUTTON_GPIOS[i]) == 0);
            uint32_t current_time = xTaskGetTickCount();

            if (is_pressed != ioManager->last_button_states[i]) {
                ioManager->last_interrupt_times[i] = current_time;
                ioManager->last_button_states[i] = is_pressed;
            }

            if (is_pressed && ioManager->button_released[i] &&
                (current_time - ioManager->last_interrupt_times[i] >= pdMS_TO_TICKS(10))) {
                ButtonEvent evt;
                switch (i) {
                    case 0: evt = ButtonEvent::BUTTON1_PRESSED; break;
                    case 1: evt = ButtonEvent::BUTTON2_PRESSED; break;
                    case 2: evt = ButtonEvent::BUTTON3_PRESSED; break;
                    case 3: evt = ButtonEvent::BUTTON4_PRESSED; break;
                }
                xQueueSend(ioManager->eventQueue, &evt, 0);
                ioManager->button_released[i] = false;
            } else if (!is_pressed) {
                ioManager->button_released[i] = true;
            }
        }
        vTaskDelay(xDelay);
    }
}

void IOManager::initButtons() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = 0;

    gpio_install_isr_service(0);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        io_conf.pin_bit_mask = (1ULL << BUTTON_GPIOS[i]);
        gpio_config(&io_conf);
        gpio_isr_handler_add(BUTTON_GPIOS[i], buttonIsrHandler, (void*)BUTTON_GPIOS[i]);

        ESP_LOGI(TAG, "Button %d (GPIO %d) initialized, initial state: %d",
                 i + 1, BUTTON_GPIOS[i], gpio_get_level(BUTTON_GPIOS[i]));
    }

    ESP_LOGI(TAG, "Button GPIOs initialized");
}

void IRAM_ATTR IOManager::buttonIsrHandler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    int button_idx = -1;

    if (gpio_num == BUTTON1_GPIO) button_idx = 0;
    else if (gpio_num == BUTTON2_GPIO) button_idx = 1;
    else if (gpio_num == BUTTON3_GPIO) button_idx = 2;
    else if (gpio_num == BUTTON4_GPIO) button_idx = 3;

    if (button_idx != -1) {
        uint32_t current_time = xTaskGetTickCountFromISR();
        bool is_pressed = (gpio_get_level((gpio_num_t)gpio_num) == 0);

        if (is_pressed != last_button_states[button_idx]) {
            last_interrupt_times[button_idx] = current_time;
            last_button_states[button_idx] = is_pressed;
        }
    }
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

void IOManager::setButtonLED(int buttonIndex, bool state) {
    if (buttonIndex >= 0 && buttonIndex < NUM_BUTTONS) {
        led_control_set_button_led_status(buttonIndex, state);
    }
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