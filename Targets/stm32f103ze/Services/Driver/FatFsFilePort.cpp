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
        mFaMode = fa;
        strncpy(mPath, path, sizeof(mPath) - 1);
        mPath[sizeof(mPath) - 1] = '\0';
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

bool FatFsFilePort::seek(uint64_t offset) {
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

    // f_lseek beyond EOF failed (FR_INT_ERR on damaged exFAT cluster chain).
    // Fallback: seek to EOF, write zeros to extend, arrive at target offset.
    uint64_t curSize = f_size(&mFil);
    if (offset >= curSize) {
        mFil.err = 0;
        if (f_lseek(&mFil, curSize) != FR_OK) {
            printf("[FP] seek FAIL off=%lu err=%d fsz=%lu\r\n",
                   (unsigned long)offset, (int)lastErr, (unsigned long)curSize);
            return false;
        }
        // Zero-fill from EOF to target offset
        uint8_t zeros[64];
        memset(zeros, 0, sizeof(zeros));
        uint64_t remaining = offset - curSize;
        while (remaining > 0) {
            UINT chunk = (remaining > sizeof(zeros))
                         ? (UINT)sizeof(zeros) : (UINT)remaining;
            UINT written = 0;
            mFil.err = 0;
            if (f_write(&mFil, zeros, chunk, &written) != FR_OK
                || written != chunk) {
                printf("[FP] extend FAIL off=%lu fsz=%lu\r\n",
                       (unsigned long)offset, (unsigned long)curSize);
                return false;
            }
            remaining -= written;
        }
        return true;  // fptr now at target offset
    }

    printf("[FP] seek FAIL off=%lu err=%d fsz=%lu\r\n",
           (unsigned long)offset, (int)lastErr, (unsigned long)curSize);
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

uint64_t FatFsFilePort::tell() {
    if (!mIsOpen) return 0;
    return f_tell(&mFil);
}

uint64_t FatFsFilePort::size() {
    if (!mIsOpen) return 0;
    return f_size(&mFil);
}

bool FatFsFilePort::truncate() {
    if (!mIsOpen) return false;
    mFil.err = 0;

    // TexFAT (n_fats==2): safe to truncate — the committed FAT has a
    // correct cluster chain, so f_truncate won't corrupt anything.
    // Single FAT (n_fats==1): skip truncate — broken cluster chain at
    // cut point would make subsequent writes impossible.
    if (mFil.obj.fs->n_fats == 2) {
        return f_truncate(&mFil) == FR_OK;
    }
    return f_sync(&mFil) == FR_OK;
}

} // namespace ats
} // namespace arcana
