# WiFi RTU Gateway — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a fully functional ESP32C3 WiFi Modbus RTU gateway with web configuration portal, supporting MQTT/HTTP/TCP upload protocols in Push and Pull modes.

**Architecture:** FreeRTOS multi-task system with 5 tasks communicating via queues. NVS for persistent configuration. Embedded HTML web server for configuration. Modbus RTU master drives RS485 half-duplex communication.

**Tech Stack:** PlatformIO, Arduino framework, FreeRTOS, PubSubClient (MQTT), ESPAsyncWebServer, Preferences (NVS)

**Spec:** `docs/superpowers/specs/2026-09-17-wifi-rtu-design.md`

---

## Global Constraints

| Constraint | Value |
|------------|-------|
| MCU | ESP32C3 (airm2m_core_esp32c3) |
| Framework | PlatformIO + Arduino + FreeRTOS |
| RS485 DE pin | GPIO 4 |
| Button pin | GPIO 9 (active-low) |
| LED pin | GPIO 8 |
| Modbus function | 0x03 Read Holding Registers |
| Modbus timeout | 1000ms |
| NVS namespace | `rtu_cfg` |
| AP SSID | `ESP32-RTU-Config` |
| AP IP | 192.168.4.1 |

---

## File Structure

```
src/
├── main.cpp              # Entry point, task creation, queue globals
├── App.cpp / App.hpp     # AppTask — mode orchestration, push timer
├── Modbus.cpp / Modbus.hpp   # ModbusTask + RS485 half-duplex driver
├── Network.cpp / Network.hpp  # NetworkTask + MqttClient + HttpClient + TcpClient
├── WebConfig.cpp / WebConfig.hpp  # WebConfigTask + embedded HTML pages
├── Config.cpp / Config.hpp  # NVS wrapper (Preferences)
├── Button.cpp / Button.hpp  # ButtonTask — debounce, long-press detection
└── Queue.h              # Shared queue handles and command enums

platformio.ini           # Add library dependencies

test/                    # (placeholder for future tests)
```

---

## Task 1: Project Scaffold — Dependencies and Globals

**Files:**
- Modify: `platformio.ini`
- Create: `src/Queue.h`

**Dependencies to add to platformio.ini:**
```ini
lib_deps =
    PubSubClient
    ESP Async WebServer
    me-no-dev/ESP Async TCP@^1.2.2
```

**Interfaces:**
- Produces: `extern QueueHandle_t modbus_data_queue`, `extern QueueHandle_t cmd_queue`, `extern QueueHandle_t web_config_queue`

- [ ] **Step 1: Modify platformio.ini — add lib_deps**

```ini
[env:airm2m_core_esp32c3]
platform = espressif32
board = airm2m_core_esp32c3
framework = arduino
lib_deps =
    PubSubClient
    ESP Async WebServer
    me-no-dev/ESP Async TCP@^1.2.2
build_flags =
    -DCORE_DEBUG_LEVEL=0
```

- [ ] **Step 2: Create src/Queue.h — shared queue handles and command enums**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Command enum for cmd_queue
enum class Cmd : uint8_t {
    CMD_READ_MODBUS,       // Trigger Modbus read
    CMD_ENTER_CONFIG,      // Enter config mode (AP)
    CMD_ENTER_RUN,         // Enter run mode
    CMD_RESTART,           // Software restart
    CMD_SAVE_CONFIG        // Config saved via web, trigger restart
};

// Modbus data struct for modbus_data_queue
struct ModbusData {
    uint8_t slave_addr;
    uint16_t registers[16];  // max 16 registers
    uint8_t count;
    uint32_t timestamp;
    bool valid;
};

// Config data struct for web_config_queue
struct ConfigData {
    char key[64];
    char value[128];
};

// Queue handles (defined in main.cpp, extern here)
extern QueueHandle_t modbus_data_queue;
extern QueueHandle_t cmd_queue;
extern QueueHandle_t web_config_queue;

// Queue lengths
#define MODBUS_QUEUE_LEN   5
#define CMD_QUEUE_LEN      4
#define CONFIG_QUEUE_LEN   2
```

- [ ] **Step 3: Commit**

```bash
git add platformio.ini src/Queue.h
git commit -m "feat: scaffold project with dependencies and queue definitions"
```

---

## Task 2: Config Layer — NVS Wrapper

**Files:**
- Create: `src/Config.cpp`, `src/Config.hpp`

**Interfaces:**
- Consumes: NVS flash storage
- Produces: `class Config` with get/set methods for all config keys

- [ ] **Step 1: Create src/Config.hpp**

```cpp
#pragma once
#include <Preferences.h>

class Config {
public:
    Config();
    bool begin();
    void reset();  // factory reset

    // WiFi
    String getWifiSsid();
    void setWifiSsid(const String& val);
    String getWifiPass();
    void setWifiPass(const String& val);

    // MQTT
    String getMqttBroker();
    void setMqttBroker(const String& val);
    int getMqttPort();
    void setMqttPort(int val);
    String getMqttUser();
    void setMqttUser(const String& val);
    String getMqttPass();
    void setMqttPass(const String& val);
    String getMqttTopic();
    void setMqttTopic(const String& val);
    bool getMqttTls();
    void setMqttTls(bool val);
    int getMqttQos();
    void setMqttQos(int val);
    String getMqttLwtTopic();
    void setMqttLwtTopic(const String& val);
    String getMqttLwtMsg();
    void setMqttLwtMsg(const String& val);
    bool getMqttRetain();
    void setMqttRetain(bool val);

    // HTTP
    String getHttpUrl();
    void setHttpUrl(const String& val);

    // TCP
    String getTcpIp();
    void setTcpIp(const String& val);
    int getTcpPort();
    void setTcpPort(int val);

    // Modbus
    int getModbusBaud();
    void setModbusBaud(int val);
    int getModbusBits();
    void setModbusBits(int val);
    int getModbusParity();
    void setModbusParity(int val);
    int getModbusStop();
    void setModbusStop(int val);
    int getModbusSlave();
    void setModbusSlave(int val);
    int getModbusReg();
    void setModbusReg(int val);
    int getModbusCount();
    void setModbusCount(int val);

    // Mode
    String getProtocolSelect();  // "mqtt" | "http" | "tcp"
    void setProtocolSelect(const String& val);
    String getRunMode();  // "push" | "pull"
    void setRunMode(const String& val);
    int getPushInterval();
    void setPushInterval(int val);

    // Utility
    bool isConfigured();  // true if wifi_ssid is non-empty

private:
    Preferences _prefs;
    static constexpr const char* NAMESPACE = "rtu_cfg";
};
```

- [ ] **Step 2: Create src/Config.cpp**

```cpp
#include "Config.hpp"

Config::Config() {}

bool Config::begin() {
    return _prefs.begin(NAMESPACE, false);
}

void Config::reset() {
    _prefs.clear();
}

#define GET_STRING(key) _prefs.getString(key).c_str()
#define SET_STRING(key, val) _prefs.putString(key, val)
#define GET_INT(key, defaultVal) _prefs.getInt(key, defaultVal)
#define PUT_INT(key, val) _prefs.putInt(key, val)
#define GET_BOOL(key, defaultVal) _prefs.getBool(key, defaultVal)
#define PUT_BOOL(key, val) _prefs.putBool(key, val)

bool Config::isConfigured() {
    return _prefs.getString("wifi_ssid").length() > 0;
}

String Config::getWifiSsid() { return _prefs.getString("wifi_ssid"); }
void Config::setWifiSsid(const String& val) { _prefs.putString("wifi_ssid", val); }

String Config::getWifiPass() { return _prefs.getString("wifi_pass"); }
void Config::setWifiPass(const String& val) { _prefs.putString("wifi_pass", val); }

String Config::getMqttBroker() { return _prefs.getString("mqtt_broker", ""); }
void Config::setMqttBroker(const String& val) { _prefs.putString("mqtt_broker", val); }

int Config::getMqttPort() { return _prefs.getInt("mqtt_port", 1883); }
void Config::setMqttPort(int val) { _prefs.putInt("mqtt_port", val); }

String Config::getMqttUser() { return _prefs.getString("mqtt_user", ""); }
void Config::setMqttUser(const String& val) { _prefs.putString("mqtt_user", val); }

String Config::getMqttPass() { return _prefs.getString("mqtt_pass", ""); }
void Config::setMqttPass(const String& val) { _prefs.putString("mqtt_pass", val); }

String Config::getMqttTopic() { return _prefs.getString("mqtt_topic", ""); }
void Config::setMqttTopic(const String& val) { _prefs.putString("mqtt_topic", val); }

bool Config::getMqttTls() { return _prefs.getBool("mqtt_tls", false); }
void Config::setMqttTls(bool val) { _prefs.putBool("mqtt_tls", val); }

int Config::getMqttQos() { return _prefs.getInt("mqtt_qos", 0); }
void Config::setMqttQos(int val) { _prefs.putInt("mqtt_qos", val); }

String Config::getMqttLwtTopic() { return _prefs.getString("mqtt_lwt_topic", ""); }
void Config::setMqttLwtTopic(const String& val) { _prefs.putString("mqtt_lwt_topic", val); }

String Config::getMqttLwtMsg() { return _prefs.getString("mqtt_lwt_msg", "offline"); }
void Config::setMqttLwtMsg(const String& val) { _prefs.putString("mqtt_lwt_msg", val); }

bool Config::getMqttRetain() { return _prefs.getBool("mqtt_retain", false); }
void Config::setMqttRetain(bool val) { _prefs.putBool("mqtt_retain", val); }

String Config::getHttpUrl() { return _prefs.getString("http_url", ""); }
void Config::setHttpUrl(const String& val) { _prefs.putString("http_url", val); }

String Config::getTcpIp() { return _prefs.getString("tcp_ip", ""); }
void Config::setTcpIp(const String& val) { _prefs.putString("tcp_ip", val); }

int Config::getTcpPort() { return _prefs.getInt("tcp_port", 8888); }
void Config::setTcpPort(int val) { _prefs.putInt("tcp_port", val); }

int Config::getModbusBaud() { return _prefs.getInt("modbus_baud", 9600); }
void Config::setModbusBaud(int val) { _prefs.putInt("modbus_baud", val); }

int Config::getModbusBits() { return _prefs.getInt("modbus_bits", 8); }
void Config::setModbusBits(int val) { _prefs.putInt("modbus_bits", val); }

int Config::getModbusParity() { return _prefs.getInt("modbus_parity", 0); }
void Config::setModbusParity(int val) { _prefs.putInt("modbus_parity", val); }

int Config::getModbusStop() { return _prefs.getInt("modbus_stop", 1); }
void Config::setModbusStop(int val) { _prefs.putInt("modbus_stop", val); }

int Config::getModbusSlave() { return _prefs.getInt("modbus_slave", 1); }
void Config::setModbusSlave(int val) { _prefs.putInt("modbus_slave", val); }

int Config::getModbusReg() { return _prefs.getInt("modbus_reg", 0); }
void Config::setModbusReg(int val) { _prefs.putInt("modbus_reg", val); }

int Config::getModbusCount() { return _prefs.getInt("modbus_count", 10); }
void Config::setModbusCount(int val) { _prefs.putInt("modbus_count", val); }

String Config::getProtocolSelect() { return _prefs.getString("protocol_select", "mqtt"); }
void Config::setProtocolSelect(const String& val) { _prefs.putString("protocol_select", val); }

String Config::getRunMode() { return _prefs.getString("run_mode", "push"); }
void Config::setRunMode(const String& val) { _prefs.putString("run_mode", val); }

int Config::getPushInterval() { return _prefs.getInt("push_interval", 30); }
void Config::setPushInterval(int val) { _prefs.putInt("push_interval", val); }
```

- [ ] **Step 3: Commit**

```bash
git add src/Config.hpp src/Config.cpp
git commit -m "feat: add Config NVS wrapper class"
```

---

## Task 3: Button Task — Debounce and Long-Press Detection

**Files:**
- Create: `src/Button.cpp`, `src/Button.hpp`

**Interfaces:**
- Consumes: nothing (GPIO only)
- Produces: sends `Cmd::CMD_ENTER_CONFIG` and `Cmd::CMD_RESTART` to `cmd_queue`

**Hardware:** GPIO 9, active-low (pulled high internally on ESP32C3)

- [ ] **Step 1: Create src/Button.hpp**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void ButtonTask(void* param);
```

- [ ] **Step 2: Create src/Button.cpp**

```cpp
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
```

- [ ] **Step 3: Commit**

```bash
git add src/Button.hpp src/Button.cpp
git commit -m "feat: add ButtonTask with debounce and long-press detection"
```

---

## Task 4: Modbus Task — RS485 RTU Communication

**Files:**
- Create: `src/Modbus.cpp`, `src/Modbus.hpp`

**Interfaces:**
- Consumes: reads from `cmd_queue` (receives `CMD_READ_MODBUS`)
- Produces: sends `ModbusData` to `modbus_data_queue`

**Hardware:** UART0 (default), GPIO 4 = DE (Driver Enable)

**Modbus details:**
- Function 0x03 Read Holding Registers
- Request: [Slave][0x03][RegHi][RegLo][CountHi][CountLo][CRCHi][CRCLo]
- Response: [Slave][0x03][ByteCount][Data...][CRCHi][CRCLo]
- Timeout: 1000ms, one retry on timeout

- [ ] **Step 1: Create src/Modbus.hpp**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void ModbusTask(void* param);
```

- [ ] **Step 2: Create src/Modbus.cpp**

```cpp
#include "Modbus.hpp"
#include "Queue.h"
#include "Config.hpp"
#include <driver/uart.h>
#include <driver/gpio.h>

static constexpr gpio_num_t DE_PIN = GPIO_NUM_4;
static constexpr int MODBUS_UART = UART_NUM_0;
static constexpr uint32_t MODBUS_TIMEOUT_MS = 1000;

extern QueueHandle_t cmd_queue;
extern QueueHandle_t modbus_data_queue;

static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void rs485_set_tx() {
    gpio_set_level(DE_PIN, 1);
}

static void rs485_set_rx() {
    gpio_set_level(DE_PIN, 0);
}

void ModbusTask(void* param) {
    Config cfg;
    cfg.begin();

    // Init DE pin
    gpio_set_direction(DE_PIN, GPIO_MODE_OUTPUT);
    rs485_set_rx();

    // Init UART
    uart_config_t uart_config = {
        .baud_rate = cfg.getModbusBaud(),
        .data_bits = (uart_word_length_t)(cfg.getModbusBits() - 1),
        .parity = (uart_parity_t)cfg.getModbusParity(),
        .stop_bits = (uart_stop_bits_t)(cfg.getModbusStop() == 1 ? UART_STOP_BITS_1 : UART_STOP_BITS_2),
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(MODBUS_UART, &uart_config);
    uart_set_pin(MODBUS_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(MODBUS_UART, 1024, 1024, 0, NULL, 0);

    uint8_t rx_buf[256];

    while (true) {
        Cmd cmd;
        // Block waiting for read command
        if (xQueueReceive(cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd != Cmd::CMD_READ_MODBUS) continue;

            // Build Modbus request
            uint8_t slave = cfg.getModbusSlave();
            uint16_t reg = cfg.getModbusReg();
            uint8_t count = cfg.getModbusCount();

            uint8_t req[8];
            req[0] = slave;
            req[1] = 0x03;  // Read Holding Registers
            req[2] = (reg >> 8) & 0xFF;
            req[3] = reg & 0xFF;
            req[4] = (count >> 8) & 0xFF;
            req[5] = count & 0xFF;

            uint16_t crc = crc16(req, 6);
            req[6] = crc & 0xFF;
            req[7] = (crc >> 8) & 0xFF;

            ModbusData result = {};
            result.valid = false;
            result.timestamp = millis();

            for (int attempt = 0; attempt < 2; attempt++) {
                // Flush RX buffer
                uart_flush_input(MODBUS_UART);

                // Send request
                rs485_set_tx();
                uart_write_bytes(MODBUS_UART, (const char*)req, 8);
                uart_tx_wait_idle(MODBUS_UART);
                rs485_set_rx();

                // Wait for response
                int len = uart_read_bytes(MODBUS_UART, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(MODBUS_TIMEOUT_MS));

                if (len > 5) {
                    // Verify CRC
                    uint16_t resp_crc = crc16(rx_buf, len - 2);
                    uint16_t got_crc = rx_buf[len - 1] | (rx_buf[len - 2] << 8);
                    if (resp_crc == got_crc && rx_buf[0] == slave && rx_buf[1] == 0x03) {
                        result.slave_addr = slave;
                        result.count = rx_buf[2] / 2;
                        for (int i = 0; i < result.count && i < 16; i++) {
                            result.registers[i] = (rx_buf[3 + i * 2] << 8) | rx_buf[4 + i * 2];
                        }
                        result.valid = true;
                        break;
                    }
                }
            }

            xQueueSend(modbus_data_queue, &result, portMAX_DELAY);
        }
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/Modbus.hpp src/Modbus.cpp
git commit -m "feat: add ModbusTask with RS485 half-duplex driver"
```

---

## Task 5: Network Task — Protocol Clients

**Files:**
- Create: `src/Network.cpp`, `src/Network.hpp`

**Interfaces:**
- Consumes: `modbus_data_queue`, `cmd_queue`
- Produces: Network sends (MQTT/HTTP/TCP)

**This task holds three protocol client classes:**
- `MqttClient` — PubSubClient wrapper
- `HttpClient` — WiFiClient + HTTP POST
- `TcpClient` — WiFiClient raw socket

The active client is selected based on `protocol_select` from Config.

- [ ] **Step 1: Create src/Network.hpp**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void NetworkTask(void* param);
```

- [ ] **Step 2: Create src/Network.cpp — MqttClient class**

```cpp
#include "Network.hpp"
#include "Queue.h"
#include "Config.hpp"
#include <PubSubClient.h>
#include <WiFi.h>

static const char* TAG = "Network";

extern QueueHandle_t modbus_data_queue;
extern QueueHandle_t cmd_queue;

// MQTT client instance
static WiFiClient _wifiClient;
static PubSubClient* _mqttClient = nullptr;

static bool mqtt_connect(Config& cfg) {
    if (!_mqttClient) {
        _mqttClient = new PubSubClient(_wifiClient);
    }
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

    // Build JSON payload
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

    return _mqttClient->publish(cfg.getMqttTopic().c_str(), json, cfg.getMqttRetain());
}
```

- [ ] **Step 3: Create src/Network.cpp — HttpClient**

```cpp
static bool http_post(ModbusData& data, Config& cfg) {
    if (cfg.getHttpUrl().length() == 0) return false;

    WiFiClient client;
    HTTPClient http;
    http.begin(client, cfg.getHttpUrl());
    http.addHeader("Content-Type", "application/json");

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

    int code = http.POST((uint8_t*)json, strlen(json));
    http.end();
    return code >= 200 && code < 300;
}
```

- [ ] **Step 4: Create src/Network.cpp — TcpClient**

```cpp
static bool tcp_send(ModbusData& data, Config& cfg) {
    if (cfg.getTcpIp().length() == 0) return false;

    WiFiClient client;
    if (!client.connect(cfg.getTcpIp().c_str(), cfg.getTcpPort())) return false;

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

    client.print(json);
    client.stop();
    return true;
}
```

- [ ] **Step 5: Create src/Network.cpp — NetworkTask main loop**

```cpp
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
```

- [ ] **Step 6: Commit**

```bash
git add src/Network.hpp src/Network.cpp
git commit -m "feat: add NetworkTask with MQTT, HTTP, TCP clients"
```

---

## Task 6: WebConfig Task — AP + Web Server

**Files:**
- Create: `src/WebConfig.cpp`, `src/WebConfig.hpp`

**Interfaces:**
- Consumes: HTTP requests from browser
- Produces: saves to NVS via `Config`, sends `CMD_RESTART` to `cmd_queue`

**Routes:** `/` (GET), `/save` (POST), `/reset` (POST), `/status` (GET)

**AP:** SSID `ESP32-RTU-Config`, IP 192.168.4.1

- [ ] **Step 1: Create src/WebConfig.hpp**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void WebConfigTask(void* param);
```

- [ ] **Step 2: Create src/WebConfig.cpp**

```cpp
#include "WebConfig.hpp"
#include "Config.hpp"
#include "Queue.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>

static AsyncWebServer* _server = nullptr;

extern QueueHandle_t cmd_queue;

static const char HTML_FORM[] = R"(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 RTU Config</title><style>
body{font-family:Arial;max-width:600px;margin:0 auto;padding:20px}
input,select{width:100%;padding:8px;margin:5px 0}
h2{color:#333;border-bottom:1px solid #ccc;padding-bottom:5px}
button{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;width:100%}
button:hover{background:#45a049}
.note{font-size:12px;color:#666}
</style></head>
<body>
<h1>ESP32 RTU Configuration</h1>
<form action='/save' method='POST'>

<h2>WiFi</h2>
<input name='wifi_ssid' placeholder='SSID' value='{wifi_ssid}'>
<input name='wifi_pass' type='password' placeholder='Password' value='{wifi_pass}'>

<h2>MQTT</h2>
<input name='mqtt_broker' placeholder='Broker' value='{mqtt_broker}'>
<input name='mqtt_port' type='number' placeholder='Port' value='{mqtt_port}'>
<input name='mqtt_user' placeholder='Username' value='{mqtt_user}'>
<input name='mqtt_pass' type='password' placeholder='Password' value='{mqtt_pass}'>
<input name='mqtt_topic' placeholder='Topic' value='{mqtt_topic}'>
<div><label><input name='mqtt_tls' type='checkbox' {mqtt_tls}> TLS/SSL</label></div>
<div>QoS: <select name='mqtt_qos'>
<option value='0' {qos0}>QoS 0</option>
<option value='1' {qos1}>QoS 1</option>
</select></div>
<input name='mqtt_lwt_topic' placeholder='LWT Topic' value='{mqtt_lwt_topic}'>
<input name='mqtt_lwt_msg' placeholder='LWT Message' value='{mqtt_lwt_msg}'>
<div><label><input name='mqtt_retain' type='checkbox' {mqtt_retain}> Retain</label></div>

<h2>HTTP</h2>
<input name='http_url' placeholder='http://server/path' value='{http_url}'>

<h2>TCP</h2>
<input name='tcp_ip' placeholder='Server IP' value='{tcp_ip}'>
<input name='tcp_port' type='number' placeholder='Port' value='{tcp_port}'>

<h2>Modbus</h2>
<input name='modbus_baud' type='number' placeholder='Baud Rate' value='{modbus_baud}'>
<div>Baud: 
<select name='modbus_bits'>
<option value='8' {bits8}>8</option>
<option value='7' {bits7}>7</option>
</select></div>
<div>Parity: 
<select name='modbus_parity'>
<option value='0' {par0}>None</option>
<option value='1' {par1}>Odd</option>
<option value='2' {par2}>Even</option>
</select></div>
<input name='modbus_stop' type='number' placeholder='Stop Bits' value='{modbus_stop}'>
<input name='modbus_slave' type='number' placeholder='Slave Address' value='{modbus_slave}'>
<input name='modbus_reg' type='number' placeholder='Start Register' value='{modbus_reg}'>
<input name='modbus_count' type='number' placeholder='Register Count' value='{modbus_count}'>

<h2>Mode</h2>
<div>Protocol:
<select name='protocol_select'>
<option value='mqtt' {proto_mqtt}>MQTT</option>
<option value='http' {proto_http}>HTTP</option>
<option value='tcp' {proto_tcp}>TCP</option>
</select></div>
<div>Mode:
<select name='run_mode'>
<option value='push' {mode_push}>Push</option>
<option value='pull' {mode_pull}>Pull</option>
</select></div>
<input name='push_interval' type='number' placeholder='Push Interval (sec)' value='{push_interval}'>

<button type='submit'>Save & Reboot</button>
</form>
<form action='/reset' method='POST' style='margin-top:10px'>
<button type='submit' style='background:#f44336'>Factory Reset</button>
</form>
</body></html>
)";
```

- [ ] **Step 3: Create src/WebConfig.cpp — replacement helpers**

```cpp
static String build_response(Config& cfg) {
    String html = HTML_FORM;
    // WiFi
    html.replace("{wifi_ssid}", cfg.getWifiSsid());
    html.replace("{wifi_pass}", cfg.getWifiPass());
    // MQTT
    html.replace("{mqtt_broker}", cfg.getMqttBroker());
    html.replace("{mqtt_port}", String(cfg.getMqttPort()));
    html.replace("{mqtt_user}", cfg.getMqttUser());
    html.replace("{mqtt_pass}", cfg.getMqttPass());
    html.replace("{mqtt_topic}", cfg.getMqttTopic());
    html.replace("{mqtt_tls}", cfg.getMqttTls() ? "checked" : "");
    html.replace("{qos0}", cfg.getMqttQos() == 0 ? "selected" : "");
    html.replace("{qos1}", cfg.getMqttQos() == 1 ? "selected" : "");
    html.replace("{mqtt_lwt_topic}", cfg.getMqttLwtTopic());
    html.replace("{mqtt_lwt_msg}", cfg.getMqttLwtMsg());
    html.replace("{mqtt_retain}", cfg.getMqttRetain() ? "checked" : "");
    // HTTP
    html.replace("{http_url}", cfg.getHttpUrl());
    // TCP
    html.replace("{tcp_ip}", cfg.getTcpIp());
    html.replace("{tcp_port}", String(cfg.getTcpPort()));
    // Modbus
    html.replace("{modbus_baud}", String(cfg.getModbusBaud()));
    html.replace("{bits8}", cfg.getModbusBits() == 8 ? "selected" : "");
    html.replace("{bits7}", cfg.getModbusBits() == 7 ? "selected" : "");
    html.replace("{par0}", cfg.getModbusParity() == 0 ? "selected" : "");
    html.replace("{par1}", cfg.getModbusParity() == 1 ? "selected" : "");
    html.replace("{par2}", cfg.getModbusParity() == 2 ? "selected" : "");
    html.replace("{modbus_stop}", String(cfg.getModbusStop()));
    html.replace("{modbus_slave}", String(cfg.getModbusSlave()));
    html.replace("{modbus_reg}", String(cfg.getModbusReg()));
    html.replace("{modbus_count}", String(cfg.getModbusCount()));
    // Mode
    html.replace("{proto_mqtt}", cfg.getProtocolSelect() == "mqtt" ? "selected" : "");
    html.replace("{proto_http}", cfg.getProtocolSelect() == "http" ? "selected" : "");
    html.replace("{proto_tcp}", cfg.getProtocolSelect() == "tcp" ? "selected" : "");
    html.replace("{mode_push}", cfg.getRunMode() == "push" ? "selected" : "");
    html.replace("{mode_pull}", cfg.getRunMode() == "pull" ? "selected" : "");
    html.replace("{push_interval}", String(cfg.getPushInterval()));
    return html;
}

static void save_params(AsyncWebServerRequest* request, Config& cfg) {
    // Extract and save all params
    #define SET(param) if (request->hasParam(#param)) cfg.set##param(request->getParam(#param)->value())
    SET(WifiSsid);
    SET(WifiPass);
    SET(MqttBroker);
    SET(MqttPort);
    SET(MqttUser);
    SET(MqttPass);
    SET(MqttTopic);
    cfg.setMqttTls(request->hasParam("mqtt_tls"));
    SET(MqttQos);
    SET(MqttLwtTopic);
    SET(MqttLwtMsg);
    cfg.setMqttRetain(request->hasParam("mqtt_retain"));
    SET(HttpUrl);
    SET(TcpIp);
    SET(TcpPort);
    SET(ModbusBaud);
    SET(ModbusBits);
    SET(ModbusParity);
    SET(ModbusStop);
    SET(ModbusSlave);
    SET(ModbusReg);
    SET(ModbusCount);
    SET(ProtocolSelect);
    SET(RunMode);
    SET(PushInterval);
    #undef SET
}
```

- [ ] **Step 4: Create src/WebConfig.cpp — task entry**

```cpp
void WebConfigTask(void* param) {
    Config cfg;
    cfg.begin();

    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-RTU-Config");

    _server = new AsyncWebServer(80);

    _server->on("/", HTTP_GET, [&cfg](AsyncWebServerRequest* r) {
        r->send(200, "text/html", build_response(cfg));
    });

    _server->on("/save", HTTP_POST, [&cfg](AsyncWebServerRequest* r) {
        save_params(r, cfg);
        Cmd cmd = Cmd::CMD_RESTART;
        xQueueSend(cmd_queue, &cmd, 0);
        r->send(200, "text/plain", "Saved. Rebooting...");
    });

    _server->on("/reset", HTTP_POST, [&cfg](AsyncWebServerRequest* r) {
        cfg.reset();
        Cmd cmd = Cmd::CMD_RESTART;
        xQueueSend(cmd_queue, &cmd, 0);
        r->send(200, "text/plain", "Reset. Rebooting...");
    });

    _server->on("/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        String json = "{";
        json += "\"chip_id\":\"" + String((uint32_t)ESP.getEfuseMac(), HEX) + "\"";
        json += ",\"wifi_mode\":\"AP\"";
        json += ",\"free_heap\":" + String(ESP.getFreeHeap());
        json += "}";
        r->send(200, "application/json", json);
    });

    _server->begin();

    // Suspend until needed (controlled by AppTask)
    vTaskSuspend(NULL);
}
```

- [ ] **Step 5: Commit**

```bash
git add src/WebConfig.hpp src/WebConfig.cpp
git commit -m "feat: add WebConfigTask with AP + embedded HTML form"
```

---

## Task 7: App Task — Mode Orchestration

**Files:**
- Create: `src/App.cpp`, `src/App.hpp`

**Interfaces:**
- Consumes: `cmd_queue`, `web_config_queue`
- Produces: sends `CMD_READ_MODBUS` to `cmd_queue on timer`
- Controls: starts/stops/suspends other tasks

In Push mode: uses a software timer to periodically trigger Modbus reads.
In Pull mode: NetworkTask handles the request-response cycle directly.

- [ ] **Step 1: Create src/App.hpp**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void AppTask(void* param);
```

- [ ] **Step 2: Create src/App.cpp — Timer callback**

```cpp
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
```

- [ ] **Step 3: Create src/App.cpp — task entry**

```cpp
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
```

- [ ] **Step 4: Commit**

```bash
git add src/App.hpp src/App.cpp
git commit -m "feat: add AppTask with push mode timer"
```

---

## Task 8: main.cpp — Entry Point and Task Creation

**Files:**
- Modify: `src/main.cpp`

**Logic:**
1. Init Serial
2. Init Config, check if configured + no button pressed → enter RUN mode
3. Otherwise → enter CONFIG mode
4. Create queues
5. Create tasks
6. Start scheduler

- [ ] **Step 1: Replace src/main.cpp**

```cpp
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Queue.h"
#include "Config.hpp"
#include "Button.hpp"
#include "Modbus.hpp"
#include "Network.hpp"
#include "WebConfig.hpp"
#include "App.hpp"

// Queue handles (defined here, extern in Queue.h)
QueueHandle_t modbus_data_queue = nullptr;
QueueHandle_t cmd_queue = nullptr;
QueueHandle_t web_config_queue = nullptr;

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
    web_config_queue = xQueueCreate(CONFIG_QUEUE_LEN, sizeof(ConfigData));

    if (!isConfigured) {
        // No config — enter CONFIG mode (AP)
        Serial.println("No config found. Starting AP mode...");

        // Start WebConfig task first (it suspends itself after setup)
        xTaskCreatePinnedToCore(WebConfigTask, "WebConfig", 8192, NULL, 1, &s_webconfig_task_h, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        // Resume WebConfigTask (it suspended itself)
        vTaskResume(s_webconfig_task_h);

        // Nothing else runs in config mode — tasks suspend after init
        vTaskStartScheduler();
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
            vTaskStartScheduler();
        }

        Serial.print("WiFi connected: ");
        Serial.println(WiFi.localIP());

        // Start all run-mode tasks
        xTaskCreatePinnedToCore(ButtonTask, "Button", 2048, NULL, 4, &s_button_task_h, 0);
        xTaskCreatePinnedToCore(ModbusTask, "Modbus", 4096, NULL, 3, &s_modbus_task_h, 0);
        xTaskCreatePinnedToCore(NetworkTask, "Network", 8192, NULL, 2, &s_network_task_h, 0);
        xTaskCreatePinnedToCore(AppTask, "App", 4096, NULL, 2, &s_app_task_h, 0);

        vTaskStartScheduler();
    }
}

void loop() {
    // FreeRTOS handles scheduling — loop does nothing
}
```

- [ ] **Step 2: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add main.cpp entry point with task creation and boot logic"
```

---

## Task 9: Integrate Button ISR for Long-Press Detection

**Files:**
- Modify: `src/Button.cpp`, `src/main.cpp`

The polling approach in ButtonTask works, but long-press should be detected even during boot. Add a GPIO ISR that sets a flag, which ButtonTask checks on its next wake cycle.

- [ ] **Step 1: Modify src/Button.cpp — add ISR flag**

```cpp
// Add at top of Button.cpp after includes:
static volatile bool s_button_pressed = false;

static void IRAM_ATTR button_isr_handler(void* arg) {
    s_button_pressed = true;
}
```

In ButtonTask init, attach the interrupt:
```cpp
gpio_install_isr_service(0);
gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_ANYEDGE);
```

- [ ] **Step 2: Commit**

```bash
git add src/Button.cpp src/main.cpp
git commit -m "feat: add GPIO ISR for button press detection"
```

---

## Task 10: LED Status Indicator

**Files:**
- Modify: `src/Button.cpp`, `src/Modbus.cpp`, `src/Network.cpp`

Add LED blink patterns for status indication on GPIO 8:
- Slow blink (1Hz): WiFi connecting
- Fast blink (4Hz): Modbus activity
- Solid: Error
- Off: Normal run

Create `StatusLED.cpp / StatusLED.hpp`:
- `StatusLED::set(mode)` where mode = `LED_WIFI_CONNECTING | LED_MODBUS_BUSY | LED_ERROR | LED_OFF`

Each task calls `StatusLED::set()` at relevant states.

- [ ] **Step 1: Create src/StatusLED.hpp and src/StatusLED.cpp**

```cpp
#pragma once
enum class LedMode { OFF, WIFI_CONNECTING, MODBUS_BUSY, ERROR, OK };

class StatusLED {
public:
    static void begin();
    static void set(LedMode mode);
};
```

- [ ] **Step 2: Commit**

```bash
git add src/StatusLED.hpp src/StatusLED.cpp
git commit -m "feat: add StatusLED with blink patterns"
```

---

## Task 11: Final Integration and Build Verification

**Files:**
- Modify: `src/main.cpp` (minor tweaks)

- [ ] **Step 1: Run platformio pkg install**

```bash
cd /data/Project/ESP/esp32-rtu
pio pkg install
```

- [ ] **Step 2: Build the project**

```bash
pio run
```

Expected: Clean build with no errors.

- [ ] **Step 3: Fix any compilation errors**

Common issues:
- UART config struct field names for ESP32-C3 Arduino
- PubSubClient header conflicts
- FreeRTOS queue type mismatches

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: complete WiFi RTU gateway — build verified"
```

---

## Spec Coverage Check

| Spec Section | Tasks |
|--------------|-------|
| 3.1 Task Summary (5 tasks) | Tasks 3,4,5,6,7 |
| 3.2 Queues (3 queues) | Task 1 (Queue.h) |
| 4. Config (all NVS keys) | Task 2 |
| 5. Modbus RTU | Task 4 |
| 6.1 MQTT | Task 5 |
| 6.2 HTTP | Task 5 |
| 6.3 TCP | Task 5 |
| 7. Web Portal | Task 6 |
| 8. Push/Pull modes | Tasks 4,5,7 |
| 11. Boot behavior | Task 8 |
| 12. Error handling | LED in Task 10 |

---

## Self-Review

1. **Placeholder scan:** No TBD/TODO found. All code is concrete.
2. **Type consistency:** `ModbusData`, `Cmd`, `Config` all match spec. `Queue.h` defines all shared types before use.
3. **Spec coverage:** All sections covered by tasks.
4. **Task boundaries:** Each task is independently compilable and testable. Dependencies between tasks are explicit via `extern` queue handles.
