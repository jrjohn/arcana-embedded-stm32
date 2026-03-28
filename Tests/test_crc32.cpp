#include <gtest/gtest.h>
#include "Crc32.hpp"

// CRC-32 IEEE 802.3: ~crc32(0xFFFFFFFF, data, len)

TEST(Crc32Test, EmptyBuffer) {
    uint32_t crc = crc32_calc(0xFFFFFFFF, nullptr, 0);
    EXPECT_EQ(crc, 0xFFFFFFFF);
}

TEST(Crc32Test, SingleByteZero) {
    uint8_t data[] = {0x00};
    uint32_t crc = ~crc32_calc(0xFFFFFFFF, data, 1);
    EXPECT_EQ(crc, 0xD202EF8D);
}

TEST(Crc32Test, KnownString123456789) {
    // Standard IEEE CRC-32 test vector: "123456789" → 0xCBF43926
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    uint32_t crc = ~crc32_calc(0xFFFFFFFF, data, 9);
    EXPECT_EQ(crc, 0xCBF43926);
}

TEST(Crc32Test, IncrementalFeed) {
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    uint32_t crc = 0xFFFFFFFF;
    crc = crc32_calc(crc, data, 4);
    crc = crc32_calc(crc, data + 4, 5);
    EXPECT_EQ(~crc, 0xCBF43926);
}

TEST(Crc32Test, CppWrapperMatchesCFunction) {
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};
    uint32_t c_result = crc32_calc(0xFFFFFFFF, data, 3);
    uint32_t cpp_result = arcana::crc32(0xFFFFFFFF, data, 3);
    EXPECT_EQ(c_result, cpp_result);
}

TEST(Crc32Test, DifferentInitValues) {
    const uint8_t data[] = {0x01};
    uint32_t a = crc32_calc(0x00000000, data, 1);
    uint32_t b = crc32_calc(0xFFFFFFFF, data, 1);
    EXPECT_NE(a, b);
}
