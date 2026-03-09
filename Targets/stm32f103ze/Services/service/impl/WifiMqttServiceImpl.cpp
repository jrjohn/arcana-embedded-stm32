#include "WifiMqttServiceImpl.hpp"
#include "Ili9341Lcd.hpp"
#include <cstdio>
#include <cstring>

// Debug: show MQTT status on LCD (y=154, below WiFi/MQTT label)
static void lcdStatus(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(20, 154, 200, 8, 0x0000);
    disp.drawString(20, 154, msg, 0xFFFF, 0x0000, 1);
}

// Debug: show second line of status (y=166)
static void lcdStatus2(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(20, 166, 200, 8, 0x0000);
    disp.drawString(20, 166, msg, 0xFFFF, 0x0000, 1);
}

// Show LAST N non-CRLF chars of ESP response on LCD line 2
static void showResponse(arcana::Esp8266& esp) {
    const char* resp = esp.getResponse();
    uint16_t rlen = esp.getResponseLen();
    // First collect all non-CRLF chars (use rlen to handle embedded nulls)
    char clean[128];
    int ci = 0;
    for (uint16_t i = 0; i < rlen && ci < 127; i++) {
        char c = resp[i];
        if (c == '\0') continue;
        if (c != '\r' && c != '\n') {
            // Show non-printable as hex
            if (c < 0x20 || c > 0x7E) {
                if (ci + 3 < 127) {
                    static const char hex[] = "0123456789ABCDEF";
                    clean[ci++] = '[';
                    clean[ci++] = hex[(uint8_t)c >> 4];
                    clean[ci++] = hex[(uint8_t)c & 0xF];
                    clean[ci++] = ']';
                }
            } else {
                clean[ci++] = c;
            }
        }
    }
    clean[ci] = '\0';
    // Show last 33 chars
    const char* show = clean;
    if (ci > 33) show = clean + ci - 33;
    lcdStatus2(show);
}

namespace arcana {
namespace mqtt {

// --- Configuration ---
const char* WifiMqttServiceImpl::WIFI_SSID      = "YOUR_SSID";
const char* WifiMqttServiceImpl::WIFI_PASS      = "YOUR_PASS";
const char* WifiMqttServiceImpl::MQTT_BROKER    = "YOUR_BROKER";
const char* WifiMqttServiceImpl::MQTT_CLIENT_ID = "arcana_f103";
const char* WifiMqttServiceImpl::TOPIC_SENSOR   = "arcana/sensor";
const char* WifiMqttServiceImpl::TOPIC_CMD      = "arcana/cmd";

WifiMqttServiceImpl::WifiMqttServiceImpl()
    : mEsp(Esp8266::getInstance())
    , mCmdObs("MqttSvc Cmd")
    , mConnObs("MqttSvc Conn")
    , mCmdModel()
    , mConnModel()
    , mSensorPending(false)
    , mPendingSensor()
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    , mMqttConnected(false)
{
    output.CommandEvents    = &mCmdObs;
    output.ConnectionStatus = &mConnObs;
}

WifiMqttServiceImpl::~WifiMqttServiceImpl() {
    stop();
}

WifiMqttService& WifiMqttServiceImpl::getInstance() {
    static WifiMqttServiceImpl sInstance;
    return sInstance;
}

ServiceStatus WifiMqttServiceImpl::initHAL() {
    if (!mEsp.initHAL()) {
        return ServiceStatus::Error;
    }
    return ServiceStatus::OK;
}

ServiceStatus WifiMqttServiceImpl::init() {
    // Subscribe to sensor data for MQTT publishing
    if (input.SensorData) {
        input.SensorData->subscribe(
            reinterpret_cast<void (*)(SensorDataModel*, void*)>(onSensorData),
            this);
    }
    if (input.LightData) {
        input.LightData->subscribe(
            reinterpret_cast<void (*)(LightDataModel*, void*)>(onLightData),
            this);
    }
    return ServiceStatus::OK;
}

ServiceStatus WifiMqttServiceImpl::start() {
    mRunning = true;

    mTaskHandle = xTaskCreateStatic(
        mqttTask, "mqtt", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1,
        mTaskStack, &mTaskBuffer);

    if (!mTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void WifiMqttServiceImpl::stop() {
    mRunning = false;
}

// --- Observer callbacks ---

void WifiMqttServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    WifiMqttServiceImpl* self = static_cast<WifiMqttServiceImpl*>(ctx);
    if (!self->mMqttConnected) return;

    // Copy data for the MQTT task to publish (non-blocking)
    self->mPendingSensor = *model;
    self->mSensorPending = true;
}

void WifiMqttServiceImpl::onLightData(LightDataModel* model, void* ctx) {
    WifiMqttServiceImpl* self = static_cast<WifiMqttServiceImpl*>(ctx);
    self->mLatestLight = *model;
}

// --- MQTT task ---

void WifiMqttServiceImpl::mqttTask(void* param) {
    WifiMqttServiceImpl* self = static_cast<WifiMqttServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(1000));
    self->runTask();
    vTaskDelete(0);
}

void WifiMqttServiceImpl::runTask() {
    // Step 1: Reset ESP8266
    lcdStatus("[MQTT] Reset ESP8266...");
    mEsp.reset();

    // Step 2: Test AT communication
    lcdStatus("[MQTT] AT test...");
    if (!mEsp.sendCmd("AT", "OK", 2000)) {
        mEsp.reset();
        if (!mEsp.sendCmd("AT", "OK", 2000)) {
            lcdStatus("[MQTT] ERR: AT no response");
            return;
        }
    }
    lcdStatus("[MQTT] AT OK");

    // Step 3: Connect WiFi
    if (!connectWifi()) {
        return;  // connectWifi() shows specific error on LCD
    }
    lcdStatus("[MQTT] WiFi connected");

    // Step 4: Connect MQTT over TCP
    if (!connectMqtt()) {
        return;  // connectMqtt() shows specific error on LCD
    }

    // Step 5: Subscribe to command topic
    lcdStatus("[MQTT] Subscribe...");
    subscribeTopic();

    // Step 6: Notify connected
    lcdStatus("[MQTT] Connected!");
    mMqttConnected = true;
    mConnModel.connected = true;
    mConnModel.updateTimestamp();
    mConnObs.publish(&mConnModel);

    // Main loop: publish sensor data + check incoming messages
    uint32_t lastPingTick = xTaskGetTickCount();
    uint8_t consecutiveFails = 0;

    while (mRunning) {
        // Publish pending sensor data
        if (mSensorPending) {
            mSensorPending = false;
            if (publishSensorData(&mPendingSensor)) {
                consecutiveFails = 0;
                lastPingTick = xTaskGetTickCount();
            } else {
                consecutiveFails++;
                if (consecutiveFails >= 3) {
                    reconnect();
                    consecutiveFails = 0;
                    lastPingTick = xTaskGetTickCount();
                }
            }
        }

        // MQTT PINGREQ keepalive every 30 seconds
        if ((xTaskGetTickCount() - lastPingTick) > pdMS_TO_TICKS(30000)) {
            uint8_t ping[] = {0xC0, 0x00};  // PINGREQ
            tcpSend(ping, 2);
            lastPingTick = xTaskGetTickCount();
        }

        // Check for incoming MQTT messages
        if (mEsp.hasMqttMsg()) {
            processIncomingMsg();
            mEsp.clearMqttMsg();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Disconnect
    mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
    mMqttConnected = false;
    mConnModel.connected = false;
    mConnModel.updateTimestamp();
    mConnObs.publish(&mConnModel);
}

// --- Reconnect TCP+MQTT ---

void WifiMqttServiceImpl::reconnect() {
    lcdStatus("[MQTT] Reconnecting...");
    mMqttConnected = false;

    // Close stale connection, clear buffer
    mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
    mEsp.clearRx();
    vTaskDelay(pdMS_TO_TICKS(500));

    if (connectMqtt()) {
        subscribeTopic();
        lcdStatus("[MQTT] Reconnected!");
        mMqttConnected = true;
    } else {
        // TCP reconnect failed, try full WiFi reconnect
        lcdStatus("[MQTT] Full reconn...");
        mEsp.reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (mEsp.sendCmd("AT", "OK", 2000) && connectWifi() && connectMqtt()) {
            subscribeTopic();
            lcdStatus("[MQTT] Reconnected!");
            mMqttConnected = true;
        } else {
            lcdStatus("[MQTT] Reconn FAIL");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}

// --- WiFi connection ---

bool WifiMqttServiceImpl::connectWifi() {
    // Enable DHCP (following 野火 reference sequence)
    lcdStatus("CWDHCP...");
    mEsp.sendCmd("AT+CWDHCP_CUR=1,1", "OK", 500);

    // Station mode
    lcdStatus("CWMODE=1...");
    if (!mEsp.sendCmd("AT+CWMODE=1", "OK", 2500)) {
        // Try accepting "no change" (already in mode 1)
        if (!mEsp.responseContains("no change")) {
            lcdStatus("ERR: CWMODE fail");
            showResponse(mEsp);
            return false;
        }
    }

    // Connect to AP
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
    lcdStatus("CWJAP...");
    lcdStatus2("");
    if (mEsp.sendCmd(cmd, "OK", 15000)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
    }
    lcdStatus("ERR: CWJAP");
    showResponse(mEsp);
    return false;
}

// --- MQTT over TCP (raw MQTT 3.1.1 packets) ---

// Ensure ESP is in AT command mode (not stuck in data mode)
bool WifiMqttServiceImpl::verifyAtReady() {
    mEsp.clearRx();
    vTaskDelay(pdMS_TO_TICKS(200));
    return mEsp.sendCmd("AT", "OK", 1000);
}

bool WifiMqttServiceImpl::tcpSend(const uint8_t* data, uint16_t len) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);
    if (!mEsp.sendCmd(cmd, ">", 2000)) {
        // Check if link is dead or ESP stuck
        if (mEsp.responseContains("link is not") ||
            mEsp.responseContains("CLOSED")) {
            return false;
        }
        // ESP might be stuck — try to recover
        verifyAtReady();
        return false;
    }
    mEsp.sendData(data, len, 1000);
    if (!mEsp.waitFor("SEND OK", 5000)) {
        // SEND OK missed — wait for ESP to finish, then verify AT state
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!verifyAtReady()) {
            return false;  // ESP stuck, will trigger reconnect
        }
        // ESP is fine, just missed the response (UART overrun)
        return true;  // data was probably sent successfully
    }
    return true;
}

uint16_t WifiMqttServiceImpl::mqttBuildConnect(uint8_t* buf, const char* clientId) {
    uint16_t idLen = strlen(clientId);
    uint16_t remainLen = 10 + 2 + idLen;  // variable header(10) + clientId(2+len)
    uint16_t pos = 0;

    buf[pos++] = 0x10;  // CONNECT
    buf[pos++] = (uint8_t)remainLen;

    // Protocol Name "MQTT"
    buf[pos++] = 0x00; buf[pos++] = 0x04;
    buf[pos++] = 'M'; buf[pos++] = 'Q'; buf[pos++] = 'T'; buf[pos++] = 'T';

    // Protocol Level (MQTT 3.1.1)
    buf[pos++] = 0x04;

    // Connect Flags: Clean Session
    buf[pos++] = 0x02;

    // Keep Alive: 60 seconds
    buf[pos++] = 0x00; buf[pos++] = 0x3C;

    // Client ID
    buf[pos++] = (uint8_t)(idLen >> 8);
    buf[pos++] = (uint8_t)(idLen & 0xFF);
    memcpy(buf + pos, clientId, idLen);
    pos += idLen;

    return pos;
}

uint16_t WifiMqttServiceImpl::mqttBuildSubscribe(uint8_t* buf, const char* topic, uint16_t packetId) {
    uint16_t topicLen = strlen(topic);
    uint16_t remainLen = 2 + 2 + topicLen + 1;  // packetId(2) + topic(2+len) + qos(1)
    uint16_t pos = 0;

    buf[pos++] = 0x82;  // SUBSCRIBE
    buf[pos++] = (uint8_t)remainLen;

    // Packet ID
    buf[pos++] = (uint8_t)(packetId >> 8);
    buf[pos++] = (uint8_t)(packetId & 0xFF);

    // Topic Filter
    buf[pos++] = (uint8_t)(topicLen >> 8);
    buf[pos++] = (uint8_t)(topicLen & 0xFF);
    memcpy(buf + pos, topic, topicLen);
    pos += topicLen;

    // QoS 1
    buf[pos++] = 0x01;

    return pos;
}

uint16_t WifiMqttServiceImpl::mqttBuildPublish(uint8_t* buf, const char* topic, const char* payload) {
    uint16_t topicLen = strlen(topic);
    uint16_t payloadLen = strlen(payload);
    uint16_t remainLen = 2 + topicLen + payloadLen;
    uint16_t pos = 0;

    buf[pos++] = 0x30;  // PUBLISH QoS 0
    // Encode remaining length (supports up to 127)
    buf[pos++] = (uint8_t)remainLen;

    // Topic
    buf[pos++] = (uint8_t)(topicLen >> 8);
    buf[pos++] = (uint8_t)(topicLen & 0xFF);
    memcpy(buf + pos, topic, topicLen);
    pos += topicLen;

    // Payload
    memcpy(buf + pos, payload, payloadLen);
    pos += payloadLen;

    return pos;
}

bool WifiMqttServiceImpl::connectMqtt() {
    // Single connection mode
    mEsp.sendCmd("AT+CIPMUX=0", "OK", 500);

    // TCP connect to MQTT broker
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", MQTT_BROKER, MQTT_PORT);
    lcdStatus("TCP connect...");
    if (!mEsp.sendCmd(cmd, "OK", 10000)) {
        lcdStatus("ERR: TCP fail");
        showResponse(mEsp);
        return false;
    }
    lcdStatus("TCP connected");
    vTaskDelay(pdMS_TO_TICKS(200));

    // Send MQTT CONNECT packet
    lcdStatus("MQTT CONNECT...");
    uint8_t pkt[64];
    uint16_t len = mqttBuildConnect(pkt, MQTT_CLIENT_ID);
    if (!tcpSend(pkt, len)) {
        // tcpSend already shows specific error on LCD
        return false;
    }

    // Wait for CONNACK (0x20 0x02 0x00 0x00)
    vTaskDelay(pdMS_TO_TICKS(1000));
    return true;
}

bool WifiMqttServiceImpl::subscribeTopic() {
    uint8_t pkt[64];
    uint16_t len = mqttBuildSubscribe(pkt, TOPIC_CMD, 1);
    return tcpSend(pkt, len);
}

// --- Publish sensor data ---

bool WifiMqttServiceImpl::publishSensorData(SensorDataModel* model) {
    char payload[192];
    int tInt = (int)model->temperature;
    int tFrac = (int)(model->temperature * 10) % 10;
    if (tFrac < 0) tFrac = -tFrac;
    snprintf(payload, sizeof(payload),
             "{\"t\":%d.%d,\"ax\":%d,\"ay\":%d,\"az\":%d,\"als\":%u,\"ps\":%u}",
             tInt, tFrac,
             (int)model->accelX, (int)model->accelY, (int)model->accelZ,
             mLatestLight.ambientLight, mLatestLight.proximity);

    uint8_t pkt[256];
    uint16_t len = mqttBuildPublish(pkt, TOPIC_SENSOR, payload);
    return tcpSend(pkt, len);
}

// --- Process incoming MQTT messages ---

void WifiMqttServiceImpl::processIncomingMsg() {
    // Data arrives via +IPD,<len>:<binary MQTT data>
    // For now, parse MQTT PUBLISH packets from the raw buffer
    const char* buf = mEsp.getMqttMsg();
    uint16_t bufLen = mEsp.getMqttMsgLen();

    // Find +IPD data start (after ":")
    const char* p = strstr(buf, ":");
    if (!p) return;
    p++;  // skip ":"

    // Check if it's a PUBLISH packet (first nibble = 0x3)
    if (((uint8_t)*p & 0xF0) != 0x30) return;

    uint8_t remainLen = (uint8_t)p[1];
    p += 2;

    // Read topic length
    uint16_t topicLen = ((uint8_t)p[0] << 8) | (uint8_t)p[1];
    p += 2;

    // Skip topic
    p += topicLen;

    // Remaining is payload
    uint16_t payloadLen = remainLen - 2 - topicLen;
    uint16_t available = bufLen - (uint16_t)(p - buf);
    if (payloadLen > available) payloadLen = available;

    if (payloadLen > MqttCommandModel::MAX_DATA) {
        payloadLen = MqttCommandModel::MAX_DATA;
    }

    memcpy(mCmdModel.data, p, payloadLen);
    mCmdModel.length = (uint8_t)payloadLen;
    mCmdModel.updateTimestamp();
    mCmdObs.publish(&mCmdModel);
}

} // namespace mqtt
} // namespace arcana
