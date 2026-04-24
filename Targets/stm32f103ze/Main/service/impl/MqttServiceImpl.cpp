#include "stm32f1xx_hal.h"
#include "MqttServiceImpl.hpp"
#include "MqttPacket.hpp"
#include "FrameCodec.hpp"
#include "ChaCha20.hpp"
#include "DeviceKey.hpp"
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
#include "EventCodes.hpp"
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
static char sTopicSensor[48] = "/arcana/sensor";  // fallback (prefix up to 35 + "/sensor")
static char sTopicCmd[48]    = "/arcana/cmd";
static char sTopicRsp[48]    = "/arcana/rsp";
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
    , mNextPacketId(1)
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

// --- TCP SSL + raw MQTT 3.1.1 ---

bool MqttServiceImpl::sslConnect() {
    Esp8266& esp = input.Wifi->getEsp();

    auto& regSvc = reg::RegistrationServiceImpl::getInstance();
    const char* broker = MQTT_BROKER;
    uint16_t port = MQTT_PORT;
    if (regSvc.isRegistered()) {
        broker = regSvc.credentials().mqttBroker;
        port = regSvc.credentials().mqttPort;
    }

    char cmd[80];
    snprintf(cmd, sizeof(cmd),
             "AT+CIPSTART=\"SSL\",\"%s\",%u", broker, port);
    LOG_I(ats::ErrorSource::Mqtt, evt::MQTT_SSL_START);
    bool ok = esp.sendCmd(cmd, "CONNECT", 20000);
    LOG_I(ats::ErrorSource::Mqtt, evt::MQTT_SSL_RESULT, (uint32_t)ok);
    return ok;
}

void MqttServiceImpl::sslClose() {
    Esp8266& esp = input.Wifi->getEsp();
    esp.sendCmd("AT+CIPCLOSE", "OK", 2000);
}

bool MqttServiceImpl::sendMqttPacket(const uint8_t* pkt, uint16_t len) {
    Esp8266& esp = input.Wifi->getEsp();
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);
    if (!esp.sendCmd(cmd, ">", 5000)) return false;
    esp.sendData(pkt, len, 2000);
    return esp.waitFor("SEND OK", 5000);
}

bool MqttServiceImpl::waitMqttPacket(uint8_t* buf, uint16_t& len,
                                      uint32_t timeoutMs) {
    Esp8266& esp = input.Wifi->getEsp();
    uint32_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeoutMs)) {
        if (esp.hasMqttMsg()) {
            const uint8_t* data;
            uint16_t dataLen;
            if (MqttPacket::parseIpd(esp.getMqttMsg(), esp.getMqttMsgLen(),
                                      data, dataLen)) {
                uint16_t n = dataLen < len ? dataLen : len;
                memcpy(buf, data, n);
                len = n;
                esp.clearMqttMsg();
                return true;
            }
            esp.clearMqttMsg();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

bool MqttServiceImpl::mqttHandshake() {
    auto& regSvc = reg::RegistrationServiceImpl::getInstance();
    const char* user = "arcana";
    const char* pass = "arcana";
    if (regSvc.isRegistered()) {
        user = regSvc.credentials().mqttUser;
        pass = regSvc.credentials().mqttPass;
    }

    uint8_t pkt[128];
    uint16_t len = MqttPacket::buildConnect(pkt, sizeof(pkt),
                                             MQTT_CLIENT_ID, user, pass,
                                             KEEPALIVE_SEC);
    LOG_I(ats::ErrorSource::Mqtt, evt::MQTT_CONNECT_START);
    if (!sendMqttPacket(pkt, len)) {
        LOG_E(ats::ErrorSource::Mqtt, evt::MQTT_CONNECT_SEND_FAIL);
        return false;
    }

    // Wait CONNACK via +IPD
    uint8_t resp[16];
    uint16_t respLen = sizeof(resp);
    if (!waitMqttPacket(resp, respLen, 10000)) {
        LOG_E(ats::ErrorSource::Mqtt, evt::MQTT_NO_CONNACK);
        return false;
    }

    int rc = MqttPacket::parseConnack(resp, respLen);
    LOG_I(ats::ErrorSource::Mqtt, evt::MQTT_CONNACK, (uint32_t)rc);
    return rc == 0;
}

bool MqttServiceImpl::mqttSubscribeRaw(const char* topic, uint8_t qos) {
    uint8_t pkt[80];
    uint16_t pid = mNextPacketId++;
    uint16_t len = MqttPacket::buildSubscribe(pkt, sizeof(pkt), pid, topic, qos);
    if (!sendMqttPacket(pkt, len)) return false;

    // Wait SUBACK
    uint8_t resp[8];
    uint16_t respLen = sizeof(resp);
    if (!waitMqttPacket(resp, respLen, 5000)) return false;
    int rc = MqttPacket::parseSuback(resp, respLen);
    LOG_I(ats::ErrorSource::Mqtt, evt::MQTT_SUB_RESULT, (uint32_t)rc);
    return rc != -1 && rc != 0x80;
}

bool MqttServiceImpl::mqttPublishRaw(const char* topic, const char* payload) {
    return mqttPublishBin(topic, (const uint8_t*)payload, (uint16_t)strlen(payload));
}

bool MqttServiceImpl::mqttPublishBin(const char* topic, const uint8_t* data, uint16_t dataLen) {
    Esp8266& esp = input.Wifi->getEsp();
    uint16_t pid = mNextPacketId++;
    uint8_t pkt[192];
    uint16_t len = MqttPacket::buildPublish(pkt, sizeof(pkt), topic, data, dataLen,
                                             1, pid);  // QoS 1
    if (len > sizeof(pkt)) return false;
    if (!sendMqttPacket(pkt, len)) return false;

    // Wait for PUBACK (QoS 1 delivery confirmation)
    uint32_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(5000)) {
        if (esp.hasMqttMsg()) {
            const uint8_t* mqttData;
            uint16_t mqttDataLen;
            if (MqttPacket::parseIpd(esp.getMqttMsg(), esp.getMqttMsgLen(),
                                      mqttData, mqttDataLen)) {
                uint8_t pktType = MqttPacket::packetType(mqttData[0]);
                if (pktType == MqttPacket::PUBACK) {
                    uint16_t ackId = MqttPacket::parsePuback(mqttData, mqttDataLen);
                    esp.clearMqttMsg();
                    return ackId == pid;
                }
                if (pktType == MqttPacket::PINGRESP) {
                    esp.clearMqttMsg();
                    continue;  // ignore, keep waiting for PUBACK
                }
                // Other packet (server PUBLISH) — leave for main loop
                break;
            }
            esp.clearMqttMsg();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;  // PUBACK timeout
}

bool MqttServiceImpl::mqttDisconnectRaw() {
    uint8_t pkt[2];
    MqttPacket::buildDisconnect(pkt);
    return sendMqttPacket(pkt, 2);
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

        // ESP8266 lock: upload requested by IoServiceImpl → run upload here
        if (esp.isAccessRequested()) {
            HttpUploadServiceImpl::uploadPendingFiles(esp);
            esp.clearRequest();
        }

        // --- Phase 3: TCP SSL + MQTT CONNECT ---
        LOG_I(ats::ErrorSource::Mqtt, 0x0001);  // MQTT connecting
        esp.clearMqttMsg();  // flush stale +IPD from registration

        bool connected = false;
        for (int i = 0; i < 3 && mRunning; i++) {
            esp.clearMqttMsg();
            if (sslConnect() && mqttHandshake()) {
                connected = true;
                break;
            }
            sslClose();
            LOG_W(ats::ErrorSource::Mqtt, 0x0015, (uint32_t)(i + 1));
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        if (!connected) {
            LOG_W(ats::ErrorSource::Mqtt, 0x0005);
            // Credentials might be corrupt — invalidate to force re-register
            auto& regSvc2 = reg::RegistrationServiceImpl::getInstance();
            if (regSvc2.isRegistered()) {
                LOG_W(ats::ErrorSource::Mqtt, evt::MQTT_CREDS_CLEAR);
                regSvc2.invalidate();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp.releaseAccess();
            continue;
        }

        // --- Phase 4: Subscribe ---
        mqttSubscribeRaw(TOPIC_CMD, 0);

        LOG_I(ats::ErrorSource::Mqtt, 0x0002);  // MQTT connected
        mMqttConnected = true;
        CommandBridge::getInstance().setMqttSend(mqttSendFn, this);
        mConnModel.connected = true;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        // Syslog UDP disabled — CIPMUX=0, TCP SSL already using the link.
        // TODO: enable CIPMUX=1 for simultaneous TCP SSL + UDP syslog.

        uint32_t lastNtpTick  = xTaskGetTickCount();
        uint32_t lastPingTick = xTaskGetTickCount();

        // --- Phase 5: Main loop ---
        uint32_t lastSuccessTick = xTaskGetTickCount();
        static const uint32_t ESP_WATCHDOG_MS = 30000;
        static const uint32_t PING_INTERVAL_MS = (KEEPALIVE_SEC * 750);  // 75%

        while (mRunning && mMqttConnected) {
            // Publish sensor data
            if (mSensorPending) {
                mSensorPending = false;
                if (publishSensorData(&mPendingSensor)) {
                    lastSuccessTick = xTaskGetTickCount();
                } else {
                    // Publish failed → assume TCP broken
                    mMqttConnected = false;
                    break;
                }
            }

            // ESP8266 lock: yield MQTT, reset ESP8266, upload, reconnect
            if (esp.isAccessRequested()) {
                mqttDisconnectRaw();
                sslClose();
                mMqttConnected = false;
                if (wifi->resetAndConnect()) {
                    HttpUploadServiceImpl::uploadPendingFiles(esp);
                    esp.clearRequest();
                }
                break;
            }

            // ESP8266 watchdog
            if ((xTaskGetTickCount() - lastSuccessTick) > pdMS_TO_TICKS(ESP_WATCHDOG_MS)) {
                LOG_W(ats::ErrorSource::Wifi, 0x00F0);
                mMqttConnected = false;
                break;
            }

            // PINGREQ keepalive
            if ((xTaskGetTickCount() - lastPingTick) > pdMS_TO_TICKS(PING_INTERVAL_MS)) {
                uint8_t ping[2];
                MqttPacket::buildPingreq(ping);
                if (sendMqttPacket(ping, 2)) {
                    lastPingTick = xTaskGetTickCount();
                } else {
                    mMqttConnected = false;
                    break;
                }
            }

            // NTP resync every 6 hours
            if ((xTaskGetTickCount() - lastNtpTick) >
                pdMS_TO_TICKS(6UL * 3600UL * 1000UL)) {
                // NTP requires its own AT+CIPSTART — skip while MQTT connected.
                // Will resync on next reconnect cycle.
                lastNtpTick = xTaskGetTickCount();
            }

            // Incoming MQTT packets via +IPD → mMqttBuf
            if (esp.hasMqttMsg()) {
                LOG_I(ats::ErrorSource::Mqtt, 0x0030, (uint32_t)esp.getMqttMsgLen());
                processIncomingMqtt();
                esp.clearMqttMsg();
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // --- Phase 6: Disconnected ---
        mqttDisconnectRaw();
        sslClose();
        mMqttConnected = false;
        esp.releaseAccess();
        mConnModel.connected = false;
        mConnModel.updateTimestamp();
        mConnObs.publish(&mConnModel);

        LOG_W(ats::ErrorSource::Mqtt, 0x0003);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// --- Protobuf manual encoder (SensorData, 6 varint fields) ---
// Proto: sint32 temp_x10=1, sint32 ax=2, sint32 ay=3, sint32 az=4, uint32 als=5, uint32 ps=6

static const uint8_t SID_SENSOR = 0x20;

static uint16_t pbVarint(uint8_t* buf, uint32_t val) {
    uint16_t n = 0;
    while (val > 0x7F) { buf[n++] = (val & 0x7F) | 0x80; val >>= 7; }
    buf[n++] = val & 0x7F;
    return n;
}

static uint16_t pbSint32(uint8_t* buf, uint8_t fieldNum, int32_t val) {
    buf[0] = (fieldNum << 3);  // wire type 0
    uint32_t zz = (uint32_t)((val << 1) ^ (val >> 31));  // zigzag
    return 1 + pbVarint(buf + 1, zz);
}

static uint16_t pbUint32(uint8_t* buf, uint8_t fieldNum, uint32_t val) {
    buf[0] = (fieldNum << 3);
    return 1 + pbVarint(buf + 1, val);
}

// --- Publish sensor data ---

bool MqttServiceImpl::publishSensorData(SensorDataModel* model) {
#ifdef MQTT_SENSOR_PLAINTEXT
    // Debug: plain JSON (human-readable, unencrypted)
    char payload[192];
    int tInt = (int)model->temperature;
    int tFrac = (int)(model->temperature * 10) % 10;
    if (tFrac < 0) tFrac = -tFrac;
    snprintf(payload, sizeof(payload),
             "{\"t\":%d.%d,\"ax\":%d,\"ay\":%d,\"az\":%d,\"als\":%u,\"ps\":%u}",
             tInt, tFrac,
             (int)model->accelX, (int)model->accelY, (int)model->accelZ,
             mLatestLight.ambientLight, mLatestLight.proximity);
    return mqttPublishRaw(TOPIC_SENSOR, payload);
#else
    // Production: FrameCodec + ChaCha20 encrypted protobuf
    // Use comm_key (from registration ECDH) if available, else fall back to device_key
    auto& regSvc = reg::RegistrationServiceImpl::getInstance();
    const uint8_t* key = regSvc.getCommKey();

    // 1. Encode protobuf (~20-30 bytes)
    uint8_t pb[40];
    uint16_t pos = 0;
    pos += pbSint32(pb + pos, 1, (int32_t)(model->temperature * 10));
    pos += pbSint32(pb + pos, 2, (int32_t)model->accelX);
    pos += pbSint32(pb + pos, 3, (int32_t)model->accelY);
    pos += pbSint32(pb + pos, 4, (int32_t)model->accelZ);
    pos += pbUint32(pb + pos, 5, mLatestLight.ambientLight);
    pos += pbUint32(pb + pos, 6, mLatestLight.proximity);

    // 2. Nonce: [tick:4][0:8] — unique at 1Hz (tick increments each ms)
    uint8_t nonce[12] = {};
    uint32_t tick = xTaskGetTickCount();
    memcpy(nonce, &tick, 4);

    // 3. Encrypt in-place
    crypto::ChaCha20::crypt(key, nonce, 0, pb, pos);

    // 4. Build payload: [nonce:12][encrypted_pb:N]
    uint8_t enc[52];
    memcpy(enc, nonce, 12);
    memcpy(enc + 12, pb, pos);

    // 5. FrameCodec wrap
    uint8_t frame[72];
    size_t frameLen;
    if (!FrameCodec::frame(enc, 12 + pos, FrameCodec::kFlagFin, SID_SENSOR,
                            frame, sizeof(frame), frameLen)) return false;

    // 6. Publish binary
    return mqttPublishBin(TOPIC_SENSOR, frame, (uint16_t)frameLen);
#endif
}

// --- Process incoming +IPD (raw MQTT packets) ---

void MqttServiceImpl::processIncomingMqtt() {
    Esp8266& esp = input.Wifi->getEsp();

    // Parse +IPD to get raw MQTT bytes
    const uint8_t* mqttData;
    uint16_t mqttDataLen;
    if (!MqttPacket::parseIpd(esp.getMqttMsg(), esp.getMqttMsgLen(),
                               mqttData, mqttDataLen)) return;

    uint8_t pktType = MqttPacket::packetType(mqttData[0]);

    // PINGRESP — just acknowledge
    if (pktType == MqttPacket::PINGRESP) return;

    // Only handle PUBLISH
    if (pktType != MqttPacket::PUBLISH) return;

    const char* topic;
    uint16_t topicLen;
    const uint8_t* payload;
    uint16_t payloadLen;
    uint16_t packetId;
    if (!MqttPacket::parsePublish(mqttData, mqttDataLen,
                                   topic, topicLen, payload, payloadLen,
                                   packetId)) return;

    // QoS 1 → PUBACK
    if (packetId > 0) {
        uint8_t ack[4];
        MqttPacket::buildPuback(ack, packetId);
        sendMqttPacket(ack, 4);
    }

    const char* p = (const char*)payload;

    // Safe eject command
    if (payloadLen == 5 && memcmp(p, "eject", 5) == 0) {
        LOG_I(ats::ErrorSource::Mqtt, 0x0020);
        ats_safe_eject();
        return;
    }

#ifdef ARCANA_ECDH_ENABLED
    // Intercept KeyExchange in MQTT task context
    {
        const uint8_t* framePayload = nullptr;
        size_t framePayloadLen = 0;
        uint8_t flags = 0, sid = 0;

        if (FrameCodec::deframe(reinterpret_cast<const uint8_t*>(p), payloadLen,
                                 framePayload, framePayloadLen, flags, sid)) {
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

                    LOG_I(ats::ErrorSource::Cmd, 0x0B20);

                    uint8_t serverPub[64], authTag[32];
                    if (bridge.mKeyExchange.performKeyExchange(
                            1 /*MQTT*/, 0, msg.payload.bytes, serverPub, authTag)) {

                        arcana_CmdResponse rspMsg = arcana_CmdResponse_init_zero;
                        rspMsg.cluster = static_cast<uint32_t>(Cluster::Security);
                        rspMsg.command = SecurityCommand::KeyExchange;
                        rspMsg.status = 0;
                        rspMsg.payload.size = 96;
                        memcpy(rspMsg.payload.bytes, serverPub, 64);
                        memcpy(rspMsg.payload.bytes + 64, authTag, 32);

                        uint8_t pbBuf[arcana_CmdResponse_size];
                        pb_ostream_t ostream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
                        if (pb_encode(&ostream, arcana_CmdResponse_fields, &rspMsg)) {
                            uint8_t encBuf[arcana_CmdResponse_size + CryptoEngine::kOverhead];
                            size_t encLen = 0;
                            if (bridge.mCrypto.encrypt(pbBuf, ostream.bytes_written,
                                                        encBuf, sizeof(encBuf), encLen)) {
                                uint8_t frameBuf[TxItem::MAX_DATA];
                                size_t frameLen = 0;
                                if (FrameCodec::frame(encBuf, encLen,
                                                       FrameCodec::kFlagFin, sid,
                                                       frameBuf, sizeof(frameBuf), frameLen)) {
                                    char hexBuf[TxItem::MAX_DATA * 2 + 1];
                                    for (size_t i = 0; i < frameLen && i < TxItem::MAX_DATA; i++) {
                                        static const char hex[] = "0123456789ABCDEF";
                                        hexBuf[i * 2]     = hex[frameBuf[i] >> 4];
                                        hexBuf[i * 2 + 1] = hex[frameBuf[i] & 0x0F];
                                    }
                                    hexBuf[frameLen * 2] = '\0';
                                    mqttPublishRaw(TOPIC_RSP, hexBuf);

                                    bridge.mKeyExchange.installPendingSession(1, 0);
                                    LOG_I(ats::ErrorSource::Cmd, 0x0B21);
                                }
                            }
                        }
                    } else {
                        LOG_W(ats::ErrorSource::Cmd, 0x0B22);
                    }
                    return;
                }
            }
        }
    }
#endif

    // Submit to CommandBridge frame queue
    if (payloadLen > 0 && payloadLen <= CmdFrameItem::MAX_DATA) {
        CommandBridge::getInstance().submitFrame(payload, payloadLen,
                                                 CmdFrameItem::MQTT);
    }

    // Also publish via observable
    uint16_t obsLen = payloadLen > MqttCommandModel::MAX_DATA
                      ? MqttCommandModel::MAX_DATA : payloadLen;
    memcpy(mCmdModel.data, p, obsLen);
    mCmdModel.length = (uint8_t)obsLen;
    mCmdModel.updateTimestamp();
    mCmdObs.publish(&mCmdModel);
}

// --- CommandBridge response ---

bool MqttServiceImpl::mqttSendFn(const uint8_t* data, uint16_t len, void* ctx) {
    MqttServiceImpl* self = static_cast<MqttServiceImpl*>(ctx);
    if (!self->mMqttConnected) return false;

    char hexPayload[130];
    uint16_t hLen = len > 64 ? 64 : len;
    for (uint16_t i = 0; i < hLen; i++) {
        static const char hex[] = "0123456789ABCDEF";
        hexPayload[i * 2]     = hex[data[i] >> 4];
        hexPayload[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    hexPayload[hLen * 2] = '\0';

    return self->mqttPublishRaw(TOPIC_RSP, hexPayload);
}

} // namespace mqtt
} // namespace arcana
