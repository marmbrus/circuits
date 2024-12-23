#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "button_event.h"
#include "config.h"

// Forward declare Application
class Application;

class IOManager {
private:
    static const int NUM_BUTTONS = 4;
    static const gpio_num_t BUTTON_GPIOS[NUM_BUTTONS];
    static const gpio_num_t BUTTON_LED_GPIOS[NUM_BUTTONS];
    static QueueHandle_t eventQueue;
    static uint32_t last_interrupt_times[NUM_BUTTONS];
    static bool button_released[NUM_BUTTONS];
    static bool last_button_states[NUM_BUTTONS];
    Application* currentApp;

    static void IRAM_ATTR buttonIsrHandler(void* arg);
    static void IRAM_ATTR movementIsrHandler(void* arg);
    void initButtons();
    void initButtonLEDs();
    static void buttonPollingTask(void* arg);

public:
    IOManager(Application* app);
    bool processEvents();
    void initMovementInterrupt();
    void sendEvent(ButtonEvent evt) { xQueueSend(eventQueue, &evt, 0); }
    void setButtonLED(int buttonIndex, bool state);
    static const int QUEUE_SIZE = IO_QUEUE_SIZE;
};