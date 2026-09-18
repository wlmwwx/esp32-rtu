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
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 RTU Config</title>
<style>
*{box-sizing:border-box}
body{font-family:Arial,sans-serif;max-width:700px;margin:0 auto;padding:15px;background:#f5f5f5}
h1{text-align:center;color:#333}
h2{color:#555;border-bottom:1px solid #ddd;padding-bottom:5px;margin-top:20px;font-size:13px;text-transform:uppercase;letter-spacing:1px;background:#eee;padding:5px 8px;border-radius:4px}
.row{display:flex;align-items:center;gap:10px;margin:5px 0;flex-wrap:wrap}
label{min-width:130px;color:#444;font-size:13px;font-weight:500}
input,select{padding:6px 8px;border:1px solid #ccc;border-radius:4px;flex:1;min-width:120px}
input[type=checkbox]{width:auto;flex:none}
input[type=number]{width:90px;flex:none}
button{background:#4CAF50;color:white;padding:12px 20px;border:none;border-radius:6px;cursor:pointer;width:100%;font-size:15px;margin-top:15px}
button:hover{background:#45a049}
button.reset{background:#f44336;margin-top:8px}
.info{font-size:12px;color:#888;text-align:center;margin-top:15px}
.section{margin-top:10px}
</style>
</head>
<body>
<h1>ESP32 RTU Configuration</h1>
<form action='/save' method='POST'>

<h2>WiFi</h2>
<div class='row'><label>SSID:</label><input name='wifi_ssid' value='{wifi_ssid}'></div>
<div class='row'><label>Password:</label><input name='wifi_pass' type='password' value='{wifi_pass}'></div>

<h2>MQTT</h2>
<div class='row'><label>Broker:</label><input name='mqtt_broker' value='{mqtt_broker}'></div>
<div class='row'><label>Port:</label><input name='mqtt_port' type='number' value='{mqtt_port}'></div>
<div class='row'><label>Username:</label><input name='mqtt_user' value='{mqtt_user}'></div>
<div class='row'><label>Password:</label><input name='mqtt_pass' type='password' value='{mqtt_pass}'></div>
<div class='row'><label>Topic:</label><input name='mqtt_topic' value='{mqtt_topic}'></div>
<div class='row'><label>Use TLS:</label><input name='mqtt_tls' type='checkbox' {mqtt_tls}></div>
<div class='row'><label>QoS:</label><select name='mqtt_qos'><option value='0' {qos0}>0</option><option value='1' {qos1}>1</option></select></div>
<div class='row'><label>LWT Topic:</label><input name='mqtt_lwt_topic' value='{mqtt_lwt_topic}'></div>
<div class='row'><label>LWT Msg:</label><input name='mqtt_lwt_msg' value='{mqtt_lwt_msg}'></div>
<div class='row'><label>Retain:</label><input name='mqtt_retain' type='checkbox' {mqtt_retain}></div>

<h2>HTTP</h2>
<div class='row'><label>URL:</label><input name='http_url' value='{http_url}'></div>

<h2>TCP</h2>
<div class='row'><label>Server IP:</label><input name='tcp_ip' value='{tcp_ip}'></div>
<div class='row'><label>Port:</label><input name='tcp_port' type='number' value='{tcp_port}'></div>

<h2>Modbus</h2>
<div class='row'><label>Baud Rate:</label><input name='modbus_baud' type='number' value='{modbus_baud}'></div>
<div class='row'><label>Data Bits:</label><select name='modbus_bits'><option value='8' {bits8}>8</option><option value='7' {bits7}>7</option></select></div>
<div class='row'><label>Parity:</label><select name='modbus_parity'><option value='0' {par0}>None</option><option value='1' {par1}>Odd</option><option value='2' {par2}>Even</option></select></div>
<div class='row'><label>Stop Bits:</label><input name='modbus_stop' type='number' value='{modbus_stop}'></div>
<div class='row'><label>Slave Addr:</label><input name='modbus_slave' type='number' value='{modbus_slave}'></div>
<div class='row'><label>Start Reg:</label><input name='modbus_reg' type='number' value='{modbus_reg}'></div>
<div class='row'><label>Count:</label><input name='modbus_count' type='number' value='{modbus_count}'></div>

<h2>Mode</h2>
<div class='row'><label>Protocol:</label><select name='protocol_select'><option value='mqtt' {proto_mqtt}>MQTT</option><option value='http' {proto_http}>HTTP</option><option value='tcp' {proto_tcp}>TCP</option></select></div>
<div class='row'><label>Run Mode:</label>
<select name='run_mode'>
  <option value='push' {mode_push}>Push (MQTT/HTTP)</option>
  <option value='pull'  {mode_pull}>Pull (MQTT/HTTP)</option>
  <option value='gateway' {mode_gateway}>Gateway (TCP→RTU)</option>
</select></div>
<div class='row'><label>Gateway Port:</label><input name='gateway_port' type='number' value='{gateway_port}'></div>
<div class='row'><label>Push Interval(sec):</label><input name='push_interval' type='number' value='{push_interval}'></div>

<button type='submit'>Save & Reboot</button>
</form>
<form action='/reset' method='POST'>
<button type='submit' class='reset'>Factory Reset</button>
</form>
<div class='info'>ESP32 RTU Gateway</div>
</body>
</html>
)";

static String build_response(Config& cfg) {
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
    html.replace("{mode_gateway}", cfg.getRunMode() == "gateway" ? "selected" : "");
    html.replace("{gateway_port}", String(cfg.getGatewayPort()));
    return html;
}

static void save_params(AsyncWebServerRequest* request, Config& cfg) {
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
    if (request->hasParam("gateway_port")) cfg.setGatewayPort(request->getParam("gateway_port")->value().toInt());
}

void WebConfigTask(void* param) {
    Config cfg;
    cfg.begin();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-RTU-Config");

    _server = new AsyncWebServer(80);

    _server->on("/", HTTP_GET, [&cfg](AsyncWebServerRequest* r) {
        r->send(200, "text/html", build_response(cfg));
    });

    _server->on("/save", HTTP_POST, [&cfg](AsyncWebServerRequest* r) {
        save_params(r, cfg);
        r->send(200, "text/plain", "Saved. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    });

    _server->on("/reset", HTTP_POST, [&cfg](AsyncWebServerRequest* r) {
        cfg.reset();
        r->send(200, "text/plain", "Reset. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
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
    Serial.println("AP started at 192.168.4.1");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
