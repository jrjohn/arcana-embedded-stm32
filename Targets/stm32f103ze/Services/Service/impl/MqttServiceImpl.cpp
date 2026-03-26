#include "MqttServiceImpl.hpp"
#include "WifiServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "HttpUploadServiceImpl.hpp"
#include "RegistrationServiceImpl.hpp"
// No IoServiceImpl include — decoupled via Esp8266 resource lock
#include "Credentials.hpp"
#include "Esp8266.hpp"
#include "CommandBridge.hpp"
#include "SyslogAppender.hpp"
#include "SystemClock.hpp"
#include "Log.hpp"
#ifdef ARCANA_CMD_CRYPTO
#include "arcana_cmd.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#endif
#include <cstdio>
#include <cstring>

extern "C" volatile uint8_t g_exfat_ready;
extern "C" void ats_safe_eject(void);

namespace arcana {
namespace mqtt {

// --- Configuration ---
const char* MqttServiceImpl::MQTT_BROKER    = MQTT_BROKER_VALUE;
const char* MqttServiceImpl::MQTT_CLIENT_ID = "arcana_f103";
// Per-device topics (populated after registration)
static char sTopicSensor[40] = "/arcana/sensor";  // fallback
static char sTopicCmd[40]    = "/arcana/cmd";
static char sTopicRsp[40]    = "/arcana/rsp";
const char* MqttServiceImpl::TOPIC_SENSOR   = sTopicSensor;
const char* MqttServiceImpl::TOPIC_CMD      = sTopicCmd;
const char* MqttServiceImpl::TOPIC_RSP      = sTopicRsp;

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
    char cmd[160];

    // Use per-device credentials from registration (or fallback to hardcoded)
    auto& regSvc = reg::RegistrationServiceImpl::getInstance();
    const char* user = "arcana";
    const char* pass = "arcana";
    if (regSvc.isRegistered()) {
        user = regSvc.credentials().mqttUser;
        pass = regSvc.credentials().mqttPass;
    }

    // scheme=1: plain MQTT (scheme=2 TLS not supported on this ESP8266 AT FW)
    // TODO: test scheme=2 on ESP8266 AT v2.2+ with TLS support
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
             MQTT_CLIENT_ID, user, pass);
    return esp.sendCmd(cmd, "OK", 3000);
}

bool MqttServiceImpl::mqttConnect() {
    Esp8266& esp = input.Wifi->getEsp();
    char cmd[128];

    // Use broker from registration response, fallback to Credentials.hpp
    auto& regSvc = reg::RegistrationServiceImpl::getInstance();
    const char* broker = MQTT_BROKER;
    uint16_t port = MQTT_PORT;
    if (regSvc.isRegistered()) {
        broker = regSvc.credentials().mqttBroker;
        port = regSvc.credentials().mqttPort;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%u,1",
             broker, port);
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

        // Acquire ESP8266 (blocks if upload is using it)
        esp.requestAccess(Esp8266::User::Mqtt);

        // --- Phase 1: WiFi ---
        LOG_I(ats::ErrorSource::Wifi, 0x0001);  // WiFi connecting
        while (mRunning) {
            if (wifi->resetAndConnect()) break;
            LOG_W(ats::ErrorSource::Wifi, 0x0002);  // WiFi retry
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        if (!mRunning) { esp.releaseAccess(); break; }

        // --- Phase 2: NTP ---
        wifi->syncNtp();

        // --- Phase 2.5: Timezone auto-detect ---
        {
            auto& clock = SystemClock::getInstance();
            auto* wifiImpl = static_cast<wifi::WifiServiceImpl*>(wifi);
            auto& tzStorage = static_cast<atsstorage::AtsStorageServiceImpl&>(
                atsstorage::AtsStorageServiceImpl::getInstance());

            int16_t currentTz = clock.tzOffsetMin();
            int16_t detectedTz = 0;
            if (wifiImpl->detectTimezone(detectedTz)) {
                if (currentTz != detectedTz) {
                    clock.setTzOffset(detectedTz);
                    tzStorage.saveTzConfig(detectedTz, 1);
                    LOG_I(ats::ErrorSource::System, 0x0031,
                          (uint32_t)(uint16_t)detectedTz);
                }
            }
        }

        // --- Phase 2.7: Device registration (TOFU) ---
        {
            auto& regSvc = reg::RegistrationServiceImpl::getInstance();
            if (!regSvc.isRegistered()) {
                if (regSvc.doRegistration()) {
                    LOG_I(ats::ErrorSource::System, 0x0080);  // registration OK
                } else {
                    LOG_W(ats::ErrorSource::System, 0x0081);  // registration failed
                }
            }
            // Update per-device topics if registered
            if (regSvc.isRegistered()) {
                const char* prefix = regSvc.credentials().topicPrefix;
                snprintf(sTopicSensor, sizeof(sTopicSensor), "%s/sensor", prefix);
                snprintf(sTopicCmd, sizeof(sTopicCmd), "%s/cmd", prefix);
                snprintf(sTopicRsp, sizeof(sTopicRsp), "%s/rsp", prefix);
            }
        }

        // ESP8266 lock: upload requested by IoServiceImpl → run upload here (has stack)
        if (esp.isAccessRequested()) {
            HttpUploadServiceImpl::uploadPendingFiles(esp);
            esp.clearRequest();  // done
            // Fall through to MQTT connect
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
        uint32_t lastSuccessTick = xTaskGetTickCount();
        static const uint32_t ESP_WATCHDOG_MS = 30000;  // 30s no publish → hard reset

        while (mRunning && mMqttConnected) {
            // Publish sensor data
            if (mSensorPending) {
                mSensorPending = false;
                if (publishSensorData(&mPendingSensor)) {
                    lastSuccessTick = xTaskGetTickCount();
                } else {
                    if (!isMqttConnected()) {
                        mMqttConnected = false;
                        break;
                    }
                }
            }

            // ESP8266 lock: yield MQTT, reset ESP8266, upload, reconnect
            if (esp.isAccessRequested()) {
                mqttDisconnect();
                mMqttConnected = false;
                if (wifi->resetAndConnect()) {
                    HttpUploadServiceImpl::uploadPendingFiles(esp);
                    esp.clearRequest();  // only clear after upload ran
                }
                break;  // → outer loop (Phase 2 retries if request still set)
            }

            // ESP8266 watchdog: hard reset if no successful publish for 30s
            if ((xTaskGetTickCount() - lastSuccessTick) > pdMS_TO_TICKS(ESP_WATCHDOG_MS)) {
                LOG_W(ats::ErrorSource::Wifi, 0x00F0);  // ESP watchdog triggered
                mMqttConnected = false;
                break;  // → outer loop → resetAndConnect() → hardware reset
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
        esp.releaseAccess();  // release ESP8266 for outer loop re-acquire
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
    // Parse: skip to second comma, read length, skip third comma → payload
    const char* p = buf;
    int commas = 0;
    // Skip to second comma (after topic)
    while (p < buf + bufLen && commas < 2) {
        if (*p == ',') commas++;
        p++;
    }
    if (commas < 2) return;

    // Parse length field (decimal digits before third comma)
    uint16_t declaredLen = 0;
    while (p < buf + bufLen && *p >= '0' && *p <= '9') {
        declaredLen = declaredLen * 10 + (*p - '0');
        p++;
    }
    if (p >= buf + bufLen || *p != ',') return;
    p++;  // skip third comma

    // Use declared length (not buffer arithmetic) to avoid trailing \r\n
    uint16_t payloadLen = declaredLen;
    uint16_t available = bufLen - (uint16_t)(p - buf);
    if (payloadLen > available) payloadLen = available;


    // Safe eject command
    if (payloadLen == 5 && memcmp(p, "eject", 5) == 0) {
        LOG_I(ats::ErrorSource::Mqtt, 0x0020);  // eject command received
        ats_safe_eject();
        return;
    }

#ifdef ARCANA_ECDH_ENABLED
    // Intercept KeyExchange in MQTT task context (needs ~1.5KB stack,
    // bridgeTask only has 2KB with protobuf overhead → not enough).
    // Deframe + decrypt here, check if KeyExchange, handle directly.
    {
        const uint8_t* framePayload = nullptr;
        size_t framePayloadLen = 0;
        uint8_t flags = 0, sid = 0;

        if (FrameCodec::deframe(reinterpret_cast<const uint8_t*>(p), payloadLen,
                                 framePayload, framePayloadLen, flags, sid)) {
            // Try decrypt (PSK only for KE request)
            uint8_t plain[arcana_CmdRequest_size];
            size_t plainLen = 0;
            auto& bridge = CommandBridge::getInstance();

            if (bridge.mEncryptionEnabled &&
                bridge.mCrypto.decrypt(framePayload, framePayloadLen,
                                        plain, sizeof(plain), plainLen)) {
                arcana_CmdRequest msg = arcana_CmdRequest_init_zero;
                pb_istream_t stream = pb_istream_from_buffer(plain, plainLen);
                if (pb_decode(&stream, arcana_CmdRequest_fields, &msg) &&
                    msg.cluster == static_cast<uint32_t>(Cluster::Security) &&
                    msg.command == SecurityCommand::KeyExchange &&
                    msg.payload.size == KeyExchangeManager::kPubKeyLen) {

                    LOG_I(ats::ErrorSource::Cmd, 0x0B20);  // KE request received

                    uint8_t serverPub[64], authTag[32];
                    if (bridge.mKeyExchange.performKeyExchange(
                            1 /*MQTT*/, 0, msg.payload.bytes, serverPub, authTag)) {

                        // Build protobuf response
                        arcana_CmdResponse rspMsg = arcana_CmdResponse_init_zero;
                        rspMsg.cluster = static_cast<uint32_t>(Cluster::Security);
                        rspMsg.command = SecurityCommand::KeyExchange;
                        rspMsg.status = 0;  // OK
                        rspMsg.payload.size = 96;
                        memcpy(rspMsg.payload.bytes, serverPub, 64);
                        memcpy(rspMsg.payload.bytes + 64, authTag, 32);

                        uint8_t pbBuf[arcana_CmdResponse_size];
                        pb_ostream_t ostream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
                        if (pb_encode(&ostream, arcana_CmdResponse_fields, &rspMsg)) {
                            // Encrypt with PSK (session not installed yet)
                            uint8_t encBuf[arcana_CmdResponse_size + CryptoEngine::kOverhead];
                            size_t encLen = 0;
                            if (bridge.mCrypto.encrypt(pbBuf, ostream.bytes_written,
                                                        encBuf, sizeof(encBuf), encLen)) {
                                uint8_t frameBuf[TxItem::MAX_DATA];
                                size_t frameLen = 0;
                                if (FrameCodec::frame(encBuf, encLen,
                                                       FrameCodec::kFlagFin, sid,
                                                       frameBuf, sizeof(frameBuf), frameLen)) {
                                    // Hex-encode + publish response
                                    char hexBuf[TxItem::MAX_DATA * 2 + 1];
                                    for (size_t i = 0; i < frameLen && i < TxItem::MAX_DATA; i++) {
                                        static const char hex[] = "0123456789ABCDEF";
                                        hexBuf[i * 2]     = hex[frameBuf[i] >> 4];
                                        hexBuf[i * 2 + 1] = hex[frameBuf[i] & 0x0F];
                                    }
                                    hexBuf[frameLen * 2] = '\0';
                                    mqttPublish(TOPIC_RSP, hexBuf, 0);

                                    // Install session AFTER response sent
                                    bridge.mKeyExchange.installPendingSession(1, 0);
                                    LOG_I(ats::ErrorSource::Cmd, 0x0B21);  // KE success
                                }
                            }
                        }
                    } else {
                        LOG_W(ats::ErrorSource::Cmd, 0x0B22);  // KE failed
                    }
                    return;  // Don't submit to CommandBridge
                }
            }
        }
    }
#endif

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
