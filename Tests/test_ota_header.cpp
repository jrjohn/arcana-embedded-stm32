#include <gtest/gtest.h>
#include "ota_header.h"
#include <cstring>

TEST(OtaHeaderTest, StructSizeIs44Bytes) {
    EXPECT_EQ(sizeof(ota_meta_t), 44u);
}

TEST(OtaHeaderTest, MetaCrcOffset) {
    EXPECT_EQ(OTA_META_CRC_OFFSET, 40u);
}

TEST(OtaHeaderTest, MagicConstants) {
    EXPECT_EQ(OTA_META_MAGIC, 0x41524F54u);
    EXPECT_EQ(OTA_META_VERSION, 1u);
    EXPECT_EQ(OTA_FLAG_DR2_VALUE, 0x4F54u);
    EXPECT_EQ(OTA_FLAG_DR3_VALUE, 0x4100u);
}

TEST(OtaHeaderTest, FlashLayout) {
    EXPECT_EQ(APP_FLASH_BASE, 0x08008000u);
    EXPECT_EQ(APP_FLASH_SIZE, 476u * 1024u);
    EXPECT_EQ(APP_FLASH_END, APP_FLASH_BASE + APP_FLASH_SIZE);
    EXPECT_EQ(KEY_STORE_BASE, 0x0807F000u);
    EXPECT_EQ(FLASH_PAGE_SIZE, 2048u);
}

TEST(OtaHeaderTest, MetaStructFields) {
    ota_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic = OTA_META_MAGIC;
    meta.version = OTA_META_VERSION;
    meta.fw_size = 1024;
    meta.crc32 = 0xDEADBEEF;
    meta.target_addr = APP_FLASH_BASE;
    strncpy(meta.fw_version, "1.0.0", sizeof(meta.fw_version));
    meta.timestamp = 1711500000;

    EXPECT_EQ(meta.magic, 0x41524F54u);
    EXPECT_EQ(meta.version, 1u);
    EXPECT_EQ(meta.fw_size, 1024u);
    EXPECT_EQ(meta.crc32, 0xDEADBEEFu);
    EXPECT_EQ(meta.target_addr, APP_FLASH_BASE);
    EXPECT_STREQ(meta.fw_version, "1.0.0");
    EXPECT_EQ(meta.timestamp, 1711500000u);
}

TEST(OtaHeaderTest, FilePaths) {
    EXPECT_STREQ(OTA_FW_FILENAME, "firmware.bin");
    EXPECT_STREQ(OTA_META_FILENAME, "ota_meta.bin");
    EXPECT_STREQ(OTA_PREV_FILENAME, "fw_prev.bin");
    EXPECT_STREQ(OTA_STATUS_FILENAME, "ota_status.txt");
}

#if UINTPTR_MAX == UINT32_MAX
// ota_validate_app_image uses uint32_t→pointer cast — only valid on 32-bit
TEST(OtaHeaderTest, ValidateAppImage_InvalidSP) {
    uint32_t vec[2] = {0x10000000, 0x08008100};  // SP below RAM
    EXPECT_EQ(ota_validate_app_image(reinterpret_cast<uint32_t>(vec)), 0);
}

TEST(OtaHeaderTest, ValidateAppImage_ValidImage) {
    uint32_t vec[2] = {0x20008000, 0x08008100};
    uint32_t addr = reinterpret_cast<uint32_t>(vec);
    EXPECT_EQ(ota_validate_app_image(addr), 1);
}
#endif
