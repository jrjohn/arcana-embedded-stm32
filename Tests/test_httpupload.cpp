/**
 * @file test_httpupload.cpp
 * @brief Host coverage suite for HttpUploadServiceImpl.cpp.
 *
 * Compiles the production HttpUploadServiceImpl.cpp + RegistrationServiceImpl.cpp
 * + AtsStorageServiceImpl.cpp against the same heavy stub set as test_atsstorage.
 *
 * Strategy:
 *   - bootStorage opens device.ats + sensor.ats so isReady=true → uploadPending
 *     enters the work path.
 *   - Inject fake YYYYMMDD.ats files into the in-memory FatFs so listPending
 *     returns non-zero.
 *   - Drive the upload sequence with programmed Esp8266 AT-command responses
 *     covering: success / cipstart-fail / sendOK-fail / +IPD-no-frame /
 *     server-already-done.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>
#include <new>

#include "stm32f1xx_hal.h"
#include "ff.h"

#include "AtsStorageServiceImpl.hpp"
#include "RegistrationServiceImpl.hpp"
#include "HttpUploadServiceImpl.hpp"
#include "Esp8266.hpp"
#include "SystemClock.hpp"

namespace arcana { namespace atsstorage {
struct AtsStorageTestAccess {
    static bool openDailyDb(AtsStorageServiceImpl& s) { return s.openDailyDb(); }
    static bool openDeviceDb(AtsStorageServiceImpl& s) { return s.openDeviceDb(); }
    static bool& dbReady(AtsStorageServiceImpl& s)        { return s.mDbReady; }
    static bool& deviceDbReady(AtsStorageServiceImpl& s)  { return s.mDeviceDbReady; }
    static ats::ArcanaTsDb& db(AtsStorageServiceImpl& s)  { return s.mDb; }
    static ats::ArcanaTsDb& deviceDb(AtsStorageServiceImpl& s) { return s.mDeviceDb; }
    static ats::FatFsFilePort& filePort(AtsStorageServiceImpl& s)        { return s.mFilePort; }
    static ats::FatFsFilePort& deviceFilePort(AtsStorageServiceImpl& s) { return s.mDeviceFilePort; }
    static ats::FreeRtosMutex& mutex(AtsStorageServiceImpl& s)          { return s.mMutex; }
};
}}

namespace arcana {
struct HttpUploadServiceTestAccess {
    static bool sendHttpHeader(Esp8266& esp, const char* filename,
                                const char* deviceId, uint32_t bodySize,
                                uint32_t rangeStart, uint32_t totalSize) {
        return HttpUploadServiceImpl::sendHttpHeader(
            esp, filename, deviceId, bodySize, rangeStart, totalSize);
    }
};
}

using arcana::HttpUploadServiceImpl;
using arcana::Esp8266;
using arcana::SystemClock;
using arcana::atsstorage::AtsStorageServiceImpl;
using arcana::atsstorage::AtsStorageTestAccess;

namespace {

AtsStorageServiceImpl& storage() {
    return static_cast<AtsStorageServiceImpl&>(
        AtsStorageServiceImpl::getInstance());
}

void resetEnvironment() {
    auto& s = storage();
    AtsStorageTestAccess::dbReady(s)       = false;
    AtsStorageTestAccess::deviceDbReady(s) = false;

    test_ff_reset();
    SystemClock::getInstance().resetForTest();
    Esp8266::getInstance().resetForTest();

    /* Reset the engine + file port + mutex (same trick as test_atsstorage) */
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

    /* Clear global upload progress */
    arcana::g_uploadProgress.totalFiles  = 0;
    arcana::g_uploadProgress.currentFile = 0;
    arcana::g_uploadProgress.bytesSent   = 0;
    arcana::g_uploadProgress.totalBytes  = 0;
    arcana::g_uploadProgress.resumeOffset = 0;
}

bool bootStorage() {
    auto& s = storage();
    if (s.initHAL() != arcana::ServiceStatus::OK) return false;
    if (s.init()    != arcana::ServiceStatus::OK) return false;
    if (!AtsStorageTestAccess::openDeviceDb(s))   return false;
    if (!AtsStorageTestAccess::openDailyDb(s))    return false;
    return true;
}

void createFakeDailyFile(const char* name, uint32_t size) {
    std::vector<uint8_t> bytes(size, 0xAB);
    test_ff_create(name, bytes.data(), (UINT)size);
}

} // anonymous namespace

// ── Smoke ──────────────────────────────────────────────────────────────────

TEST(HttpUploadSmoke, GlobalProgressIsAccessible) {
    resetEnvironment();
    EXPECT_EQ(arcana::g_uploadProgress.totalFiles, 0u);
}

// ── uploadPendingFiles ─────────────────────────────────────────────────────

TEST(HttpUploadPending, NotReadyReturnsZero) {
    resetEnvironment();
    /* No bootStorage → isReady() == false → early return */
    auto& esp = Esp8266::getInstance();
    EXPECT_EQ(HttpUploadServiceImpl::uploadPendingFiles(esp), 0u);
}

TEST(HttpUploadPending, NoPendingFilesReturnsZero) {
    resetEnvironment();
    ASSERT_TRUE(bootStorage());
    /* sensor.ats and device.ats exist but listPending filters them out
     * (not 12-char YYYYMMDD.ats format). */
    auto& esp = Esp8266::getInstance();
    EXPECT_EQ(HttpUploadServiceImpl::uploadPendingFiles(esp), 0u);
}

TEST(HttpUploadPending, CipStartFailureRunsRetryLoop) {
    /* uploadPendingFiles with a fake daily file but ESP failing all
     * AT commands → uploadFile retries up to MAX_ATTEMPTS=200 times then
     * gives up. Coverage of the listPending + retry-loop entry path. */
    resetEnvironment();
    ASSERT_TRUE(bootStorage());
    createFakeDailyFile("20260101.ats", 256);

    auto& esp = Esp8266::getInstance();
    /* Make every AT command fail — uploadFile will retry but eventually
     * the stall counter trips and it bails. */
    for (int i = 0; i < 100; ++i) esp.pushResponse("ERROR");

    uint8_t n = HttpUploadServiceImpl::uploadPendingFiles(esp);
    EXPECT_EQ(n, 0u);
}

TEST(HttpUploadPending, CipStartFailureBailsEarly) {
    resetEnvironment();
    ASSERT_TRUE(bootStorage());
    createFakeDailyFile("20260101.ats", 1024);

    auto& esp = Esp8266::getInstance();
    /* CIPSTART returns ERROR for queryServerOffset → bails;
     * MAX_ATTEMPTS (200) retries each fail → eventually breaks */
    for (int i = 0; i < 50; ++i) esp.pushResponse("ERROR");

    uint8_t n = HttpUploadServiceImpl::uploadPendingFiles(esp);
    /* No successful uploads */
    EXPECT_EQ(n, 0u);
}

// ── uploadFile direct ──────────────────────────────────────────────────────

TEST(HttpUploadFile, NonexistentFileReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    EXPECT_FALSE(HttpUploadServiceImpl::uploadFile(esp, "missing.ats", "DEADBEEF"));
}

TEST(HttpUploadFile, EmptyFileReturnsFalse) {
    resetEnvironment();
    /* Create a 0-byte file */
    test_ff_create("empty.ats", nullptr, 0);
    auto& esp = Esp8266::getInstance();
    EXPECT_FALSE(HttpUploadServiceImpl::uploadFile(esp, "empty.ats", "DEADBEEF"));
}

TEST(HttpUploadFile, ServerAlreadyHasFile) {
    resetEnvironment();
    /* Create a 100-byte file, server reports size:100 → already done branch */
    createFakeDailyFile("done.ats", 100);

    auto& esp = Esp8266::getInstance();
    /* queryServerOffset response */
    esp.pushResponse("OK");                  // CIPSTART
    esp.pushResponse(">");                   // CIPSEND
    esp.pushResponse("");                    // sendData header
    esp.pushResponse("SEND OK");             // waitFor SEND OK
    esp.pushResponse("\"size\":100");        // waitFor "size"
    esp.pushResponse("OK");                  // CIPCLOSE

    EXPECT_TRUE(HttpUploadServiceImpl::uploadFile(esp, "done.ats", "DEADBEEF"));
}

TEST(HttpUploadFile, FullUploadHappyPath) {
    /* End-to-end uploadFile success — drives the entire upload pipeline:
     * queryServerOffset → sslConnect → CIPMODE=1 → CIPSEND → header send →
     * streamFileBody → CIPMODE=0 → waitHttpResponse → sslClose. */
    resetEnvironment();
    createFakeDailyFile("happy.ats", 256);

    auto& esp = Esp8266::getInstance();

    /* 1. queryServerOffset: CIPSTART → CIPSEND → sendData → waitFor("SEND OK")
     *    → waitFor("\"size\"") → CIPCLOSE */
    esp.pushResponse("OK");                  // CIPSTART
    esp.pushResponse(">");                   // CIPSEND
    esp.pushResponse("");                    // sendData header
    esp.pushResponse("SEND OK");             // waitFor SEND OK
    esp.pushResponse("\"size\":0");          // waitFor "size" → offset 0
    esp.pushResponse("OK");                  // CIPCLOSE

    /* 2. sslConnect: CIPSTART */
    esp.pushResponse("OK");                  // CIPSTART
    /* 3. CIPMODE=1 */
    esp.pushResponse("OK");                  // CIPMODE=1
    /* 4. AT+CIPSEND (transparent mode) */
    esp.pushResponse(">");                   // CIPSEND
    /* 5. sendData header (POST) — empty response */
    esp.pushResponse("");                    // sendData header
    /* 6. streamFileBody — 256 bytes = 1 chunk via sendData */
    esp.pushResponse("");                    // sendData body chunk
    /* 7. sendData "+++" exit transparent mode */
    esp.pushResponse("");                    // sendData "+++"
    /* 8. AT+CIPMODE=0 */
    esp.pushResponse("OK");                  // CIPMODE=0
    /* 9. waitHttpResponse: waitFor "+IPD" → check "200" */
    esp.pushResponse("HTTP/1.1 200 OK\r\n+IPD,5:hello");
    /* 10. sslClose */
    esp.pushResponse("OK");                  // CIPCLOSE

    EXPECT_TRUE(HttpUploadServiceImpl::uploadFile(esp, "happy.ats", "DEADBEEF"));
    /* The Esp mock should have received many sentCmds */
    EXPECT_GE(esp.sentCmds().size(), 6u);
}

TEST(HttpUploadFile, CipModeFailureRetriesAndBails) {
    resetEnvironment();
    createFakeDailyFile("retry.ats", 128);
    auto& esp = Esp8266::getInstance();

    /* First attempt: queryServerOffset succeeds, then CIPMODE=1 fails */
    for (int attempt = 0; attempt < 5; ++attempt) {
        esp.pushResponse("OK");                  // CIPSTART query
        esp.pushResponse(">");                   // CIPSEND query
        esp.pushResponse("");                    // sendData query
        esp.pushResponse("SEND OK");             // waitFor SEND OK
        esp.pushResponse("\"size\":0");          // waitFor "size"
        esp.pushResponse("OK");                  // CIPCLOSE
        esp.pushResponse("OK");                  // CIPSTART upload
        esp.pushResponse("ERROR");               // CIPMODE=1 → fail
        esp.pushResponse("OK");                  // CIPCLOSE
    }

    /* Eventually stalls or runs out of attempts */
    bool ok = HttpUploadServiceImpl::uploadFile(esp, "retry.ats", "DEADBEEF");
    EXPECT_FALSE(ok);
}

TEST(HttpUploadFile, SendHeaderTransparentFailureBails) {
    resetEnvironment();
    createFakeDailyFile("nosend.ats", 64);
    auto& esp = Esp8266::getInstance();

    for (int attempt = 0; attempt < 5; ++attempt) {
        esp.pushResponse("OK");                  // CIPSTART query
        esp.pushResponse(">");                   // CIPSEND query
        esp.pushResponse("");                    // sendData query
        esp.pushResponse("SEND OK");             // waitFor SEND OK
        esp.pushResponse("\"size\":0");          // waitFor "size"
        esp.pushResponse("OK");                  // CIPCLOSE
        esp.pushResponse("OK");                  // CIPSTART upload
        esp.pushResponse("OK");                  // CIPMODE=1
        esp.pushResponse("ERROR");               // CIPSEND → fails (no >)
        esp.pushResponse("OK");                  // CIPMODE=0
        esp.pushResponse("OK");                  // CIPCLOSE
    }

    bool ok = HttpUploadServiceImpl::uploadFile(esp, "nosend.ats", "DEADBEEF");
    EXPECT_FALSE(ok);
}

TEST(HttpUploadFile, WaitHttpResponseSuccess200) {
    resetEnvironment();
    createFakeDailyFile("rsp.ats", 100);
    auto& esp = Esp8266::getInstance();

    /* Drive a single attempt where waitHttpResponse sees "200" */
    esp.pushResponse("OK");                  // CIPSTART query
    esp.pushResponse(">");                   // CIPSEND query
    esp.pushResponse("");                    // sendData query
    esp.pushResponse("SEND OK");
    esp.pushResponse("\"size\":0");
    esp.pushResponse("OK");                  // CIPCLOSE query
    esp.pushResponse("OK");                  // CIPSTART upload
    esp.pushResponse("OK");                  // CIPMODE=1
    esp.pushResponse(">");                   // CIPSEND
    esp.pushResponse("");                    // sendData header
    esp.pushResponse("");                    // sendData body
    esp.pushResponse("");                    // sendData +++
    esp.pushResponse("OK");                  // CIPMODE=0
    esp.pushResponse("+IPD,5:HTTP/1.1 200 OK\r\n");
    esp.pushResponse("OK");                  // CIPCLOSE

    EXPECT_TRUE(HttpUploadServiceImpl::uploadFile(esp, "rsp.ats", "DEADBEEF"));
}

TEST(HttpUploadFile, WaitHttpResponseCompleteFallback) {
    resetEnvironment();
    createFakeDailyFile("rsp2.ats", 100);
    auto& esp = Esp8266::getInstance();

    /* Server responds without "200" but with "complete" in body */
    esp.pushResponse("OK");                  // CIPSTART query
    esp.pushResponse(">");                   // CIPSEND query
    esp.pushResponse("");                    // sendData query
    esp.pushResponse("SEND OK");
    esp.pushResponse("\"size\":0");
    esp.pushResponse("OK");                  // CIPCLOSE query
    esp.pushResponse("OK");                  // CIPSTART upload
    esp.pushResponse("OK");                  // CIPMODE=1
    esp.pushResponse(">");                   // CIPSEND
    esp.pushResponse("");                    // sendData header
    esp.pushResponse("");                    // sendData body
    esp.pushResponse("");                    // sendData +++
    esp.pushResponse("OK");                  // CIPMODE=0
    esp.pushResponse("+IPD,8:complete");
    esp.pushResponse("OK");                  // CIPCLOSE

    EXPECT_TRUE(HttpUploadServiceImpl::uploadFile(esp, "rsp2.ats", "DEADBEEF"));
}

TEST(HttpUploadPending, FullSuccessTriggersMarkUploadedAndDeviceAts) {
    /* End-to-end uploadPendingFiles success path: queues responses for
     * 1 daily file (full happy path) + the device.ats follow-up.
     * Covers lines 93-110 (markUploaded → uploaded++ → device.ats upload). */
    resetEnvironment();
    ASSERT_TRUE(bootStorage());
    createFakeDailyFile("20260301.ats", 200);

    auto& esp = Esp8266::getInstance();

    /* Helper lambda: push the 14-response sequence for one full uploadFile */
    auto pushOneUploadFileSequence = [&]() {
        esp.pushResponse("OK");                  // CIPSTART query
        esp.pushResponse(">");                   // CIPSEND query
        esp.pushResponse("");                    // sendData query
        esp.pushResponse("SEND OK");
        esp.pushResponse("\"size\":0");
        esp.pushResponse("OK");                  // CIPCLOSE query
        esp.pushResponse("OK");                  // CIPSTART upload
        esp.pushResponse("OK");                  // CIPMODE=1
        esp.pushResponse(">");                   // CIPSEND
        esp.pushResponse("");                    // sendData header
        esp.pushResponse("");                    // sendData body
        esp.pushResponse("");                    // sendData +++
        esp.pushResponse("OK");                  // CIPMODE=0
        esp.pushResponse("+IPD:HTTP/1.1 200 OK");
        esp.pushResponse("OK");                  // CIPCLOSE
    };

    pushOneUploadFileSequence();   // for 20260301.ats
    pushOneUploadFileSequence();   // for device.ats follow-up

    uint8_t n = HttpUploadServiceImpl::uploadPendingFiles(esp);
    EXPECT_EQ(n, 1u);
}

TEST(HttpUploadFile, AlreadyConnectedRecoveryPath) {
    /* sslConnect: CIPSTART returns ALREADY CONNECTED → responseContains
     * branch returns true, sslConnect succeeds. Covers line 295. */
    resetEnvironment();
    createFakeDailyFile("ac.ats", 100);
    auto& esp = Esp8266::getInstance();

    /* queryServerOffset uses sslConnect — feed ALREADY CONNECTED there */
    esp.pushResponse("ALREADY CONNECTED");       // CIPSTART query (no OK)
    esp.pushResponse(">");                       // CIPSEND
    esp.pushResponse("");                        // sendData
    esp.pushResponse("SEND OK");
    esp.pushResponse("\"size\":100");            // already done → fast path
    esp.pushResponse("OK");                      // CIPCLOSE

    EXPECT_TRUE(HttpUploadServiceImpl::uploadFile(esp, "ac.ats", "DEADBEEF"));
}

TEST(HttpUploadFile, QueryServerOffsetCipSendFailReturnsZero) {
    /* queryServerOffset: CIPSEND > fails → bails with offset=0 → uploadFile
     * proceeds to actual upload with offset 0. We make subsequent CIPSTART
     * also fail to short-circuit the test. */
    resetEnvironment();
    createFakeDailyFile("nq.ats", 50);
    auto& esp = Esp8266::getInstance();

    /* Fail CIPSEND in queryServerOffset, then fail every retry */
    for (int i = 0; i < 30; ++i) {
        esp.pushResponse("OK");                  // CIPSTART query
        esp.pushResponse("ERROR");               // CIPSEND query → no >
        esp.pushResponse("OK");                  // CIPCLOSE
        esp.pushResponse("ERROR");               // CIPSTART upload → fail
        /* retry loop continues */
    }

    bool ok = HttpUploadServiceImpl::uploadFile(esp, "nq.ats", "DEADBEEF");
    EXPECT_FALSE(ok);
}

TEST(HttpUploadFile, QueryServerOffsetSendOkFailReturnsZero) {
    resetEnvironment();
    createFakeDailyFile("nso.ats", 50);
    auto& esp = Esp8266::getInstance();

    for (int i = 0; i < 30; ++i) {
        esp.pushResponse("OK");                  // CIPSTART query
        esp.pushResponse(">");                   // CIPSEND query
        esp.pushResponse("");                    // sendData query
        esp.pushResponse("ERROR");               // waitFor SEND OK → fail
        esp.pushResponse("OK");                  // CIPCLOSE query
        esp.pushResponse("ERROR");               // CIPSTART upload → fail
    }

    bool ok = HttpUploadServiceImpl::uploadFile(esp, "nso.ats", "DEADBEEF");
    EXPECT_FALSE(ok);
}

TEST(HttpUploadFile, QueryServerOffsetNoSizeFieldReturnsZero) {
    resetEnvironment();
    createFakeDailyFile("nosize.ats", 50);
    auto& esp = Esp8266::getInstance();

    for (int i = 0; i < 30; ++i) {
        esp.pushResponse("OK");                  // CIPSTART query
        esp.pushResponse(">");                   // CIPSEND query
        esp.pushResponse("");                    // sendData query
        esp.pushResponse("SEND OK");
        esp.pushResponse("garbage");             // waitFor "size" → fail
        esp.pushResponse("OK");                  // CIPCLOSE query
        esp.pushResponse("ERROR");               // CIPSTART upload → fail
    }

    bool ok = HttpUploadServiceImpl::uploadFile(esp, "nosize.ats", "DEADBEEF");
    EXPECT_FALSE(ok);
}

TEST(HttpUploadFile, ResumeFromServerOffset) {
    /* Server reports partial offset → uploadFile resumes from there.
     * Exercises the f_lseek path + Content-Range header. */
    resetEnvironment();
    createFakeDailyFile("resume.ats", 1000);
    auto& esp = Esp8266::getInstance();

    esp.pushResponse("OK");                  // CIPSTART query
    esp.pushResponse(">");                   // CIPSEND query
    esp.pushResponse("");                    // sendData query
    esp.pushResponse("SEND OK");
    esp.pushResponse("\"size\":500");        // server has half — resume from 500
    esp.pushResponse("OK");                  // CIPCLOSE query
    esp.pushResponse("OK");                  // CIPSTART upload
    esp.pushResponse("OK");                  // CIPMODE=1
    esp.pushResponse(">");                   // CIPSEND
    esp.pushResponse("");                    // sendData header
    esp.pushResponse("");                    // sendData body chunk
    esp.pushResponse("");                    // sendData +++
    esp.pushResponse("OK");                  // CIPMODE=0
    esp.pushResponse("+IPD,5:HTTP/1.1 200 OK");
    esp.pushResponse("OK");                  // CIPCLOSE

    EXPECT_TRUE(HttpUploadServiceImpl::uploadFile(esp, "resume.ats", "DEADBEEF"));
    EXPECT_EQ(arcana::g_uploadProgress.resumeOffset, 500u);
}

// ── sendHttpHeader (legacy non-transparent path) ────────────────────────────
//
// sendHttpHeader is declared in the header but only its inline twin is used
// from uploadFile. We exercise the static helper directly to claim coverage
// for the dead-but-shipped code path.

TEST(HttpUploadHeader, SendHttpHeaderHappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">");        // CIPSEND prompt
    esp.pushResponse("");         // sendData
    esp.pushResponse("SEND OK");  // waitFor

    EXPECT_TRUE(arcana::HttpUploadServiceTestAccess::sendHttpHeader(
        esp, "20260101.ats", "DEADBEEF", /*body*/ 100, /*range*/ 0, /*total*/ 100));
}

TEST(HttpUploadHeader, SendHttpHeaderCipsendFails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ERROR");    // CIPSEND prompt fails

    EXPECT_FALSE(arcana::HttpUploadServiceTestAccess::sendHttpHeader(
        esp, "20260101.ats", "DEADBEEF", 100, 0, 100));
}

TEST(HttpUploadHeader, SendHttpHeaderSendOkFails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse(">");        // CIPSEND prompt
    esp.pushResponse("");         // sendData
    esp.pushResponse("ERROR");    // waitFor SEND OK → fail

    EXPECT_FALSE(arcana::HttpUploadServiceTestAccess::sendHttpHeader(
        esp, "20260101.ats", "DEADBEEF", 100, 0, 100));
}
