#include "Config.hpp"

Config::Config() {}

bool Config::begin() {
    return _prefs.begin(NAMESPACE, false);
}

void Config::reset() {
    _prefs.clear();
}

bool Config::isConfigured() {
    return _prefs.getString("wifi_ssid").length() > 0;
}

String Config::getWifiSsid() { return _prefs.getString("wifi_ssid"); }
void Config::setWifiSsid(const String& val) { _prefs.putString("wifi_ssid", val); }

String Config::getWifiPass() { return _prefs.getString("wifi_pass"); }
void Config::setWifiPass(const String& val) { _prefs.putString("wifi_pass", val); }

String Config::getMqttBroker() { return _prefs.getString("mqtt_broker", ""); }
void Config::setMqttBroker(const String& val) { _prefs.putString("mqtt_broker", val); }

int Config::getMqttPort() { return _prefs.getInt("mqtt_port", 1883); }
void Config::setMqttPort(int val) { _prefs.putInt("mqtt_port", val); }

String Config::getMqttUser() { return _prefs.getString("mqtt_user", ""); }
void Config::setMqttUser(const String& val) { _prefs.putString("mqtt_user", val); }

String Config::getMqttPass() { return _prefs.getString("mqtt_pass", ""); }
void Config::setMqttPass(const String& val) { _prefs.putString("mqtt_pass", val); }

String Config::getMqttTopic() { return _prefs.getString("mqtt_topic", ""); }
void Config::setMqttTopic(const String& val) { _prefs.putString("mqtt_topic", val); }

bool Config::getMqttTls() { return _prefs.getBool("mqtt_tls", false); }
void Config::setMqttTls(bool val) { _prefs.putBool("mqtt_tls", val); }

int Config::getMqttQos() { return _prefs.getInt("mqtt_qos", 0); }
void Config::setMqttQos(int val) { _prefs.putInt("mqtt_qos", val); }

String Config::getMqttLwtTopic() { return _prefs.getString("mqtt_lwt_topic", ""); }
void Config::setMqttLwtTopic(const String& val) { _prefs.putString("mqtt_lwt_topic", val); }

String Config::getMqttLwtMsg() { return _prefs.getString("mqtt_lwt_msg", "offline"); }
void Config::setMqttLwtMsg(const String& val) { _prefs.putString("mqtt_lwt_msg", val); }

bool Config::getMqttRetain() { return _prefs.getBool("mqtt_retain", false); }
void Config::setMqttRetain(bool val) { _prefs.putBool("mqtt_retain", val); }

String Config::getHttpUrl() { return _prefs.getString("http_url", ""); }
void Config::setHttpUrl(const String& val) { _prefs.putString("http_url", val); }

String Config::getTcpIp() { return _prefs.getString("tcp_ip", ""); }
void Config::setTcpIp(const String& val) { _prefs.putString("tcp_ip", val); }

int Config::getTcpPort() { return _prefs.getInt("tcp_port", 8888); }
void Config::setTcpPort(int val) { _prefs.putInt("tcp_port", val); }

int Config::getModbusBaud() { return _prefs.getInt("modbus_baud", 9600); }
void Config::setModbusBaud(int val) { _prefs.putInt("modbus_baud", val); }

int Config::getModbusBits() { return _prefs.getInt("modbus_bits", 8); }
void Config::setModbusBits(int val) { _prefs.putInt("modbus_bits", val); }

int Config::getModbusParity() { return _prefs.getInt("modbus_parity", 0); }
void Config::setModbusParity(int val) { _prefs.putInt("modbus_parity", val); }

int Config::getModbusStop() { return _prefs.getInt("modbus_stop", 1); }
void Config::setModbusStop(int val) { _prefs.putInt("modbus_stop", val); }

int Config::getModbusSlave() { return _prefs.getInt("modbus_slave", 1); }
void Config::setModbusSlave(int val) { _prefs.putInt("modbus_slave", val); }

int Config::getModbusReg() { return _prefs.getInt("modbus_reg", 0); }
void Config::setModbusReg(int val) { _prefs.putInt("modbus_reg", val); }

int Config::getModbusCount() { return _prefs.getInt("modbus_count", 10); }
void Config::setModbusCount(int val) { _prefs.putInt("modbus_count", val); }

String Config::getProtocolSelect() { return _prefs.getString("protocol_select", "mqtt"); }
void Config::setProtocolSelect(const String& val) { _prefs.putString("protocol_select", val); }

String Config::getRunMode() { return _prefs.getString("run_mode", "push"); }
void Config::setRunMode(const String& val) { _prefs.putString("run_mode", val); }

int Config::getPushInterval() { return _prefs.getInt("push_interval", 30); }
void Config::setPushInterval(int val) { _prefs.putInt("push_interval", val); }
