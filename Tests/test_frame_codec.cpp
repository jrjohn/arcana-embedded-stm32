#include <gtest/gtest.h>
#include "FrameCodec.hpp"
#include <cstring>

using namespace arcana;

// Helper to build a valid frame for test input
static bool makeFrame(const uint8_t* payload, size_t payloadLen,
                      uint8_t* buf, size_t bufSize, size_t& outLen,
                      uint8_t flags = FrameCodec::kFlagFin,
                      uint8_t sid = FrameCodec::kSidNone) {
    return FrameCodec::frame(payload, payloadLen, flags, sid, buf, bufSize, outLen);
}

// ── frame() tests ────────────────────────────────────────────────────────────

TEST(FrameCodecTest, FrameSucceedsWithPayload) {
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[64];
    size_t outLen = 0;
    EXPECT_TRUE(FrameCodec::frame(payload, 3, FrameCodec::kFlagFin, FrameCodec::kSidNone,
                                  buf, sizeof(buf), outLen));
    EXPECT_EQ(outLen, 3 + FrameCodec::kOverhead);
}

TEST(FrameCodecTest, FrameFailsIfBufferTooSmall) {
    uint8_t payload[] = {0xAA, 0xBB};
    uint8_t buf[5];  // Too small (need 2 + 9 = 11)
    size_t outLen = 0;
    EXPECT_FALSE(FrameCodec::frame(payload, 2, FrameCodec::kFlagFin, 0, buf, sizeof(buf), outLen));
}

TEST(FrameCodecTest, FrameEmptyPayload) {
    uint8_t buf[16];
    size_t outLen = 0;
    EXPECT_TRUE(FrameCodec::frame(nullptr, 0, FrameCodec::kFlagFin, 0, buf, sizeof(buf), outLen));
    EXPECT_EQ(outLen, FrameCodec::kOverhead);
}

TEST(FrameCodecTest, FrameSetsMagicBytes) {
    uint8_t payload[] = {0x42};
    uint8_t buf[32];
    size_t outLen = 0;
    ASSERT_TRUE(makeFrame(payload, 1, buf, sizeof(buf), outLen));
    EXPECT_EQ(buf[FrameCodec::kOffMagic0], FrameCodec::kMagic0);
    EXPECT_EQ(buf[FrameCodec::kOffMagic1], FrameCodec::kMagic1);
    EXPECT_EQ(buf[FrameCodec::kOffVer],    FrameCodec::kVersion);
}

TEST(FrameCodecTest, FrameSetsFlags) {
    uint8_t payload[] = {0x01};
    uint8_t buf[32];
    size_t outLen = 0;
    uint8_t flags = FrameCodec::kFlagFin;
    ASSERT_TRUE(FrameCodec::frame(payload, 1, flags, 0x05, buf, sizeof(buf), outLen));
    EXPECT_EQ(buf[FrameCodec::kOffFlags], flags);
    EXPECT_EQ(buf[FrameCodec::kOffSid],   0x05);
}

// ── deframe() tests ──────────────────────────────────────────────────────────

TEST(FrameCodecTest, RoundTripPayloadRecovered) {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[64];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 4, buf, sizeof(buf), frameLen));

    const uint8_t* outPayload = nullptr;
    size_t outLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(buf, frameLen, outPayload, outLen, flags, sid));
    EXPECT_EQ(outLen, 4u);
    EXPECT_EQ(memcmp(outPayload, payload, 4), 0);
    EXPECT_EQ(flags, FrameCodec::kFlagFin);
}

TEST(FrameCodecTest, RoundTripEmptyPayload) {
    uint8_t buf[16];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(nullptr, 0, buf, sizeof(buf), frameLen));

    const uint8_t* outPayload = nullptr;
    size_t outLen = 99;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(buf, frameLen, outPayload, outLen, flags, sid));
    EXPECT_EQ(outLen, 0u);
}

TEST(FrameCodecTest, DeframeFailsOnCorruptedCRC) {
    uint8_t payload[] = {0x11, 0x22};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 2, buf, sizeof(buf), frameLen));
    buf[frameLen - 1] ^= 0xFF;  // Corrupt last CRC byte

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::deframe(buf, frameLen, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnWrongMagic) {
    uint8_t payload[] = {0x55};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 1, buf, sizeof(buf), frameLen));
    buf[0] = 0x00;  // Wrong magic0

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::deframe(buf, frameLen, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnTooShortBuffer) {
    uint8_t buf[4] = {0xAC, 0xDA, 0x01, 0x00};  // Incomplete header
    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::deframe(buf, 4, p, len, f, s));
}

TEST(FrameCodecTest, RoundTripStreamId) {
    uint8_t payload[] = {0xAB};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::frame(payload, 1, FrameCodec::kFlagFin, 0x42, buf, sizeof(buf), frameLen));

    const uint8_t* p = nullptr; size_t len = 0; uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(buf, frameLen, p, len, flags, sid));
    EXPECT_EQ(sid, 0x42);
}

TEST(FrameCodecTest, PayloadCorruptionDetected) {
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 3, buf, sizeof(buf), frameLen));
    buf[FrameCodec::kHeaderSize + 1] ^= 0x01;  // Flip bit in payload

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::deframe(buf, frameLen, p, len, f, s));
}
