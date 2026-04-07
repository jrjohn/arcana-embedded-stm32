/**
 * @file test_ota.cpp
 * @brief Host coverage suite for OtaServiceImpl.cpp.
 *
 * Compiles the production OtaServiceImpl.cpp against:
 *   - Esp8266 mock (programmable AT-command queue)
 *   - ff.h in-memory FatFs
 *   - HAL stub (BKP, NVIC_SystemReset, HAL_PWR_EnableBkUpAccess)
 *
 * The full startUpdate path ends in NVIC_SystemReset() which is a no-op
 * on host, so we can drive it end-to-end.
 *
 * Strategy: each test exercises one of the helpers (verifyCrc, writeOtaMeta)
 * directly via friend access OR drives startUpdate to hit a specific failure
 * branch (httpGet/receiveToFile/verify/meta) by programming the Esp8266
 * response queue.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "stm32f1xx_hal.h"
#include "ff.h"
#include "Crc32.hpp"
#include "ota_header.h"

#include "OtaServiceImpl.hpp"
#include "Esp8266.hpp"

namespace arcana {

/* Friend struct — exposes private methods for direct testing */
struct OtaServiceTestAccess {
    static bool httpGet(OtaServiceImpl& s, const char* host, uint16_t port,
                        const char* path) {
        return s.httpGet(host, port, path);
    }
    static bool receiveToFile(OtaServiceImpl& s, uint32_t expected) {
        return s.receiveToFile(expected);
    }
    static bool verifyCrc(OtaServiceImpl& s, uint32_t crc, uint32_t size) {
        return s.verifyCrc(crc, size);
    }
    static bool writeOtaMeta(OtaServiceImpl& s, uint32_t fwSize, uint32_t crc,
                              const char* version) {
        return s.writeOtaMeta(fwSize, crc, version);
    }
    static void setOtaFlag(OtaServiceImpl& s) { s.setOtaFlag(); }
    static volatile uint8_t& progress(OtaServiceImpl& s) { return s.mProgress; }
    static volatile bool&    active(OtaServiceImpl& s)    { return s.mActive; }
};

} // namespace arcana

using arcana::OtaServiceImpl;
using arcana::OtaServiceTestAccess;
using arcana::Esp8266;

namespace {

OtaServiceImpl& ota() {
    return static_cast<OtaServiceImpl&>(OtaServiceImpl::getInstance());
}

void resetEnvironment() {
    test_ff_reset();
    Esp8266::getInstance().resetForTest();
    auto& s = ota();
    OtaServiceTestAccess::progress(s) = 0;
    OtaServiceTestAccess::active(s)   = false;
    s.input.esp = &Esp8266::getInstance();
}

/* Build a "firmware.bin"-style payload, write it to the in-memory FatFs,
 * compute its CRC-32, and return the CRC. */
uint32_t createFakeFirmware(const char* name, uint32_t size, uint8_t fillByte = 0xAB) {
    std::vector<uint8_t> bytes(size, fillByte);
    test_ff_create(name, bytes.data(), (UINT)size);
    return ~crc32_calc(0xFFFFFFFF, bytes.data(), size);
}

} // anonymous namespace

// ── Lifecycle ──────────────────────────────────────────────────────────────

TEST(OtaLifecycle, GetInstanceSingleton) {
    auto& a = OtaServiceImpl::getInstance();
    auto& b = OtaServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(OtaLifecycle, FreshInstanceNotActive) {
    resetEnvironment();
    EXPECT_FALSE(ota().isActive());
    EXPECT_EQ(ota().getProgress(), 0);
}

// ── verifyCrc ──────────────────────────────────────────────────────────────

TEST(OtaVerify, VerifyCrcMatchesWrittenBytes) {
    resetEnvironment();
    uint32_t expectedCrc = createFakeFirmware("firmware.bin", 1000, 0xCD);
    EXPECT_TRUE(OtaServiceTestAccess::verifyCrc(ota(), expectedCrc, 1000));
}

TEST(OtaVerify, VerifyCrcMismatchReturnsFalse) {
    resetEnvironment();
    createFakeFirmware("firmware.bin", 500, 0x55);
    EXPECT_FALSE(OtaServiceTestAccess::verifyCrc(ota(), 0xDEADBEEF, 500));
}

TEST(OtaVerify, VerifyCrcMissingFileReturnsFalse) {
    resetEnvironment();
    EXPECT_FALSE(OtaServiceTestAccess::verifyCrc(ota(), 0, 1000));
}

TEST(OtaVerify, VerifyCrcChunkedRead) {
    /* File larger than the 256-byte read buffer → exercises the loop */
    resetEnvironment();
    uint32_t expectedCrc = createFakeFirmware("firmware.bin", 1024, 0x77);
    EXPECT_TRUE(OtaServiceTestAccess::verifyCrc(ota(), expectedCrc, 1024));
}

// ── writeOtaMeta ───────────────────────────────────────────────────────────

TEST(OtaMeta, WriteOtaMetaProducesValidStruct) {
    resetEnvironment();
    EXPECT_TRUE(OtaServiceTestAccess::writeOtaMeta(ota(), 1000, 0xDEADBEEF, "v1.2.3"));
    EXPECT_TRUE(test_ff_exists("ota_meta.bin"));

    /* Read it back and verify the magic + fields */
    uint8_t buf[64] = {};
    UINT len = 0;
    ASSERT_EQ(test_ff_read("ota_meta.bin", buf, sizeof(buf), &len), FR_OK);
    ASSERT_GE(len, sizeof(ota_meta_t));

    ota_meta_t meta;
    std::memcpy(&meta, buf, sizeof(meta));
    EXPECT_EQ(meta.magic, OTA_META_MAGIC);
    EXPECT_EQ(meta.version, OTA_META_VERSION);
    EXPECT_EQ(meta.fw_size, 1000u);
    EXPECT_EQ(meta.crc32, 0xDEADBEEFu);
    EXPECT_EQ(meta.target_addr, APP_FLASH_BASE);
    EXPECT_STREQ(meta.fw_version, "v1.2.3");

    /* Verify self-CRC: meta_crc = ~crc32(0xFFFFFFFF, &meta, 40) */
    uint32_t expectedMetaCrc = ~crc32_calc(0xFFFFFFFF, buf, OTA_META_CRC_OFFSET);
    EXPECT_EQ(meta.meta_crc, expectedMetaCrc);
}

TEST(OtaMeta, WriteOtaMetaTruncatesLongVersion) {
    resetEnvironment();
    /* Version string longer than the 16-char field → truncated */
    EXPECT_TRUE(OtaServiceTestAccess::writeOtaMeta(
        ota(), 100, 0x12345678, "very-long-version-string-here"));
    uint8_t buf[64] = {};
    UINT len = 0;
    ASSERT_EQ(test_ff_read("ota_meta.bin", buf, sizeof(buf), &len), FR_OK);
    ota_meta_t meta;
    std::memcpy(&meta, buf, sizeof(meta));
    /* fw_version is 16 bytes max, last byte must be NUL */
    EXPECT_EQ(meta.fw_version[15], '\0');
}

// ── setOtaFlag ─────────────────────────────────────────────────────────────

TEST(OtaFlag, SetOtaFlagWritesBkpDr2Dr3) {
    resetEnvironment();
    /* Wipe BKP first */
    BKP->DR2 = 0;
    BKP->DR3 = 0;
    OtaServiceTestAccess::setOtaFlag(ota());
    EXPECT_EQ(BKP->DR2, OTA_FLAG_DR2_VALUE);
    EXPECT_EQ(BKP->DR3, OTA_FLAG_DR3_VALUE);
}

// ── httpGet ────────────────────────────────────────────────────────────────

TEST(OtaHttp, HttpGetCipStartFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPCLOSE (existing)
    esp.pushResponse("ERROR");   // CIPSTART → fail
    EXPECT_FALSE(OtaServiceTestAccess::httpGet(ota(), "host", 443, "/fw.bin"));
}

TEST(OtaHttp, HttpGetCipSendFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPCLOSE
    esp.pushResponse("OK");      // CIPSTART
    esp.pushResponse("ERROR");   // CIPSEND → no >
    EXPECT_FALSE(OtaServiceTestAccess::httpGet(ota(), "host", 443, "/fw.bin"));
}

TEST(OtaHttp, HttpGetSendOkFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPCLOSE
    esp.pushResponse("OK");      // CIPSTART
    esp.pushResponse(">");       // CIPSEND
    esp.pushResponse("");        // sendData
    esp.pushResponse("ERROR");   // waitFor SEND OK → fail
    EXPECT_FALSE(OtaServiceTestAccess::httpGet(ota(), "host", 443, "/fw.bin"));
}

TEST(OtaHttp, HttpGetHappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPCLOSE
    esp.pushResponse("OK");      // CIPSTART
    esp.pushResponse(">");       // CIPSEND
    esp.pushResponse("");        // sendData
    esp.pushResponse("SEND OK");
    EXPECT_TRUE(OtaServiceTestAccess::httpGet(ota(), "host", 443, "/fw.bin"));
}

// ── startUpdate orchestrator ───────────────────────────────────────────────

TEST(OtaStartUpdate, NoEspReturnsFalse) {
    resetEnvironment();
    ota().input.esp = nullptr;
    EXPECT_FALSE(ota().startUpdate("host", 443, "/fw.bin", 1000, 0));
}

TEST(OtaStartUpdate, AlreadyActiveReturnsFalse) {
    resetEnvironment();
    OtaServiceTestAccess::active(ota()) = true;
    EXPECT_FALSE(ota().startUpdate("host", 443, "/fw.bin", 1000, 0));
}

TEST(OtaStartUpdate, HttpGetFailureBailsEarly) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPCLOSE
    esp.pushResponse("ERROR");   // CIPSTART → bail

    EXPECT_FALSE(ota().startUpdate("host", 443, "/fw.bin", 1000, 0));
    EXPECT_FALSE(ota().isActive());
}

// ── receiveToFile ──────────────────────────────────────────────────────────

TEST(OtaReceive, ReceiveToFileNoIpdReturnsFalse) {
    /* receiveToFile waits for "+IPD," 4 times then bails. */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* Three rounds of "no +IPD" → noDataCount > 3 → bail */
    for (int i = 0; i < 5; ++i) esp.pushResponse("");
    EXPECT_FALSE(OtaServiceTestAccess::receiveToFile(ota(), 1000));
}

TEST(OtaReceive, ReceiveToFileServerClosedEarly) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* CLOSED in response → break out of loop with bytesWritten=0 → return false */
    esp.pushResponse("CLOSED");
    EXPECT_FALSE(OtaServiceTestAccess::receiveToFile(ota(), 1000));
}

TEST(OtaReceive, ReceiveToFileWith200OkAndBody) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* First +IPD with HTTP 200 OK headers + body */
    std::string ipd = "+IPD,50:HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nDATA";
    esp.pushResponse(ipd);
    /* Subsequent waitFor returns no +IPD → noDataCount loops then bails */
    for (int i = 0; i < 5; ++i) esp.pushResponse("");
    /* expected size = 4 (just "DATA") so first write completes */
    EXPECT_TRUE(OtaServiceTestAccess::receiveToFile(ota(), 4));
    EXPECT_EQ(OtaServiceTestAccess::progress(ota()), 100);
}

TEST(OtaReceive, ReceiveToFileNonHttpResponseFails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    std::string bad = "+IPD,12:GARBAGE\r\n\r\n";
    esp.pushResponse(bad);
    EXPECT_FALSE(OtaServiceTestAccess::receiveToFile(ota(), 100));
}

TEST(OtaReceive, ReceiveToFileHttp404Fails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    std::string ipd = "+IPD,30:HTTP/1.1 404 Not Found\r\n\r\n";
    esp.pushResponse(ipd);
    EXPECT_FALSE(OtaServiceTestAccess::receiveToFile(ota(), 100));
}
