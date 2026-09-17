#include "App.hpp"
#include "Queue.h"
#include "Config.hpp"
#include <esp_timer.h>

extern QueueHandle_t cmd_queue;

static esp_timer_handle_t s_push_timer = nullptr;

static void push_timer_callback(void* arg) {
    Cmd cmd = Cmd::CMD_READ_MODBUS;
    xQueueSend(cmd_queue, &cmd, 0);
}

void AppTask(void* param) {
    Config cfg;
    cfg.begin();

    String runMode = cfg.getRunMode();
    int pushInterval = cfg.getPushInterval();

    if (runMode == "push") {
        // Create periodic timer
        esp_timer_create_args_t timer_args = {
            .callback = &push_timer_callback,
            .arg = NULL,
            .name = "push_timer"
        };
        esp_timer_create(&timer_args, &s_push_timer);
        esp_timer_start_periodic(s_push_timer, pushInterval * 1000000ULL);
    }

    while (true) {
        Cmd cmd;
        if (xQueueReceive(cmd_queue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (cmd) {
                case Cmd::CMD_RESTART:
                    esp_restart();
                    break;
                case Cmd::CMD_SAVE_CONFIG:
                    // Already handled by WebConfigTask
                    break;
                default:
                    break;
            }
        }
    }
}
