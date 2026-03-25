#include "MqttServiceImpl.hpp"
#include "WifiServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "Credentials.hpp"
#include "Esp8266.hpp"
#include "CommandBridge.hpp"
#include "SyslogAppender.hpp"
#include "SystemClock.hpp"
#include "Log.hpp"
#include <cstdio>
#include <cstring>

extern "C" volatile uint8_t g_exfat_ready;
extern "C" void ats_safe_eject(void);

namespace arcana {
namespace mqtt {

// --- Configuration ---
const char* MqttServiceImpl::MQTT_BROKER    = MQTT_BROKER_VALUE;
const char* MqttServiceImpl::MQTT_CLIENT_ID = "arcana_f103";
const char* MqttServiceImpl::TOPIC_SENSOR   = "/arcana/sensor";
const char* MqttServiceImpl::TOPIC_CMD      = "/arcana/cmd";
const char* MqttServiceImpl::TOPIC_RSP      = "/arcana/rsp";

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
        input.SensorData->subscribe(onSensorData, this);
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
    // scheme=1: MQTT over TCP (ESP8266 TLS too limited for WSS)
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"arcana\",\"arcana\",0,0,\"\"",
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
        LOG_I(ats::ErrorSource::Wifi, 0x0001);  // WiFi connecting
        while (mRunning) {
            if (wifi->resetAndConnect()) break;
            LOG_W(ats::ErrorSource::Wifi, 0x0002);  // WiFi retry
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        if (!mRunning) break;

        // --- Phase 2: NTP ---
        wifi->syncNtp();

        // --- Phase 2.5: Timezone auto-detect ---
        {
            auto& clock = SystemClock::getInstance();
            auto* wifiImpl = static_cast<wifi::WifiServiceImpl*>(wifi);
            auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
                atsstorage::AtsStorageServiceImpl::getInstance());

            int16_t currentTz = clock.tzOffsetMin();
            int16_t detectedTz = 0;

            if (wifiImpl->detectTimezone(detectedTz)) {
                if (currentTz != detectedTz) {
                    // First boot (currentTz==0 and no config) → apply silently
                    // Otherwise → TODO: LCD prompt KEY1=update KEY2=ignore
                    clock.setTzOffset(detectedTz);
                    storage.saveTzConfig(detectedTz, 1);
                    LOG_I(ats::ErrorSource::System, 0x0031,
                          (uint32_t)(uint16_t)detectedTz);
                }
            }
        }

        // --- Phase 3: MQTT config + connect ---
        LOG_I(ats::ErrorSource::Mqtt, 0x0001);  // MQTT connecting
        LOG_D(ats::ErrorSource::Mqtt, 0x0010);  // MQTT config start
        if (!mqttConfig()) {
            LOG_W(ats::ErrorSource::Mqtt, 0x0011);  // MQTT config failed
            LOG_W(ats::ErrorSource::Mqtt, 0x0004);  // MQTT config fail
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        LOG_D(ats::ErrorSource::Mqtt, 0x0012);  // MQTT config OK

        bool connected = false;
        for (int i = 0; i < 3 && mRunning; i++) {
            LOG_D(ats::ErrorSource::Mqtt, 0x0013, (uint32_t)(i+1));  // connect attempt
            if (mqttConnect()) {
                connected = true;
                LOG_D(ats::ErrorSource::Mqtt, 0x0014);  // connect OK
                break;
            }
            LOG_W(ats::ErrorSource::Mqtt, 0x0015, (uint32_t)(i+1));  // connect failed
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        if (!connected) {
            LOG_W(ats::ErrorSource::Mqtt, 0x0016);  // all attempts failed
            LOG_W(ats::ErrorSource::Mqtt, 0x0005);  // MQTT connect fail
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        // --- Phase 4: Subscribe ---
        LOG_D(ats::ErrorSource::Mqtt, 0x0017);  // subscribe
        mqttSubscribe(TOPIC_CMD, 0);

        LOG_I(ats::ErrorSource::Mqtt, 0x0002);  // MQTT connected
        mMqttConnected = true;
        CommandBridge::getInstance().setMqttSend(mqttSendFn, this);
        mConnModel.connected = true;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        // Open UDP to syslog server (coexists with AT+MQTT)
        log::SyslogAppender::getInstance().openUdp(esp);

        uint32_t lastNtpTick = xTaskGetTickCount();

        // --- Phase 5: Main loop ---
        // --- Phase 5: Main loop ---
        while (mRunning && mMqttConnected) {
            // Publish sensor data
            if (mSensorPending) {
                mSensorPending = false;
                if (!publishSensorData(&mPendingSensor)) {
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
                LOG_I(ats::ErrorSource::Mqtt, 0x0030, (uint32_t)esp.getMqttMsgLen());
                processIncomingMsg();
                esp.clearMqttMsg();
            }

            // Flush pending syslog messages via UDP (max 4 per cycle)
            {
                auto& syslog = log::SyslogAppender::getInstance();
                if (syslog.pending() > 0) {
                    syslog.flushViaUdp(esp);
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

        LOG_W(ats::ErrorSource::Mqtt, 0x0003);  // MQTT disconnected
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
        LOG_I(ats::ErrorSource::Mqtt, 0x0020);  // eject command received
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
