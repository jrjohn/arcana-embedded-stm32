/**
 * @file test_fatfs_file_port.cpp
 * @brief Host coverage for FatFsFilePort.cpp retry + error branches.
 *
 * The in-memory ff_host_stub now supports fault injection:
 *   - test_ff_fail_read(N)  → next N reads return FR_DISK_ERR
 *   - test_ff_fail_write(N) → next N writes return FR_DISK_ERR
 *   - test_ff_fail_lseek(N) → next N lseeks return FR_INT_ERR
 *   - test_ff_fail_sync(N)  → next N syncs return FR_DISK_ERR
 *   - test_ff_set_n_fats(n) → flip the singleton FATFS n_fats field
 *
 * That lets us drive the 3-retry + sdio_force_reinit fallback paths in
 * read/write/seek/sync, the seek-beyond-EOF zero-fill recovery, and the
 * truncate(n_fats=1) → f_sync branch — all of which the production tests
 * (test_atsstorage etc) leave uncov because the mock never fails.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "ff.h"
#include "ats/ArcanaTsTypes.hpp"
#include "ats/IFilePort.hpp"
#include "FatFsFilePort.hpp"

using arcana::ats::FatFsFilePort;
using arcana::ats::ATS_MODE_READ;
using arcana::ats::ATS_MODE_WRITE;
using arcana::ats::ATS_MODE_CREATE;

extern "C" void sdio_force_reinit(void) {}

namespace {

void resetEnv() {
    test_ff_reset();
}

} // anonymous namespace

// ── open / close ────────────────────────────────────────────────────────────

TEST(FatFsFilePortBasic, OpenAndCloseRoundTrip) {
    resetEnv();
    FatFsFilePort fp;
    EXPECT_FALSE(fp.isOpen());
    EXPECT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    EXPECT_TRUE(fp.isOpen());
    EXPECT_TRUE(fp.close());
    EXPECT_FALSE(fp.isOpen());
}

TEST(FatFsFilePortBasic, OpenWhileAlreadyOpenReturnsFalse) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    EXPECT_FALSE(fp.open("b.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
}

TEST(FatFsFilePortBasic, OpenMissingExistingReportsFalseWithoutLog) {
    resetEnv();
    FatFsFilePort fp;
    /* FR_NO_FILE is the silent branch — no log warning. */
    EXPECT_FALSE(fp.open("nope.dat", ATS_MODE_READ));
    EXPECT_FALSE(fp.isOpen());
}

TEST(FatFsFilePortBasic, CloseWhenNotOpenReturnsFalse) {
    resetEnv();
    FatFsFilePort fp;
    EXPECT_FALSE(fp.close());
}

// ── read / write happy paths ────────────────────────────────────────────────

TEST(FatFsFilePortIo, WriteThenReadBack) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("rw.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    EXPECT_EQ(fp.write(payload, 5), 5);
    EXPECT_EQ(fp.tell(), 5u);
    EXPECT_EQ(fp.size(), 5u);
    EXPECT_TRUE(fp.close());

    /* Reopen for read */
    ASSERT_TRUE(fp.open("rw.dat", ATS_MODE_READ));
    uint8_t buf[5] = {};
    EXPECT_EQ(fp.read(buf, 5), 5);
    EXPECT_EQ(0, std::memcmp(buf, payload, 5));
    fp.close();
}

TEST(FatFsFilePortIo, ReadOnNotOpenReturnsMinusOne) {
    FatFsFilePort fp;
    uint8_t buf[4];
    EXPECT_EQ(fp.read(buf, 4), -1);
}

TEST(FatFsFilePortIo, WriteOnNotOpenReturnsMinusOne) {
    FatFsFilePort fp;
    EXPECT_EQ(fp.write((const uint8_t*)"x", 1), -1);
}

// ── retry + reinit branches via fault injection ─────────────────────────────

TEST(FatFsFilePortRetry, ReadFailsAllRoundsReturnsMinusOne) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    const uint8_t one = 1;
    fp.write(&one, 1);
    fp.close();
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_READ));

    /* 2 rounds × 3 retries = 6 failures → never recovers */
    test_ff_fail_read(6);
    uint8_t buf[1];
    EXPECT_EQ(fp.read(buf, 1), -1);
}

TEST(FatFsFilePortRetry, ReadRecoversAfterReinit) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    const uint8_t one = 0xAB;
    fp.write(&one, 1);
    fp.close();
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_READ));

    /* Round 0 fails 3 times → sdio_force_reinit → round 1 succeeds */
    test_ff_fail_read(3);
    uint8_t buf[1];
    EXPECT_EQ(fp.read(buf, 1), 1);
    EXPECT_EQ(buf[0], 0xAB);
}

TEST(FatFsFilePortRetry, WriteFailsAllRoundsReturnsMinusOne) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    test_ff_fail_write(6);
    EXPECT_EQ(fp.write((const uint8_t*)"x", 1), -1);
}

TEST(FatFsFilePortRetry, WriteRecoversAfterReinit) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    test_ff_fail_write(3);
    EXPECT_EQ(fp.write((const uint8_t*)"yz", 2), 2);
    /* The successful write should have actually landed */
    EXPECT_EQ(fp.size(), 2u);
}

TEST(FatFsFilePortRetry, SeekFailsAllRoundsHitsZeroFillFallback) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    /* Write 100 bytes so curSize = 100 */
    uint8_t pad[100] = {};
    fp.write(pad, sizeof(pad));

    /* Seek to 200 (beyond curSize=100). All 6 fast-path lseek attempts fail
     * → falls into the curSize-based zero-fill recovery. */
    test_ff_fail_lseek(6);
    EXPECT_TRUE(fp.seek(200));
    EXPECT_EQ(fp.size(), 200u);
}

TEST(FatFsFilePortRetry, SeekFailsBeforeEofReturnsFalse) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    uint8_t pad[100] = {};
    fp.write(pad, sizeof(pad));

    /* Seek to 50 (within file). All 6 attempts fail → no zero-fill (offset
     * < curSize) → return false. */
    test_ff_fail_lseek(6);
    EXPECT_FALSE(fp.seek(50));
}

TEST(FatFsFilePortRetry, SyncFailsBothRoundsReturnsFalse) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    test_ff_fail_sync(2);
    EXPECT_FALSE(fp.sync());
}

TEST(FatFsFilePortRetry, SyncRecoversAfterReinit) {
    resetEnv();
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    test_ff_fail_sync(1);
    EXPECT_TRUE(fp.sync());
}

// ── truncate: n_fats=2 (TexFAT) vs n_fats=1 (single-FAT, sync only) ────────

TEST(FatFsFilePortTruncate, TexfatBranchCallsFTruncate) {
    resetEnv();
    test_ff_set_n_fats(2);
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    fp.write((const uint8_t*)"hello", 5);
    EXPECT_TRUE(fp.truncate());
}

TEST(FatFsFilePortTruncate, SingleFatBranchCallsFSyncInstead) {
    resetEnv();
    test_ff_set_n_fats(1);
    FatFsFilePort fp;
    ASSERT_TRUE(fp.open("a.dat", ATS_MODE_WRITE | ATS_MODE_CREATE));
    fp.write((const uint8_t*)"hello", 5);
    EXPECT_TRUE(fp.truncate());
    /* Restore default for other tests */
    test_ff_set_n_fats(2);
}

TEST(FatFsFilePortTruncate, NotOpenReturnsFalse) {
    FatFsFilePort fp;
    EXPECT_FALSE(fp.truncate());
}

// ── tell / size when not open ───────────────────────────────────────────────

TEST(FatFsFilePortInfo, TellWhenNotOpenIsZero) {
    FatFsFilePort fp;
    EXPECT_EQ(fp.tell(), 0u);
}

TEST(FatFsFilePortInfo, SizeWhenNotOpenIsZero) {
    FatFsFilePort fp;
    EXPECT_EQ(fp.size(), 0u);
}

// ── seek when not open ──────────────────────────────────────────────────────

TEST(FatFsFilePortSeek, NotOpenReturnsFalse) {
    FatFsFilePort fp;
    EXPECT_FALSE(fp.seek(0));
}

// ── sync when not open ──────────────────────────────────────────────────────

TEST(FatFsFilePortSync, NotOpenReturnsFalse) {
    FatFsFilePort fp;
    EXPECT_FALSE(fp.sync());
}
