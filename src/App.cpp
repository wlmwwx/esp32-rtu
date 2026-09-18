#include "App.hpp"
#include "Queue.h"
#include "Config.hpp"
#include "StatusLED.hpp"

extern QueueHandle_t cmd_queue;

static TimerHandle_t s_push_timer = nullptr;

static void push_timer_callback(TimerHandle_t t) {
    (void)t;
    Cmd cmd = Cmd::CMD_READ_MODBUS;
    xQueueSend(cmd_queue, &cmd, 0);
}

void AppTask(void* param) {
    Config cfg;
    cfg.begin();

    String runMode = cfg.getRunMode();
    int pushInterval = cfg.getPushInterval();

    StatusLED::begin();

    if (runMode == "push") {
        s_push_timer = xTimerCreate(
            "push_timer",
            pdMS_TO_TICKS(pushInterval * 1000),
            pdTRUE,
            NULL,
            push_timer_callback
        );
        if (s_push_timer) {
            xTimerStart(s_push_timer, 0);
        }
    }

    while (true) {
        StatusLED::tick();
        vTaskDelay(pdMS_TO_TICKS(10));

        Cmd cmd;
        if (xQueueReceive(cmd_queue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (cmd) {
                case Cmd::CMD_RESTART:
                    esp_restart();
                    break;
                case Cmd::CMD_SAVE_CONFIG:
                    break;
                default:
                    break;
            }
        }
    }
}
