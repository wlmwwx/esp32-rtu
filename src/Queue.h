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

// Queue lengths
#define MODBUS_QUEUE_LEN   5
#define CMD_QUEUE_LEN      4