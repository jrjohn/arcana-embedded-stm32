/**
 * @file AtsStorageServiceImpl_stub.cpp
 * @brief Host stub for AtsStorageServiceImpl symbols.
 *
 * RegistrationServiceImpl.cpp lives in the same directory as the real
 * AtsStorageServiceImpl.hpp, so the C++ "quoted-include same-directory-first"
 * rule pins it to the production header — we cannot substitute the header.
 *
 * Workaround: include the production header here too, then provide minimal
 * stub definitions for the methods called from RegistrationServiceImpl
 * (getInstance, ctor/dtor, loadCredentials, saveCredentials, plus the static
 * buffers and pure-virtual overrides that the linker needs to construct the
 * class). Test code drives behavior via the helpers at the bottom of this
 * file (test_storage_*).
 *
 * The non-trivial members (ArcanaTsDb, FatFsFilePort, FreeRtosMutex,
 * ChaCha20Cipher, Observable<StorageStatsModel>) are themselves host-portable
 * — Tests/CMakeLists.txt links their .cpp impls into this target.
 */
#include "AtsStorageServiceImpl.hpp"

#include <cstring>

namespace arcana {
namespace atsstorage {

/* ── Static member definitions (declarations live in the real header) ────── */
uint8_t AtsStorageServiceImpl::sKey[crypto::ChaCha20::KEY_SIZE]   = {};
uint8_t AtsStorageServiceImpl::sSlowBuf[ats::BLOCK_SIZE]          = {};
uint8_t AtsStorageServiceImpl::sReadCache[ats::BLOCK_SIZE]        = {};
uint8_t AtsStorageServiceImpl::sDevSlowBuf[ats::BLOCK_SIZE]       = {};
FIL     AtsStorageServiceImpl::sSharedFil                         = {};

/* ── Test-controllable internal state ────────────────────────────────────── */
namespace {
bool     g_loadOk    = false;
bool     g_saveOk    = true;
uint8_t  g_stored[256] = {};
uint16_t g_storedLen = 0;
} // anonymous namespace

/* ── Stub ctor: minimal — exercises member ctors so the class can exist ──── */
AtsStorageServiceImpl::AtsStorageServiceImpl()
    : mDb()
    , mFilePort()
    , mMutex()
    , mCipher()
    , mDeviceDb()
    , mDeviceFilePort()
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    /* mDbReady is read by inline isReady(); always-true on host so the
     * RegistrationServiceImpl "ready" branch is exercised. The "not ready"
     * branch (~5 lines) remains uncov but doesn't justify a friend-access. */
    , mDbReady(true)
    , mDeviceDbReady(true)
    , mFormatRequested(false)
    , mUploadPause(false)
    , mUploadRequested(false)
    , mPendingData()
    , mWriteSemBuffer()
    , mWriteSem(0)
    , mStatsObs("stub")
    , mStatsModel()
    , mTotalRecords(0)
    , mWindowStartTick(0)
    , mWritesInWindow(0)
    , mLastRate(0)
    , mBaselineBlocksFailed(0)
{
    input.SensorData  = nullptr;
    output.StatsEvents = &mStatsObs;
}

AtsStorageServiceImpl::~AtsStorageServiceImpl() = default;

AtsStorageService& AtsStorageServiceImpl::getInstance() {
    static AtsStorageServiceImpl sInstance;
    return sInstance;
}

/* ── Minimal AtsStorageService overrides (pure-virtual in base) ──────────── */
ServiceStatus AtsStorageServiceImpl::initHAL() { return ServiceStatus::OK; }
ServiceStatus AtsStorageServiceImpl::init()    { return ServiceStatus::OK; }
ServiceStatus AtsStorageServiceImpl::start()   { return ServiceStatus::OK; }
void          AtsStorageServiceImpl::stop()    {}
uint16_t      AtsStorageServiceImpl::queryByDate(uint32_t /*d*/,
                                                 SensorDataModel* /*o*/,
                                                 uint16_t /*m*/) {
    return 0;
}

/* ── The two methods RegistrationServiceImpl actually calls ──────────────── */
bool AtsStorageServiceImpl::loadCredentials(uint8_t* out, uint16_t bufSize,
                                             uint16_t& outLen) {
    if (!g_loadOk) { outLen = 0; return false; }
    uint16_t n = g_storedLen < bufSize ? g_storedLen : bufSize;
    std::memcpy(out, g_stored, n);
    outLen = n;
    return true;
}

bool AtsStorageServiceImpl::saveCredentials(const uint8_t* data, uint16_t len) {
    if (!g_saveOk) return false;
    if (len > sizeof(g_stored)) len = sizeof(g_stored);
    std::memcpy(g_stored, data, len);
    g_storedLen = len;
    g_loadOk    = true;     /* successful save → next load returns the bytes */
    return true;
}

} // namespace atsstorage
} // namespace arcana

/* ── C-linkage symbols referenced by RegistrationServiceImpl.cpp ─────────── */
extern "C" volatile uint8_t g_exfat_ready = 1;

/* sdio_force_reinit / sd_card_full_reinit / texfat_format / ats_safe_eject
 * are referenced by FatFsFilePort and AtsStorageServiceImpl. They have no
 * meaning on host — provide no-op stubs so linking succeeds. */
extern "C" {
void sdio_force_reinit(void)   {}
void sd_card_full_reinit(void) {}
void ats_safe_eject(void)      {}
int  texfat_format(void)       { return 0; /* FR_OK */ }
}

/* ── Test control surface ────────────────────────────────────────────────── */
namespace arcana { namespace atsstorage {
void test_storage_set_load_ok(bool ok) { g_loadOk = ok; }
void test_storage_set_save_ok(bool ok) { g_saveOk = ok; }
void test_storage_set_stored(const uint8_t* data, uint16_t len) {
    if (len > sizeof(g_stored)) len = sizeof(g_stored);
    std::memcpy(g_stored, data, len);
    g_storedLen = len;
}
void test_storage_reset() {
    g_loadOk = false;
    g_saveOk = true;
    std::memset(g_stored, 0, sizeof(g_stored));
    g_storedLen = 0;
}
const uint8_t* test_storage_stored() { return g_stored; }
uint16_t test_storage_stored_len() { return g_storedLen; }
}} // namespace arcana::atsstorage
