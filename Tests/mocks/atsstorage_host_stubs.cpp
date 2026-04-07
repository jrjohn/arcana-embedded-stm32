/**
 * @file atsstorage_host_stubs.cpp
 *
 * Strong-symbol definitions backing the mock headers used by test_atsstorage:
 *   - io::IoServiceImpl::getInstance()
 *   - sdbench::SdBenchmarkServiceImpl::getInstance()
 *   - sdbench::sFatFs (extern FATFS)
 *   - display::g_display = nullptr (DisplayStatus inline functions early-return)
 *   - g_exfat_ready = 1
 *   - sdio_force_reinit / sd_card_full_reinit / ats_safe_eject / texfat_format
 *     (no-op extern "C" stubs)
 *
 * IMPORTANT: This file is linked into test_atsstorage (which compiles the
 * REAL AtsStorageServiceImpl.cpp). It must NOT be linked into test_registration
 * (which uses AtsStorageServiceImpl_stub.cpp instead).
 */
/* The mocks/{IoServiceImpl,SdBenchmarkServiceImpl}.hpp files we wrote earlier
 * are dead code: AtsStorageServiceImpl.cpp lives in F103_SVC_IMPL alongside
 * the real headers, so the C++ same-directory rule pins includes there. We
 * therefore include the REAL headers here and provide strong-symbol stubs
 * for the methods AtsStorageServiceImpl.cpp actually calls. */
#include "IoServiceImpl.hpp"
#include "SdBenchmarkServiceImpl.hpp"
#include "SdCard.hpp"
#include "ff.h"

namespace arcana {

/* display::g_display = nullptr → DisplayStatus.hpp inline functions all
 * early-return on `if (!g_display) return;`. */
namespace display {
class IDisplay;
IDisplay* g_display = nullptr;
} // namespace display

/* ── IoServiceImpl singleton stub (real header, host body) ─────────────── */
namespace io {

IoServiceImpl::IoServiceImpl()
    : mUploadRequested(false)
    , mCancelRequested(false)
    , mFormatRequested(false)
    , mCancelArmed(false)
    , mCooldownUntil(0)
    , mKey2Seen(false)
    , mKey2Prev(false)
    , mKey1Hold(0)
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
{}

IoServiceImpl& IoServiceImpl::getInstance() {
    static IoServiceImpl sInstance;
    return sInstance;
}

ServiceStatus IoServiceImpl::initHAL() { return ServiceStatus::OK; }
ServiceStatus IoServiceImpl::start()   { return ServiceStatus::OK; }
void IoServiceImpl::armCancel()        {}
void IoServiceImpl::disarmCancel()     {}

/* The static private taskFunc is referenced by xTaskCreateStatic in the
 * real start() — but our stub start() never calls it. Provide a no-op
 * definition to satisfy any vtable / static-dispatch requirement. */
void IoServiceImpl::taskFunc(void*) {}
void IoServiceImpl::taskLoop()       {}

} // namespace io

/* ── SdCard singleton stub ────────────────────────────────────────────── */
SdCard::SdCard() : mReady(false) {}
SdCard::~SdCard() = default;
SdCard& SdCard::getInstance() {
    static SdCard sInstance;
    return sInstance;
}
bool SdCard::initHAL() { return true; }
bool SdCard::writeBlocks(const uint8_t*, uint32_t, uint32_t) { return true; }
bool SdCard::startWrite(const uint8_t*, uint32_t, uint32_t) { return true; }
bool SdCard::waitWrite() { return true; }
uint32_t SdCard::getLastError() const { return 0; }
uint32_t SdCard::getBlockCount() const { return 0; }
void SdCard::initGpio() {}

/* ── SdBenchmarkServiceImpl singleton stub (real header, host body) ────── */
namespace sdbench {

FATFS sFatFs = {};

SdBenchmarkServiceImpl::SdBenchmarkServiceImpl()
    : mSd(SdCard::getInstance())
    , mRunning(false)
    , mBlockCount(0)
    , mStatsObs("stub")
    , mStats()
    , mWindowStartTick(0)
    , mBytesInWindow(0)
    , mRecordsInWindow(0)
    , mTotalBytesWritten(0)
    , mTotalRecords(0)
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
{
    output.StatsEvents = &mStatsObs;
}

SdBenchmarkServiceImpl::~SdBenchmarkServiceImpl() = default;

SdBenchmarkService& SdBenchmarkServiceImpl::getInstance() {
    static SdBenchmarkServiceImpl sInstance;
    return sInstance;
}

ServiceStatus SdBenchmarkServiceImpl::initHAL() { return ServiceStatus::OK; }
ServiceStatus SdBenchmarkServiceImpl::init()    { return ServiceStatus::OK; }
ServiceStatus SdBenchmarkServiceImpl::start()   { return ServiceStatus::OK; }
void SdBenchmarkServiceImpl::stop()             {}
void SdBenchmarkServiceImpl::refreshSdInfo()    {}

/* Internal methods referenced via taskFunc — provide no-op definitions so
 * the linker is happy even though we never invoke them. */
void SdBenchmarkServiceImpl::benchmarkTask(void*) {}
void SdBenchmarkServiceImpl::runBenchmark()       {}
void SdBenchmarkServiceImpl::fillAndEncryptBuffer(uint8_t*, uint32_t) {}
void SdBenchmarkServiceImpl::publishStats()       {}

} // namespace sdbench

} // namespace arcana

/* AtsStorageServiceImpl.cpp itself defines ats_safe_eject() at file scope —
 * don't redefine here. Production texfat_format() returns FRESULT, not int. */
extern "C" {
volatile uint8_t g_exfat_ready = 1;
void    sdio_force_reinit(void)   {}
void    sd_card_full_reinit(void) {}
FRESULT texfat_format(void)       { return FR_OK; }
} // extern "C"
