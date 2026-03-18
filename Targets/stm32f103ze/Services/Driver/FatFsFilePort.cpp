/**
 * @file FatFsFilePort.cpp
 * @brief IFilePort → FatFS implementation with retry + SDIO reinit
 *
 * FIL.err sticky flag cleared before every operation (SdFalAdapter pattern).
 * On persistent FR_DISK_ERR: SDIO force reinit + second retry round.
 */

#include "FatFsFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>
#include <cstdio>

extern "C" { void sdio_force_reinit(void); }

namespace arcana {
namespace ats {

static const int MAX_RETRIES = 3;

bool FatFsFilePort::open(const char* path, uint8_t mode) {
    if (mIsOpen) return false;
    memset(&mFil, 0, sizeof(mFil));

    BYTE fa = 0;
    if (mode & ATS_MODE_READ)   fa |= FA_READ;
    if (mode & ATS_MODE_WRITE)  fa |= FA_WRITE;
    if (mode & ATS_MODE_CREATE) fa |= FA_CREATE_ALWAYS;
    else                        fa |= FA_OPEN_EXISTING;

    FRESULT fr = f_open(&mFil, path, fa);
    if (fr == FR_OK) {
        mIsOpen = true;
        return true;
    }
    printf("[FP] open '%s' fa=0x%02X err=%d\r\n", path, (int)fa, (int)fr);
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

    for (int round = 0; round < 2; round++) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            mFil.err = 0;
            UINT bytesRead = 0;
            FRESULT fr = f_read(&mFil, buf, size, &bytesRead);
            if (fr == FR_OK) {
                return static_cast<int32_t>(bytesRead);
            }
            vTaskDelay(1);
        }
        if (round == 0) {
            sdio_force_reinit();
        }
    }
    return -1;
}

int32_t FatFsFilePort::write(const uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;

    FRESULT lastErr = FR_OK;
    UINT lastWr = 0;
    for (int round = 0; round < 2; round++) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            mFil.err = 0;
            lastWr = 0;
            lastErr = f_write(&mFil, buf, size, &lastWr);
            if (lastErr == FR_OK && lastWr == size) {
                return static_cast<int32_t>(lastWr);
            }
            vTaskDelay(1);
        }
        if (round == 0) {
            sdio_force_reinit();
        }
    }
    printf("[FP] write FAIL sz=%lu wr=%lu err=%d fpos=%lu\r\n",
           (unsigned long)size, (unsigned long)lastWr,
           (int)lastErr, (unsigned long)f_tell(&mFil));
    return -1;
}

bool FatFsFilePort::seek(uint32_t offset) {
    if (!mIsOpen) return false;

    FRESULT lastErr = FR_OK;
    for (int round = 0; round < 2; round++) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            mFil.err = 0;
            lastErr = f_lseek(&mFil, offset);
            if (lastErr == FR_OK) return true;
            vTaskDelay(1);
        }
        if (round == 0) {
            sdio_force_reinit();
        }
    }
    printf("[FP] seek FAIL off=%lu err=%d fsz=%lu\r\n",
           (unsigned long)offset, (int)lastErr,
           (unsigned long)f_size(&mFil));
    return false;
}

bool FatFsFilePort::sync() {
    if (!mIsOpen) return false;
    for (int round = 0; round < 2; round++) {
        mFil.err = 0;
        if (f_sync(&mFil) == FR_OK) return true;
        sdio_force_reinit();
    }
    return false;
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
