#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <WiFi.h>
#include "Queue.h"
#include "Config.hpp"
#include "Button.hpp"
#include "Modbus.hpp"
#include "Network.hpp"
#include "WebConfig.hpp"
#include "StatusLED.hpp"
#include "App.hpp"

// Queue handles (defined here, extern in Queue.h)
QueueHandle_t modbus_data_queue = nullptr;
QueueHandle_t cmd_queue = nullptr;

// Task handles
static TaskHandle_t s_button_task_h = nullptr;
static TaskHandle_t s_modbus_task_h = nullptr;
static TaskHandle_t s_network_task_h = nullptr;
static TaskHandle_t s_webconfig_task_h = nullptr;
static TaskHandle_t s_app_task_h = nullptr;

static void IRAM_ATTR button_isr(void* arg) {
    // ISR not used — polling in ButtonTask
}

void setup() {
    Serial.begin(115200);

    Config cfg;
    cfg.begin();

    // Check if configured (WiFi SSID present)
    bool isConfigured = cfg.isConfigured();

    // Create queues
    modbus_data_queue = xQueueCreate(MODBUS_QUEUE_LEN, sizeof(ModbusData));
    cmd_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(Cmd));

    if (!isConfigured) {
        // No config — enter CONFIG mode (AP)
        Serial.println("No config found. Starting AP mode...");

        // Start WebConfig task first (it suspends itself after setup)
        xTaskCreatePinnedToCore(WebConfigTask, "WebConfig", 8192, NULL, 1, &s_webconfig_task_h, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        // Resume WebConfigTask (it suspended itself)
        vTaskResume(s_webconfig_task_h);

        // Nothing else runs in config mode — scheduler already running
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    } else {
        // Configured — enter RUN mode
        Serial.println("Config found. Starting RUN mode...");

        // Connect to WiFi
        WiFi.mode(WIFI_STA);
        WiFi.begin(cfg.getWifiSsid().c_str(), cfg.getWifiPass().c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            vTaskDelay(pdMS_TO_TICKS(500));
            attempts++;
        }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi failed. Starting AP config...");
            // Fall back to config mode
            xTaskCreatePinnedToCore(WebConfigTask, "WebConfig", 8192, NULL, 1, &s_webconfig_task_h, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            vTaskResume(s_webconfig_task_h);
            while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
        }

        Serial.print("WiFi connected: ");
        Serial.println(WiFi.localIP());

        // Start all run-mode tasks
        xTaskCreatePinnedToCore(ButtonTask, "Button", 2048, NULL, 4, &s_button_task_h, 0);
        xTaskCreatePinnedToCore(ModbusTask, "Modbus", 4096, NULL, 3, &s_modbus_task_h, 0);
        xTaskCreatePinnedToCore(NetworkTask, "Network", 8192, NULL, 2, &s_network_task_h, 0);
        xTaskCreatePinnedToCore(AppTask, "App", 4096, NULL, 2, &s_app_task_h, 0);

        // Scheduler already running — just yield to loop
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
}

void loop() {
    // FreeRTOS handles scheduling — loop does nothing
}
