# WiFi RTU Gateway — Design Specification

**Date:** 2026-09-17
**Project:** ESP32C3 WiFi Modbus RTU Gateway
**Framework:** PlatformIO + Arduino + FreeRTOS

---

## 1. Overview

A WiFi-enabled Modbus RTU gateway that bridges RS485-based Modbus devices to the cloud via MQTT (primary), HTTP (secondary), or TCP (tertiary) protocols. The device operates in either Push mode (periodic upload) or Pull mode (server-initiated queries). Configuration is provided via an embedded AP + Web portal, and persisted via NVS.

**Core Loop:**
```
Modbus RTU Device <--> ESP32C3 <--> WiFi <--> Cloud (MQTT/HTTP/TCP)
```

---

## 2. Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32C3 (airm2m_core_esp32c3) |
| RS485 Interface | External UART-based RS485 transceiver (half-duplex) |
| Button | Physical button on GPIO (active-low), short press = restart, long press (3s) = enter config mode |
| Indicator | Onboard LED (GPIO 8) for status indication |

---

## 3. FreeRTOS Task Architecture

### 3.1 Task Summary

| Task | Priority | Stack Size | Core | Responsibilities |
|------|----------|------------|------|------------------|
| `ButtonTask` | 4 (HIGH) | 2KB | Core 0 | Debounce button, detect long-press |
| `ModbusTask` | 3 (HIGH) | 4KB | Core 0 | RTU communication, register polling |
| `NetworkTask` | 2 (MED) | 8KB | Core 0 | Protocol selection and data transmission |
| `AppTask` | 2 (MED) | 4KB | Core 0 | Mode orchestration, timer management |
| `WebConfigTask` | 1 (LOW) | 8KB | Core 0 | AP + Web server for configuration |

### 3.2 Inter-Task Communication

```
Queue: modbus_data_queue (xQueueCreate, length=5)
  → ModbusTask produces, NetworkTask consumes

Queue: cmd_queue (xQueueCreate, length=4)
  → ButtonTask/AppTask produce, AppTask/ModbusTask/NetworkTask consume

Queue: web_config_queue (xQueueCreate, length=2)
  → WebConfigTask produces, AppTask consumes (triggers save + restart)
```

### 3.3 Task Details

#### ButtonTask
- Polls GPIO button every 50ms
- Short press (< 1s): restart device
- Long press (>= 3s): send CMD_ENTER_CONFIG to cmd_queue

#### ModbusTask
- Manages UART/ RS485 half-duplex communication
- Receives Modbus read requests from NetworkTask (Pull mode)
- In Push mode: executes periodic polling on a timer
- Publishes completed responses to modbus_data_queue

#### NetworkTask
- Subscribes to cmd_queue for mode switches
- Loads active protocol client based on current_protocol
- In Push mode: consumes modbus_data_queue and uploads
- In Pull mode: receives server requests, forwards to ModbusTask, returns response

#### AppTask
- Reads config from NVS on boot
- Orchestrates task startup
- Processes cmd_queue for mode transitions (RUN ↔ CONFIG)
- Manages push_interval timer for Push mode

#### WebConfigTask
- When RUN_MODE == CONFIG: starts WiFi AP ("ESP32-RTU-Config"), serves Web UI
- When RUN_MODE == RUN: suspends itself
- Saves config to NVS and triggers restart on /save

---

## 4. Configuration Storage (NVS)

**Namespace:** `rtu_cfg`

| Key | Type | Description |
|-----|------|-------------|
| `wifi_ssid` | string | WiFi SSID |
| `wifi_pass` | string | WiFi password |
| `mqtt_broker` | string | MQTT broker host |
| `mqtt_port` | int | MQTT port (default 1883, 8883 for TLS) |
| `mqtt_user` | string | MQTT username |
| `mqtt_pass` | string | MQTT password |
| `mqtt_topic` | string | MQTT publish/subscribe topic |
| `mqtt_tls` | bool | MQTT TLS/SSL enable |
| `mqtt_qos` | int | MQTT QoS (0 or 1) |
| `mqtt_lwt_topic` | string | MQTT LWT topic |
| `mqtt_lwt_msg` | string | MQTT LWT message |
| `mqtt_retain` | bool | MQTT retain messages |
| `http_url` | string | HTTP POST URL |
| `tcp_ip` | string | TCP server IP |
| `tcp_port` | int | TCP server port |
| `modbus_baud` | int | Modbus baud rate (default 9600) |
| `modbus_bits` | int | Data bits (default 8) |
| `modbus_parity` | int | Parity: 0=None, 1=Odd, 2=Even |
| `modbus_stop` | int | Stop bits (default 1) |
| `modbus_slave` | int | Modbus slave address |
| `modbus_reg` | int | Start register address |
| `modbus_count` | int | Number of registers to read |
| `protocol_select` | string | "mqtt" \| "http" \| "tcp" |
| `run_mode` | string | "push" \| "pull" |
| `push_interval` | int | Push mode interval in seconds (default 30) |

---

## 5. Modbus Communication

### 5.1 Modbus RTU Framing

- Function code: **0x03** (Read Holding Registers)
- Request: `[SlaveAddr] [0x03] [StartRegHi] [StartRegLo] [CountHi] [CountLo] [CRCHi] [CRCLo]`
- Response: `[SlaveAddr] [0x03] [ByteCount] [Data...] [CRCHi] [CRCLo]`

### 5.2 RS485 Half-Duplex

- DE (Driver Enable) pin controls TX/RX direction
- TX: set DE=High before sending, set DE=Low after TX complete
- Timeout: 1000ms for response

---

## 6. Network Protocols

### 6.1 MQTT (Primary)

- Client: **PubSubClient** (Arduino) or **mqtt-lite**
- Features: TLS support, QoS 0/1, LWT, Retained messages
- Payload format: JSON
  ```json
  {
    "device_id": "<chip_id>",
    "timestamp": 1726000000,
    "registers": [
      {"addr": 0, "value": 123},
      {"addr": 1, "value": 456}
    ]
  }
  ```

### 6.2 HTTP (Secondary)

- Method: POST
- Content-Type: application/json
- Payload: same JSON as MQTT

### 6.3 TCP (Tertiary)

- Plain TCP socket connection to server
- JSON payload sent as raw bytes
- Server responds with ACK or request (Pull mode)

---

## 7. Web Configuration Portal

### 7.1 AP Configuration

- SSID: `ESP32-RTU-Config`
- Password: none (open) or configurable
- IP: 192.168.4.1

### 7.2 Web Pages

| Route | Method | Description |
|-------|--------|-------------|
| `/` | GET | Configuration form (all fields) |
| `/save` | POST | Save parameters to NVS, reboot |
| `/reset` | POST | Factory reset, reboot |
| `/status` | GET | Device status JSON |

### 7.3 Form Fields

All configuration items from Section 4 presented as an HTML form.

---

## 8. Modes of Operation

### 8.1 Push Mode

- AppTask starts a periodic timer (push_interval)
- Timer fires → AppTask sends CMD_READ_MODBUS to ModbusTask
- ModbusTask reads registers → puts data in modbus_data_queue
- NetworkTask consumes queue → uploads via active protocol

### 8.2 Pull Mode

- NetworkTask listens for incoming requests
- Request received → NetworkTask sends CMD_READ_MODBUS to ModbusTask
- ModbusTask reads registers → puts data in queue
- NetworkTask consumes queue → responds to requester

### 8.3 Protocol Switching

- NetworkTask holds all three protocol clients initialized
- `protocol_select` determines which one is active at any time
- Switch takes effect on next transmission cycle

---

## 9. Directory Structure

```
src/
├── main.cpp              # Entry point, task creation
├── App.cpp / App.hpp     # AppTask — mode orchestration
├── Modbus.cpp / Modbus.hpp   # ModbusTask + RS485 driver
├── Network.cpp / Network.hpp # NetworkTask + protocol clients
├── WebConfig.cpp / WebConfig.hpp  # WebConfigTask + embedded HTML
├── Config.cpp / Config.hpp  # NVS wrapper
├── Button.cpp / Button.hpp  # ButtonTask
└── Queue.h              # Shared queue definitions

test/
└── (placeholder)

platformio.ini
```

---

## 10. Dependencies

| Library | Purpose |
|---------|---------|
| Arduino | Framework base |
| PubSubClient | MQTT client |
| ESPAsyncWebServer | Web server |
| Preferences | NVS storage |
| FreeRTOS (built-in) | Multi-tasking |

---

## 11. Boot Behavior

1. **Load config from NVS**
2. **If no WiFi config OR long-press detected:**
   - Set RUN_MODE = CONFIG
   - Start WebConfigTask (AP mode)
3. **Else:**
   - Set RUN_MODE = RUN
   - Connect to WiFi
   - Start AppTask, ModbusTask, NetworkTask
4. **Configuration saved:**
   - WebConfigTask writes NVS
   - Device reboots → normal boot sequence

---

## 12. Error Handling

| Scenario | Behavior |
|----------|----------|
| WiFi connection failed | Retry every 10s, LED slow blink |
| Modbus timeout | Retry once, then report error |
| MQTT disconnect | Auto-reconnect with backoff |
| HTTP POST failed | Retry 3x, then log error |
| TCP connection lost | Reconnect on next transmission |
