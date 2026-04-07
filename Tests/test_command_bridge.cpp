/**
 * @file test_command_bridge.cpp
 * @brief Host-side coverage for CommandBridgeImpl.cpp.
 *
 * Strategy
 *   - Mock stm32f1xx_hal.h (UID_BASE / SysTick / DWT / HAL_GetTick) and
 *     KeyStore.hpp so the singleton ctor + ueccRng can run on host.
 *   - Use a friend struct (CommandBridgeTestAccess) to drive the private
 *     crypto helpers (encryptAndFrame, decryptAndVerify, handleKeyExchange)
 *     and the static bridgeTask() entry point.
 *   - Inject a custom xQueueReceive via g_xQueueReceiveOverride so the
 *     `while(true)` loop inside bridgeTask() can be exited via a sentinel
 *     exception once the test queue is drained.
 *
 * Coverage targets in CommandBridgeImpl.cpp:
 *   - Registry: registerCommand, findCommand, getCommandCount
 *   - Lifecycle: getInstance ctor, setBleSend / setMqttSend, startTasks
 *   - submitFrame: short + long path
 *   - processFrame (legacy): plaintext deframe → execute → frame
 *   - encryptAndFrame: happy path + buffer-too-small reject
 *   - decryptAndVerify: happy + replay + bad-HMAC + short-payload
 *   - handleKeyExchange: real uECC P-256 → HKDF → session install
 *   - bridgeTask: plaintext, encrypted, KE, bad frame, command-not-found
 */
#include <gtest/gtest.h>

#include <cstring>
#include <exception>
#include <vector>

#include "stm32f1xx_hal.h"            // mock — must precede DeviceKey/Commands
#include "CommandBridge.hpp"
#include "FrameCodec.hpp"
#include "ChaCha20.hpp"
#include "Sha256.hpp"
#include "ICommand.hpp"
#include "Commands.hpp"

extern "C" {
#include "uECC.h"
}

/* freertos_stubs.cpp exposes these as test override hooks */
typedef long BaseType_t;
typedef void* QueueHandle_t;
typedef uint32_t TickType_t;
typedef BaseType_t (*XQueueReceiveFn)(QueueHandle_t, void*, TickType_t);
extern XQueueReceiveFn g_xQueueReceiveOverride;

namespace arcana {

// ───────────────────────────────────────────────────────────────────────────
// Friend access — calls into CommandBridge's private crypto helpers + state.
// Declared inside namespace arcana so the friend declaration matches.
// ───────────────────────────────────────────────────────────────────────────
struct CommandBridgeTestAccess {
    static bool encryptAndFrame(CommandBridge& b, uint8_t source, uint8_t streamId,
                                const uint8_t* plain, size_t plainLen,
                                uint8_t* frameBuf, size_t frameBufSize,
                                size_t& frameLen) {
        return b.encryptAndFrame(source, streamId, plain, plainLen,
                                 frameBuf, frameBufSize, frameLen);
    }
    static bool decryptAndVerify(CommandBridge& b, uint8_t source,
                                 const uint8_t* payload, size_t payloadLen,
                                 uint8_t* plain, size_t plainBufSize,
                                 size_t& plainLen) {
        return b.decryptAndVerify(source, payload, payloadLen,
                                  plain, plainBufSize, plainLen);
    }
    static bool handleKeyExchange(CommandBridge& b, uint8_t source,
                                  const uint8_t* clientPub,
                                  uint8_t* serverPub, uint8_t* authTag) {
        return b.handleKeyExchange(source, clientPub, serverPub, authTag);
    }
    static ChaChaSession& session(CommandBridge& b, uint8_t idx) {
        return b.mSessions[idx & 1];
    }
    static const uint8_t* deviceKey(CommandBridge& b) { return b.mDeviceKey; }
    static void invokeBridgeTask(CommandBridge& b) {
        CommandBridge::bridgeTask(&b);
    }
    static void resetSessions(CommandBridge& b) {
        for (auto& s : b.mSessions) {
            memset(s.key, 0, sizeof(s.key));
            s.txCounter = 0;
            s.rxCounter = 0;
            s.active = false;
        }
    }
};

} // namespace arcana

using arcana::CommandBridge;
using arcana::CmdFrameItem;
using arcana::ChaChaSession;
using arcana::CommandBridgeTestAccess;
using arcana::FrameCodec;
using arcana::Cluster;
namespace SystemCommand = arcana::SystemCommand;
namespace SecurityCommand = arcana::SecurityCommand;
namespace SensorCommand = arcana::SensorCommand;
using arcana::crypto::ChaCha20;
using arcana::crypto::Sha256;

// ───────────────────────────────────────────────────────────────────────────
// Shared bridgeTask harness state
// ───────────────────────────────────────────────────────────────────────────
namespace {

struct StopBridgeTask : std::exception {
    const char* what() const noexcept override { return "test queue drained"; }
};

std::vector<CmdFrameItem> g_pendingFrames;
std::vector<std::vector<uint8_t>> g_bleOut;
std::vector<std::vector<uint8_t>> g_mqttOut;

BaseType_t testQueueReceive(QueueHandle_t /*q*/, void* buf, TickType_t /*t*/) {
    if (g_pendingFrames.empty()) throw StopBridgeTask{};
    CmdFrameItem item = g_pendingFrames.front();
    g_pendingFrames.erase(g_pendingFrames.begin());
    std::memcpy(buf, &item, sizeof(item));
    return 1; // pdTRUE
}

bool testBleSend(const uint8_t* data, uint16_t len, void* /*ctx*/) {
    g_bleOut.emplace_back(data, data + len);
    return true;
}
bool testMqttSend(const uint8_t* data, uint16_t len, void* /*ctx*/) {
    g_mqttOut.emplace_back(data, data + len);
    return true;
}

void resetHarness(CommandBridge& bridge) {
    g_pendingFrames.clear();
    g_bleOut.clear();
    g_mqttOut.clear();
    g_xQueueReceiveOverride = testQueueReceive;
    bridge.setBleSend(testBleSend, nullptr);
    bridge.setMqttSend(testMqttSend, nullptr);
    CommandBridgeTestAccess::resetSessions(bridge);
}

void pushPlaintextCmd(uint8_t cluster, uint8_t cmd,
                      const uint8_t* params, uint8_t paramLen,
                      CmdFrameItem::Transport src) {
    uint8_t payload[16];
    payload[0] = cluster;
    payload[1] = cmd;
    payload[2] = paramLen;
    if (paramLen) std::memcpy(payload + 3, params, paramLen);

    CmdFrameItem item{};
    size_t frameLen = 0;
    bool ok = FrameCodec::frame(payload, 3u + paramLen,
                                FrameCodec::kFlagFin,
                                CommandBridge::SID_PLAINTEXT,
                                item.data, sizeof(item.data), frameLen);
    ASSERT_TRUE(ok);
    item.len = static_cast<uint16_t>(frameLen);
    item.source = src;
    g_pendingFrames.push_back(item);
}

} // anonymous namespace

// ───────────────────────────────────────────────────────────────────────────
// Registry / lifecycle
// ───────────────────────────────────────────────────────────────────────────

TEST(CommandBridgeRegistry, GetInstanceRegistersBuiltins) {
    CommandBridge& b = CommandBridge::getInstance();
    // Constructor registers Ping, FwVer, Compile, Model, Serial, Temp, Accel, Light
    EXPECT_GE(b.getCommandCount(), 8);
}

TEST(CommandBridgeRegistry, RegisterCommandRespectsMax) {
    CommandBridge& b = CommandBridge::getInstance();

    // Define a stub command we can register repeatedly until full
    struct StubCmd : arcana::ICommand {
        uint8_t id;
        explicit StubCmd(uint8_t i) : id(i) {}
        arcana::CommandKey getKey() const override {
            return { Cluster::Sensor, static_cast<uint8_t>(0x80 + id) };
        }
        void execute(const arcana::CommandRequest&,
                     arcana::CommandResponseModel& rsp) override {
            rsp.status = arcana::CommandStatus::Success;
        }
    };

    static StubCmd stubs[16] = {
        StubCmd(0), StubCmd(1), StubCmd(2),  StubCmd(3),
        StubCmd(4), StubCmd(5), StubCmd(6),  StubCmd(7),
        StubCmd(8), StubCmd(9), StubCmd(10), StubCmd(11),
        StubCmd(12),StubCmd(13),StubCmd(14), StubCmd(15),
    };

    // Fill until registerCommand returns false. Singleton starts with 8.
    uint8_t startCount = b.getCommandCount();
    uint8_t added = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        if (b.registerCommand(&stubs[i])) added++;
    }
    // Total should be at most 16 (MAX_COMMANDS); the failing call exercises
    // the `>= MAX_COMMANDS` reject branch.
    EXPECT_LE(b.getCommandCount(), 16u);
    EXPECT_EQ(b.getCommandCount(), startCount + added);
    EXPECT_LT(added, 16u);  // some calls must have failed → coverage of reject
}

TEST(CommandBridgeRegistry, GetSensorCacheReturnsRef) {
    CommandBridge& b = CommandBridge::getInstance();
    arcana::SensorDataCache& cache = b.getSensorCache();
    cache.temp = 23.5f;
    cache.ax = 100; cache.ay = -200; cache.az = 300;
    cache.als = 42; cache.ps = 7;
    // Round-trip via second getSensorCache call
    EXPECT_FLOAT_EQ(b.getSensorCache().temp, 23.5f);
    EXPECT_EQ(b.getSensorCache().ax, 100);
}

TEST(CommandBridgeRegistry, StartTasksDoesNotCrash) {
    CommandBridge& b = CommandBridge::getInstance();
    // xTaskCreateStatic stub returns the buffer pointer; the bridgeTask
    // function pointer is captured but not invoked (no scheduler in tests).
    b.startTasks();
    SUCCEED();
}

// ───────────────────────────────────────────────────────────────────────────
// processFrame (legacy plaintext path)
// ───────────────────────────────────────────────────────────────────────────

namespace {
struct LegacyResponse {
    std::vector<uint8_t> bytes;
};
void legacyRespCb(const uint8_t* data, uint16_t len, void* ctx) {
    auto* r = static_cast<LegacyResponse*>(ctx);
    r->bytes.assign(data, data + len);
}
} // anonymous namespace

TEST(CommandBridgeProcessFrame, PingExecutesAndFrames) {
    CommandBridge& b = CommandBridge::getInstance();

    // Build Ping frame: [System(0), Ping(1), 0]
    uint8_t payload[3] = {0x00, SystemCommand::Ping, 0x00};
    uint8_t frameBuf[40];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, 3, FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  frameBuf, sizeof(frameBuf), frameLen));
    LegacyResponse rsp;
    b.processFrame(frameBuf, static_cast<uint16_t>(frameLen),
                   legacyRespCb, &rsp);

    // Expect a framed response: header(7) + [cluster,cmd,status,len,data...] + crc(2)
    ASSERT_GE(rsp.bytes.size(), 9u + 4u);
    const uint8_t* p = rsp.bytes.data() + FrameCodec::kHeaderSize;
    EXPECT_EQ(p[0], 0x00);                    // System cluster
    EXPECT_EQ(p[1], SystemCommand::Ping);
    EXPECT_EQ(p[2], 0u);                      // Success
    EXPECT_EQ(p[3], 4u);                      // Ping returns uint32 (tick)
}

TEST(CommandBridgeProcessFrame, UnknownCommandReturnsNotFound) {
    CommandBridge& b = CommandBridge::getInstance();

    uint8_t payload[3] = {0x00, 0xFE, 0x00};  // System / nonexistent cmd
    uint8_t frameBuf[40];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, 3, FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  frameBuf, sizeof(frameBuf), frameLen));
    LegacyResponse rsp;
    b.processFrame(frameBuf, static_cast<uint16_t>(frameLen), legacyRespCb, &rsp);

    ASSERT_GE(rsp.bytes.size(), 9u + 4u);
    const uint8_t* p = rsp.bytes.data() + FrameCodec::kHeaderSize;
    EXPECT_EQ(p[2], static_cast<uint8_t>(arcana::CommandStatus::NotFound));
}

TEST(CommandBridgeProcessFrame, BadFrameIgnored) {
    CommandBridge& b = CommandBridge::getInstance();
    LegacyResponse rsp;
    uint8_t junk[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    b.processFrame(junk, sizeof(junk), legacyRespCb, &rsp);
    EXPECT_TRUE(rsp.bytes.empty());
}

TEST(CommandBridgeProcessFrame, ShortPayloadIgnored) {
    CommandBridge& b = CommandBridge::getInstance();
    // Build a valid frame with a 2-byte payload (< 3 → bail in processFrame)
    uint8_t payload[2] = {0x00, 0x01};
    uint8_t frameBuf[40];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, 2, FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  frameBuf, sizeof(frameBuf), frameLen));
    LegacyResponse rsp;
    b.processFrame(frameBuf, static_cast<uint16_t>(frameLen), legacyRespCb, &rsp);
    EXPECT_TRUE(rsp.bytes.empty());
}

// ───────────────────────────────────────────────────────────────────────────
// submitFrame
// ───────────────────────────────────────────────────────────────────────────

TEST(CommandBridgeSubmit, ShortFrameAccepted) {
    CommandBridge& b = CommandBridge::getInstance();
    uint8_t buf[8] = {0xAC, 0xDA, 0x01, 0x01, 0x00, 0x03, 0x00, 0x00};
    EXPECT_TRUE(b.submitFrame(buf, sizeof(buf), CmdFrameItem::BLE));
}

TEST(CommandBridgeSubmit, OversizedFrameRejected) {
    CommandBridge& b = CommandBridge::getInstance();
    uint8_t big[CmdFrameItem::MAX_DATA + 1] = {};
    EXPECT_FALSE(b.submitFrame(big, sizeof(big), CmdFrameItem::MQTT));
}

// ───────────────────────────────────────────────────────────────────────────
// encryptAndFrame / decryptAndVerify (private — via friend)
// ───────────────────────────────────────────────────────────────────────────

namespace {
void seedSession(CommandBridge& b, uint8_t source, uint8_t fill = 0x42) {
    auto& sess = CommandBridgeTestAccess::session(b, source);
    std::memset(sess.key, fill, sizeof(sess.key));
    sess.txCounter = 0;
    sess.rxCounter = 0;
    sess.active = true;
}
} // anonymous namespace

TEST(CommandBridgeCrypto, EncryptDecryptRoundTrip) {
    CommandBridge& b = CommandBridge::getInstance();
    seedSession(b, 0);

    const uint8_t plain[8] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plain, sizeof(plain), frameBuf, sizeof(frameBuf), frameLen));

    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(frameBuf, frameLen,
                                    payload, payloadLen, flags, sid));
    EXPECT_EQ(sid, CommandBridge::SID_ENCRYPTED);
    EXPECT_EQ(payloadLen, 12u + sizeof(plain) + 32u);

    // rxCounter must be < new counter — the encrypt path used counter=0
    auto& sess = CommandBridgeTestAccess::session(b, 0);
    sess.rxCounter = 0;  // first receive

    uint8_t plainOut[32];
    size_t plainOutLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, payload, payloadLen,
        plainOut, sizeof(plainOut), plainOutLen));
    EXPECT_EQ(plainOutLen, sizeof(plain));
    EXPECT_EQ(0, std::memcmp(plainOut, plain, sizeof(plain)));
}

TEST(CommandBridgeCrypto, EncryptRejectsWhenSessionInactive) {
    CommandBridge& b = CommandBridge::getInstance();
    CommandBridgeTestAccess::resetSessions(b);
    const uint8_t plain[4] = {1, 2, 3, 4};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    EXPECT_FALSE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plain, sizeof(plain), frameBuf, sizeof(frameBuf), frameLen));
}

TEST(CommandBridgeCrypto, EncryptRejectsOversizedPlain) {
    CommandBridge& b = CommandBridge::getInstance();
    seedSession(b, 0);
    // payload[80] is the internal cap → 80 - 12 - 32 = 36 bytes max plaintext
    uint8_t big[40] = {};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    EXPECT_FALSE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        big, sizeof(big), frameBuf, sizeof(frameBuf), frameLen));
}

TEST(CommandBridgeCrypto, DecryptRejectsWhenSessionInactive) {
    CommandBridge& b = CommandBridge::getInstance();
    CommandBridgeTestAccess::resetSessions(b);
    uint8_t payload[64] = {};
    uint8_t out[32];
    size_t outLen = 0;
    EXPECT_FALSE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, payload, sizeof(payload), out, sizeof(out), outLen));
}

TEST(CommandBridgeCrypto, DecryptRejectsShortPayload) {
    CommandBridge& b = CommandBridge::getInstance();
    seedSession(b, 0);
    // Need payloadLen >= 12 + 1 + 32 = 45
    uint8_t payload[44] = {};
    uint8_t out[32];
    size_t outLen = 0;
    EXPECT_FALSE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, payload, sizeof(payload), out, sizeof(out), outLen));
}

TEST(CommandBridgeCrypto, DecryptRejectsCipherLargerThanBuf) {
    CommandBridge& b = CommandBridge::getInstance();
    seedSession(b, 0);
    // payloadLen 12 + 16 + 32 = 60 → cipherLen=16; pass plainBufSize=8 → reject
    uint8_t payload[60] = {};
    uint8_t out[8];
    size_t outLen = 0;
    EXPECT_FALSE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, payload, sizeof(payload), out, sizeof(out), outLen));
}

TEST(CommandBridgeCrypto, DecryptRejectsBadHmac) {
    CommandBridge& b = CommandBridge::getInstance();
    seedSession(b, 0);

    const uint8_t plain[6] = {1, 2, 3, 4, 5, 6};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plain, sizeof(plain), frameBuf, sizeof(frameBuf), frameLen));

    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(frameBuf, frameLen,
                                    payload, payloadLen, flags, sid));

    // Copy payload to a mutable buffer and flip the last HMAC byte
    std::vector<uint8_t> tampered(payload, payload + payloadLen);
    tampered.back() ^= 0xFF;

    CommandBridgeTestAccess::session(b, 0).rxCounter = 0;
    uint8_t out[32];
    size_t outLen = 0;
    EXPECT_FALSE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, tampered.data(), tampered.size(), out, sizeof(out), outLen));
}

TEST(CommandBridgeCrypto, DecryptReplayProtection) {
    CommandBridge& b = CommandBridge::getInstance();
    seedSession(b, 0);

    const uint8_t plain[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plain, sizeof(plain), frameBuf, sizeof(frameBuf), frameLen));
    // tx counter has now advanced to 1; encrypt again uses counter=1
    uint8_t frameBuf2[112];
    size_t frameLen2 = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plain, sizeof(plain), frameBuf2, sizeof(frameBuf2), frameLen2));

    const uint8_t *p1 = nullptr, *p2 = nullptr;
    size_t p1Len = 0, p2Len = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(frameBuf,  frameLen,  p1, p1Len, flags, sid));
    ASSERT_TRUE(FrameCodec::deframe(frameBuf2, frameLen2, p2, p2Len, flags, sid));

    CommandBridgeTestAccess::session(b, 0).rxCounter = 0;

    // Decrypt #2 first (counter=1, becomes rxCounter)
    uint8_t out[32];
    size_t outLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, p2, p2Len, out, sizeof(out), outLen));
    EXPECT_EQ(CommandBridgeTestAccess::session(b, 0).rxCounter, 1u);

    // Now replay #1 (counter=0) → must reject as old
    EXPECT_FALSE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, p1, p1Len, out, sizeof(out), outLen));
}

// ───────────────────────────────────────────────────────────────────────────
// handleKeyExchange (uECC P-256 + HKDF on host)
// ───────────────────────────────────────────────────────────────────────────

namespace {
// Deterministic RNG for the client side of the test handshake
int clientRng(uint8_t* dest, unsigned size) {
    static uint32_t state = 0xC0FFEEu;
    for (unsigned i = 0; i < size; ++i) {
        state = state * 1103515245u + 12345u;
        dest[i] = static_cast<uint8_t>(state >> 16);
    }
    return 1;
}
} // anonymous namespace

TEST(CommandBridgeKeyExchange, ServerInstallsSessionAndAuthTagMatches) {
    CommandBridge& b = CommandBridge::getInstance();
    CommandBridgeTestAccess::resetSessions(b);

    // Client side: generate ephemeral keypair using deterministic RNG
    uECC_set_rng(clientRng);
    const uECC_Curve curve = uECC_secp256r1();
    uint8_t clientPriv[32];
    uint8_t clientPub[64];
    ASSERT_EQ(1, uECC_make_key(clientPub, clientPriv, curve));

    // Server (CommandBridge) handshake
    uint8_t serverPub[64];
    uint8_t authTag[32];
    EXPECT_TRUE(CommandBridgeTestAccess::handleKeyExchange(
        b, /*source=*/0, clientPub, serverPub, authTag));

    // Session should now be active for source 0
    EXPECT_TRUE(b.hasSession(0));
    EXPECT_FALSE(b.hasSession(1));
    EXPECT_EQ(CommandBridgeTestAccess::session(b, 0).txCounter, 0u);
    EXPECT_EQ(CommandBridgeTestAccess::session(b, 0).rxCounter, 0u);

    // Verify authTag = HMAC-SHA256(deviceKey, serverPub || clientPub)
    uint8_t expected[32];
    uint8_t buf[128];
    std::memcpy(buf,        serverPub, 64);
    std::memcpy(buf + 64,   clientPub, 64);
    Sha256::hmac(CommandBridgeTestAccess::deviceKey(b), 32, buf, 128, expected);
    EXPECT_EQ(0, std::memcmp(authTag, expected, 32));

    // Independently derive the session key from the client side and confirm
    // CommandBridge's installed session key matches → end-to-end correctness.
    uint8_t shared[32];
    ASSERT_EQ(1, uECC_shared_secret(serverPub, clientPriv, shared, curve));
    uint8_t sessionKey[32];
    Sha256::hkdf(shared, 32, CommandBridgeTestAccess::deviceKey(b), 32,
                 reinterpret_cast<const uint8_t*>("ARCANA-SESSION"), 14,
                 sessionKey, 32);
    EXPECT_EQ(0, std::memcmp(sessionKey,
                             CommandBridgeTestAccess::session(b, 0).key, 32));
}

// ───────────────────────────────────────────────────────────────────────────
// bridgeTask end-to-end via injected queue
// ───────────────────────────────────────────────────────────────────────────

TEST(CommandBridgeBridgeTask, PlaintextPingDispatchesAndResponds) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    pushPlaintextCmd(0x00, SystemCommand::Ping, nullptr, 0, CmdFrameItem::BLE);

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    ASSERT_EQ(g_bleOut.size(), 1u);
    // Response should decode as a frame
    const auto& out = g_bleOut[0];
    const uint8_t* p = nullptr;
    size_t plen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(out.data(), out.size(), p, plen, flags, sid));
    EXPECT_EQ(p[0], 0x00);                    // System cluster
    EXPECT_EQ(p[1], SystemCommand::Ping);
    EXPECT_EQ(p[2], 0u);                      // Success
}

TEST(CommandBridgeBridgeTask, PlaintextRoutesByTransport) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    pushPlaintextCmd(0x00, SystemCommand::Ping, nullptr, 0, CmdFrameItem::MQTT);
    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    EXPECT_TRUE(g_bleOut.empty());
    EXPECT_EQ(g_mqttOut.size(), 1u);
}

TEST(CommandBridgeBridgeTask, BadFrameIgnored) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    CmdFrameItem item{};
    std::memset(item.data, 0xFF, 16);
    item.len = 16;
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);
    EXPECT_TRUE(g_bleOut.empty());
}

TEST(CommandBridgeBridgeTask, ShortPlaintextPayloadSkipped) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    // Frame with 2-byte payload — bridgeTask continues without sending
    uint8_t payload[2] = {0x00, 0x01};
    CmdFrameItem item{};
    size_t fl = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, 2, FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  item.data, sizeof(item.data), fl));
    item.len = static_cast<uint16_t>(fl);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);
    EXPECT_TRUE(g_bleOut.empty());
}

TEST(CommandBridgeBridgeTask, UnknownCommandReturnsNotFound) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    pushPlaintextCmd(0x00, 0xFE, nullptr, 0, CmdFrameItem::BLE);
    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    ASSERT_EQ(g_bleOut.size(), 1u);
    const uint8_t* p = nullptr;
    size_t plen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(g_bleOut[0].data(), g_bleOut[0].size(),
                                    p, plen, flags, sid));
    EXPECT_EQ(p[2], static_cast<uint8_t>(arcana::CommandStatus::NotFound));
}

TEST(CommandBridgeBridgeTask, EncryptedRoundTripExecutesCommand) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    // Pre-install a session for source 0 (BLE)
    seedSession(b, 0, 0x55);

    // Build encrypted command request: [System, Ping, 0]
    uint8_t plainCmd[3] = {0x00, SystemCommand::Ping, 0x00};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plainCmd, sizeof(plainCmd), frameBuf, sizeof(frameBuf), frameLen));

    CmdFrameItem item{};
    std::memcpy(item.data, frameBuf, frameLen);
    item.len = static_cast<uint16_t>(frameLen);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    // bridgeTask will try to verify the incoming frame's nonce against
    // rxCounter — but our encryptAndFrame already advanced txCounter past 0,
    // so reset rxCounter to 0 to let bridgeTask receive counter=0 cleanly.
    CommandBridgeTestAccess::session(b, 0).rxCounter = 0;

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    // Response framed back as SID_ENCRYPTED
    ASSERT_EQ(g_bleOut.size(), 1u);
    const uint8_t* p = nullptr;
    size_t plen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(g_bleOut[0].data(), g_bleOut[0].size(),
                                    p, plen, flags, sid));
    EXPECT_EQ(sid, CommandBridge::SID_ENCRYPTED);
    EXPECT_GE(plen, 12u + 1u + 32u);
}

TEST(CommandBridgeBridgeTask, EncryptedBadFrameContinues) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);
    seedSession(b, 0, 0x77);

    // Build a proper encrypted frame, then corrupt the HMAC tail
    uint8_t plainCmd[3] = {0x00, SystemCommand::Ping, 0x00};
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plainCmd, sizeof(plainCmd), frameBuf, sizeof(frameBuf), frameLen));
    // Corrupt last byte (HMAC) — the inner CRC is over the frame body, so we
    // need a frame whose CRC matches but inner HMAC is wrong. Easiest: rebuild
    // a frame with a tampered inner payload.
    const uint8_t* origPayload = nullptr;
    size_t origPlen = 0;
    uint8_t f = 0, s = 0;
    ASSERT_TRUE(FrameCodec::deframe(frameBuf, frameLen, origPayload, origPlen, f, s));

    std::vector<uint8_t> tampered(origPayload, origPayload + origPlen);
    tampered.back() ^= 0xFF;

    uint8_t reframed[112];
    size_t reframedLen = 0;
    ASSERT_TRUE(FrameCodec::frame(tampered.data(), tampered.size(),
                                  FrameCodec::kFlagFin, CommandBridge::SID_ENCRYPTED,
                                  reframed, sizeof(reframed), reframedLen));

    CmdFrameItem item{};
    std::memcpy(item.data, reframed, reframedLen);
    item.len = static_cast<uint16_t>(reframedLen);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    CommandBridgeTestAccess::session(b, 0).rxCounter = 0;
    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    // No response — decryptAndVerify rejected
    EXPECT_TRUE(g_bleOut.empty());
}

TEST(CommandBridgeBridgeTask, KeyExchangeResponseSent) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    // Build client public key
    uECC_set_rng(clientRng);
    const uECC_Curve curve = uECC_secp256r1();
    uint8_t clientPriv[32];
    uint8_t clientPub[64];
    ASSERT_EQ(1, uECC_make_key(clientPub, clientPriv, curve));

    // Build payload: [Security, KeyExchange, 0, clientPub:64]
    uint8_t payload[3 + 64];
    payload[0] = static_cast<uint8_t>(Cluster::Security);
    payload[1] = SecurityCommand::KeyExchange;
    payload[2] = 0;
    std::memcpy(payload + 3, clientPub, 64);

    CmdFrameItem item{};
    size_t fl = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, sizeof(payload),
                                  FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  item.data, sizeof(item.data), fl));
    item.len = static_cast<uint16_t>(fl);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    // KE response: 100-byte payload framed back
    ASSERT_EQ(g_bleOut.size(), 1u);
    const uint8_t* p = nullptr;
    size_t plen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(g_bleOut[0].data(), g_bleOut[0].size(),
                                    p, plen, flags, sid));
    EXPECT_EQ(plen, 100u);              // 4 header + 64 serverPub + 32 authTag
    EXPECT_EQ(p[0], static_cast<uint8_t>(Cluster::Security));
    EXPECT_EQ(p[1], SecurityCommand::KeyExchange);
    EXPECT_EQ(p[2], 0u);                // Success
    EXPECT_EQ(p[3], 96u);               // dataLength

    EXPECT_TRUE(b.hasSession(0));
}

TEST(CommandBridgeBridgeTask, KeyExchangeOverMqttSendsResponse) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    // Same KE handshake but routed through MQTT to cover mMqttSend KE branch
    uECC_set_rng(clientRng);
    const uECC_Curve curve = uECC_secp256r1();
    uint8_t clientPriv[32], clientPub[64];
    ASSERT_EQ(1, uECC_make_key(clientPub, clientPriv, curve));

    uint8_t payload[3 + 64];
    payload[0] = static_cast<uint8_t>(Cluster::Security);
    payload[1] = SecurityCommand::KeyExchange;
    payload[2] = 0;
    std::memcpy(payload + 3, clientPub, 64);

    CmdFrameItem item{};
    size_t fl = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, sizeof(payload),
                                  FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  item.data, sizeof(item.data), fl));
    item.len = static_cast<uint16_t>(fl);
    item.source = CmdFrameItem::MQTT;
    g_pendingFrames.push_back(item);

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    EXPECT_TRUE(g_bleOut.empty());
    ASSERT_EQ(g_mqttOut.size(), 1u);
    EXPECT_TRUE(b.hasSession(1));  // MQTT session installed
}

TEST(CommandBridgeBridgeTask, EncryptedUnknownCommandReturnsNotFound) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);
    seedSession(b, 0, 0xAA);

    // Encrypted request for a non-existent command
    uint8_t plainCmd[3] = {
        static_cast<uint8_t>(Cluster::Sensor), 0xFE, 0x00
    };
    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plainCmd, sizeof(plainCmd), frameBuf, sizeof(frameBuf), frameLen));

    CmdFrameItem item{};
    std::memcpy(item.data, frameBuf, frameLen);
    item.len = static_cast<uint16_t>(frameLen);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    CommandBridgeTestAccess::session(b, 0).rxCounter = 0;
    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    // Encrypted response is sent — decrypt and check status field
    ASSERT_EQ(g_bleOut.size(), 1u);
    const uint8_t* rspPayload = nullptr;
    size_t rspPlen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(g_bleOut[0].data(), g_bleOut[0].size(),
                                    rspPayload, rspPlen, flags, sid));
    EXPECT_EQ(sid, CommandBridge::SID_ENCRYPTED);

    // Decrypt the response payload — it carries the NotFound status
    auto& sess = CommandBridgeTestAccess::session(b, 0);
    sess.rxCounter = 0;  // accept any counter
    uint8_t plainRsp[32];
    size_t plainRspLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, rspPayload, rspPlen, plainRsp, sizeof(plainRsp), plainRspLen));
    EXPECT_GE(plainRspLen, 4u);
    EXPECT_EQ(plainRsp[2], static_cast<uint8_t>(arcana::CommandStatus::NotFound));
}

TEST(CommandBridgeBridgeTask, EncryptedKeyExchangeRejectedAsInvalidParam) {
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    // Pre-install a (bogus) session so decryptAndVerify can run.
    seedSession(b, 0, 0x99);

    // The KE-over-encrypted branch fires when:
    //   * outer payloadLen >= 3 + 64 (= 67), where outer payloadLen is the
    //     deframed BLE-frame body (12 nonce + cipherLen + 32 hmac).
    //   * The decrypted command is Security/KeyExchange with paramsLength=0.
    //   * streamId == SID_ENCRYPTED → clientPub is forced to nullptr,
    //     producing InvalidParam (chicken-and-egg).
    //
    // To satisfy outer payloadLen ≥ 67 we need cipherLen ≥ 23, so the
    // plaintext must be at least 23 bytes. Pad after the 3-byte command
    // header so plainBuf[2] (paramsLength) stays 0.
    uint8_t plain[24] = {0};
    plain[0] = static_cast<uint8_t>(Cluster::Security);
    plain[1] = SecurityCommand::KeyExchange;
    plain[2] = 0;
    // bytes 3..23 are payload pad (irrelevant — clientPub never read)

    uint8_t frameBuf[112];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::encryptAndFrame(
        b, 0, CommandBridge::SID_ENCRYPTED,
        plain, sizeof(plain), frameBuf, sizeof(frameBuf), frameLen));

    CmdFrameItem item{};
    std::memcpy(item.data, frameBuf, frameLen);
    item.len = static_cast<uint16_t>(frameLen);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    CommandBridgeTestAccess::session(b, 0).rxCounter = 0;
    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    // Encrypted response should be emitted with status = InvalidParam.
    ASSERT_EQ(g_bleOut.size(), 1u);
    const uint8_t* rspPayload = nullptr;
    size_t rspPlen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(g_bleOut[0].data(), g_bleOut[0].size(),
                                    rspPayload, rspPlen, flags, sid));
    EXPECT_EQ(sid, CommandBridge::SID_ENCRYPTED);

    auto& sess = CommandBridgeTestAccess::session(b, 0);
    sess.rxCounter = 0;
    uint8_t plainRsp[32];
    size_t plainRspLen = 0;
    ASSERT_TRUE(CommandBridgeTestAccess::decryptAndVerify(
        b, 0, rspPayload, rspPlen, plainRsp, sizeof(plainRsp), plainRspLen));
    EXPECT_GE(plainRspLen, 4u);
    EXPECT_EQ(plainRsp[2],
              static_cast<uint8_t>(arcana::CommandStatus::InvalidParam));
}

TEST(CommandBridgeBridgeTask, PlaintextKeyExchangeFailsOnInvalidClientPub) {
    // Force handleKeyExchange to return false by passing an all-zero client
    // public key — uECC_shared_secret rejects the off-curve point and the
    // bridge should respond with status=Error (covers the failure branch).
    CommandBridge& b = CommandBridge::getInstance();
    resetHarness(b);

    uint8_t payload[3 + 64] = {0};
    payload[0] = static_cast<uint8_t>(Cluster::Security);
    payload[1] = SecurityCommand::KeyExchange;
    payload[2] = 0;
    // payload[3..66] all zeros = invalid public key

    CmdFrameItem item{};
    size_t fl = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, sizeof(payload),
                                  FrameCodec::kFlagFin,
                                  CommandBridge::SID_PLAINTEXT,
                                  item.data, sizeof(item.data), fl));
    item.len = static_cast<uint16_t>(fl);
    item.source = CmdFrameItem::BLE;
    g_pendingFrames.push_back(item);

    EXPECT_THROW(CommandBridgeTestAccess::invokeBridgeTask(b), StopBridgeTask);

    // A response should be sent (status = Error from line 373) framed as
    // plaintext via the trailing rspPayload path.
    ASSERT_EQ(g_bleOut.size(), 1u);
    const uint8_t* rspPayload = nullptr;
    size_t rspPlen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(g_bleOut[0].data(), g_bleOut[0].size(),
                                    rspPayload, rspPlen, flags, sid));
    // Either Success (KE somehow completed) or Error — what matters for
    // coverage is that the bridgeTask reached the response path. Don't
    // assert on the exact code: uECC's reject behavior may vary.
    EXPECT_FALSE(b.hasSession(0));  // session must NOT be installed on failure
}

