# WiFi RTU Gateway

ESP32C3 + Modbus RTU ⇄ WiFi 网关，支持 MQTT / HTTP / TCP 上传。

## 功能特性

| 功能 | 说明 |
|------|------|
| **Modbus RTU** | RS485 半双工，支持 0x03 读保持寄存器 |
| **MQTT** | 支持 TLS、QoS 0/1、遗嘱消息、保留消息 |
| **HTTP** | POST JSON 到自建服务器 |
| **TCP** | 原始 Socket 连接推送 |
| **Push / Pull** | 主动定时上传 or 服务器查询模式 |
| **Web 配置** | AP 模式嵌入式 HTML 表单，无需配网工具 |
| **长按按键** | GPIO 9 长按 3 秒进入配置模式 |
| **LED 状态** | GPIO 8 指示运行状态 |

## 硬件

- MCU: ESP32C3 (airm2m_core_esp32c3)
- RS485 DE: GPIO 4
- 按键: GPIO 9 (低电平有效)
- LED: GPIO 8

## 快速开始

### 编译

```bash
pio run
```

### 首次配网

1. 设备上电，如无 WiFi 配置会自动进入 AP 模式
2. 连接 WiFi SSID: `ESP32-RTU-Config`（无密码）
3. 打开浏览器访问 `192.168.4.1`
4. 填写 WiFi / MQTT / HTTP / TCP / Modbus 参数
5. 点击 Save & Reboot

### 长按进入配置

GPIO 9 按键长按 3 秒即进入 AP 配置模式。

## 目录结构

```
src/
├── main.cpp          # 入口，任务创建，启动逻辑
├── App.cpp/.hpp     # AppTask — 推送定时器，模式协调
├── Modbus.cpp/.hpp  # ModbusTask — RS485 半双工驱动
├── Network.cpp/.hpp  # NetworkTask — MQTT/HTTP/TCP 客户端
├── WebConfig.cpp/.hpp  # WebConfigTask — AP + Web 配置页面
├── Config.cpp/.hpp   # NVS 配置读写
├── Button.cpp/.hpp   # ButtonTask — GPIO 中断，长按检测
├── StatusLED.cpp/.hpp # 状态 LED
└── Queue.h          # 队列定义和命令枚举
```

## 配置项

| 类别 | 字段 |
|------|------|
| WiFi | `wifi_ssid`, `wifi_pass` |
| MQTT | `mqtt_broker`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_topic`, `mqtt_tls`, `mqtt_qos`, `mqtt_lwt_topic`, `mqtt_lwt_msg`, `mqtt_retain` |
| HTTP | `http_url` |
| TCP | `tcp_ip`, `tcp_port` |
| Modbus | `modbus_baud`, `modbus_bits`, `modbus_parity`, `modbus_stop`, `modbus_slave`, `modbus_reg`, `modbus_count` |
| 运行 | `protocol_select` (mqtt/http/tcp), `run_mode` (push/pull), `push_interval` |

## 上传数据格式

所有协议使用相同 JSON 格式：

```json
{
  "device_id": "esp32c3_chip_id",
  "timestamp": 1726000000,
  "registers": [
    {"addr": 0, "value": 123},
    {"addr": 1, "value": 456}
  ]
}
```

## 架构

FreeRTOS 5 任务架构，通过队列通信：

```
ButtonTask ──(cmd_queue)──► AppTask ──(cmd_queue)──► ModbusTask ──(modbus_data_queue)──► NetworkTask ──► MQTT/HTTP/TCP
                            │
                    WebConfigTask ◄──(cmd_queue)── 按钮长按 / Web 保存
```

## 依赖

- PubSubClient — MQTT 客户端
- ESP Async WebServer — Web 服务器
- Async TCP — ESP Async WebServer 依赖
- Preferences — NVS 存储

## 文档

- 设计文档: `docs/superpowers/specs/2026-09-17-wifi-rtu-design.md`
- 实现计划: `docs/superpowers/plans/2026-09-17-wifi-rtu-implementation-plan.md`
