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
                uart_wait_tx_done(MODBUS_UART, 100);
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
