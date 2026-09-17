#include "StatusLED.hpp"
#include <driver/gpio.h>
#include <esp_timer.h>

static constexpr gpio_num_t LED_GPIO = GPIO_NUM_8;

// Blink intervals in ms
static constexpr uint32_t BLINK_SLOW_MS = 500;   // 1Hz toggle (500ms on/off)
static constexpr uint32_t BLINK_FAST_MS = 125;   // 4Hz toggle (125ms on/off)

static LedMode s_current_mode = LedMode::OFF;
static int64_t s_last_toggle_us = 0;
static bool s_led_state = false;

void StatusLED::begin() {
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);  // LED off by default
    s_last_toggle_us = esp_timer_get_time();
}

void StatusLED::set(LedMode mode) {
    s_current_mode = mode;
}

void StatusLED::tick() {
    int64_t now_us = esp_timer_get_time();

    switch (s_current_mode) {
        case LedMode::OFF:
            gpio_set_level(LED_GPIO, 0);
            s_led_state = false;
            s_last_toggle_us = now_us;
            break;

        case LedMode::WIFI_CONNECTING:
            if ((now_us - s_last_toggle_us) >= BLINK_SLOW_MS * 1000ULL) {
                s_led_state = !s_led_state;
                gpio_set_level(LED_GPIO, s_led_state ? 1 : 0);
                s_last_toggle_us = now_us;
            }
            break;

        case LedMode::MODBUS_BUSY:
            if ((now_us - s_last_toggle_us) >= BLINK_FAST_MS * 1000ULL) {
                s_led_state = !s_led_state;
                gpio_set_level(LED_GPIO, s_led_state ? 1 : 0);
                s_last_toggle_us = now_us;
            }
            break;

        case LedMode::ERROR:
            gpio_set_level(LED_GPIO, 1);  // solid on
            s_led_state = true;
            s_last_toggle_us = now_us;
            break;

        case LedMode::OK:
            gpio_set_level(LED_GPIO, 0);  // solid off
            s_led_state = false;
            s_last_toggle_us = now_us;
            break;
    }
}
