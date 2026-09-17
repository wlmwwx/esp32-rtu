#include "Network.hpp"
#include "Queue.h"
#include "Config.hpp"
#include <PubSubClient.h>
#include <WiFi.h>
#include <HTTPClient.h>

static const char* TAG = "Network";

extern QueueHandle_t modbus_data_queue;
extern QueueHandle_t cmd_queue;

// MQTT client instance
static WiFiClient* _wifiClient = nullptr;
static PubSubClient* _mqttClient = nullptr;

static String build_json_payload(ModbusData& data, Config& cfg) {
    char json[512];
    snprintf(json, sizeof(json),
        "{\"device_id\":\"%s\",\"timestamp\":%lu,\"registers\":[",
        String((uint32_t)ESP.getEfuseMac(), HEX).c_str(),
        data.timestamp);
    for (int i = 0; i < data.count; i++) {
        if (i > 0) strcat(json, ",");
        char reg[32];
        snprintf(reg, sizeof(reg), "{\"addr\":%d,\"value\":%d}", i + cfg.getModbusReg(), data.registers[i]);
        strcat(json, reg);
    }
    strcat(json, "]}");
    return String(json);
}

static bool mqtt_connect(Config& cfg) {
    // Fix memory leak: delete old clients before allocating new ones
    if (_mqttClient) {
        delete _mqttClient;
        _mqttClient = nullptr;
    }
    if (_wifiClient) {
        delete _wifiClient;
        _wifiClient = nullptr;
    }

    if (cfg.getMqttTls()) {
        _wifiClient = new WiFiClientSecure();
        ((WiFiClientSecure*)_wifiClient)->setInsecure();
    } else {
        _wifiClient = new WiFiClient();
    }
    _mqttClient = new PubSubClient(*_wifiClient);
    _mqttClient->setServer(cfg.getMqttBroker().c_str(), cfg.getMqttPort());

    String clientId = "ESP32-RTU-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool connected = _mqttClient->connect(clientId.c_str(),
        cfg.getMqttUser().c_str(),
        cfg.getMqttPass().c_str(),
        cfg.getMqttLwtTopic().c_str(),
        0,
        cfg.getMqttRetain(),
        cfg.getMqttLwtMsg().c_str());

    if (connected) {
        // Publish LWT online message
        _mqttClient->publish(cfg.getMqttLwtTopic().c_str(), "online", cfg.getMqttRetain());
    }
    return connected;
}

static bool mqtt_publish(ModbusData& data, Config& cfg) {
    if (!_mqttClient || !_mqttClient->connected()) {
        if (!mqtt_connect(cfg)) return false;
    }

    String payload = build_json_payload(data, cfg);
    return _mqttClient->publish(cfg.getMqttTopic().c_str(), (const uint8_t*)payload.c_str(), payload.length(), cfg.getMqttRetain());
}

static bool http_post(ModbusData& data, Config& cfg) {
    if (cfg.getHttpUrl().length() == 0) return false;

    WiFiClient client;
    HTTPClient http;
    http.begin(client, cfg.getHttpUrl());
    http.addHeader("Content-Type", "application/json");

    String payload = build_json_payload(data, cfg);
    int code = http.POST((uint8_t*)payload.c_str(), payload.length());
    http.end();
    return code >= 200 && code < 300;
}

static bool tcp_send(ModbusData& data, Config& cfg) {
    if (cfg.getTcpIp().length() == 0) return false;

    WiFiClient client;
    if (!client.connect(cfg.getTcpIp().c_str(), cfg.getTcpPort())) return false;

    String payload = build_json_payload(data, cfg);
    client.print(payload.c_str());
    client.stop();
    return true;
}

void NetworkTask(void* param) {
    Config cfg;
    cfg.begin();

    while (true) {
        Cmd cmd;
        // Listen for commands (mode switches)
        if (xQueueReceive(cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Handle mode commands if needed
        }

        // Listen for Modbus data to upload
        ModbusData data;
        if (xQueueReceive(modbus_data_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!data.valid) continue;

            String protocol = cfg.getProtocolSelect();

            if (protocol == "mqtt") {
                mqtt_publish(data, cfg);
            } else if (protocol == "http") {
                http_post(data, cfg);
            } else if (protocol == "tcp") {
                tcp_send(data, cfg);
            }
        }

        // MQTT loop for keepalive
        if (_mqttClient && _mqttClient->connected()) {
            _mqttClient->loop();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
