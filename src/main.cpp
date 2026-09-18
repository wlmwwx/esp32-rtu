#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include "Config.hpp"
#include "StatusLED.hpp"
#include "ConfigServer.hpp"
#include "Queue.h"
#include <driver/uart.h>
#include <driver/gpio.h>

// Queue handles
QueueHandle_t modbus_data_queue = nullptr;
QueueHandle_t cmd_queue = nullptr;

// Modbus RTU state
static constexpr gpio_num_t DE_PIN = GPIO_NUM_4;
static constexpr int MODBUS_UART = UART_NUM_0;
static uint8_t modbus_tx_buf[16];
static uint8_t modbus_rx_buf[256];
static ModbusData latest_modbus = {};
static bool new_modbus_data = false;

// Network state
static bool wifi_connected = false;
static uint32_t last_push_ms = 0;
static uint32_t last_button_check_ms = 0;

// Button state
static constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_9;
static bool button_was_pressed = false;
static int64_t press_start_us = 0;

// --- Modbus helpers ---
static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

static void rs485_tx() { gpio_set_level(DE_PIN, 1); }
static void rs485_rx() { gpio_set_level(DE_PIN, 0); }

static bool modbus_read_registers(uint8_t slave, uint16_t reg, uint8_t count) {
    uint8_t req[8];
    req[0] = slave;
    req[1] = 0x03;
    req[2] = (reg >> 8) & 0xFF;
    req[3] = reg & 0xFF;
    req[4] = (count >> 8) & 0xFF;
    req[5] = count & 0xFF;
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    uart_flush_input(MODBUS_UART);
    rs485_tx();
    uart_write_bytes(MODBUS_UART, (const char*)req, 8);
    uart_wait_tx_done(MODBUS_UART, 100);
    rs485_rx();

    int len = uart_read_bytes(MODBUS_UART, modbus_rx_buf, sizeof(modbus_rx_buf), pdMS_TO_TICKS(1000));
    if (len > 5) {
        uint16_t got_crc = modbus_rx_buf[len - 1] | (modbus_rx_buf[len - 2] << 8);
        if (crc16(modbus_rx_buf, len - 2) == got_crc &&
            modbus_rx_buf[0] == slave && modbus_rx_buf[1] == 0x03) {
            latest_modbus.slave_addr = slave;
            latest_modbus.count = modbus_rx_buf[2] / 2;
            for (int i = 0; i < latest_modbus.count && i < 16; i++) {
                latest_modbus.registers[i] = (modbus_rx_buf[3 + i * 2] << 8) | modbus_rx_buf[4 + i * 2];
            }
            latest_modbus.valid = true;
            latest_modbus.timestamp = millis();
            new_modbus_data = true;
            return true;
        }
    }
    return false;
}

// --- Network helpers ---
static WiFiClient _wifi_client;
static PubSubClient* _mqtt_client = nullptr;
static WiFiClientSecure* _mqtt_secure_client = nullptr;

static bool mqtt_connect(Config& cfg) {
    if (_mqtt_client) { delete _mqtt_client; _mqtt_client = nullptr; }
    if (_mqtt_secure_client) { delete _mqtt_secure_client; _mqtt_secure_client = nullptr; }
    if (_mqtt_client) { delete _mqtt_client; _mqtt_client = nullptr; }

    if (cfg.getMqttTls()) {
        _mqtt_secure_client = new WiFiClientSecure();
        _mqtt_secure_client->setInsecure();
        _mqtt_client = new PubSubClient(*_mqtt_secure_client);
    } else {
        _wifi_client = WiFiClient();
        _mqtt_client = new PubSubClient(_wifi_client);
    }
    _mqtt_client->setServer(cfg.getMqttBroker().c_str(), cfg.getMqttPort());
    String cid = "ESP32-RTU-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok = _mqtt_client->connect(cid.c_str(), cfg.getMqttUser().c_str(),
        cfg.getMqttPass().c_str(), cfg.getMqttLwtTopic().c_str(), 0,
        cfg.getMqttRetain(), cfg.getMqttLwtMsg().c_str());
    if (ok) {
        _mqtt_client->publish(cfg.getMqttLwtTopic().c_str(), "online", cfg.getMqttRetain());
    }
    return ok;
}

static String build_json(ModbusData& d, Config& cfg) {
    char json[512];
    snprintf(json, sizeof(json),
        "{\"device_id\":\"%s\",\"timestamp\":%lu,\"registers\":[",
        String((uint32_t)ESP.getEfuseMac(), HEX).c_str(), d.timestamp);
    for (int i = 0; i < d.count; i++) {
        if (i > 0) strcat(json, ",");
        char r[32];
        snprintf(r, sizeof(r), "{\"addr\":%d,\"value\":%d}", i + cfg.getModbusReg(), d.registers[i]);
        strcat(json, r);
    }
    strcat(json, "]}");
    return String(json);
}

static void mqtt_publish(ModbusData& d, Config& cfg) {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        if (!mqtt_connect(cfg)) return;
    }
    String payload = build_json(d, cfg);
    _mqtt_client->publish(cfg.getMqttTopic().c_str(), payload.c_str(), cfg.getMqttRetain());
    _mqtt_client->loop();
}

static void http_post(ModbusData& d, Config& cfg) {
    if (cfg.getHttpUrl().length() == 0) return;
    WiFiClient c;
    HTTPClient http;
    http.begin(c, cfg.getHttpUrl());
    http.addHeader("Content-Type", "application/json");
    http.POST((uint8_t*)build_json(d, cfg).c_str(), build_json(d, cfg).length());
    http.end();
}

static void tcp_send(ModbusData& d, Config& cfg) {
    if (cfg.getTcpIp().length() == 0) return;
    WiFiClient c;
    if (!c.connect(cfg.getTcpIp().c_str(), cfg.getTcpPort())) return;
    c.print(build_json(d, cfg).c_str());
    c.stop();
}

// --- Button check ---
static void check_button() {
    bool pressed = (gpio_get_level(BUTTON_GPIO) == 0);
    if (pressed && !button_was_pressed) {
        press_start_us = esp_timer_get_time();
    } else if (!pressed && button_was_pressed) {
        uint64_t dur = esp_timer_get_time() - press_start_us;
        if (dur >= 3000000) {
            // Long press — enter config mode
            ESP.restart();
        }
    }
    button_was_pressed = pressed;
}

// --- UART init for Modbus ---
static void init_modbus_uart(int baud, int bits, int parity, int stop) {
    uart_config_t uc = {};
    uc.baud_rate = baud;
    uc.data_bits = (uart_word_length_t)(bits - 1);
    uc.parity = (uart_parity_t)parity;
    uc.stop_bits = (uart_stop_bits_t)(stop == 1 ? UART_STOP_BITS_1 : UART_STOP_BITS_2);
    uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_param_config(MODBUS_UART, &uc);
    uart_set_pin(MODBUS_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(MODBUS_UART, 1024, 1024, 0, NULL, 0);
    gpio_set_direction(DE_PIN, GPIO_MODE_OUTPUT);
    rs485_rx();
}

// =============================================================================
// SETUP — runs once
// =============================================================================
void setup() {
    Serial.begin(115200);

    Config cfg;
    cfg.begin();

    bool isConfigured = cfg.isConfigured();

    // Button init
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_GPIO);

    // LED init
    StatusLED::begin();

    if (!isConfigured) {
        Serial.println("No config found. Starting AP mode...");
        startConfigServer();  // blocks forever
        return;  // never reached
    }

    // ---- RUN MODE ----
    Serial.println("Starting RUN mode...");

    // Init Modbus UART
    init_modbus_uart(cfg.getModbusBaud(), cfg.getModbusBits(),
                     cfg.getModbusParity(), cfg.getModbusStop());

    // WiFi connect
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.getWifiSsid().c_str(), cfg.getWifiPass().c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        StatusLED::tick();
        delay(50);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed. Rebooting...");
        delay(1000);
        ESP.restart();
        return;
    }

    wifi_connected = true;
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    StatusLED::set(LedMode::OK);

    // Pre-connect MQTT if using
    if (cfg.getProtocolSelect() == "mqtt") {
        mqtt_connect(cfg);
    }
}

// =============================================================================
// LOOP — bare-metal, no FreeRTOS scheduler
// =============================================================================
void loop() {
    uint32_t now = millis();
    StatusLED::tick();
    check_button();

    Config cfg;
    cfg.begin();

    String runMode = cfg.getRunMode();
    int pushInterval = cfg.getPushInterval() * 1000;  // to ms
    String protocol = cfg.getProtocolSelect();

    if (runMode == "push") {
        // Periodic Modbus read
        if (now - last_push_ms >= (uint32_t)pushInterval) {
            last_push_ms = now;
            StatusLED::set(LedMode::MODBUS_BUSY);
            bool ok = modbus_read_registers(
                cfg.getModbusSlave(), cfg.getModbusReg(), cfg.getModbusCount());

            if (ok && new_modbus_data) {
                new_modbus_data = false;
                ModbusData d = latest_modbus;
                if (protocol == "mqtt") mqtt_publish(d, cfg);
                else if (protocol == "http") http_post(d, cfg);
                else if (protocol == "tcp") tcp_send(d, cfg);
                StatusLED::set(LedMode::OK);
            } else {
                StatusLED::set(LedMode::ERROR);
            }
        }
    }

    // MQTT loop keepalive
    if (_mqtt_client && _mqtt_client->connected()) {
        _mqtt_client->loop();
    }

    delay(10);
}
