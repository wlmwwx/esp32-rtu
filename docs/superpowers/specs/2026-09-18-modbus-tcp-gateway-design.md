# Modbus TCP Gateway — Design Specification

**Date:** 2026-09-18
**Project:** ESP32C3 WiFi Modbus RTU Gateway
**Parent:** 2026-09-17-wifi-rtu-design.md
**Framework:** PlatformIO + Arduino + FreeRTOS

---

## 1. Overview

新增 **Modbus TCP Gateway** 模式：ESP32C3 作为 Modbus TCP Server，将来自网络的 Modbus TCP 请求透明转换为 Modbus RTU 发送给 RS485 从机，并将 RTU 响应转换回 Modbus TCP 返回给 TCP 客户端。

Gateway 模式与 Upload 模式（MQTT/HTTP）互斥，Gateway 启用时仅做协议转换，不上传云端。

**数据流：**
```
Modbus TCP Client (任意主站工具)
    ↓ WiFi STA
ESP32C3: ModbusTCPGateway (端口 502)
    ↓ UART + DE 控制
RS485 总线
    ↓
Modbus RTU 从机设备
```

---

## 2. 工作模式

| 模式 | 说明 |
|------|------|
| **Config** | AP Web 门户，配置参数 |
| **Upload** | WiFi RTU 网关，MQTT/HTTP/TCP 上传云端（现有功能） |
| **Gateway** | Modbus TCP ↔ Modbus RTU 双向转换，不上传云端 |

三种模式通过 Web 配置页面选择，`run_mode` 字段新增 `gateway` 选项：
- `push` / `pull` → Upload 模式
- `gateway` → Gateway 模式

---

## 3. 配置项（NVS 扩展）

**Namespace:** `rtu_cfg`（新增）

| Key | Type | Description |
|-----|------|-------------|
| `gateway_enable` | bool | Gateway 模式使能（与 Upload 互斥）|
| `gateway_port` | int | TCP 监听端口（默认 502）|

---

## 4. FreeRTOS 任务架构（Gateway 模式）

### 4.1 任务列表

| Task | Priority | Stack | Core | 职责 |
|------|----------|-------|------|------|
| `ModbusTCPGatewayTask` | 3 (HIGH) | 8KB | Core 0 | 接收 TCP 请求，转换为 RTU，响应转回 TCP |
| `ModbusTask` | 3 (HIGH) | 4KB | Core 0 | RTU 通信（共享 RS485 硬件）|
| `ButtonTask` | 4 (HIGH) | 2KB | Core 0 | 物理按钮检测 |
| `WebConfigTask` | 1 (LOW) | 8KB | Core 0 | AP + Web 配置 |

> Upload 模式任务（NetworkTask、AppTask、push timer）在 Gateway 模式下不启动。

### 4.2 队列

| Queue | 生产者 | 消费者 | 说明 |
|-------|--------|--------|------|
| `modbus_gw_req_queue` | ModbusTCPGatewayTask | ModbusTask | TCP→RTU 请求 |
| `modbus_gw_rsp_queue` | ModbusTask | ModbusTCPGatewayTask | RTU→TCP 响应 |

队列长度均为 4，超时 2000ms（Modbus RTU 超时时间）。

---

## 5. Modbus TCP 协议处理

### 5.1 MBAP 头（7 字节）

```
[TransactionID:2][ProtocolID:2][Length:2][UnitID:1]
```

| 字段 | 长度 | 说明 |
|------|------|------|
| Transaction ID | 2 bytes | 客户端生成，服务器原样返回 |
| Protocol ID | 2 bytes | 固定 0x0000（Modbus）|
| Length | 2 bytes | 后续字节数（UnitID + PDU）|
| Unit ID | 1 byte | 相当于 RTU 从机地址 |

### 5.2 TCP→RTU 转换

1. 解析 MBAP 头，提取 Transaction ID 和 Unit ID
2. 校验 Protocol ID == 0x0000，Length 合理
3. 提取 PDU（功能码 + 数据）
4. 取 Unit ID 作为 RTU 从机地址
5. 追加 CRC16（0xA001）生成完整 RTU 帧
6. 通过 `modbus_gw_req_queue` 发送给 ModbusTask

### 5.3 RTU→TCP 转换

1. ModbusTask 从 `modbus_gw_req_queue` 接收请求
2. 发送 RTU 帧到 RS485 总线，等待响应（含 CRC）
3. 收到响应后，去掉末尾 2 字节 CRC
4. 构建 MBAP 头：Transaction ID 拷贝，Protocol ID=0x0000，Length=PDU长度+1
5. 通过 `modbus_gw_rsp_queue` 返回给 ModbusTCPGatewayTask
6. ModbusTCPGatewayTask 发送 TCP 响应给客户端

### 5.4 超时处理

- ModbusTask 发送 RTU 后等待响应，超时 2000ms
- 超时则向 `modbus_gw_rsp_queue` 发送错误响应
- ModbusTCPGatewayTask 构建 Modbus TCP 异常帧（功能码 | 0x80 + 异常码）发回 TCP 客户端

### 5.5 支持的功能码

| 功能码 | 名称 | 说明 |
|--------|------|------|
| 0x03 | Read Holding Registers | 读保持寄存器 |
| 0x04 | Read Input Registers | 读输入寄存器 |
| 0x06 | Write Single Register | 写单个寄存器 |

> 其他功能码返回异常码 0x02（非法功能码）。

---

## 6. ModbusTCPGatewayTask 行为

### 6.1 TCP 连接管理

- 监听端口 502（可配置）
- 单客户端模式：同时只允许一个 TCP 连接
- 新连接到达时，若已有客户端连接，先断开旧连接
- 连接断开后继续监听，不退出任务

### 6.2 响应 Transaction ID

每个 TCP 请求携带 Transaction ID，响应时必须原样返回，以便客户端匹配请求/响应。

### 6.3 主循环伪代码

```
1. accept() 等待 TCP 连接
2. recv() 接收 TCP 数据（可能跨多个包，需缓冲）
3. 解析完整 MBAP 帧（7字节头 + Length 指示的正文）
4. 提取 PDU + Transaction ID + Unit ID
5. 构造 RTU 请求帧（UnitID + PDU + CRC）
6. 发送到 modbus_gw_req_queue
7. 从 modbus_gw_rsp_queue 接收 RTU 响应（或超时错误）
8. 构造 TCP 响应（MBAP 头 + PDU + 数据）
9. send() 发回 TCP 客户端
10. 回到步骤 2
```

---

## 7. ModbusTask 改造

ModbusTask 增加 gateway 模式支持：

- 监听 `modbus_gw_req_queue`（Gateway 请求）和 `cmd_queue`（Upload 请求）
- 优先处理 Gateway 请求（队列优先级）
- 两种请求共用同一 RS485 硬件和 DE 控制逻辑
- Gateway 请求响应发送到 `modbus_gw_rsp_queue`
- Upload 请求响应发送到 `modbus_data_queue`（现有队列）

---

## 8. Web 配置页面改造

在现有配置表单的 **Mode** 区段增加：

```html
<div class='row'>
  <label>Run Mode:</label>
  <select name='run_mode'>
    <option value='push' {mode_push}>Push (MQTT/HTTP/TCP)</option>
    <option value='pull'  {mode_pull}>Pull (MQTT/HTTP/TCP)</option>
    <option value='gateway' {mode_gateway}>Gateway (TCP→RTU)</option>
  </select>
</div>
<div class='row'>
  <label>Gateway Port:</label>
  <input name='gateway_port' type='number' value='{gateway_port}'>
</div>
```

保存时 `run_mode == "gateway"` → 启用 Gateway 模式，启动对应任务。

---

## 9. 启动逻辑（main.cpp）

```
setup():
  load config from NVS
  create queues

  if run_mode == "gateway":
    WiFi connects to AP (STA mode)
    if WiFi not connected: fallback to Config mode
    create ModbusTCPGatewayTask
    create ModbusTask
    create ButtonTask
    while(1) { vTaskDelay() }

  elif run_mode == "push" or "pull":
    现有 Upload 逻辑不变

  else:
    Config mode (WebConfigTask)
```

---

## 10. 文件变更

| 操作 | 文件 |
|------|------|
| 新建 | `src/ModbusTCPGateway.cpp` / `src/ModbusTCPGateway.hpp` |
| 修改 | `src/Modbus.cpp` / `src/Modbus.hpp` — 增加 gateway 队列监听 |
| 修改 | `src/Config.cpp` / `src/Config.hpp` — 增加 `gateway_enable`、`gateway_port` |
| 修改 | `src/WebConfig.cpp` — 增加 gateway 配置 UI |
| 修改 | `src/main.cpp` — Gateway 模式任务启动逻辑 |

---

## 11. 错误处理

| 场景 | 处理 |
|------|------|
| Modbus RTU 超时（2000ms）| 构建异常响应：功能码\|0x80，异常码 0x04（从机设备忙）|
| TCP 客户端断开 | 继续监听新连接 |
| WiFi 断开 | LED 慢闪，等待重连 |
| RS485 总线冲突 | DE 控制保证半双工，不主动处理 |
| Modbus CRC 错误 | 丢弃帧，请求重发（等待下次队列请求）|
