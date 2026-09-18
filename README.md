# WiFi RTU Gateway

ESP32C3 + Modbus RTU ⇄ WiFi 网关，支持 MQTT / HTTP / TCP 上传和 Modbus TCP Gateway 模式。

## 功能特性

| 功能 | 说明 |
|------|------|
| **Modbus RTU** | RS485 半双工，支持 0x03 读保持寄存器 |
| **MQTT** | 支持 TLS、QoS 0/1、遗嘱消息、保留消息 |
| **HTTP** | POST JSON 到自建服务器 |
| **TCP** | 原始 Socket 连接推送 |
| **Modbus TCP Gateway** | Modbus TCP ⇄ Modbus RTU 双向转换（TCP Server 端口 502）|
| **Push / Pull** | 主动定时上传 or 服务器查询模式 |
| **Gateway** | 独立 Gateway 模式（TCP→RTU 协议转换）|
| **Web 配置** | AP 模式嵌入式 HTML 表单，无需配网工具 |
| **长按按键** | GPIO 9 长按 3 秒进入配置模式 |
| **LED 状态** | GPIO 8 指示运行状态 |

## 硬件

- MCU: ESP32C3 (airm2m_core_esp32c3)
- RS485 DE: GPIO 4
- 按键: GPIO 9 (低电平有效)
- LED: GPIO 8

## 三种运行模式

### Upload 模式（MQTT / HTTP / TCP）

设备作为 Modbus RTU 从机采集器，通过 WiFi 上传数据到云端。

```
Modbus RTU 从机 ← RS485 ← ESP32C3 ← WiFi → MQTT / HTTP / TCP Server
```

### Gateway 模式（Modbus TCP ↔ RTU）

设备作为 Modbus TCP Server，将来自网络的 Modbus TCP 请求透明转换为 Modbus RTU 发往下位机，响应再转回 TCP。

```
Modbus TCP Client ← WiFi ← ESP32C3 ← RS485 → Modbus RTU 从机
```

Gateway 模式与 Upload 模式互斥，启用后不进行云端上传。

## 快速开始

### 编译

```bash
pio run
```

### 首次配网

1. 设备上电，如无 WiFi 配置会自动进入 AP 模式
2. 连接 WiFi SSID: `ESP32-RTU-Config`（无密码）
3. 打开浏览器访问 `192.168.4.1`
4. 填写 WiFi / MQTT / HTTP / Modbus 参数
5. 选择运行模式：Push (MQTT/HTTP)、Pull (MQTT/HTTP) 或 Gateway (TCP→RTU)
6. 点击 Save & Reboot

### Gateway 模式配置

1. 在 Web 配置页面选择 `Run Mode: Gateway (TCP→RTU)`
2. 设置 `Gateway Port`（默认 502）
3. 保存重启后，设备在端口 502 监听 TCP 连接
4. 使用任意 Modbus TCP 主站工具连接即可

### 长按进入配置

GPIO 9 按键长按 3 秒即进入 AP 配置模式。

## 目录结构

```
src/
├── main.cpp              # 入口，任务创建，启动逻辑
├── App.cpp/.hpp         # AppTask — 推送定时器，模式协调
├── Modbus.cpp/.hpp      # ModbusTask — RS485 半双工驱动（支持双队列）
├── ModbusTCPGateway.cpp/.hpp  # ModbusTCPGatewayTask — TCP Server，TCP↔RTU 转换
├── Network.cpp/.hpp     # NetworkTask — MQTT/HTTP/TCP 客户端
├── WebConfig.cpp/.hpp   # WebConfigTask — AP + Web 配置页面
├── Config.cpp/.hpp      # NVS 配置读写
├── Button.cpp/.hpp      # ButtonTask — GPIO 轮询，长按检测
├── StatusLED.cpp/.hpp    # 状态 LED
└── Queue.h             # 队列定义和命令枚举
```

## 配置项

| 类别 | 字段 |
|------|------|
| WiFi | `wifi_ssid`, `wifi_pass` |
| MQTT | `mqtt_broker`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_topic`, `mqtt_tls`, `mqtt_qos`, `mqtt_lwt_topic`, `mqtt_lwt_msg`, `mqtt_retain` |
| HTTP | `http_url` |
| TCP | `tcp_ip`, `tcp_port` |
| Modbus | `modbus_baud`, `modbus_bits`, `modbus_parity`, `modbus_stop`, `modbus_slave`, `modbus_reg`, `modbus_count` |
| Gateway | `gateway_port`（默认 502）|
| 运行 | `protocol_select` (mqtt/http/tcp), `run_mode` (push/pull/gateway), `push_interval` |

## 上传数据格式（Upload 模式）

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

## Gateway 协议格式

### Modbus TCP → RTU

Modbus TCP 客户端发送标准 MBAP 头 + PDU，Gateway 解析后：
1. 提取 Transaction ID 和 Unit ID（作为 RTU 从机地址）
2. 取 PDU（功能码 + 数据）追加 CRC16 生成 RTU 帧
3. 通过 RS485 转发给从机

### RTU → TCP

从机响应后，去掉 CRC，附加 MBAP 头（Transaction ID 原样返回），发回 TCP 客户端。

### 支持的功能码

| 功能码 | 名称 |
|--------|------|
| 0x03 | 读保持寄存器 |
| 0x04 | 读输入寄存器 |
| 0x06 | 写单个寄存器 |

其他功能码返回异常码 0x02。超时（2000ms）返回异常码 0x04。

## 架构

### Upload 模式（Push / Pull）

```
ButtonTask ──(cmd_queue)──► AppTask ──(cmd_queue)──► ModbusTask ──(modbus_data_queue)──► NetworkTask ──► MQTT/HTTP/TCP
                            │
                    WebConfigTask ◄──(cmd_queue)── 按钮长按 / Web 保存
```

### Gateway 模式

```
Modbus TCP Client ← WiFi ← ModbusTCPGatewayTask ←(modbus_gw_rsp_queue)──┐
                                                                    ▲
Modbus RTU 从机 ← RS485 ← ModbusTask ←(modbus_gw_req_queue)──────────┘
        │
ButtonTask ──(cmd_queue)──► ModbusTCPGatewayTask
```

## 依赖

- PubSubClient — MQTT 客户端
- ESP Async WebServer — Web 服务器
- Async TCP — ESP Async WebServer 依赖
- Preferences — NVS 存储

## 文档

- 设计文档（基础）: `docs/superpowers/specs/2026-09-17-wifi-rtu-design.md`
- 设计文档（Gateway）: `docs/superpowers/specs/2026-09-18-modbus-tcp-gateway-design.md`
- 实现计划（Gateway）: `docs/superpowers/plans/2026-09-18-modbus-tcp-gateway-plan.md`
