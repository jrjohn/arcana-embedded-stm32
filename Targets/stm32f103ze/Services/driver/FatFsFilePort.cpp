/**
 * @file FatFsFilePort.cpp
 * @brief IFilePort → FatFS implementation with 3-retry logic
 *
 * FIL.err sticky flag cleared before every operation (SdFalAdapter pattern).
 * vTaskDelay(1) between retries for SDIO transient failures.
 */

#include "FatFsFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace ats {

static const int MAX_RETRIES = 3;

bool FatFsFilePort::open(const char* path, uint8_t mode) {
    if (mIsOpen) return false;

    BYTE fa = 0;
    if (mode & ATS_MODE_READ)   fa |= FA_READ;
    if (mode & ATS_MODE_WRITE)  fa |= FA_WRITE;
    if (mode & ATS_MODE_CREATE) fa |= FA_CREATE_ALWAYS;
    else if (mode & ATS_MODE_WRITE) fa |= FA_OPEN_ALWAYS;

    FRESULT fr = f_open(&mFil, path, fa);
    if (fr == FR_OK) {
        mIsOpen = true;
        return true;
    }
    return false;
}

bool FatFsFilePort::close() {
    if (!mIsOpen) return false;
    FRESULT fr = f_close(&mFil);
    mIsOpen = false;
    return fr == FR_OK;
}

int32_t FatFsFilePort::read(uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        UINT bytesRead = 0;
        FRESULT fr = f_read(&mFil, buf, size, &bytesRead);
        if (fr == FR_OK) {
            return static_cast<int32_t>(bytesRead);
        }
        vTaskDelay(1);
    }
    return -1;
}

int32_t FatFsFilePort::write(const uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        UINT bytesWritten = 0;
        FRESULT fr = f_write(&mFil, buf, size, &bytesWritten);
        if (fr == FR_OK && bytesWritten == size) {
            return static_cast<int32_t>(bytesWritten);
        }
        vTaskDelay(1);
    }
    return -1;
}

bool FatFsFilePort::seek(uint32_t offset) {
    if (!mIsOpen) return false;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        FRESULT fr = f_lseek(&mFil, offset);
        if (fr == FR_OK) return true;
        vTaskDelay(1);
    }
    return false;
}

bool FatFsFilePort::sync() {
    if (!mIsOpen) return false;
    mFil.err = 0;
    return f_sync(&mFil) == FR_OK;
}

uint32_t FatFsFilePort::tell() {
    if (!mIsOpen) return 0;
    return static_cast<uint32_t>(f_tell(&mFil));
}

uint32_t FatFsFilePort::size() {
    if (!mIsOpen) return 0;
    return static_cast<uint32_t>(f_size(&mFil));
}

bool FatFsFilePort::truncate() {
    if (!mIsOpen) return false;
    mFil.err = 0;
    return f_truncate(&mFil) == FR_OK;
}

} // namespace ats
} // namespace arcana
