#include "io_manager.h"
#include "esp_log.h"
#include "config.h"
#include "esp_sleep.h"

static const char* TAG = "IOManager";

const gpio_num_t IOManager::BUTTON_GPIOS[NUM_BUTTONS] = {
    (gpio_num_t)BUTTON1_GPIO,  // Button 1
    (gpio_num_t)BUTTON2_GPIO   // Button 2
};

QueueHandle_t IOManager::eventQueue = nullptr;
uint32_t IOManager::last_interrupt_times[NUM_BUTTONS] = {0, 0};

IOManager::IOManager(Application* app) : currentApp(app) {
    eventQueue = xQueueCreate(QUEUE_SIZE, sizeof(ButtonEvent));
    initButtons();

    // Check if waking up from deep sleep
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask & (1ULL << BUTTON1_GPIO)) {
            currentApp->onButton1Pressed();
        } else if (wakeup_pin_mask & (1ULL << BUTTON2_GPIO)) {
            currentApp->onButton2Pressed();
        }
    }
}

void IRAM_ATTR IOManager::buttonIsrHandler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    int button_idx = (gpio_num == BUTTON1_GPIO) ? 0 : 1;

    uint32_t interrupt_time = xTaskGetTickCountFromISR();
    if (interrupt_time - last_interrupt_times[button_idx] > pdMS_TO_TICKS(BUTTON_DEBOUNCE_TIME_MS)) {
        ButtonEvent evt = (button_idx == 0) ? ButtonEvent::BUTTON1_PRESSED
                                          : ButtonEvent::BUTTON2_PRESSED;
        xQueueSendFromISR(eventQueue, &evt, NULL);
    }
    last_interrupt_times[button_idx] = interrupt_time;
}

void IOManager::initButtons() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    gpio_install_isr_service(0);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        io_conf.pin_bit_mask = (1ULL << BUTTON_GPIOS[i]);
        gpio_config(&io_conf);
        gpio_isr_handler_add(BUTTON_GPIOS[i], buttonIsrHandler, (void*)BUTTON_GPIOS[i]);
    }

    ESP_LOGI(TAG, "Button GPIOs initialized");
}

bool IOManager::processEvents() {
    ButtonEvent event;
    if (xQueueReceive(eventQueue, &event, 0)) {
        switch (event) {
            case ButtonEvent::BUTTON1_PRESSED:
                currentApp->onButton1Pressed();
                break;
            case ButtonEvent::BUTTON2_PRESSED:
                currentApp->onButton2Pressed();
                break;
        }
        return true; // Event processed
    }
    return false; // No event processed
}