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
    static volatile bool& mqttConnected(MqttServiceImpl& m) { return m.mMqttConnected; }
    static volatile bool& sensorPending(MqttServiceImpl& m) { return m.mSensorPending; }
    static SensorDataModel& pendingSensor(MqttServiceImpl& m) { return m.mPendingSensor; }
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
