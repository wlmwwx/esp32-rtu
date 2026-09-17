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
