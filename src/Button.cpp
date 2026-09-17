#include "Button.hpp"
#include "Queue.h"
#include <driver/gpio.h>
#include <esp_timer.h>

static constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_9;
static constexpr uint32_t LONG_PRESS_US = 3'000'000;  // 3 seconds
static constexpr uint32_t SHORT_PRESS_US = 1'000'000; // 1 second
static constexpr uint32_t DEBOUNCE_US = 50'000;       // 50ms debounce

extern QueueHandle_t cmd_queue;

void ButtonTask(void* param) {
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_GPIO);   // enable internal pull-up

    int64_t press_start = 0;
    bool was_pressed = false;

    while (true) {
        bool pressed = (gpio_get_level(BUTTON_GPIO) == 0);  // active low

        if (pressed && !was_pressed) {
            // Transition from high to low — start press timer
            press_start = esp_timer_get_time();
        } else if (!pressed && was_pressed) {
            // Transition from low to high — release
            uint64_t duration = esp_timer_get_time() - press_start;
            if (duration >= LONG_PRESS_US) {
                // Long press — enter config mode
                Cmd cmd = Cmd::CMD_ENTER_CONFIG;
                xQueueSend(cmd_queue, &cmd, 0);
            } else if (duration >= DEBOUNCE_US && duration < SHORT_PRESS_US) {
                // Short press — restart
                Cmd cmd = Cmd::CMD_RESTART;
                xQueueSend(cmd_queue, &cmd, 0);
            }
            press_start = 0;
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(50));  // poll every 50ms
    }
}
