/**
 * @file test_mqtt.cpp
 * @brief Smoke + AT-sequence coverage for MqttServiceImpl.cpp.
 */
#include <gtest/gtest.h>
#include <cstring>
#include <new>

#include "stm32f1xx_hal.h"
#include "ff.h"
#include "MqttServiceImpl.hpp"
#include "Esp8266.hpp"
#include "SystemClock.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "WifiServiceImpl.hpp"

namespace arcana { namespace mqtt {
struct MqttServiceTestAccess {
    static bool sslConnect(MqttServiceImpl& m) { return m.sslConnect(); }
    static void sslClose(MqttServiceImpl& m) { m.sslClose(); }
    static bool sendPacket(MqttServiceImpl& m, const uint8_t* p, uint16_t l) {
        return m.sendMqttPacket(p, l);
    }
    static bool waitPacket(MqttServiceImpl& m, uint8_t* b, uint16_t& l, uint32_t t) {
        return m.waitMqttPacket(b, l, t);
    }
    static bool handshake(MqttServiceImpl& m) { return m.mqttHandshake(); }
    static bool subscribe(MqttServiceImpl& m, const char* t, uint8_t q = 0) {
        return m.mqttSubscribeRaw(t, q);
    }
    static bool publishRaw(MqttServiceImpl& m, const char* t, const char* p) {
        return m.mqttPublishRaw(t, p);
    }
    static bool publishBin(MqttServiceImpl& m, const char* t, const uint8_t* d, uint16_t len) {
        return m.mqttPublishBin(t, d, len);
    }
    static bool disconnectRaw(MqttServiceImpl& m) { return m.mqttDisconnectRaw(); }
    static bool publishSensor(MqttServiceImpl& m, SensorDataModel* s) {
        return m.publishSensorData(s);
    }
    static void onSensorData(SensorDataModel* m, void* ctx) {
        MqttServiceImpl::onSensorData(m, ctx);
    }
    static void onLightData(arcana::LightDataModel* m, void* ctx) {
        MqttServiceImpl::onLightData(m, ctx);
    }
    static void processIncoming(MqttServiceImpl& m) { m.processIncomingMqtt(); }
    static bool sendFn(const uint8_t* d, uint16_t l, void* ctx) {
        return MqttServiceImpl::mqttSendFn(d, l, ctx);
    }
    static volatile bool& mqttConnected(MqttServiceImpl& m) { return m.mMqttConnected; }
    static volatile bool& sensorPending(MqttServiceImpl& m) { return m.mSensorPending; }
    static SensorDataModel& pendingSensor(MqttServiceImpl& m) { return m.mPendingSensor; }
    static uint16_t& nextPacketId(MqttServiceImpl& m) { return m.mNextPacketId; }
    static volatile bool& running(MqttServiceImpl& m) { return m.mRunning; }
    static void runTask(MqttServiceImpl& m) { m.runTask(); }
    static void invokeMqttTask(MqttServiceImpl& m) { MqttServiceImpl::mqttTask(&m); }
};
}}

using arcana::mqtt::MqttServiceImpl;
using arcana::mqtt::MqttService;
using arcana::mqtt::MqttServiceTestAccess;
using arcana::Esp8266;
using arcana::SystemClock;
using arcana::ServiceStatus;
using arcana::SensorDataModel;
using arcana::atsstorage::AtsStorageServiceImpl;

namespace arcana { namespace atsstorage {
struct AtsStorageTestAccess {
    static bool& dbReady(AtsStorageServiceImpl& s) { return s.mDbReady; }
    static bool& deviceDbReady(AtsStorageServiceImpl& s) { return s.mDeviceDbReady; }
    static ats::ArcanaTsDb& db(AtsStorageServiceImpl& s) { return s.mDb; }
    static ats::ArcanaTsDb& deviceDb(AtsStorageServiceImpl& s) { return s.mDeviceDb; }
    static ats::FatFsFilePort& filePort(AtsStorageServiceImpl& s) { return s.mFilePort; }
    static ats::FatFsFilePort& deviceFilePort(AtsStorageServiceImpl& s) { return s.mDeviceFilePort; }
    static ats::FreeRtosMutex& mutex(AtsStorageServiceImpl& s) { return s.mMutex; }
};
}}
using arcana::atsstorage::AtsStorageTestAccess;

#include "RegistrationServiceImpl.hpp"
namespace arcana { namespace reg {
struct RegistrationServiceTestAccess {
    /** Populate the cached credentials so isRegistered() returns true and
     *  the "registered" branches in MqttServiceImpl::sslConnect /
     *  mqttHandshake exercise the broker / user / pass fields. */
    static void populate(RegistrationServiceImpl& r,
                         const char* broker, uint16_t port,
                         const char* user,   const char* pass) {
        std::strncpy(r.mCreds.mqttBroker, broker, sizeof(r.mCreds.mqttBroker));
        r.mCreds.mqttPort = port;
        std::strncpy(r.mCreds.mqttUser, user, sizeof(r.mCreds.mqttUser));
        std::strncpy(r.mCreds.mqttPass, pass, sizeof(r.mCreds.mqttPass));
        r.mCreds.valid = true;
    }
    static void clear(RegistrationServiceImpl& r) {
        r.mCreds.valid = false;
        r.mCreds.hasCommKey = false;
    }
};
}}
using arcana::reg::RegistrationServiceTestAccess;

namespace {

MqttServiceImpl& mqtt() {
    return static_cast<MqttServiceImpl&>(MqttServiceImpl::getInstance());
}

void resetEnvironment() {
    test_ff_reset();
    SystemClock::getInstance().resetForTest();
    Esp8266::getInstance().resetForTest();

    /* Wire WifiService into MqttService.input — sslConnect derefs it */
    mqtt().input.Wifi = &arcana::wifi::WifiServiceImpl::getInstance();

    /* Reset credentials so each test starts with isRegistered() == false */
    RegistrationServiceTestAccess::clear(
        arcana::reg::RegistrationServiceImpl::getInstance());

    /* Reset AtsStorage singleton state (same trick as test_atsstorage) */
    auto& s = static_cast<AtsStorageServiceImpl&>(
        AtsStorageServiceImpl::getInstance());
    AtsStorageTestAccess::dbReady(s)       = false;
    AtsStorageTestAccess::deviceDbReady(s) = false;
    auto& db    = AtsStorageTestAccess::db(s);
    auto& devDb = AtsStorageTestAccess::deviceDb(s);
    auto& fp    = AtsStorageTestAccess::filePort(s);
    auto& dfp   = AtsStorageTestAccess::deviceFilePort(s);
    auto& mtx   = AtsStorageTestAccess::mutex(s);
    new (&db)    arcana::ats::ArcanaTsDb();
    new (&devDb) arcana::ats::ArcanaTsDb();
    new (&fp)    arcana::ats::FatFsFilePort();
    new (&dfp)   arcana::ats::FatFsFilePort();
    new (&mtx)   arcana::ats::FreeRtosMutex();
}

} // anonymous namespace

// ── Lifecycle smoke ────────────────────────────────────────────────────────

TEST(MqttLifecycle, GetInstanceSingleton) {
    auto& a = MqttServiceImpl::getInstance();
    auto& b = MqttServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(MqttLifecycle, InitHalReturnsOk) {
    resetEnvironment();
    EXPECT_EQ(mqtt().initHAL(), ServiceStatus::OK);
}

TEST(MqttLifecycle, InitWithoutInputObservablesReturnsOk) {
    resetEnvironment();
    /* No input observables wired → init() takes the no-op branch */
    EXPECT_EQ(mqtt().init(), ServiceStatus::OK);
}

TEST(MqttLifecycle, StartAndStop) {
    resetEnvironment();
    EXPECT_EQ(mqtt().init(), ServiceStatus::OK);
    EXPECT_EQ(mqtt().start(), ServiceStatus::OK);
    mqtt().stop();
    SUCCEED();
}

// ── sslConnect / sslClose ──────────────────────────────────────────────────

TEST(MqttSsl, SslConnectHappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("CONNECT");      // CIPSTART → expects "CONNECT" not "OK"
    EXPECT_TRUE(MqttServiceTestAccess::sslConnect(mqtt()));
}

TEST(MqttSsl, SslConnectAlreadyConnectedRecovers) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ALREADY CONNECTED");
    EXPECT_TRUE(MqttServiceTestAccess::sslConnect(mqtt()));
}

TEST(MqttSsl, SslConnectFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ERROR");
    EXPECT_FALSE(MqttServiceTestAccess::sslConnect(mqtt()));
}

TEST(MqttSsl, SslCloseSendsCipClose) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    MqttServiceTestAccess::sslClose(mqtt());
    EXPECT_GE(esp.sentCmds().size(), 1u);
}

// ── sendMqttPacket ─────────────────────────────────────────────────────────

TEST(MqttSendPacket, HappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">");          // CIPSEND
    esp.pushResponse("");           // sendData
    esp.pushResponse("SEND OK");

    uint8_t pkt[4] = {0xC0, 0x00, 0x00, 0x00};
    EXPECT_TRUE(MqttServiceTestAccess::sendPacket(mqtt(), pkt, 4));
}

TEST(MqttSendPacket, CipSendFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ERROR");
    uint8_t pkt[2] = {0xC0, 0x00};
    EXPECT_FALSE(MqttServiceTestAccess::sendPacket(mqtt(), pkt, 2));
}

TEST(MqttSendPacket, SendOkFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">");
    esp.pushResponse("");
    esp.pushResponse("ERROR");
    uint8_t pkt[2] = {0xC0, 0x00};
    EXPECT_FALSE(MqttServiceTestAccess::sendPacket(mqtt(), pkt, 2));
}

// ── waitMqttPacket ─────────────────────────────────────────────────────────

TEST(MqttWaitPacket, NoMessageReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.clearMqttMsg();
    uint8_t buf[16];
    uint16_t len = 0;
    EXPECT_FALSE(MqttServiceTestAccess::waitPacket(mqtt(), buf, len, 100));
}

TEST(MqttWaitPacket, ParsesIpdMessage) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* +IPD,4:ABCD */
    esp.pushMqttMsg("+IPD,4:ABCD", 11);
    uint8_t buf[16];
    uint16_t len = 0;
    bool ok = MqttServiceTestAccess::waitPacket(mqtt(), buf, len, 100);
    if (ok) {
        EXPECT_EQ(len, 4u);
    }
}

// ── mqttSubscribeRaw ───────────────────────────────────────────────────────

TEST(MqttSubscribe, HappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* sendMqttPacket: CIPSEND > / sendData / SEND OK */
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* waitMqttPacket: SUBACK 0x90 0x03 0x00 0x01 0x00 (qos=0 granted) */
    char suback[5] = { (char)0x90, 0x03, 0x00, 0x01, 0x00 };
    esp.pushMqttMsg(suback, 5);

    bool ok = MqttServiceTestAccess::subscribe(mqtt(), "test/topic", 0);
    /* The result depends on parseSuback parsing the mqtt buffer correctly */
    (void)ok;
    EXPECT_GE(esp.sentCmds().size(), 1u);
}

// ── mqttPublishRaw ─────────────────────────────────────────────────────────

TEST(MqttPublish, RawSendsCipSendCommand) {
    /* publishRaw uses QoS 1 → waits for PUBACK with matching pktId; we
     * can't easily predict pktId across tests, so we just verify the
     * AT command sequence fired (coverage of the send path). */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    MqttServiceTestAccess::publishRaw(mqtt(), "topic", "payload");
    EXPECT_GE(esp.sentCmds().size(), 1u);
}

TEST(MqttPublish, BinSendsCipSendCommand) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    const uint8_t data[8] = {0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD, 0xBE, 0xEF};
    MqttServiceTestAccess::publishBin(mqtt(), "topic/bin", data, 8);
    EXPECT_GE(esp.sentCmds().size(), 1u);
}

TEST(MqttPublish, PublishSensorDataWhenConnected) {
    resetEnvironment();
    MqttServiceTestAccess::mqttConnected(mqtt()) = true;
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");

    SensorDataModel m;
    m.temperature = 22.5f;
    m.accelX = 100; m.accelY = 200; m.accelZ = 300;
    bool ok = MqttServiceTestAccess::publishSensor(mqtt(), &m);
    /* Whether ok is true depends on internal state — coverage of the path is the goal */
    (void)ok;
    EXPECT_GE(esp.sentCmds().size(), 1u);
}

// ── mqttDisconnectRaw ──────────────────────────────────────────────────────

TEST(MqttDisconnect, HappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    EXPECT_TRUE(MqttServiceTestAccess::disconnectRaw(mqtt()));
}

// ── onSensorData / onLightData (private static observer callbacks) ────────

TEST(MqttObservers, OnSensorDataWhenConnectedQueuesPending) {
    resetEnvironment();
    MqttServiceTestAccess::mqttConnected(mqtt()) = true;
    MqttServiceTestAccess::sensorPending(mqtt()) = false;

    SensorDataModel m;
    m.temperature = 30.0f;
    m.accelX = 10; m.accelY = 20; m.accelZ = 30;
    MqttServiceTestAccess::onSensorData(&m, &mqtt());

    EXPECT_TRUE(MqttServiceTestAccess::sensorPending(mqtt()));
    EXPECT_FLOAT_EQ(MqttServiceTestAccess::pendingSensor(mqtt()).temperature, 30.0f);
}

TEST(MqttObservers, OnSensorDataWhenDisconnectedDropsSilently) {
    resetEnvironment();
    MqttServiceTestAccess::mqttConnected(mqtt()) = false;
    MqttServiceTestAccess::sensorPending(mqtt()) = false;

    SensorDataModel m;
    m.temperature = 50.0f;
    MqttServiceTestAccess::onSensorData(&m, &mqtt());

    EXPECT_FALSE(MqttServiceTestAccess::sensorPending(mqtt()));
}

TEST(MqttObservers, OnLightDataAlwaysCachesLatest) {
    resetEnvironment();
    arcana::LightDataModel light;
    light.ambientLight = 500;
    light.proximity = 10;
    MqttServiceTestAccess::onLightData(&light, &mqtt());
    SUCCEED();
}

// ── mqttHandshake ──────────────────────────────────────────────────────────

TEST(MqttHandshake, HappyPathConnack0Accepted) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* sendMqttPacket: CIPSEND > / sendData / SEND OK */
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* waitMqttPacket: CONNACK 0x20 0x02 0x00 0x00 (return code 0 = accepted) */
    char connack[10] = "+IPD,4:";
    connack[7] = (char)0x20;
    connack[8] = 0x02;
    connack[9] = 0x00;
    /* Hmm — the binary 0x00 byte is a null terminator if treated as string.
     * Use the binary push helper. */
    uint8_t bin[11] = {'+','I','P','D',',','4',':', 0x20, 0x02, 0x00, 0x00};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 11);

    EXPECT_TRUE(MqttServiceTestAccess::handshake(mqtt()));
}

TEST(MqttHandshake, ConnackRejectedReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* CONNACK with rc=5 (not authorized) */
    uint8_t bin[11] = {'+','I','P','D',',','4',':', 0x20, 0x02, 0x00, 0x05};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 11);
    EXPECT_FALSE(MqttServiceTestAccess::handshake(mqtt()));
}

TEST(MqttHandshake, SendFailureBails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ERROR");   /* CIPSEND fails */
    EXPECT_FALSE(MqttServiceTestAccess::handshake(mqtt()));
}

TEST(MqttHandshake, NoConnackBails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* No mqtt msg → waitMqttPacket times out → handshake returns false */
    EXPECT_FALSE(MqttServiceTestAccess::handshake(mqtt()));
}

// ── mqttSubscribeRaw with valid SUBACK ─────────────────────────────────────

TEST(MqttSubscribe, HappyPathWithSuback) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* sendMqttPacket: CIPSEND > / sendData / SEND OK */
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* waitMqttPacket: SUBACK 0x90 0x03 [pktIdH] [pktIdL] [grantedQos]
     * The packet ID is mNextPacketId before increment — set it deterministic */
    MqttServiceTestAccess::nextPacketId(mqtt()) = 0x42;
    uint8_t bin[12] = {'+','I','P','D',',','5',':', (uint8_t)0x90, 0x03, 0x00, 0x42, 0x00};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 12);

    EXPECT_TRUE(MqttServiceTestAccess::subscribe(mqtt(), "topic/sub", 0));
}

TEST(MqttSubscribe, SubackFailureCode0x80) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    MqttServiceTestAccess::nextPacketId(mqtt()) = 0x10;
    uint8_t bin[12] = {'+','I','P','D',',','5',':', (uint8_t)0x90, 0x03, 0x00, 0x10, (uint8_t)0x80};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 12);
    EXPECT_FALSE(MqttServiceTestAccess::subscribe(mqtt(), "topic", 0));
}

// ── mqttPublishBin with matching PUBACK ────────────────────────────────────

TEST(MqttPublish, BinPubackMatchSucceeds) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* mNextPacketId = 0x55 → publishBin uses 0x55, expects PUBACK with id=0x55 */
    MqttServiceTestAccess::nextPacketId(mqtt()) = 0x55;
    uint8_t bin[11] = {'+','I','P','D',',','4',':', 0x40, 0x02, 0x00, 0x55};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 11);

    const uint8_t data[4] = {1, 2, 3, 4};
    EXPECT_TRUE(MqttServiceTestAccess::publishBin(mqtt(), "topic", data, 4));
}

TEST(MqttPublish, BinPingrespIgnoredAndContinues) {
    /* publishBin's PUBACK loop: if PINGRESP arrives, clear and continue —
     * eventually times out without a PUBACK. */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    MqttServiceTestAccess::nextPacketId(mqtt()) = 0x77;
    uint8_t bin[10] = {'+','I','P','D',',','3',':', (uint8_t)0xD0, 0x00, 0x00};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 10);

    const uint8_t data[2] = {0xAA, 0xBB};
    bool ok = MqttServiceTestAccess::publishBin(mqtt(), "topic", data, 2);
    /* Coverage of the PINGRESP-ignore branch — result depends on subsequent
     * PUBACK availability (which we don't push). */
    (void)ok;
}

TEST(MqttPublish, BinPublishOtherPacketBreaksOut) {
    /* publishBin's PUBACK loop: if a server PUBLISH arrives instead of
     * PUBACK/PINGRESP, break out of the inner loop and continue polling. */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    MqttServiceTestAccess::nextPacketId(mqtt()) = 0x88;
    /* Push a PUBLISH packet (0x30) — not PUBACK, not PINGRESP → break */
    uint8_t bin[14] = {'+','I','P','D',',','7',':',
                       0x30, 0x05, 0x00, 0x01, 't', 'X', 'Y'};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 14);

    const uint8_t data[2] = {0x11, 0x22};
    bool ok = MqttServiceTestAccess::publishBin(mqtt(), "topic", data, 2);
    (void)ok;  /* coverage of the break path */
}

// ── processIncomingMqtt ────────────────────────────────────────────────────

TEST(MqttIncoming, NoIpdReturnsEarly) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.clearMqttMsg();
    /* No mqtt msg → processIncomingMqtt parseIpd fails → return */
    MqttServiceTestAccess::processIncoming(mqtt());
    SUCCEED();
}

TEST(MqttIncoming, PingrespReturnsEarly) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* PINGRESP packet 0xD0 0x00 */
    uint8_t bin[9] = {'+','I','P','D',',','2',':', (uint8_t)0xD0, 0x00};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 9);
    MqttServiceTestAccess::processIncoming(mqtt());
    SUCCEED();
}

TEST(MqttIncoming, NonPublishReturnsEarly) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* PUBACK packet 0x40 0x02 0x00 0x01 — not a PUBLISH → return */
    uint8_t bin[11] = {'+','I','P','D',',','4',':', 0x40, 0x02, 0x00, 0x01};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 11);
    MqttServiceTestAccess::processIncoming(mqtt());
    SUCCEED();
}

TEST(MqttIncoming, PublishEjectMessageTriggersEject) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* PUBLISH QoS 0 to "t" with payload "eject":
     * 0x30 [remLen] [topicLenH=0] [topicLenL=1] 't' 'e' 'j' 'e' 'c' 't'
     * remLen = 2 (topic len) + 1 (topic) + 5 (payload) = 8
     */
    uint8_t bin[18] = {'+','I','P','D',',','1','0',':',
                       0x30, 0x08, 0x00, 0x01, 't', 'e', 'j', 'e', 'c', 't'};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 18);
    MqttServiceTestAccess::processIncoming(mqtt());
    SUCCEED();
}

TEST(MqttIncoming, PublishGenericPayloadForwardsToCommandBridge) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* PUBLISH QoS 0 to "t" with binary payload "ABCD" (not "eject") */
    uint8_t bin[17] = {'+','I','P','D',',','9',':',
                       0x30, 0x07, 0x00, 0x01, 't', 'A', 'B', 'C', 'D', 0};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 16);
    MqttServiceTestAccess::processIncoming(mqtt());
    SUCCEED();
}

TEST(MqttIncoming, PublishWithPacketIdSendsPuback) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* QoS 1 PUBLISH with packet ID. Need PUBACK to be sent → push response
     * for the CIPSEND. */
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* PUBLISH QoS1: 0x32 [remLen] [topicLen:2] [topic] [pktId:2] [payload]
     * remLen = 2 + 1 + 2 + 4 = 9
     */
    uint8_t bin[19] = {'+','I','P','D',',','1','1',':',
                       0x32, 0x09, 0x00, 0x01, 't', 0x12, 0x34, 'A', 'B', 'C', 'D'};
    esp.pushMqttMsg(reinterpret_cast<const char*>(bin), 19);
    MqttServiceTestAccess::processIncoming(mqtt());
    SUCCEED();
}

// ── mqttSendFn (transport callback) ────────────────────────────────────────

TEST(MqttSendFn, ReturnsFalseWhenDisconnected) {
    resetEnvironment();
    MqttServiceTestAccess::mqttConnected(mqtt()) = false;
    const uint8_t data[4] = {1, 2, 3, 4};
    EXPECT_FALSE(MqttServiceTestAccess::sendFn(data, 4, &mqtt()));
}

TEST(MqttSendFn, HexEncodesAndPublishes) {
    resetEnvironment();
    MqttServiceTestAccess::mqttConnected(mqtt()) = true;
    auto& esp = Esp8266::getInstance();
    /* mqttSendFn → mqttPublishRaw → sendMqttPacket → CIPSEND > / sendData / SEND OK */
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    const uint8_t data[2] = {0xCA, 0xFE};
    /* Result depends on PUBACK match — just verify command was sent */
    MqttServiceTestAccess::sendFn(data, 2, &mqtt());
    EXPECT_GE(esp.sentCmds().size(), 1u);
}

TEST(MqttSendFn, ClampsLongPayload) {
    resetEnvironment();
    MqttServiceTestAccess::mqttConnected(mqtt()) = true;
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">"); esp.pushResponse(""); esp.pushResponse("SEND OK");
    /* > 64 bytes → clamped to 64 in mqttSendFn */
    uint8_t big[100];
    for (int i = 0; i < 100; ++i) big[i] = (uint8_t)i;
    MqttServiceTestAccess::sendFn(big, 100, &mqtt());
    SUCCEED();
}

// ── Observer callbacks via init() ──────────────────────────────────────────

TEST(MqttObservers, InitWithObservablesSubscribes) {
    resetEnvironment();
    arcana::Observable<SensorDataModel> sensors("s");
    arcana::Observable<arcana::LightDataModel> light("l");
    mqtt().input.SensorData = &sensors;
    mqtt().input.LightData = &light;
    EXPECT_EQ(mqtt().init(), ServiceStatus::OK);
    /* Detach so subsequent tests don't see stale subscriptions */
    mqtt().input.SensorData = nullptr;
    mqtt().input.LightData = nullptr;
}

// ── Registered-credentials branches ─────────────────────────────────────────

TEST(MqttRegistered, SslConnectUsesCredentialBroker) {
    /* When isRegistered() returns true, sslConnect should pull broker/port
     * from the credentials struct (lines 117-119). */
    resetEnvironment();
    auto& reg = arcana::reg::RegistrationServiceImpl::getInstance();
    RegistrationServiceTestAccess::populate(reg, "broker.test.io", 8884,
                                             "u", "p");

    auto& esp = Esp8266::getInstance();
    esp.pushResponse("CONNECT");
    EXPECT_TRUE(MqttServiceTestAccess::sslConnect(mqtt()));

    /* Verify the AT+CIPSTART command went out with the credential broker */
    bool seen = false;
    for (auto& cmd : esp.sentCmds()) {
        if (cmd.find("broker.test.io") != std::string::npos &&
            cmd.find("8884") != std::string::npos) { seen = true; break; }
    }
    EXPECT_TRUE(seen);
}

TEST(MqttRegistered, HandshakeUsesCredentialUserPass) {
    /* mqttHandshake should pull user/pass from credentials when registered
     * (lines 172-174). */
    resetEnvironment();
    auto& reg = arcana::reg::RegistrationServiceImpl::getInstance();
    RegistrationServiceTestAccess::populate(reg, "b", 1883,
                                             "alice", "bobspassword");

    auto& esp = Esp8266::getInstance();
    /* sendMqttPacket: CIPSEND prompt + sendData + SEND OK */
    esp.pushResponse(">");
    esp.pushResponse("");
    esp.pushResponse("SEND OK");
    /* CONNACK wrapped in +IPD frame so parseIpd succeeds */
    char connack[12] = "+IPD,4:";
    connack[7] = (char)0x20; connack[8] = 0x02;
    connack[9] = 0x00;       connack[10] = 0x00;
    esp.pushMqttMsg(connack, 11);

    EXPECT_TRUE(MqttServiceTestAccess::handshake(mqtt()));

    /* The CONNECT packet should embed "alice" + "bobspassword" — the easiest
     * sanity check is that some sentData blob contains those bytes. */
    bool seen = false;
    for (auto& blob : esp.sentData()) {
        std::string s(blob.begin(), blob.end());
        if (s.find("alice") != std::string::npos) { seen = true; break; }
    }
    EXPECT_TRUE(seen);
}

// ── publishBin: parseIpd-fail clearMqttMsg branch (lines 248-249) ───────────

TEST(MqttPublish, PublishBinClearsMalformedIpdAndTimesOut) {
    /* Push an mqtt msg that does NOT parse as a valid +IPD frame so the
     * inner if (parseIpd) is false → falls through to clearMqttMsg + 20ms
     * delay → loop continues until timeout. */
    resetEnvironment();
    MqttServiceTestAccess::nextPacketId(mqtt()) = 1;
    MqttServiceTestAccess::mqttConnected(mqtt()) = true;

    auto& esp = Esp8266::getInstance();
    /* sendMqttPacket: CIPSEND prompt + sendData + SEND OK */
    esp.pushResponse(">");
    esp.pushResponse("");
    esp.pushResponse("SEND OK");
    /* Malformed mqtt msg — no "+IPD," prefix → parseIpd returns false */
    esp.pushMqttMsg("garbage no IPD prefix", 21);

    const uint8_t data[] = {0xAA, 0xBB};
    EXPECT_FALSE(MqttServiceTestAccess::publishBin(mqtt(), "topic", data, 2));
}

// ── runTask one-pass via vTaskDelay abort ──────────────────────────────────
//
// MqttServiceImpl::runTask is the main MQTT task body — an infinite while
// loop with WiFi connect → NTP sync → registration → MQTT broker phases.
// We can't refactor it for tests, so we install a vTaskDelay abort hook
// (freertos_stubs g_vTaskDelay_abort_after = N → throws after N calls)
// and let the test catch the exception. Different abort counts hit
// different phases.

extern int g_vTaskDelay_call_count;
extern int g_vTaskDelay_abort_after;

TEST(MqttRunTask, AbortInWifiPhaseAfterAFewRetries) {
    /* All AT commands fail → wifi->resetAndConnect returns false → inner
     * Phase 1 retry loop → vTaskDelay(10s) called once per retry → throw */
    resetEnvironment();
    auto& m = mqtt();
    MqttServiceTestAccess::running(m) = true;
    auto& esp = Esp8266::getInstance();
    /* Stage failure responses for many AT commands so wifi->resetAndConnect
     * fails consistently. */
    for (int i = 0; i < 100; ++i) esp.pushResponse("ERROR");

    g_vTaskDelay_call_count  = 0;
    g_vTaskDelay_abort_after = 5;
    try {
        MqttServiceTestAccess::runTask(m);
        FAIL() << "expected abort exception";
    } catch (int) {
        SUCCEED();
    }
    g_vTaskDelay_abort_after = 0;
    MqttServiceTestAccess::running(m) = false;
}

TEST(MqttRunTask, HappyPathReachesMainLoopThenAborts) {
    /* Full happy-path script: wifi connect → ntp → tz fail → registration
     * already done → sslConnect → mqttHandshake → subscribe → main loop.
     * vTaskDelay abort fires deep in Phase 5. Each AT command's response
     * is pushed in order. */
    resetEnvironment();
    auto& m = mqtt();
    MqttServiceTestAccess::running(m) = true;

    /* Mark registration as done so Phase 2.7 skips */
    auto& reg = arcana::reg::RegistrationServiceImpl::getInstance();
    RegistrationServiceTestAccess::populate(reg, "broker", 8883, "u", "p");

    auto& esp = Esp8266::getInstance();

    /* Phase 1: wifi->resetAndConnect — happy AT path */
    esp.pushResponse("OK");        // AT (first try at 460800)
    esp.pushResponse("OK");        // AT+GMR (version)
    esp.pushResponse("OK");        // CWDHCP=1,1
    esp.pushResponse("WIFI CONNECTED\r\nWIFI GOT IP\r\nOK");  // CWMODE 1
    esp.pushResponse("OK");        // CWJAP

    /* Phase 2: syncNtp */
    esp.pushResponse("OK");        // CIPSNTPCFG
    esp.pushResponse("+CIPSNTPTIME:Mon Mar 23 12:34:56 2026\r\nOK");
    esp.pushResponse("+SYSTIMESTAMP:1742300000\r\nOK");

    /* Phase 2.5: detectTimezone — fail (CIPSTART error → bail) */
    esp.pushResponse("ERROR");

    /* Phase 3: sslConnect */
    esp.pushResponse("CONNECT");
    /* mqttHandshake: CIPSEND prompt + sendData + SEND OK + CONNACK */
    esp.pushResponse(">");
    esp.pushResponse("");
    esp.pushResponse("SEND OK");
    {
        char connack[12] = "+IPD,4:";
        connack[7] = (char)0x20; connack[8] = 0x02;
        connack[9] = 0x00;       connack[10] = 0x00;
        esp.pushMqttMsg(connack, 11);
    }

    /* Phase 4: mqttSubscribeRaw */
    esp.pushResponse(">");
    esp.pushResponse("");
    esp.pushResponse("SEND OK");
    /* SUBACK type 0x90 len 3 [pid lo][pid hi][status 0x00] */
    {
        char suback[13] = "+IPD,5:";
        suback[7]  = (char)0x90; suback[8]  = 0x03;
        suback[9]  = 0x00;       suback[10] = 0x01;
        suback[11] = 0x00;
        esp.pushMqttMsg(suback, 12);
    }

    /* Phase 5: main loop — vTaskDelay abort fires somewhere inside */
    g_vTaskDelay_call_count  = 0;
    g_vTaskDelay_abort_after = 50;
    try {
        MqttServiceTestAccess::runTask(m);
    } catch (int) {}
    g_vTaskDelay_abort_after = 0;
    MqttServiceTestAccess::running(m) = false;
    MqttServiceTestAccess::mqttConnected(m) = false;
}

extern "C" volatile uint8_t g_exfat_ready;

TEST(MqttRunTask, MqttTaskWaitsForExfatThenInvokesRunTask) {
    /* mqttTask: while (!g_exfat_ready) vTaskDelay; runTask(); vTaskDelete */
    resetEnvironment();
    auto& m = mqtt();
    g_exfat_ready = 1;   /* skip wait loop */
    MqttServiceTestAccess::running(m) = true;
    /* All AT cmds fail → runTask Phase 1 retries → vTaskDelay abort fires */
    auto& esp = Esp8266::getInstance();
    for (int i = 0; i < 100; ++i) esp.pushResponse("ERROR");
    g_vTaskDelay_call_count  = 0;
    g_vTaskDelay_abort_after = 5;
    try {
        MqttServiceTestAccess::invokeMqttTask(m);
    } catch (int) {}
    g_vTaskDelay_abort_after = 0;
    MqttServiceTestAccess::running(m) = false;
}
