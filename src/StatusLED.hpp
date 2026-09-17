#pragma once

enum class LedMode { OFF, WIFI_CONNECTING, MODBUS_BUSY, ERROR, OK };

class StatusLED {
public:
    static void begin();
    static void set(LedMode mode);
    static void tick();  // Call periodically to update LED state
};
