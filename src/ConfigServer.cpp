#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "Config.hpp"

// Forward declaration — defined in Config.hpp
// Config class has all the get/set methods we need.

static const char HTML_FORM[] = R"(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 RTU Config</title>
<style>
body{font-family:Arial;max-width:600px;margin:0 auto;padding:20px}
input,select{width:100%;padding:8px;margin:5px 0}
h2{color:#333;border-bottom:1px solid #ccc;padding-bottom:5px}
button{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;width:100%}
button:hover{background:#45a049}
input[type=submit]{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;width:100%}
</style>
</head>
<body>
<h1>ESP32 RTU Configuration</h1>
<form method='POST' action='/save'>
<h2>WiFi</h2>
<input name='wifi_ssid' placeholder='SSID' value='{wifi_ssid}'>
<input name='wifi_pass' type='password' placeholder='Password' value='{wifi_pass}'>

<h2>MQTT</h2>
<input name='mqtt_broker' placeholder='Broker' value='{mqtt_broker}'>
<input name='mqtt_port' type='number' placeholder='Port' value='{mqtt_port}'>
<input name='mqtt_user' placeholder='Username' value='{mqtt_user}'>
<input name='mqtt_pass' type='password' placeholder='Password' value='{mqtt_pass}'>
<input name='mqtt_topic' placeholder='Topic' value='{mqtt_topic}'>
<div><label><input name='mqtt_tls' type='checkbox' {mqtt_tls}> TLS/SSL</label></div>
<div>QoS: <select name='mqtt_qos'><option value='0' {qos0}>QoS 0</option><option value='1' {qos1}>QoS 1</option></select></div>
<input name='mqtt_lwt_topic' placeholder='LWT Topic' value='{mqtt_lwt_topic}'>
<input name='mqtt_lwt_msg' placeholder='LWT Message' value='{mqtt_lwt_msg}'>
<div><label><input name='mqtt_retain' type='checkbox' {mqtt_retain}> Retain</label></div>

<h2>HTTP</h2>
<input name='http_url' placeholder='http://server/path' value='{http_url}'>

<h2>TCP</h2>
<input name='tcp_ip' placeholder='Server IP' value='{tcp_ip}'>
<input name='tcp_port' type='number' placeholder='Port' value='{tcp_port}'>

<h2>Modbus</h2>
<input name='modbus_baud' type='number' placeholder='Baud Rate' value='{modbus_baud}'>
<div>Data Bits: <select name='modbus_bits'><option value='8' {bits8}>8</option><option value='7' {bits7}>7</option></select></div>
<div>Parity: <select name='modbus_parity'><option value='0' {par0}>None</option><option value='1' {par1}>Odd</option><option value='2' {par2}>Even</option></select></div>
<input name='modbus_stop' type='number' placeholder='Stop Bits' value='{modbus_stop}'>
<input name='modbus_slave' type='number' placeholder='Slave Address' value='{modbus_slave}'>
<input name='modbus_reg' type='number' placeholder='Start Register' value='{modbus_reg}'>
<input name='modbus_count' type='number' placeholder='Register Count' value='{modbus_count}'>

<h2>Mode</h2>
<div>Protocol: <select name='protocol_select'>
<option value='mqtt' {proto_mqtt}>MQTT</option>
<option value='http' {proto_http}>HTTP</option>
<option value='tcp' {proto_tcp}>TCP</option>
</select></div>
<div>Run Mode: <select name='run_mode'>
<option value='push' {mode_push}>Push</option>
<option value='pull' {mode_pull}>Pull</option>
</select></div>
<input name='push_interval' type='number' placeholder='Push Interval (sec)' value='{push_interval}'>

<input type='submit' value='Save & Reboot'>
</form>
<form method='POST' action='/reset' style='margin-top:10px'>
<input type='submit' value='Factory Reset' style='background:#f44336'>
</form>
</body>
</html>
)";

static String buildForm(Config& cfg) {
    String html = HTML_FORM;
    html.replace("{wifi_ssid}", cfg.getWifiSsid());
    html.replace("{wifi_pass}", cfg.getWifiPass());
    html.replace("{mqtt_broker}", cfg.getMqttBroker());
    html.replace("{mqtt_port}", String(cfg.getMqttPort()));
    html.replace("{mqtt_user}", cfg.getMqttUser());
    html.replace("{mqtt_pass}", cfg.getMqttPass());
    html.replace("{mqtt_topic}", cfg.getMqttTopic());
    html.replace("{mqtt_tls}", cfg.getMqttTls() ? "checked" : "");
    html.replace("{qos0}", cfg.getMqttQos() == 0 ? "selected" : "");
    html.replace("{qos1}", cfg.getMqttQos() == 1 ? "selected" : "");
    html.replace("{mqtt_lwt_topic}", cfg.getMqttLwtTopic());
    html.replace("{mqtt_lwt_msg}", cfg.getMqttLwtMsg());
    html.replace("{mqtt_retain}", cfg.getMqttRetain() ? "checked" : "");
    html.replace("{http_url}", cfg.getHttpUrl());
    html.replace("{tcp_ip}", cfg.getTcpIp());
    html.replace("{tcp_port}", String(cfg.getTcpPort()));
    html.replace("{modbus_baud}", String(cfg.getModbusBaud()));
    html.replace("{bits8}", cfg.getModbusBits() == 8 ? "selected" : "");
    html.replace("{bits7}", cfg.getModbusBits() == 7 ? "selected" : "");
    html.replace("{par0}", cfg.getModbusParity() == 0 ? "selected" : "");
    html.replace("{par1}", cfg.getModbusParity() == 1 ? "selected" : "");
    html.replace("{par2}", cfg.getModbusParity() == 2 ? "selected" : "");
    html.replace("{modbus_stop}", String(cfg.getModbusStop()));
    html.replace("{modbus_slave}", String(cfg.getModbusSlave()));
    html.replace("{modbus_reg}", String(cfg.getModbusReg()));
    html.replace("{modbus_count}", String(cfg.getModbusCount()));
    html.replace("{proto_mqtt}", cfg.getProtocolSelect() == "mqtt" ? "selected" : "");
    html.replace("{proto_http}", cfg.getProtocolSelect() == "http" ? "selected" : "");
    html.replace("{proto_tcp}", cfg.getProtocolSelect() == "tcp" ? "selected" : "");
    html.replace("{mode_push}", cfg.getRunMode() == "push" ? "selected" : "");
    html.replace("{mode_pull}", cfg.getRunMode() == "pull" ? "selected" : "");
    html.replace("{push_interval}", String(cfg.getPushInterval()));
    return html;
}

void startConfigServer() {
    Config cfg;
    cfg.begin();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-RTU-Config");

    AsyncWebServer server(80);

    server.on("/", HTTP_GET, [&cfg](AsyncWebServerRequest* request) {
        request->send(200, "text/html", buildForm(cfg));
    });

    server.on("/save", HTTP_POST, [&cfg](AsyncWebServerRequest* request) {
        int params = request->params();
        for (int i = 0; i < params; i++) {
            const AsyncWebParameter* p = request->getParam(i);
            String name = p->name();
            String value = p->value();
            if (name == "wifi_ssid") cfg.setWifiSsid(value);
            else if (name == "wifi_pass") cfg.setWifiPass(value);
            else if (name == "mqtt_broker") cfg.setMqttBroker(value);
            else if (name == "mqtt_port") cfg.setMqttPort(value.toInt());
            else if (name == "mqtt_user") cfg.setMqttUser(value);
            else if (name == "mqtt_pass") cfg.setMqttPass(value);
            else if (name == "mqtt_topic") cfg.setMqttTopic(value);
            else if (name == "mqtt_tls") cfg.setMqttTls(true);
            else if (name == "mqtt_qos") cfg.setMqttQos(value.toInt());
            else if (name == "mqtt_lwt_topic") cfg.setMqttLwtTopic(value);
            else if (name == "mqtt_lwt_msg") cfg.setMqttLwtMsg(value);
            else if (name == "mqtt_retain") cfg.setMqttRetain(true);
            else if (name == "http_url") cfg.setHttpUrl(value);
            else if (name == "tcp_ip") cfg.setTcpIp(value);
            else if (name == "tcp_port") cfg.setTcpPort(value.toInt());
            else if (name == "modbus_baud") cfg.setModbusBaud(value.toInt());
            else if (name == "modbus_bits") cfg.setModbusBits(value.toInt());
            else if (name == "modbus_parity") cfg.setModbusParity(value.toInt());
            else if (name == "modbus_stop") cfg.setModbusStop(value.toInt());
            else if (name == "modbus_slave") cfg.setModbusSlave(value.toInt());
            else if (name == "modbus_reg") cfg.setModbusReg(value.toInt());
            else if (name == "modbus_count") cfg.setModbusCount(value.toInt());
            else if (name == "protocol_select") cfg.setProtocolSelect(value);
            else if (name == "run_mode") cfg.setRunMode(value);
            else if (name == "push_interval") cfg.setPushInterval(value.toInt());
        }
        request->send(200, "text/plain", "Saved. Rebooting...");
        delay(500);
        ESP.restart();
    });

    server.on("/reset", HTTP_POST, [&cfg](AsyncWebServerRequest* request) {
        cfg.reset();
        request->send(200, "text/plain", "Reset. Rebooting...");
        delay(500);
        ESP.restart();
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"chip_id\":\"" + String((uint32_t)ESP.getEfuseMac(), HEX) + "\"";
        json += ",\"wifi_mode\":\"AP\"";
        json += ",\"free_heap\":" + String(ESP.getFreeHeap());
        json += "}";
        request->send(200, "application/json", json);
    });

    server.begin();
    Serial.println("AP started at 192.168.4.1");

    // Block forever — config mode runs without scheduler
    while (true) {
        delay(1000);
    }
}
