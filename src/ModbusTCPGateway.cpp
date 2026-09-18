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
