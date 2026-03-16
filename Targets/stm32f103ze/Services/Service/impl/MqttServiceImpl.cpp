#include "MqttServiceImpl.hpp"
#include "WifiService.hpp"
#include "Credentials.hpp"
#include "Esp8266.hpp"
#include "Ili9341Lcd.hpp"
#include <cstdio>
#include <cstring>

/* Wait for exFAT format+mount before connecting */
extern "C" volatile uint8_t g_exfat_ready;

static void lcdStatus(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(20, 154, 200, 8, 0x0000);
    disp.drawString(20, 154, msg, 0xFFFF, 0x0000, 1);
}

static void lcdStatus2(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(20, 166, 200, 8, 0x0000);
    disp.drawString(20, 166, msg, 0xFFFF, 0x0000, 1);
}

static void showResponse(arcana::Esp8266& esp) {
    const char* resp = esp.getResponse();
    uint16_t rlen = esp.getResponseLen();
    char clean[128];
    int ci = 0;
    for (uint16_t i = 0; i < rlen && ci < 127; i++) {
        char c = resp[i];
        if (c == '\0') continue;
        if (c != '\r' && c != '\n') {
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
    const char* show = clean;
    if (ci > 33) show = clean + ci - 33;
    lcdStatus2(show);
}

namespace arcana {
namespace mqtt {

// --- Configuration ---
const char* MqttServiceImpl::MQTT_BROKER    = MQTT_BROKER_VALUE;
const char* MqttServiceImpl::MQTT_CLIENT_ID = "arcana_f103";
const char* MqttServiceImpl::TOPIC_SENSOR   = "arcana/sensor";
const char* MqttServiceImpl::TOPIC_CMD      = "arcana/cmd";

MqttServiceImpl::MqttServiceImpl()
    : mCmdObs("MqttSvc Cmd")
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

MqttServiceImpl::~MqttServiceImpl() {
    stop();
}

MqttService& MqttServiceImpl::getInstance() {
    static MqttServiceImpl sInstance;
    return sInstance;
}

ServiceStatus MqttServiceImpl::initHAL() {
    return ServiceStatus::OK;  // ESP8266 initialized by WifiService
}

ServiceStatus MqttServiceImpl::init() {
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

ServiceStatus MqttServiceImpl::start() {
    mRunning = true;

    mTaskHandle = xTaskCreateStatic(
        mqttTask, "mqtt", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1,
        mTaskStack, &mTaskBuffer);

    if (!mTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void MqttServiceImpl::stop() {
    mRunning = false;
}

// --- Observer callbacks ---

void MqttServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    MqttServiceImpl* self = static_cast<MqttServiceImpl*>(ctx);
    if (!self->mMqttConnected) return;

    self->mPendingSensor = *model;
    self->mSensorPending = true;
}

void MqttServiceImpl::onLightData(LightDataModel* model, void* ctx) {
    MqttServiceImpl* self = static_cast<MqttServiceImpl*>(ctx);
    self->mLatestLight = *model;
}

// --- MQTT task ---

void MqttServiceImpl::mqttTask(void* param) {
    MqttServiceImpl* self = static_cast<MqttServiceImpl*>(param);

    while (!g_exfat_ready) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    self->runTask();
    vTaskDelete(0);
}

void MqttServiceImpl::runTask() {
    wifi::WifiService* wifi = input.Wifi;
    Esp8266& esp = wifi->getEsp();

    // Outer loop: auto-reconnect WiFi + MQTT forever
    while (mRunning) {

        // --- Phase 1: WiFi connection (retry until success) ---
        lcdStatus("[WiFi] Connecting...");
        while (mRunning) {
            if (wifi->resetAndConnect()) break;
            lcdStatus("[WiFi] Retry 10s...");
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        if (!mRunning) break;

        // --- Phase 2: NTP time sync (non-blocking on failure) ---
        wifi->syncNtp();

        // --- Phase 3: MQTT connect (retry 3 times, then restart WiFi) ---
        bool mqttOk = false;
        for (int i = 0; i < 3 && mRunning; i++) {
            if (connectMqtt()) {
                subscribeTopic();
                mqttOk = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        if (!mqttOk) {
            lcdStatus("[MQTT] Connect fail");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;  // restart from WiFi
        }

        // --- Phase 4: Connected ---
        lcdStatus("[MQTT] Connected!");
        mMqttConnected = true;
        mConnModel.connected = true;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        uint32_t lastPingTick = xTaskGetTickCount();
        uint32_t lastNtpTick  = xTaskGetTickCount();
        uint8_t consecutiveFails = 0;

        // --- Phase 5: Main publish loop ---
        while (mRunning && mMqttConnected) {
            // Publish pending sensor data
            if (mSensorPending) {
                mSensorPending = false;
                if (publishSensorData(&mPendingSensor)) {
                    consecutiveFails = 0;
                    lastPingTick = xTaskGetTickCount();
                } else {
                    consecutiveFails++;
                    if (consecutiveFails >= 3) {
                        // Try quick MQTT-only reconnect
                        if (!quickReconnect()) {
                            mMqttConnected = false;  // will restart WiFi
                        }
                        consecutiveFails = 0;
                        lastPingTick = xTaskGetTickCount();
                    }
                }
            }

            // MQTT PINGREQ keepalive every 30 seconds
            if ((xTaskGetTickCount() - lastPingTick) > pdMS_TO_TICKS(30000)) {
                uint8_t ping[] = {0xC0, 0x00};
                tcpSend(ping, 2);
                lastPingTick = xTaskGetTickCount();
            }

            // NTP resync every 6 hours
            if ((xTaskGetTickCount() - lastNtpTick) >
                pdMS_TO_TICKS(6UL * 3600UL * 1000UL)) {
                wifi->syncNtp();
                lastNtpTick = xTaskGetTickCount();
            }

            // Check for incoming MQTT messages
            if (esp.hasMqttMsg()) {
                processIncomingMsg();
                esp.clearMqttMsg();
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // --- Phase 6: Disconnected, clean up ---
        esp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        mMqttConnected = false;
        mConnModel.connected = false;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        lcdStatus("[MQTT] Disconnected");
        vTaskDelay(pdMS_TO_TICKS(3000));
        // Loop back to Phase 1
    }
}

// --- Quick MQTT reconnect (TCP only, no WiFi reset) ---

bool MqttServiceImpl::quickReconnect() {
    lcdStatus("[MQTT] Reconnecting...");
    wifi::WifiService* wifi = input.Wifi;
    Esp8266& esp = wifi->getEsp();

    esp.sendCmd("AT+CIPCLOSE", "OK", 1000);
    esp.clearRx();
    vTaskDelay(pdMS_TO_TICKS(500));

    if (connectMqtt()) {
        subscribeTopic();
        lcdStatus("[MQTT] Reconnected!");
        return true;
    }

    // TCP failed — try WiFi reconnect (without full reset)
    lcdStatus("[MQTT] WiFi reconn...");
    if (wifi->connect() && connectMqtt()) {
        wifi->syncNtp();
        subscribeTopic();
        lcdStatus("[MQTT] Reconnected!");
        return true;
    }

    return false;  // caller will do full WiFi reset
}

// --- MQTT over TCP helpers ---

bool MqttServiceImpl::verifyAtReady() {
    Esp8266& esp = input.Wifi->getEsp();
    esp.clearRx();
    vTaskDelay(pdMS_TO_TICKS(200));
    return esp.sendCmd("AT", "OK", 1000);
}

bool MqttServiceImpl::tcpSend(const uint8_t* data, uint16_t len) {
    Esp8266& esp = input.Wifi->getEsp();
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);
    if (!esp.sendCmd(cmd, ">", 2000)) {
        if (esp.responseContains("link is not") ||
            esp.responseContains("CLOSED")) {
            return false;
        }
        verifyAtReady();
        return false;
    }
    esp.sendData(data, len, 1000);
    if (!esp.waitFor("SEND OK", 5000)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!verifyAtReady()) {
            return false;
        }
        return true;
    }
    return true;
}

bool MqttServiceImpl::connectMqtt() {
    Esp8266& esp = input.Wifi->getEsp();

    esp.sendCmd("AT+CIPMUX=0", "OK", 500);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u",
             MQTT_BROKER, MQTT_PORT);
    lcdStatus("TCP connect...");
    if (!esp.sendCmd(cmd, "OK", 10000)) {
        lcdStatus("ERR: TCP fail");
        showResponse(esp);
        return false;
    }
    lcdStatus("TCP connected");
    vTaskDelay(pdMS_TO_TICKS(200));

    lcdStatus("MQTT CONNECT...");
    uint8_t pkt[64];
    uint16_t len = mqttBuildConnect(pkt, MQTT_CLIENT_ID);
    if (!tcpSend(pkt, len)) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    return true;
}

bool MqttServiceImpl::subscribeTopic() {
    uint8_t pkt[64];
    uint16_t len = mqttBuildSubscribe(pkt, TOPIC_CMD, 1);
    return tcpSend(pkt, len);
}

// --- MQTT packet builders ---

uint16_t MqttServiceImpl::mqttBuildConnect(uint8_t* buf, const char* clientId) {
    uint16_t idLen = strlen(clientId);
    uint16_t remainLen = 11 + 2 + idLen;
    uint16_t pos = 0;

    buf[pos++] = 0x10;  // CONNECT
    buf[pos++] = (uint8_t)remainLen;

    buf[pos++] = 0x00; buf[pos++] = 0x04;
    buf[pos++] = 'M'; buf[pos++] = 'Q'; buf[pos++] = 'T'; buf[pos++] = 'T';
    buf[pos++] = 0x05;  // MQTT 5.0
    buf[pos++] = 0x02;  // Clean Start
    buf[pos++] = 0x00; buf[pos++] = 0x3C;  // Keep Alive 60s
    buf[pos++] = 0x00;  // Properties Length: 0

    buf[pos++] = (uint8_t)(idLen >> 8);
    buf[pos++] = (uint8_t)(idLen & 0xFF);
    memcpy(buf + pos, clientId, idLen);
    pos += idLen;

    return pos;
}

uint16_t MqttServiceImpl::mqttBuildSubscribe(uint8_t* buf, const char* topic, uint16_t packetId) {
    uint16_t topicLen = strlen(topic);
    uint16_t remainLen = 2 + 1 + 2 + topicLen + 1;
    uint16_t pos = 0;

    buf[pos++] = 0x82;  // SUBSCRIBE
    buf[pos++] = (uint8_t)remainLen;
    buf[pos++] = (uint8_t)(packetId >> 8);
    buf[pos++] = (uint8_t)(packetId & 0xFF);
    buf[pos++] = 0x00;  // Properties Length: 0

    buf[pos++] = (uint8_t)(topicLen >> 8);
    buf[pos++] = (uint8_t)(topicLen & 0xFF);
    memcpy(buf + pos, topic, topicLen);
    pos += topicLen;
    buf[pos++] = 0x01;  // QoS 1

    return pos;
}

uint16_t MqttServiceImpl::mqttBuildPublish(uint8_t* buf, const char* topic, const char* payload) {
    uint16_t topicLen = strlen(topic);
    uint16_t payloadLen = strlen(payload);
    uint16_t remainLen = 2 + topicLen + 1 + payloadLen;
    uint16_t pos = 0;

    buf[pos++] = 0x30;  // PUBLISH QoS 0
    buf[pos++] = (uint8_t)remainLen;

    buf[pos++] = (uint8_t)(topicLen >> 8);
    buf[pos++] = (uint8_t)(topicLen & 0xFF);
    memcpy(buf + pos, topic, topicLen);
    pos += topicLen;
    buf[pos++] = 0x00;  // Properties Length: 0

    memcpy(buf + pos, payload, payloadLen);
    pos += payloadLen;

    return pos;
}

// --- Publish sensor data ---

bool MqttServiceImpl::publishSensorData(SensorDataModel* model) {
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

void MqttServiceImpl::processIncomingMsg() {
    Esp8266& esp = input.Wifi->getEsp();
    const char* buf = esp.getMqttMsg();
    uint16_t bufLen = esp.getMqttMsgLen();

    const char* p = strstr(buf, ":");
    if (!p) return;
    p++;

    if (((uint8_t)*p & 0xF0) != 0x30) return;

    uint8_t remainLen = (uint8_t)p[1];
    p += 2;

    uint16_t topicLen = ((uint8_t)p[0] << 8) | (uint8_t)p[1];
    p += 2;
    p += topicLen;

    uint8_t propsLen = (uint8_t)*p;
    p += 1 + propsLen;

    uint16_t payloadLen = remainLen - 2 - topicLen - 1 - propsLen;
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
