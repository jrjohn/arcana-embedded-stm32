#include <gtest/gtest.h>
#include "Crc16.hpp"

using namespace arcana;

TEST(Crc16Test, EmptyBufferReturnsInit) {
    EXPECT_EQ(crc16(0, nullptr, 0), 0);
}

TEST(Crc16Test, SingleZeroByte) {
    uint8_t data[] = {0x00};
    uint16_t result = crc16(0, data, 1);
    // poly=0x8408 reflected: byte 0x00 XORed into crc=0 → all iterations keep crc=0
    EXPECT_EQ(result, 0x0000);
}

TEST(Crc16Test, KnownValue_CCITT) {
    // CRC-16/KERMIT (poly=0x8408 reflected, init=0) for "123456789" = 0x2189
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    uint16_t result = crc16(0, data, sizeof(data));
    EXPECT_EQ(result, 0x2189);
}

TEST(Crc16Test, IncrementalFeedSameAsOnce) {
    const uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint16_t one_shot = crc16(0, data, 6);
    uint16_t incremental = crc16(crc16(0, data, 3), data + 3, 3);
    EXPECT_EQ(one_shot, incremental);
}

TEST(Crc16Test, AllZeros4Bytes) {
    uint8_t data[4] = {0, 0, 0, 0};
    uint16_t r1 = crc16(0, data, 4);
    uint16_t r2 = crc16(0, data, 4);
    EXPECT_EQ(r1, r2);  // Deterministic
}

TEST(Crc16Test, AllFF4Bytes) {
    uint8_t data[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t result = crc16(0, data, 4);
    EXPECT_NE(result, 0);
}

TEST(Crc16Test, DifferentInitsDifferentResults) {
    uint8_t data[] = {0xAB, 0xCD};
    uint16_t r0 = crc16(0x0000, data, 2);
    uint16_t r1 = crc16(0xFFFF, data, 2);
    EXPECT_NE(r0, r1);
}

TEST(Crc16Test, ChangedByteChangesResult) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};
    EXPECT_NE(crc16(0, data1, 3), crc16(0, data2, 3));
}
