#include <gtest/gtest.h>
#include "CommandCodec.hpp"
#include <cstring>

using namespace arcana;

// Helper: encode a request manually then decode
static void buildRequestFrame(Cluster cluster, uint8_t cmdId,
                               const uint8_t* params, uint8_t paramsLen,
                               uint8_t* buf, size_t bufSize, size_t& outLen) {
    // Payload: [cluster][cmdId][paramsLen][params...]
    uint8_t payload[12];
    payload[0] = static_cast<uint8_t>(cluster);
    payload[1] = cmdId;
    payload[2] = paramsLen;
    if (paramsLen > 0 && params) memcpy(payload + 3, params, paramsLen);
    size_t payloadLen = 3 + paramsLen;

    FrameCodec::frame(payload, payloadLen, FrameCodec::kFlagFin, FrameCodec::kSidNone,
                      buf, bufSize, outLen);
}

// ── decodeRequest ─────────────────────────────────────────────────────────────

TEST(CommandCodecTest, DecodeRequestPingNoParams) {
    uint8_t buf[32];
    size_t frameLen = 0;
    buildRequestFrame(Cluster::System, SystemCommand::Ping, nullptr, 0, buf, sizeof(buf), frameLen);

    CommandRequest req{};
    ASSERT_TRUE(CommandCodec::decodeRequest(buf, frameLen, req));
    EXPECT_EQ(req.key.cluster,   Cluster::System);
    EXPECT_EQ(req.key.commandId, SystemCommand::Ping);
    EXPECT_EQ(req.paramsLength,  0);
}

TEST(CommandCodecTest, DecodeRequestWithParams) {
    uint8_t params[] = {0x01, 0x02, 0x03};
    uint8_t buf[32];
    size_t frameLen = 0;
    buildRequestFrame(Cluster::Sensor, SensorCommand::GetCounter, params, 3,
                      buf, sizeof(buf), frameLen);

    CommandRequest req{};
    ASSERT_TRUE(CommandCodec::decodeRequest(buf, frameLen, req));
    EXPECT_EQ(req.key.cluster,   Cluster::Sensor);
    EXPECT_EQ(req.key.commandId, SensorCommand::GetCounter);
    EXPECT_EQ(req.paramsLength,  3);
    EXPECT_EQ(memcmp(req.params, params, 3), 0);
}

TEST(CommandCodecTest, DecodeRequestFailsOnBadCRC) {
    uint8_t buf[32];
    size_t frameLen = 0;
    buildRequestFrame(Cluster::System, SystemCommand::Ping, nullptr, 0, buf, sizeof(buf), frameLen);
    buf[frameLen - 1] ^= 0xFF;  // Corrupt CRC

    CommandRequest req{};
    EXPECT_FALSE(CommandCodec::decodeRequest(buf, frameLen, req));
}

TEST(CommandCodecTest, DecodeRequestFailsOnTooShort) {
    uint8_t buf[4] = {0xAC, 0xDA, 0x01, 0x00};
    CommandRequest req{};
    EXPECT_FALSE(CommandCodec::decodeRequest(buf, 4, req));
}

// ── encodeResponse ────────────────────────────────────────────────────────────

TEST(CommandCodecTest, EncodeResponsePingSuccess) {
    CommandResponseModel rsp{};
    rsp.key = {Cluster::System, SystemCommand::Ping};
    rsp.status = CommandStatus::Success;
    rsp.dataLength = 0;

    uint8_t buf[CommandCodec::MAX_RESPONSE_FRAME];
    size_t outLen = 0;
    ASSERT_TRUE(CommandCodec::encodeResponse(rsp, buf, sizeof(buf), outLen));
    EXPECT_GT(outLen, FrameCodec::kOverhead);
    EXPECT_LE(outLen, sizeof(buf));
}

TEST(CommandCodecTest, EncodeResponseWithUint32Data) {
    CommandResponseModel rsp{};
    rsp.key = {Cluster::Sensor, SensorCommand::GetCounter};
    rsp.status = CommandStatus::Success;
    rsp.setUint32(0x12345678);

    uint8_t buf[CommandCodec::MAX_RESPONSE_FRAME];
    size_t outLen = 0;
    ASSERT_TRUE(CommandCodec::encodeResponse(rsp, buf, sizeof(buf), outLen));
    EXPECT_GT(outLen, FrameCodec::kOverhead + 4);
}

TEST(CommandCodecTest, EncodeResponseFailsIfBufferTooSmall) {
    CommandResponseModel rsp{};
    rsp.key = {Cluster::System, SystemCommand::Ping};
    rsp.status = CommandStatus::Success;
    rsp.dataLength = 0;

    uint8_t buf[4];  // Way too small
    size_t outLen = 0;
    EXPECT_FALSE(CommandCodec::encodeResponse(rsp, buf, sizeof(buf), outLen));
}

TEST(CommandCodecTest, EncodeDecodeRoundTrip) {
    // Encode a response, verify it's a valid frame
    CommandResponseModel rsp{};
    rsp.key = {Cluster::Sensor, SensorCommand::GetCounter};
    rsp.status = CommandStatus::Success;
    rsp.setUint32(42);

    uint8_t frameBuf[CommandCodec::MAX_RESPONSE_FRAME];
    size_t frameLen = 0;
    ASSERT_TRUE(CommandCodec::encodeResponse(rsp, frameBuf, sizeof(frameBuf), frameLen));

    // Deframe manually to verify CRC and structure
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(frameBuf, frameLen, payload, payloadLen, flags, sid));
    EXPECT_EQ(flags, FrameCodec::kFlagFin);
    // Response payload: [cluster][cmdId][status][dataLen][data...]
    ASSERT_GE(payloadLen, 4u);
    EXPECT_EQ(payload[0], static_cast<uint8_t>(Cluster::Sensor));
    EXPECT_EQ(payload[1], SensorCommand::GetCounter);
    EXPECT_EQ(payload[2], static_cast<uint8_t>(CommandStatus::Success));
    EXPECT_EQ(payload[3], 4);  // dataLen = 4 bytes for uint32
}

TEST(CommandCodecTest, EncodeResponseNotFoundStatus) {
    CommandResponseModel rsp{};
    rsp.key = {Cluster::System, 0x99};
    rsp.status = CommandStatus::NotFound;
    rsp.dataLength = 0;

    uint8_t buf[CommandCodec::MAX_RESPONSE_FRAME];
    size_t outLen = 0;
    ASSERT_TRUE(CommandCodec::encodeResponse(rsp, buf, sizeof(buf), outLen));

    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(buf, outLen, payload, payloadLen, flags, sid));
    EXPECT_EQ(payload[2], static_cast<uint8_t>(CommandStatus::NotFound));
}
