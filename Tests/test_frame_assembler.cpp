#include <gtest/gtest.h>
#include "FrameAssembler.hpp"
#include "FrameCodec.hpp"

using namespace arcana;

class FrameAssemblerTest : public ::testing::Test {
protected:
    FrameAssembler asm_;

    // Build a valid frame using FrameCodec
    std::vector<uint8_t> makeFrame(const uint8_t* payload, uint16_t len,
                                    uint8_t flags = FrameCodec::kFlagFin,
                                    uint8_t sid = FrameCodec::kSidNone) {
        uint8_t buf[FrameAssembler::MAX_FRAME];
        size_t outLen = 0;
        bool ok = FrameCodec::frame(payload, len, flags, sid, buf, sizeof(buf), outLen);
        EXPECT_TRUE(ok);
        return std::vector<uint8_t>(buf, buf + outLen);
    }
};

TEST_F(FrameAssemblerTest, InitialState) {
    EXPECT_EQ(asm_.getFrameLen(), 0);
    EXPECT_NE(asm_.getFrame(), nullptr);
}

TEST_F(FrameAssemblerTest, RandomBytesStayIdle) {
    for (uint8_t b = 0; b < 200; b++) {
        if (b == FrameCodec::kMagic0) continue;
        EXPECT_FALSE(asm_.feedByte(b));
    }
    EXPECT_EQ(asm_.getFrameLen(), 0);
}

TEST_F(FrameAssemblerTest, Magic0ButNotMagic1Resets) {
    EXPECT_FALSE(asm_.feedByte(FrameCodec::kMagic0));
    EXPECT_FALSE(asm_.feedByte(0x00));  // Not magic1 → reset
    EXPECT_EQ(asm_.getFrameLen(), 0);
}

TEST_F(FrameAssemblerTest, Magic0FollowedByAnotherMagic0StaysAtMagic1State) {
    EXPECT_FALSE(asm_.feedByte(FrameCodec::kMagic0));
    EXPECT_FALSE(asm_.feedByte(FrameCodec::kMagic0));  // restart
    EXPECT_FALSE(asm_.feedByte(FrameCodec::kMagic1));  // now HEADER state
}

TEST_F(FrameAssemblerTest, CompleteFrameWithEmptyPayload) {
    auto frame = makeFrame(nullptr, 0);
    bool complete = false;
    for (auto b : frame) {
        complete = asm_.feedByte(b);
    }
    EXPECT_TRUE(complete);
    EXPECT_EQ(asm_.getFrameLen(), frame.size());
}

TEST_F(FrameAssemblerTest, CompleteFrameWithPayload) {
    uint8_t payload[] = {0x01, 0x02, 0x03};
    auto frame = makeFrame(payload, 3);
    bool complete = false;
    for (auto b : frame) {
        complete = asm_.feedByte(b);
    }
    EXPECT_TRUE(complete);
    EXPECT_EQ(asm_.getFrameLen(), frame.size());

    // Verify frame content matches
    for (size_t i = 0; i < frame.size(); i++) {
        EXPECT_EQ(asm_.getFrame()[i], frame[i]);
    }
}

TEST_F(FrameAssemblerTest, CompleteStateIgnoresNewBytes) {
    uint8_t payload[] = {0xAA};
    auto frame = makeFrame(payload, 1);
    for (auto b : frame) asm_.feedByte(b);

    // Now in COMPLETE state — should ignore additional bytes
    EXPECT_FALSE(asm_.feedByte(0xFF));
    EXPECT_FALSE(asm_.feedByte(FrameCodec::kMagic0));
}

TEST_F(FrameAssemblerTest, ResetClearsState) {
    uint8_t payload[] = {0xAA};
    auto frame = makeFrame(payload, 1);
    for (auto b : frame) asm_.feedByte(b);

    asm_.reset();
    EXPECT_EQ(asm_.getFrameLen(), 0);

    // Can assemble again after reset
    bool complete = false;
    for (auto b : frame) {
        complete = asm_.feedByte(b);
    }
    EXPECT_TRUE(complete);
}

TEST_F(FrameAssemblerTest, FrameTooLargeResets) {
    // Feed magic + header with payload length that exceeds MAX_FRAME
    asm_.feedByte(FrameCodec::kMagic0);
    asm_.feedByte(FrameCodec::kMagic1);
    asm_.feedByte(0x01); // ver
    asm_.feedByte(0x00); // flags
    asm_.feedByte(0x00); // sid
    asm_.feedByte(0xFF); // lenLo = 255  (way too large)
    asm_.feedByte(0x00); // lenHi = 0

    // Should have reset due to frame too large
    EXPECT_EQ(asm_.getFrameLen(), 0);
}

TEST_F(FrameAssemblerTest, ByteByByteFeedMatchesFullFrame) {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    auto frame = makeFrame(payload, 4);

    for (size_t i = 0; i < frame.size() - 1; i++) {
        EXPECT_FALSE(asm_.feedByte(frame[i]));
    }
    EXPECT_TRUE(asm_.feedByte(frame.back()));
}
