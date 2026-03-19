#include "MqttServiceImpl.hpp"
#include "WifiService.hpp"
#include "Credentials.hpp"
#include "Esp8266.hpp"
#include "Ili9341Lcd.hpp"
#include "CommandBridge.hpp"
#include "SyslogAppender.hpp"
#include <cstdio>
#include <cstring>

extern "C" volatile uint8_t g_exfat_ready;
extern "C" void ats_safe_eject(void);

static void lcdStatus(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(0, 154, 240, 10, 0x0000);
    disp.drawString(20, 154, msg, 0xFFFF, 0x0000, 1);
}

namespace arcana {
namespace mqtt {

// --- Configuration ---
const char* MqttServiceImpl::MQTT_BROKER    = MQTT_BROKER_VALUE;
const char* MqttServiceImpl::MQTT_CLIENT_ID = "arcana_f103";
const char* MqttServiceImpl::TOPIC_SENSOR   = "arcana/sensor";
const char* MqttServiceImpl::TOPIC_CMD      = "arcana/cmd";
const char* MqttServiceImpl::TOPIC_RSP      = "arcana/rsp";

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

MqttServiceImpl::~MqttServiceImpl() { stop(); }

MqttService& MqttServiceImpl::getInstance() {
    static MqttServiceImpl sInstance;
    return sInstance;
}

ServiceStatus MqttServiceImpl::initHAL() { return ServiceStatus::OK; }

ServiceStatus MqttServiceImpl::init() {
    if (input.SensorData) {
        input.SensorData->subscribe(
            reinterpret_cast<void (*)(SensorDataModel*, void*)>(onSensorData), this);
    }
    if (input.LightData) {
        input.LightData->subscribe(
            reinterpret_cast<void (*)(LightDataModel*, void*)>(onLightData), this);
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

void MqttServiceImpl::stop() { mRunning = false; }

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

// --- AT+MQTT helpers ---

bool MqttServiceImpl::mqttConfig() {
    Esp8266& esp = input.Wifi->getEsp();
    char cmd[128];
    // AT+MQTTUSERCFG=<LinkID>,<scheme>,<"client_id">,<"username">,<"password">,<cert_key_ID>,<CA_ID>,<"path">
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"\",\"\",0,0,\"\"",
             MQTT_CLIENT_ID);
    return esp.sendCmd(cmd, "OK", 3000);
}

bool MqttServiceImpl::mqttConnect() {
    Esp8266& esp = input.Wifi->getEsp();
    char cmd[128];
    // AT+MQTTCONN=<LinkID>,<"host">,<port>,<reconnect>
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%u,1",  // reconnect=1: auto-reconnect
             MQTT_BROKER, MQTT_PORT);
    return esp.sendCmd(cmd, "OK", 10000);
}

bool MqttServiceImpl::mqttSubscribe(const char* topic, uint8_t qos) {
    Esp8266& esp = input.Wifi->getEsp();
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u", topic, qos);
    return esp.sendCmd(cmd, "OK", 5000);
}

bool MqttServiceImpl::mqttPublish(const char* topic, const char* payload, uint8_t qos) {
    Esp8266& esp = input.Wifi->getEsp();
    uint16_t payloadLen = strlen(payload);
    char cmd[128];
    // AT+MQTTPUBRAW=<LinkID>,<"topic">,<length>,<qos>,<retain>
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%u,%u,0", topic, payloadLen, qos);
    if (!esp.sendCmd(cmd, ">", 3000)) {
        return false;
    }
    // Send raw payload after ">" prompt
    esp.sendData((const uint8_t*)payload, payloadLen, 2000);
    return esp.waitFor("OK", 5000);
}

bool MqttServiceImpl::mqttDisconnect() {
    Esp8266& esp = input.Wifi->getEsp();
    return esp.sendCmd("AT+MQTTCLEAN=0", "OK", 2000);
}

bool MqttServiceImpl::isMqttConnected() {
    Esp8266& esp = input.Wifi->getEsp();
    if (!esp.sendCmd("AT+MQTTCONN?", "OK", 2000)) return false;
    // Response contains state: +MQTTCONN:0,<state>,...
    // state 3 = CONNECTED, state 4 = CONNECTED_NO_SUB
    return esp.responseContains(",3,") || esp.responseContains(",4,");
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

    while (mRunning) {

        // --- Phase 1: WiFi ---
        lcdStatus("[WiFi] Connecting...");
        while (mRunning) {
            if (wifi->resetAndConnect()) break;
            lcdStatus("[WiFi] Retry 10s...");
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        if (!mRunning) break;

        // --- Phase 2: NTP ---
        wifi->syncNtp();

        // --- Phase 3: MQTT config + connect ---
        lcdStatus("[MQTT] Connecting...");
        printf("[MQTT] Config...\r\n");
        if (!mqttConfig()) {
            printf("[MQTT] Config FAILED\r\n");
            lcdStatus("[MQTT] Config fail");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        printf("[MQTT] Config OK\r\n");

        bool connected = false;
        for (int i = 0; i < 3 && mRunning; i++) {
            printf("[MQTT] Connect attempt %d to %s:%u...\r\n", i+1, MQTT_BROKER, MQTT_PORT);
            if (mqttConnect()) {
                connected = true;
                printf("[MQTT] Connect OK\r\n");
                break;
            }
            printf("[MQTT] Connect failed, resp: %.*s\r\n",
                   esp.getResponseLen() > 80 ? 80 : esp.getResponseLen(),
                   esp.getResponse());
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        if (!connected) {
            printf("[MQTT] All attempts failed\r\n");
            lcdStatus("[MQTT] Connect fail");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        // --- Phase 4: Subscribe ---
        printf("[MQTT] Subscribe %s\r\n", TOPIC_CMD);
        mqttSubscribe(TOPIC_CMD, 0);

        lcdStatus("[MQTT] Connected!");
        mMqttConnected = true;
        CommandBridge::getInstance().setMqttSend(mqttSendFn, this);
        mConnModel.connected = true;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        // Open UDP to syslog server (coexists with AT+MQTT)
        log::SyslogAppender::getInstance().openUdp(esp);

        uint32_t lastNtpTick = xTaskGetTickCount();

        // --- Phase 5: Main loop ---
        while (mRunning && mMqttConnected) {
            // Publish sensor data
            if (mSensorPending) {
                mSensorPending = false;
                if (!publishSensorData(&mPendingSensor)) {
                    // Check if still connected
                    if (!isMqttConnected()) {
                        mMqttConnected = false;
                        break;
                    }
                }
            }

            // NTP resync every 6 hours
            if ((xTaskGetTickCount() - lastNtpTick) >
                pdMS_TO_TICKS(6UL * 3600UL * 1000UL)) {
                wifi->syncNtp();
                lastNtpTick = xTaskGetTickCount();
            }

            // Check incoming +MQTTSUBRECV
            if (esp.hasMqttMsg()) {
                processIncomingMsg();
                esp.clearMqttMsg();
            }

            // Flush pending syslog messages via UDP (max 4 per cycle)
            {
                auto& syslog = log::SyslogAppender::getInstance();
                if (syslog.pending() > 0) {
                    syslog.flushViaUdp(esp);
                    // If UDP broke, try reopen next cycle
                    if (syslog.pending() > 0) {
                        syslog.openUdp(esp);
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // --- Phase 6: Disconnected ---
        log::SyslogAppender::getInstance().closeUdp(esp);
        mqttDisconnect();
        mMqttConnected = false;
        mConnModel.connected = false;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        lcdStatus("[MQTT] Disconnected");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
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

    return mqttPublish(TOPIC_SENSOR, payload, 0);
}

// --- Process incoming +MQTTSUBRECV ---

void MqttServiceImpl::processIncomingMsg() {
    Esp8266& esp = input.Wifi->getEsp();
    const char* buf = esp.getMqttMsg();
    uint16_t bufLen = esp.getMqttMsgLen();

    // Format: +MQTTSUBRECV:0,"topic",len,payload
    // Find the payload after the third comma
    const char* p = buf;
    int commas = 0;
    while (p < buf + bufLen && commas < 3) {
        if (*p == ',') commas++;
        p++;
    }
    if (commas < 3) return;

    uint16_t payloadLen = bufLen - (uint16_t)(p - buf);

    // Safe eject command
    if (payloadLen == 5 && memcmp(p, "eject", 5) == 0) {
        printf("[MQTT] Eject command received\r\n");
        ats_safe_eject();
        return;
    }

    // Submit to CommandBridge frame queue
    if (payloadLen > 0 && payloadLen <= CmdFrameItem::MAX_DATA) {
        CommandBridge::getInstance().submitFrame(
            reinterpret_cast<const uint8_t*>(p), payloadLen,
            CmdFrameItem::MQTT);
    }

    // Also publish via observable
    if (payloadLen > MqttCommandModel::MAX_DATA) {
        payloadLen = MqttCommandModel::MAX_DATA;
    }
    memcpy(mCmdModel.data, p, payloadLen);
    mCmdModel.length = (uint8_t)payloadLen;
    mCmdModel.updateTimestamp();
    mCmdObs.publish(&mCmdModel);
}

// --- CommandBridge response via AT+MQTTPUB ---

bool MqttServiceImpl::mqttSendFn(const uint8_t* data, uint16_t len, void* ctx) {
    MqttServiceImpl* self = static_cast<MqttServiceImpl*>(ctx);
    if (!self->mMqttConnected) return false;

    // Binary frame data → hex-encode for AT+MQTTPUB text payload
    // For now, publish raw bytes as topic payload
    // CommandBridge frames are small (<64 bytes), fit in AT command
    char hexPayload[130];  // 64 bytes × 2 + null
    uint16_t hLen = len > 64 ? 64 : len;
    for (uint16_t i = 0; i < hLen; i++) {
        static const char hex[] = "0123456789ABCDEF";
        hexPayload[i * 2]     = hex[data[i] >> 4];
        hexPayload[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    hexPayload[hLen * 2] = '\0';

    return self->mqttPublish(TOPIC_RSP, hexPayload, 0);
}

} // namespace mqtt
} // namespace arcana
