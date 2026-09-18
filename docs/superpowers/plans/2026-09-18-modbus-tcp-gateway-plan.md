# Modbus TCP Gateway — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Modbus TCP 到 Modbus RTU 的双向透明转换网关。

**Architecture:** ESP32C3 作为 Modbus TCP Server（端口 502）监听 TCP 连接，将 Modbus TCP 帧解析后转换为 RTU 发往 RS485 从机，响应转换回 TCP 返回客户端。Gateway 模式与 Upload 模式互斥。

**Tech Stack:** PlatformIO + Arduino + FreeRTOS + lwIP (TCP server)

**Spec:** `docs/superpowers/specs/2026-09-18-modbus-tcp-gateway-design.md`

---

## Global Constraints

- FreeRTOS 任务必须用 `vTaskDelay()`，不能用 Arduino `delay()`
- 任务函数不得返回（使用 `while(1)` 无限循环）
- 队列满时使用 `portMAX_DELAY` 等待
- RS485 半双务：DE 高电平发送，低电平接收
- Modbus CRC16 多项式：0xA001
- MBAP Transaction ID 原样返回

---

## Task 1: Config 层 — 添加 Gateway 配置项

**Files:**
- Modify: `src/Config.hpp:64-70` — `getRunMode()` 返回类型说明增加 `"gateway"`
- Modify: `src/Config.cpp` — 实现 `getGatewayEnable()`、`setGatewayEnable()`、`getGatewayPort()`、`setGatewayPort()`
- Modify: `src/Config.hpp:64-70` — 增加上述四个方法的声明
- Modify: `src/Config.cpp` — 构造函数中 `_prefs` 的 `begin()` 已存在，确认 NVS key `gateway_enable`（bool，默认 false）、`gateway_port`（int，默认 502）读写正常

**Interfaces:**
- Consumes: 无
- Produces: `Config::getGatewayEnable()` → `bool`, `Config::getGatewayPort()` → `int`

**Steps:**

- [ ] **Step 1: 修改 `src/Config.hpp`**

在 `getRunMode()` 注释后、`isConfigured()` 前插入：

```cpp
    // Gateway
    bool getGatewayEnable();
    void setGatewayEnable(bool val);
    int getGatewayPort();
    void setGatewayPort(int val);
```

- [ ] **Step 2: 修改 `src/Config.cpp`**

在文件末尾（`isConfigured` 实现前）插入：

```cpp
bool Config::getGatewayEnable() {
    return _prefs.getBool("gateway_enable", false);
}
void Config::setGatewayEnable(bool val) {
    _prefs.putBool("gateway_enable", val);
}
int Config::getGatewayPort() {
    return _prefs.getInt("gateway_port", 502);
}
void Config::setGatewayPort(int val) {
    _prefs.putInt("gateway_port", val);
}
```

- [ ] **Step 3: 编译验证**

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: SUCCESS，无新增错误

- [ ] **Step 4: 提交**

```bash
git add src/Config.hpp src/Config.cpp
git commit -m "feat: add gateway_enable and gateway_port to Config NVS"
```

---

## Task 2: Queue 层 — 添加 Gateway 队列

**Files:**
- Modify: `src/Queue.h:29-37` — 追加 `modbus_gw_req_queue`、`modbus_gw_rsp_queue` 定义及 `GwrData` 结构体

**Interfaces:**
- Consumes: 无
- Produces: `modbus_gw_req_queue`（`QueueHandle_t`）、`modbus_gw_rsp_queue`（`QueueHandle_t`）

**Steps:**

- [ ] **Step 1: 修改 `src/Queue.h`**

在 `extern QueueHandle_t cmd_queue;` 后、`#define MODBUS_QUEUE_LEN` 前插入：

```cpp
// Gateway 请求（TCP→RTU）：ModbusTCPGatewayTask → ModbusTask
struct GwrRequest {
    uint8_t slave_addr;      // 从机地址（来自 TCP UnitID）
    uint8_t pdu[256];        // Modbus PDU（不含 CRC）
    uint8_t pdu_len;         // PDU 长度
    uint16_t transaction_id; // TCP Transaction ID（原样返回）
};

extern QueueHandle_t modbus_gw_req_queue;
extern QueueHandle_t modbus_gw_rsp_queue;
```

在 `modbus_data_queue` 声明后插入：

```cpp
// Gateway 响应（RTU→TCP）：ModbusTask → ModbusTCPGatewayTask
struct GwrResponse {
    uint8_t pdu[256];        // Modbus PDU 响应（含功能码+数据，不含 CRC）
    uint8_t pdu_len;         // PDU 长度
    uint16_t transaction_id; // TCP Transaction ID
    bool error;              // true=超时/错误，响应中已含异常码
};
```

- [ ] **Step 2: 修改 `src/main.cpp`**

在 `modbus_data_queue = xQueueCreate(...)` 后、`cmd_queue = xQueueCreate(...)` 前插入：

```cpp
modbus_gw_req_queue = xQueueCreate(4, sizeof(GwrRequest));
modbus_gw_rsp_queue = xQueueCreate(4, sizeof(GwrResponse));
```

- [ ] **Step 3: 编译验证**

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | tail -5
```

- [ ] **Step 4: 提交**

```bash
git add src/Queue.h src/main.cpp
git commit -m "feat: add gateway queues and GwrRequest/GwrResponse structs"
```

---

## Task 3: ModbusTask — 增加 Gateway 模式支持

**Files:**
- Modify: `src/Modbus.cpp:37-117` — 重构为同时监听 `cmd_queue`（Upload）和 `modbus_gw_req_queue`（Gateway）

**Interfaces:**
- Consumes: `cmd_queue`（Upload 请求）、`modbus_gw_req_queue`（Gateway 请求）
- Produces: `modbus_data_queue`（Upload 响应）、`modbus_gw_rsp_queue`（Gateway 响应）

**Steps:**

- [ ] **Step 1: 添加 extern 声明**

在 `src/Modbus.cpp` 顶部 `extern QueueHandle_t cmd_queue;` 后添加：

```cpp
extern QueueHandle_t modbus_gw_req_queue;
extern QueueHandle_t modbus_gw_rsp_queue;
```

- [ ] **Step 2: 重构 ModbusTask 主循环**

将 `while(true)` 内部替换为双队列监听：

```cpp
while (true) {
    // 等待任一队列有数据：cmd_queue（Upload）或 modbus_gw_req_queue（Gateway）
    QueueHandle_t queues[2] = { cmd_queue, modbus_gw_req_queue };
    QueueSetMemberHandle_t active = xQueueSelectFromSet(queues, 2, portMAX_DELAY);

    if (active == modbus_gw_req_queue) {
        // === Gateway 模式：处理 TCP→RTU 转换 ===
        GwrRequest req;
        if (xQueueReceive(modbus_gw_req_queue, &req, 0) != pdTRUE) continue;

        GwrResponse rsp = {};
        rsp.transaction_id = req.transaction_id;
        rsp.error = false;

        // 构造完整 RTU 帧：[slave_addr][pdu...][crc]
        uint8_t rtu_buf[256];
        rtu_buf[0] = req.slave_addr;
        memcpy(rtu_buf + 1, req.pdu, req.pdu_len);
        uint16_t crc = crc16(rtu_buf, 1 + req.pdu_len);
        rtu_buf[1 + req.pdu_len] = crc & 0xFF;
        rtu_buf[2 + req.pdu_len] = (crc >> 8) & 0xFF;
        size_t rtu_len = 3 + req.pdu_len;

        // 发送 RTU 请求
        uart_flush_input(MODBUS_UART);
        rs485_set_tx();
        uart_write_bytes(MODBUS_UART, (const char*)rtu_buf, rtu_len);
        uart_wait_tx_done(MODBUS_UART, 100);
        rs485_set_rx();

        // 等待 RTU 响应
        int len = uart_read_bytes(MODBUS_UART, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(MODBUS_TIMEOUT_MS));

        if (len > 5) {
            uint16_t resp_crc = crc16(rx_buf, len - 2);
            uint16_t got_crc = rx_buf[len - 1] | (rx_buf[len - 2] << 8);
            if (resp_crc == got_crc) {
                rsp.pdu_len = len - 2 - 3; // 去掉 slave + crc 后为 PDU
                memcpy(rsp.pdu, rx_buf + 1, rsp.pdu_len); // 去掉 slave addr
                rsp.pdu_len = len - 3; // 去掉 slave + CRC
            } else {
                rsp.error = true;
                rsp.pdu[0] = req.pdu[0] | 0x80; // 功能码|0x80
                rsp.pdu[1] = 0x04; // 异常码：从机设备忙
                rsp.pdu_len = 2;
            }
        } else {
            rsp.error = true;
            rsp.pdu[0] = req.pdu[0] | 0x80;
            rsp.pdu[1] = 0x04;
            rsp.pdu_len = 2;
        }

        xQueueSend(modbus_gw_rsp_queue, &rsp, portMAX_DELAY);

    } else if (active == cmd_queue) {
        // === Upload 模式：现有逻辑 ===
        Cmd cmd;
        if (xQueueReceive(cmd_queue, &cmd, 0) != pdTRUE) continue;
        if (cmd != Cmd::CMD_READ_MODBUS) continue;
        // [现有 Upload 逻辑保持不变，含 result.valid 判断和 xQueueSend(modbus_data_queue)]
        // ...（复制现有代码不变）...
    }
}
```

> **注意：** 现有 Upload 逻辑完整保留，插入到 `else if (active == cmd_queue)` 分支内。

- [ ] **Step 2: 编译验证**

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: SUCCESS

- [ ] **Step 3: 提交**

```bash
git add src/Modbus.cpp
git commit -m "feat: ModbusTask supports dual-queue gateway mode"
```

---

## Task 4: ModbusTCPGateway — 新建 TCP Server

**Files:**
- Create: `src/ModbusTCPGateway.hpp`
- Create: `src/ModbusTCPGateway.cpp`

**Interfaces:**
- Consumes: `modbus_gw_rsp_queue`
- Produces: `modbus_gw_req_queue`

**Steps:**

- [ ] **Step 1: 创建 `src/ModbusTCPGateway.hpp`**

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void ModbusTCPGatewayTask(void* param);
```

- [ ] **Step 2: 创建 `src/ModbusTCPGateway.cpp`**

```cpp
#include "ModbusTCPGateway.hpp"
#include "Queue.h"
#include "Config.hpp"
#include <WiFi.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

static constexpr uint32_t GW_TIMEOUT_MS = 2000;

static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

void ModbusTCPGatewayTask(void* param) {
    Config cfg;
    cfg.begin();

    int listen_port = cfg.getGatewayPort();
    if (listen_port <= 0) listen_port = 502;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listen_port);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    Serial.print("ModbusTCP Gateway listening on port ");
    Serial.println(listen_port);

    uint8_t rx_buf[512];
    uint8_t tx_buf[512];

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        Serial.println("ModbusTCP client connected");

        bool connected = true;
        while (connected) {
            // 接收 TCP 数据（读取完整 MBAP 帧）
            int n = recv(client_fd, rx_buf, sizeof(rx_buf), 0);
            if (n <= 0) { connected = false; break; }

            // 至少需要 7 字节 MBAP 头
            if (n < 7) continue;

            // 解析 MBAP 头
            uint16_t tid = (rx_buf[0] << 8) | rx_buf[1];
            uint16_t proto = (rx_buf[2] << 8) | rx_buf[3];
            uint16_t length = (rx_buf[4] << 8) | rx_buf[5];
            uint8_t unit_id = rx_buf[6];

            // 校验 Protocol ID（必须是 0）
            if (proto != 0) continue;
            if (n < 7 + length) continue; // 数据不完整

            // 提取 PDU（功能码+数据）
            uint8_t pdu_len = length - 1;
            uint8_t pdu[256];
            memcpy(pdu, rx_buf + 7, pdu_len);

            uint8_t func_code = pdu[0];

            // 支持的功能码：0x03, 0x04, 0x06
            if (func_code != 0x03 && func_code != 0x04 && func_code != 0x06) {
                // 异常：非法功能码
                tx_buf[0] = rx_buf[0]; tx_buf[1] = rx_buf[1]; // Transaction ID
                tx_buf[2] = rx_buf[2]; tx_buf[3] = rx_buf[3]; // Protocol ID
                tx_buf[4] = 0x00; tx_buf[5] = 0x03; // Length = 3
                tx_buf[6] = unit_id;
                tx_buf[7] = func_code | 0x80;
                tx_buf[8] = 0x02; // 异常码：非法功能
                send(client_fd, tx_buf, 9, 0);
                continue;
            }

            // 构造 Gateway 请求
            GwrRequest req = {};
            req.slave_addr = unit_id;
            req.transaction_id = tid;
            memcpy(req.pdu, pdu, pdu_len);
            req.pdu_len = pdu_len;

            // 发送到 RTU 队列
            xQueueSend(modbus_gw_req_queue, &req, portMAX_DELAY);

            // 等待 RTU 响应
            GwrResponse rsp = {};
            BaseType_t rv = xQueueReceive(modbus_gw_rsp_queue, &rsp, pdMS_TO_TICKS(GW_TIMEOUT_MS));

            // 构建 TCP 响应 MBAP
            tx_buf[0] = (rsp.transaction_id >> 8) & 0xFF;
            tx_buf[1] = rsp.transaction_id & 0xFF;
            tx_buf[2] = 0x00;
            tx_buf[3] = 0x00;
            tx_buf[6] = unit_id;

            if (rv != pdTRUE || rsp.error) {
                // 超时或错误
                tx_buf[4] = 0x00;
                tx_buf[5] = 0x03;
                tx_buf[7] = func_code | 0x80;
                tx_buf[8] = 0x04; // 从机设备忙
                send(client_fd, tx_buf, 9, 0);
            } else {
                // 成功：拷贝 PDU 响应
                uint16_t tcp_len = 1 + rsp.pdu_len; // UnitID + PDU
                tx_buf[4] = (tcp_len >> 8) & 0xFF;
                tx_buf[5] = tcp_len & 0xFF;
                memcpy(tx_buf + 7, rsp.pdu, rsp.pdu_len);
                send(client_fd, tx_buf, 7 + rsp.pdu_len, 0);
            }
        }

        close(client_fd);
        Serial.println("ModbusTCP client disconnected");
    }
}
```

- [ ] **Step 3: 编译验证**

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: SUCCESS

- [ ] **Step 4: 提交**

```bash
git add src/ModbusTCPGateway.hpp src/ModbusTCPGateway.cpp
git commit -m "feat: add ModbusTCPGateway task (TCP port 502)"
```

---

## Task 5: WebConfig — 增加 Gateway 配置 UI

**Files:**
- Modify: `src/WebConfig.cpp` — HTML 表单增加 Gateway 选项，build_response 增加 gateway 占位符替换，save_params 增加 gateway_enable / gateway_port 保存

**Steps:**

- [ ] **Step 1: 修改 HTML_FORM**

在 Mode 区段，将 run_mode select 替换为：

```html
<div class='row'><label>Run Mode:</label>
<select name='run_mode'>
  <option value='push' {mode_push}>Push (MQTT/HTTP)</option>
  <option value='pull'  {mode_pull}>Pull (MQTT/HTTP)</option>
  <option value='gateway' {mode_gateway}>Gateway (TCP→RTU)</option>
</select></div>
<div class='row'><label>Gateway Port:</label><input name='gateway_port' type='number' value='{gateway_port}'></div>
```

- [ ] **Step 2: 修改 build_response()**

在 html.replace 调用末尾添加：

```cpp
html.replace("{mode_gateway}", cfg.getRunMode() == "gateway" ? "selected" : "");
html.replace("{gateway_port}", String(cfg.getGatewayPort()));
```

- [ ] **Step 3: 修改 save_params()**

在函数末尾（`push_interval` 后）添加：

```cpp
if (request->hasParam("gateway_port")) {
    cfg.setGatewayPort(request->getParam("gateway_port")->value().toInt());
}
```

- [ ] **Step 4: 编译验证**

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: SUCCESS

- [ ] **Step 5: 提交**

```bash
git add src/WebConfig.cpp
git commit -m "feat: add gateway mode to web config form"
```

---

## Task 6: main.cpp — Gateway 模式启动逻辑

**Files:**
- Modify: `src/main.cpp:29-93` — 在 setup() 中增加 `run_mode == "gateway"` 分支

**Steps:**

- [ ] **Step 1: 添加 include**

在 `#include "StatusLED.hpp"` 后添加：

```cpp
#include "ModbusTCPGateway.hpp"
```

- [ ] **Step 2: 修改 setup() 分支逻辑**

将 `setup()` 中的 if-else 结构调整为：

```cpp
String runMode = cfg.getRunMode();

if (runMode == "gateway") {
    // Gateway 模式：WiFi STA + ModbusTCPGateway + ModbusTask + ButtonTask
    Serial.println("Starting Gateway mode...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.getWifiSsid().c_str(), cfg.getWifiPass().c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed. Falling back to config mode...");
        xTaskCreatePinnedToCore(WebConfigTask, "WebConfig", 8192, NULL, 1, &s_webconfig_task_h, 0);
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());

    xTaskCreatePinnedToCore(ModbusTCPGatewayTask, "MBGateway", 8192, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(ModbusTask, "Modbus", 4096, NULL, 3, &s_modbus_task_h, 0);
    xTaskCreatePinnedToCore(ButtonTask, "Button", 2048, NULL, 4, &s_button_task_h, 0);

    while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }

} else if (!isConfigured) {
    // Config 模式（无变化）
    ...

} else {
    // Upload 模式（push/pull）（现有逻辑不变）
    ...
}
```

- [ ] **Step 3: 编译验证**

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | tail -10
```

- [ ] **Step 4: 提交**

```bash
git add src/main.cpp
git commit -m "feat: add gateway mode startup in main.cpp"
```

---

## Task 7: 端到端编译验证

执行完整编译，确认所有任务文件正确链接：

```
/home/wlmwwx/.platformio/penv/bin/pio run 2>&1 | grep -E "(SUCCESS|ERROR|error:|warning:)"
```

预期：SUCCESS，无 error

---

## 文件变更汇总

| Task | 新建 | 修改 |
|------|------|------|
| 1 | — | `Config.hpp`, `Config.cpp` |
| 2 | — | `Queue.h`, `main.cpp` |
| 3 | — | `Modbus.cpp` |
| 4 | `ModbusTCPGateway.hpp`, `ModbusTCPGateway.cpp` | — |
| 5 | — | `WebConfig.cpp` |
| 6 | — | `main.cpp` |
