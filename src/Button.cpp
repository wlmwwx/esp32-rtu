#include "Button.hpp"
#include "Queue.h"
#include <driver/gpio.h>
#include <esp_timer.h>

static constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_9;
static constexpr uint32_t LONG_PRESS_US = 3000000;  // 3 seconds
static constexpr uint32_t SHORT_PRESS_US = 1000000; // 1 second
static constexpr uint32_t DEBOUNCE_US = 50000;       // 50ms debounce

extern QueueHandle_t cmd_queue;

static volatile bool s_button_pressed = false;

static void IRAM_ATTR button_isr_handler(void* arg) {
    s_button_pressed = true;
}

void ButtonTask(void* param) {
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_GPIO);   // enable internal pull-up

    // Install ISR service and attach handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_ANYEDGE);

    int64_t press_start = 0;
    bool was_pressed = false;

    while (true) {
        // Check ISR flag first
        if (s_button_pressed) {
            s_button_pressed = false;
            // Process button press using gpio_get_level
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
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // poll every 50ms
    }
}
