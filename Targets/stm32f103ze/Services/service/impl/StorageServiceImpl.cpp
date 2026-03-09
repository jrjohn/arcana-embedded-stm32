#include "StorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include <cstring>
#include "stm32f1xx_hal.h"

namespace arcana {
namespace storage {

// Per-device encryption key (derived from master secret + hardware UID)
uint8_t StorageServiceImpl::sKey[crypto::ChaCha20::KEY_SIZE] = {};

const char* StorageServiceImpl::FILENAME = "sensor.dat";

StorageServiceImpl::StorageServiceImpl()
    : mLfs()
    , mFlash(FlashBlockDevice::getInstance())
    , mMounted(false)
    , mRecordCount(0)
    , mFile()
    , mFileBuf{}
    , mFileConfig()
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    , mPendingData()
    , mWriteSemBuffer()
    , mWriteSem(0)
    , mMutexBuffer()
    , mMutex(0)
    , mStatsObs("StorageSvc Stats")
    , mStats()
    , mWindowStartTick(0)
    , mWritesInWindow(0)
    , mLastRate(0)
{
    input.SensorData = 0;
    output.StatsEvents = &mStatsObs;
}

StorageServiceImpl::~StorageServiceImpl() {
    stop();
}

StorageService& StorageServiceImpl::getInstance() {
    static StorageServiceImpl sInstance;
    return sInstance;
}

ServiceStatus StorageServiceImpl::initHAL() {
    // Derive per-device encryption key from hardware UID
    crypto::DeviceKey::deriveKey(sKey);
    return ServiceStatus::OK;
}

ServiceStatus StorageServiceImpl::init() {
    // Create mutex
    mMutex = xSemaphoreCreateMutexStatic(&mMutexBuffer);
    if (!mMutex) return ServiceStatus::Error;

    // Create binary semaphore for task signaling
    mWriteSem = xSemaphoreCreateBinaryStatic(&mWriteSemBuffer);
    if (!mWriteSem) return ServiceStatus::Error;

    // Mount filesystem
    lfs_config* cfg = mFlash.getConfig();
    int err = lfs_mount(&mLfs, cfg);
    if (err) {
        // First use: format then mount
        err = lfs_format(&mLfs, cfg);
        if (err) return ServiceStatus::Error;

        err = lfs_mount(&mLfs, cfg);
        if (err) return ServiceStatus::Error;
    }
    mMounted = true;

    // Read existing record count from file header
    memset(&mFileConfig, 0, sizeof(mFileConfig));
    mFileConfig.buffer = mFileBuf;

    err = lfs_file_opencfg(&mLfs, &mFile, FILENAME,
                           LFS_O_RDONLY, &mFileConfig);
    if (err == LFS_ERR_OK) {
        uint8_t header[HEADER_SIZE];
        lfs_ssize_t rd = lfs_file_read(&mLfs, &mFile, header, HEADER_SIZE);
        if (rd == HEADER_SIZE) {
            mRecordCount = (uint32_t)header[0]       | ((uint32_t)header[1] << 8) |
                           ((uint32_t)header[2] << 16) | ((uint32_t)header[3] << 24);
        }
        lfs_file_close(&mLfs, &mFile);
    }

    return ServiceStatus::OK;
}

ServiceStatus StorageServiceImpl::start() {
    if (!mMounted) return ServiceStatus::Error;

    // Launch dedicated storage task
    mRunning = true;
    mTaskHandle = xTaskCreateStatic(
        storageTask, "Storage", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1, mTaskStack, &mTaskBuffer);
    if (!mTaskHandle) return ServiceStatus::Error;

    // Subscribe to sensor data events
    if (input.SensorData) {
        input.SensorData->subscribe(onSensorData, this);
    }

    return ServiceStatus::OK;
}

void StorageServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        xSemaphoreGive(mWriteSem);  // Wake task so it can exit
        vTaskDelay(pdMS_TO_TICKS(50));
        mTaskHandle = 0;
    }
    if (mMounted) {
        lfs_unmount(&mLfs);
        mMounted = false;
    }
}

void StorageServiceImpl::onSensorData(SensorDataModel* model, void* context) {
    StorageServiceImpl* self = static_cast<StorageServiceImpl*>(context);
    // Lightweight: copy data and signal the dedicated task
    self->mPendingData = *model;
    xSemaphoreGive(self->mWriteSem);
}

void StorageServiceImpl::storageTask(void* param) {
    StorageServiceImpl* self = static_cast<StorageServiceImpl*>(param);

    // Benchmark mode: write as fast as possible with fake sensor data
    SensorDataModel fakeData;
    fakeData.temperature = 25.5f;
    fakeData.accelX = 100;
    fakeData.accelY = 200;
    fakeData.accelZ = 16384;

    while (self->mRunning) {
        fakeData.updateTimestamp();
        self->writeRecord(&fakeData);
        taskYIELD();  // Let other tasks run briefly
    }
    vTaskDelete(0);
}

void StorageServiceImpl::writeRecord(const SensorDataModel* model) {
    if (!mMounted) return;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    // Serialize record
    uint8_t record[RECORD_SIZE];
    serializeRecord(model, record);

    // Encrypt record
    cryptRecord(record, RECORD_SIZE, mRecordCount);

    // Open file for writing
    memset(&mFileConfig, 0, sizeof(mFileConfig));
    mFileConfig.buffer = mFileBuf;

    int err = lfs_file_opencfg(&mLfs, &mFile, FILENAME,
                               LFS_O_RDWR | LFS_O_CREAT, &mFileConfig);
    if (err != LFS_ERR_OK) {
        xSemaphoreGive(mMutex);
        return;
    }

    // Write/update header (record count + 1)
    uint32_t newCount = mRecordCount + 1;
    uint8_t header[HEADER_SIZE];
    header[0] = (newCount >>  0) & 0xFF;
    header[1] = (newCount >>  8) & 0xFF;
    header[2] = (newCount >> 16) & 0xFF;
    header[3] = (newCount >> 24) & 0xFF;

    lfs_file_seek(&mLfs, &mFile, 0, LFS_SEEK_SET);
    lfs_file_write(&mLfs, &mFile, header, HEADER_SIZE);

    // Seek to end of records and append
    lfs_off_t writePos = HEADER_SIZE + (mRecordCount * RECORD_SIZE);
    lfs_file_seek(&mLfs, &mFile, writePos, LFS_SEEK_SET);
    lfs_ssize_t written = lfs_file_write(&mLfs, &mFile, record, RECORD_SIZE);

    lfs_file_close(&mLfs, &mFile);

    if (written == RECORD_SIZE) {
        mRecordCount = newCount;
        mWritesInWindow++;

        // Update rate every 1 second using DWT cycle counter
        // (xTaskGetTickCount is unreliable when __disable_irq is used by SD writes)
        static volatile uint32_t* const DWT_CYCCNT_PTR = (volatile uint32_t*)0xE0001004;
        uint32_t now = *DWT_CYCCNT_PTR;
        if (mWindowStartTick == 0) {
            mWindowStartTick = now;
        }
        uint32_t elapsedCycles = now - mWindowStartTick;
        uint32_t elapsedMs = elapsedCycles / (SystemCoreClock / 1000);
        if (elapsedMs >= 1000) {
            mLastRate = mWritesInWindow;
            mWritesInWindow = 0;
            mWindowStartTick = now;
        }
    }

    xSemaphoreGive(mMutex);

    publishStats();
}

uint16_t StorageServiceImpl::readRecords(SensorDataModel* out, uint16_t maxCount) {
    if (!mMounted || mRecordCount == 0) return 0;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(200)) != pdTRUE) return 0;

    memset(&mFileConfig, 0, sizeof(mFileConfig));
    mFileConfig.buffer = mFileBuf;

    int err = lfs_file_opencfg(&mLfs, &mFile, FILENAME,
                               LFS_O_RDONLY, &mFileConfig);
    if (err != LFS_ERR_OK) {
        xSemaphoreGive(mMutex);
        return 0;
    }

    // Read from most recent records
    uint32_t startIdx = 0;
    if (mRecordCount > maxCount) {
        startIdx = mRecordCount - maxCount;
    }
    uint16_t toRead = (mRecordCount < maxCount) ? (uint16_t)mRecordCount : maxCount;

    lfs_off_t readPos = HEADER_SIZE + (startIdx * RECORD_SIZE);
    lfs_file_seek(&mLfs, &mFile, readPos, LFS_SEEK_SET);

    uint16_t count = 0;
    uint8_t record[RECORD_SIZE];
    for (uint16_t i = 0; i < toRead; i++) {
        lfs_ssize_t rd = lfs_file_read(&mLfs, &mFile, record, RECORD_SIZE);
        if (rd != RECORD_SIZE) break;

        // Decrypt
        cryptRecord(record, RECORD_SIZE, startIdx + i);
        deserializeRecord(record, &out[count]);
        count++;
    }

    lfs_file_close(&mLfs, &mFile);
    xSemaphoreGive(mMutex);
    return count;
}

uint32_t StorageServiceImpl::getRecordCount() {
    return mRecordCount;
}

void StorageServiceImpl::cryptRecord(uint8_t* buf, uint32_t len, uint32_t recordIndex) {
    uint8_t nonce[crypto::ChaCha20::NONCE_SIZE];
    makeNonce(nonce, recordIndex);
    crypto::ChaCha20::crypt(sKey, nonce, 0, buf, len);
}

void StorageServiceImpl::makeNonce(uint8_t nonce[crypto::ChaCha20::NONCE_SIZE],
                                    uint32_t recordIndex) {
    // Nonce: [0x00 × 8][recordIndex LE32]
    // Unique per record, deterministic for read-back
    memset(nonce, 0, crypto::ChaCha20::NONCE_SIZE);
    nonce[8]  = (recordIndex >>  0) & 0xFF;
    nonce[9]  = (recordIndex >>  8) & 0xFF;
    nonce[10] = (recordIndex >> 16) & 0xFF;
    nonce[11] = (recordIndex >> 24) & 0xFF;
}

void StorageServiceImpl::serializeRecord(const SensorDataModel* model, uint8_t* buf) {
    // [timestamp:4 LE][temperature:4 float][accelX:2][accelY:2][accelZ:2] = 14 bytes
    uint32_t ts = model->timestamp;
    buf[0] = (ts >>  0) & 0xFF;
    buf[1] = (ts >>  8) & 0xFF;
    buf[2] = (ts >> 16) & 0xFF;
    buf[3] = (ts >> 24) & 0xFF;

    uint32_t temp;
    memcpy(&temp, &model->temperature, 4);
    buf[4] = (temp >>  0) & 0xFF;
    buf[5] = (temp >>  8) & 0xFF;
    buf[6] = (temp >> 16) & 0xFF;
    buf[7] = (temp >> 24) & 0xFF;

    buf[8]  = (uint8_t)(model->accelX & 0xFF);
    buf[9]  = (uint8_t)((model->accelX >> 8) & 0xFF);
    buf[10] = (uint8_t)(model->accelY & 0xFF);
    buf[11] = (uint8_t)((model->accelY >> 8) & 0xFF);
    buf[12] = (uint8_t)(model->accelZ & 0xFF);
    buf[13] = (uint8_t)((model->accelZ >> 8) & 0xFF);
}

void StorageServiceImpl::deserializeRecord(const uint8_t* buf, SensorDataModel* model) {
    model->timestamp = (uint32_t)buf[0]       | ((uint32_t)buf[1] << 8) |
                       ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    uint32_t temp = (uint32_t)buf[4]       | ((uint32_t)buf[5] << 8) |
                    ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    memcpy(&model->temperature, &temp, 4);

    model->accelX = (int16_t)((uint16_t)buf[8]  | ((uint16_t)buf[9] << 8));
    model->accelY = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8));
    model->accelZ = (int16_t)((uint16_t)buf[12] | ((uint16_t)buf[13] << 8));
}

void StorageServiceImpl::publishStats() {
    mStats.recordCount = mRecordCount;
    mStats.writesPerSec = mLastRate;
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
}

} // namespace storage
} // namespace arcana
