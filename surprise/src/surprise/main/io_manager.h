#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "application.h"
#include "config.h"

enum class ButtonEvent {
    BUTTON1_PRESSED,
    BUTTON2_PRESSED
};

class IOManager {
private:
    static const int NUM_BUTTONS = 2;
    static const gpio_num_t BUTTON_GPIOS[NUM_BUTTONS];
    static QueueHandle_t eventQueue;
    static uint32_t last_interrupt_times[NUM_BUTTONS];
    Application* currentApp;

    static void IRAM_ATTR buttonIsrHandler(void* arg);
    void initButtons();

public:
    IOManager(Application* app);
    bool processEvents();
    static const int QUEUE_SIZE = IO_QUEUE_SIZE;
};