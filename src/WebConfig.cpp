#include "WebConfig.hpp"
#include "Config.hpp"
#include "Queue.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>

static AsyncWebServer* _server = nullptr;

extern QueueHandle_t cmd_queue;

static const char HTML_FORM[] = R"(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 RTU Config</title><style>
body{font-family:Arial;max-width:600px;margin:0 auto;padding:20px}
input,select{width:100%;padding:8px;margin:5px 0}
h2{color:#333;border-bottom:1px solid #ccc;padding-bottom:5px}
button{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;width:100%}
button:hover{background:#45a049}
.note{font-size:12px;color:#666}
</style></head>
<body>
<h1>ESP32 RTU Configuration</h1>
<form action='/save' method='POST'>

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
<div>QoS: <select name='mqtt_qos'>
<option value='0' {qos0}>QoS 0</option>
<option value='1' {qos1}>QoS 1</option>
</select></div>
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
<div>Baud:
<select name='modbus_bits'>
<option value='8' {bits8}>8</option>
<option value='7' {bits7}>7</option>
</select></div>
<div>Parity:
<select name='modbus_parity'>
<option value='0' {par0}>None</option>
<option value='1' {par1}>Odd</option>
<option value='2' {par2}>Even</option>
</select></div>
<input name='modbus_stop' type='number' placeholder='Stop Bits' value='{modbus_stop}'>
<input name='modbus_slave' type='number' placeholder='Slave Address' value='{modbus_slave}'>
<input name='modbus_reg' type='number' placeholder='Start Register' value='{modbus_reg}'>
<input name='modbus_count' type='number' placeholder='Register Count' value='{modbus_count}'>

<h2>Mode</h2>
<div>Protocol:
<select name='protocol_select'>
<option value='mqtt' {proto_mqtt}>MQTT</option>
<option value='http' {proto_http}>HTTP</option>
<option value='tcp' {proto_tcp}>TCP</option>
</select></div>
<div>Mode:
<select name='run_mode'>
<option value='push' {mode_push}>Push</option>
<option value='pull' {mode_pull}>Pull</option>
</select></div>
<input name='push_interval' type='number' placeholder='Push Interval (sec)' value='{push_interval}'>

<button type='submit'>Save & Reboot</button>
</form>
<form action='/reset' method='POST' style='margin-top:10px'>
<button type='submit' style='background:#f44336'>Factory Reset</button>
</form>
</body></html>
)";

static String build_response(Config& cfg) {
    String html = HTML_FORM;
    // WiFi
    html.replace("{wifi_ssid}", cfg.getWifiSsid());
    html.replace("{wifi_pass}", cfg.getWifiPass());
    // MQTT
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
    // HTTP
    html.replace("{http_url}", cfg.getHttpUrl());
    // TCP
    html.replace("{tcp_ip}", cfg.getTcpIp());
    html.replace("{tcp_port}", String(cfg.getTcpPort()));
    // Modbus
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
    // Mode
    html.replace("{proto_mqtt}", cfg.getProtocolSelect() == "mqtt" ? "selected" : "");
    html.replace("{proto_http}", cfg.getProtocolSelect() == "http" ? "selected" : "");
    html.replace("{proto_tcp}", cfg.getProtocolSelect() == "tcp" ? "selected" : "");
    html.replace("{mode_push}", cfg.getRunMode() == "push" ? "selected" : "");
    html.replace("{mode_pull}", cfg.getRunMode() == "pull" ? "selected" : "");
    html.replace("{push_interval}", String(cfg.getPushInterval()));
    return html;
}

static void save_params(AsyncWebServerRequest* request, Config& cfg) {
    // Extract and save all params
    // Note: HTML form uses lowercase field names (wifi_ssid, mqtt_broker, etc.)
    // but Config class uses capitalized setters (setWifiSsid, setMqttBroker, etc.)
    if (request->hasParam("wifi_ssid")) cfg.setWifiSsid(request->getParam("wifi_ssid")->value());
    if (request->hasParam("wifi_pass")) cfg.setWifiPass(request->getParam("wifi_pass")->value());
    if (request->hasParam("mqtt_broker")) cfg.setMqttBroker(request->getParam("mqtt_broker")->value());
    if (request->hasParam("mqtt_port")) cfg.setMqttPort(request->getParam("mqtt_port")->value().toInt());
    if (request->hasParam("mqtt_user")) cfg.setMqttUser(request->getParam("mqtt_user")->value());
    if (request->hasParam("mqtt_pass")) cfg.setMqttPass(request->getParam("mqtt_pass")->value());
    if (request->hasParam("mqtt_topic")) cfg.setMqttTopic(request->getParam("mqtt_topic")->value());
    cfg.setMqttTls(request->hasParam("mqtt_tls"));
    if (request->hasParam("mqtt_qos")) cfg.setMqttQos(request->getParam("mqtt_qos")->value().toInt());
    if (request->hasParam("mqtt_lwt_topic")) cfg.setMqttLwtTopic(request->getParam("mqtt_lwt_topic")->value());
    if (request->hasParam("mqtt_lwt_msg")) cfg.setMqttLwtMsg(request->getParam("mqtt_lwt_msg")->value());
    cfg.setMqttRetain(request->hasParam("mqtt_retain"));
    if (request->hasParam("http_url")) cfg.setHttpUrl(request->getParam("http_url")->value());
    if (request->hasParam("tcp_ip")) cfg.setTcpIp(request->getParam("tcp_ip")->value());
    if (request->hasParam("tcp_port")) cfg.setTcpPort(request->getParam("tcp_port")->value().toInt());
    if (request->hasParam("modbus_baud")) cfg.setModbusBaud(request->getParam("modbus_baud")->value().toInt());
    if (request->hasParam("modbus_bits")) cfg.setModbusBits(request->getParam("modbus_bits")->value().toInt());
    if (request->hasParam("modbus_parity")) cfg.setModbusParity(request->getParam("modbus_parity")->value().toInt());
    if (request->hasParam("modbus_stop")) cfg.setModbusStop(request->getParam("modbus_stop")->value().toInt());
    if (request->hasParam("modbus_slave")) cfg.setModbusSlave(request->getParam("modbus_slave")->value().toInt());
    if (request->hasParam("modbus_reg")) cfg.setModbusReg(request->getParam("modbus_reg")->value().toInt());
    if (request->hasParam("modbus_count")) cfg.setModbusCount(request->getParam("modbus_count")->value().toInt());
    if (request->hasParam("protocol_select")) cfg.setProtocolSelect(request->getParam("protocol_select")->value());
    if (request->hasParam("run_mode")) cfg.setRunMode(request->getParam("run_mode")->value());
    if (request->hasParam("push_interval")) cfg.setPushInterval(request->getParam("push_interval")->value().toInt());
}

void WebConfigTask(void* param) {
    Config cfg;
    cfg.begin();

    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-RTU-Config");

    _server = new AsyncWebServer(80);

    _server->on("/", HTTP_GET, [&cfg](AsyncWebServerRequest* r) {
        r->send(200, "text/html", build_response(cfg));
    });

    _server->on("/save", HTTP_POST, [&cfg](AsyncWebServerRequest* r) {
        save_params(r, cfg);
        Cmd cmd = Cmd::CMD_RESTART;
        xQueueSend(cmd_queue, &cmd, 0);
        r->send(200, "text/plain", "Saved. Rebooting...");
    });

    _server->on("/reset", HTTP_POST, [&cfg](AsyncWebServerRequest* r) {
        cfg.reset();
        Cmd cmd = Cmd::CMD_RESTART;
        xQueueSend(cmd_queue, &cmd, 0);
        r->send(200, "text/plain", "Reset. Rebooting...");
    });

    _server->on("/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        String json = "{";
        json += "\"chip_id\":\"" + String((uint32_t)ESP.getEfuseMac(), HEX) + "\"";
        json += ",\"wifi_mode\":\"AP\"";
        json += ",\"free_heap\":" + String(ESP.getFreeHeap());
        json += "}";
        r->send(200, "application/json", json);
    });

    _server->begin();

    // Suspend until needed (controlled by AppTask)
    vTaskSuspend(NULL);
}
