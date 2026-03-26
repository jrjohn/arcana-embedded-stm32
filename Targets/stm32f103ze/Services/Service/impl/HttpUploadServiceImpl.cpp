#include "stm32f1xx_hal.h"
#include "HttpUploadServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "Credentials.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "DisplayStatus.hpp"
#include "IoServiceImpl.hpp"
#include <cstdio>
#include <cstring>

extern "C" {
    void sdio_force_reinit(void);
    void sd_card_full_reinit(void);
    void sd_enable_dma_reads(void);
    void sd_disable_dma_reads(void);
}

namespace arcana {

const char* HttpUploadServiceImpl::SERVER = UPLOAD_SERVER_VALUE;
const uint16_t HttpUploadServiceImpl::PORT = UPLOAD_PORT_VALUE;

UploadProgress g_uploadProgress = {0, 0, 0, 0};

// ---------------------------------------------------------------------------
// Upload all pending files
// ---------------------------------------------------------------------------

uint8_t HttpUploadServiceImpl::uploadPendingFiles(Esp8266& esp) {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());

    // Get device ID (UID hex, first 8 chars)
    uint8_t uid[12];
    crypto::DeviceKey::getUID(uid);
    char deviceId[9];
    for (int i = 0; i < 4; i++) {
        static const char hex[] = "0123456789ABCDEF";
        deviceId[i * 2]     = hex[uid[i] >> 4];
        deviceId[i * 2 + 1] = hex[uid[i] & 0x0F];
    }
    deviceId[8] = '\0';

    // Wait for ATS to finish boot (recovery scan etc.)
    for (int i = 0; i < 30 && !storage.isReady(); i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!storage.isReady()) { printf("[UPL] not ready\r\n"); return 0; }

    printf("[UPL] pausing ATS...\r\n");
    // Pause ATS recording cooperatively — FatFS not thread-safe
    storage.pauseRecording();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Light SDIO reinit (not full — keep FatFS mount intact)
    sdio_force_reinit();
    vTaskDelay(pdMS_TO_TICKS(200));

    // List pending files (safe now — ATS task suspended)
    atsstorage::AtsStorageServiceImpl::PendingFile pending[atsstorage::AtsStorageServiceImpl::MAX_PENDING];
    uint8_t count = storage.listPendingUploads(pending, atsstorage::AtsStorageServiceImpl::MAX_PENDING);

    printf("[UPL] pending=%u\r\n", count);

    if (count == 0) {
        storage.resumeRecording();
        return 0;
    }

    // Enable IPD passthrough so +IPD stays in mRxBuf (not intercepted as MQTT)
    esp.setIpdPassthrough(true);

    // Enable DMA reads — ATS paused, no writes, no direction switching
    sd_enable_dma_reads();

    // Set global progress for LCD
    g_uploadProgress.totalFiles = count;
    g_uploadProgress.currentFile = 0;
    g_uploadProgress.bytesSent = 0;
    g_uploadProgress.totalBytes = 0;

    uint8_t uploaded = 0;
    for (uint8_t i = 0; i < count; i++) {
        g_uploadProgress.currentFile = i + 1;
        LOG_I(ats::ErrorSource::System, 0x0071, pending[i].date);  // uploading date

        if (uploadFile(esp, pending[i].name, deviceId)) {
            storage.markUploaded(pending[i].date);
            uploaded++;
            LOG_I(ats::ErrorSource::System, 0x0072, pending[i].date);  // upload OK
        } else {
            LOG_W(ats::ErrorSource::System, 0x0073, pending[i].date);  // upload failed
            break;  // stop on first failure (connection may be broken)
        }
        vTaskDelay(pdMS_TO_TICKS(500));  // brief pause between files
    }

    // Always upload device.ats blackbox (latest version)
    if (uploaded > 0) {
        uploadFile(esp, "device.ats", deviceId);
    }

    // Clear upload progress + dismiss toast (safe: runs in MQTT task, same as render)
    g_uploadProgress.currentFile = 0;
    display::toastState().active = false;

    // Restore DMA write direction before resuming ATS recording
    sd_disable_dma_reads();

    // Restore normal +IPD handling for MQTT
    esp.setIpdPassthrough(false);

    // Resume ATS recording
    storage.resumeRecording();

    return uploaded;
}

// ---------------------------------------------------------------------------
// Upload single file
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::uploadFile(Esp8266& esp, const char* filename,
                                    const char* deviceId) {
    printf("[UPL] open %s\r\n", filename);
    // Shared FIL — clear before use
    FIL& fp = atsstorage::AtsStorageServiceImpl::sSharedFil;
    memset(&fp, 0, sizeof(FIL));
    FRESULT fr = f_open(&fp, filename, FA_READ);
    if (fr != FR_OK) {
        printf("[UPL] open FAIL %d\r\n", (int)fr);
        return false;
    }

    uint32_t fileSize = (uint32_t)f_size(&fp);
    if (fileSize == 0) { f_close(&fp); return false; }

    printf("[UPL] size=%luKB\r\n", (unsigned long)(fileSize / 1024));
    g_uploadProgress.totalBytes = fileSize;
    g_uploadProgress.bytesSent = 0;

    // --- Retry loop: resume from server offset on each attempt ---
    static const int MAX_ATTEMPTS = 200;
    static const int MAX_STALL = 3;
    static const uint16_t CANCELLED = 0xFFFF;  // sentinel
    bool ok = false;
    uint32_t lastOffset = 0;
    int stallCount = 0;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            printf("[UPL] retry %d\r\n", attempt + 1);
            // Full SDIO reinit between retries
            sd_card_full_reinit();
            vTaskDelay(pdMS_TO_TICKS(3000));  // 3s recovery

            // Re-open file after SDIO reinit
            memset(&fp, 0, sizeof(FIL));
            fr = f_open(&fp, filename, FA_READ);
            if (fr != FR_OK) {
                printf("[UPL] reopen FAIL %d\r\n", (int)fr);
                break;
            }
        }

        // Query server for already uploaded bytes
        uint32_t resumeOffset = queryServerOffset(esp, filename, deviceId);
        if (resumeOffset >= fileSize) {
            printf("[UPL] already complete (%luKB)\r\n", (unsigned long)(fileSize / 1024));
            f_close(&fp);
            return true;
        }

        // Stall detection: give up if no progress for MAX_STALL consecutive retries
        if (attempt > 0) {
            if (resumeOffset <= lastOffset) {
                stallCount++;
                if (stallCount >= MAX_STALL) {
                    printf("[UPL] stalled %d retries, giving up\r\n", MAX_STALL);
                    break;
                }
            } else {
                stallCount = 0;  // progress made, reset
            }
        }
        lastOffset = resumeOffset;

        if (resumeOffset > 0) {
            printf("[UPL] resume at %luKB / %luKB\r\n",
                   (unsigned long)(resumeOffset / 1024), (unsigned long)(fileSize / 1024));
            fp.err = 0;  // clear sticky FatFS error
            f_lseek(&fp, resumeOffset);
        }

        uint32_t remainSize = fileSize - resumeOffset;

        // TCP connect
        if (!sslConnect(esp)) {
            printf("[UPL] TCP FAIL\r\n");
            continue;  // retry
        }

        // Enter transparent passthrough mode
        if (!esp.sendCmd("AT+CIPMODE=1", "OK", 2000)) {
            printf("[UPL] CIPMODE FAIL\r\n");
            sslClose(esp);
            continue;
        }
        if (!esp.sendCmd("AT+CIPSEND", ">", 3000)) {
            printf("[UPL] transparent FAIL\r\n");
            esp.sendCmd("AT+CIPMODE=0", "OK", 1000);
            sslClose(esp);
            continue;
        }

        // Send HTTP header (raw bytes, transparent mode)
        {
            char header[320];
            uint32_t rangeEnd = resumeOffset + remainSize - 1;
            int hLen = snprintf(header, sizeof(header),
                "POST /upload/%s/%s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Length: %lu\r\n"
                "Content-Range: bytes %lu-%lu/%lu\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Connection: close\r\n"
                "\r\n",
                deviceId, filename, SERVER,
                (unsigned long)remainSize,
                (unsigned long)resumeOffset, (unsigned long)rangeEnd,
                (unsigned long)fileSize);
            esp.sendData(reinterpret_cast<const uint8_t*>(header), hLen, 3000);
        }

        // Stream file body (transparent + DMA read)
        ok = streamFileBody(esp, &fp, remainSize);

        // Exit transparent mode: 1s silence → +++ → 1s silence
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp.sendData(reinterpret_cast<const uint8_t*>("+++"), 3, 1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp.sendCmd("AT+CIPMODE=0", "OK", 2000);

        if (ok) {
            ok = waitHttpResponse(esp);
            if (!ok) LOG_W(ats::ErrorSource::System, 0x0077);
        } else {
            LOG_W(ats::ErrorSource::System, 0x0078);
        }

        sslClose(esp);

        if (ok) break;  // success!

        // KEY2 cancel — stop all retries
        if (io::IoServiceImpl::getInstance().isCancelRequested()) {
            io::IoServiceImpl::getInstance().disarmCancel();
            break;
        }

        printf("[UPL] attempt %d failed\r\n", attempt + 1);
        f_close(&fp);
        memset(&fp, 0, sizeof(FIL));
    }

    // Always close — safe on zeroed FIL (returns FR_INVALID_OBJECT, no side effects)
    f_close(&fp);
    return ok;
}

// ---------------------------------------------------------------------------
// SSL connection
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::sslConnect(Esp8266& esp) {
    char cmd[80];
    // Try TCP first (SSL may not be supported on all ESP8266 AT versions)
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", SERVER, PORT);
    if (!esp.sendCmd(cmd, "OK", 10000)) {
        if (!esp.responseContains("ALREADY CONNECTED")) {
            return false;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    return true;
}

void HttpUploadServiceImpl::sslClose(Esp8266& esp) {
    esp.sendCmd("AT+CIPCLOSE", "OK", 2000);
}

// ---------------------------------------------------------------------------
// HTTP POST header
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Query server for resume offset
// ---------------------------------------------------------------------------

uint32_t HttpUploadServiceImpl::queryServerOffset(Esp8266& esp, const char* filename,
                                                    const char* deviceId) {
    if (!sslConnect(esp)) {
        printf("[UPL] resume: TCP FAIL\r\n");
        return 0;
    }

    char header[256];
    int hLen = snprintf(header, sizeof(header),
        "GET /upload/%s/%s/status HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        deviceId, filename, SERVER);

    char cipsend[24];
    snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%d", hLen);
    if (!esp.sendCmd(cipsend, ">", 3000)) {
        printf("[UPL] resume: CIPSEND FAIL\r\n");
        sslClose(esp); return 0;
    }
    esp.sendData(reinterpret_cast<const uint8_t*>(header), hLen, 3000);
    if (!esp.waitFor("SEND OK", 5000)) {
        printf("[UPL] resume: SEND FAIL\r\n");
        sslClose(esp); return 0;
    }

    // Clear buffer before waiting for server response
    esp.clearRx();

    // Wait for JSON body (may arrive in 2nd +IPD after headers)
    if (!esp.waitFor("\"size\"", 8000)) {
        printf("[UPL] resume: no size in resp (%u bytes)\r\n", esp.getResponseLen());
        sslClose(esp); return 0;
    }

    // Parse "size":NNNN from JSON response
    const char* resp = esp.getResponse();
    const char* sizeStr = strstr(resp, "\"size\":");
    uint32_t offset = 0;
    if (sizeStr) {
        sizeStr += 7;  // skip "size":
        while (*sizeStr == ' ') sizeStr++;  // skip space after colon
        while (*sizeStr >= '0' && *sizeStr <= '9') {
            offset = offset * 10 + (*sizeStr - '0');
            sizeStr++;
        }
    }
    printf("[UPL] resume: offset=%luKB\r\n", (unsigned long)(offset / 1024));

    sslClose(esp);
    return offset;
}

// ---------------------------------------------------------------------------
// HTTP POST header
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::sendHttpHeader(Esp8266& esp, const char* filename,
                                        const char* deviceId, uint32_t bodySize,
                                        uint32_t rangeStart, uint32_t totalSize) {
    char header[320];
    int hLen;

    // Always send Content-Range to prevent server from truncating partial uploads
    uint32_t rangeEnd = rangeStart + bodySize - 1;
    hLen = snprintf(header, sizeof(header),
        "POST /upload/%s/%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %lu\r\n"
        "Content-Range: bytes %lu-%lu/%lu\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n"
        "\r\n",
        deviceId, filename, SERVER,
        (unsigned long)bodySize,
        (unsigned long)rangeStart, (unsigned long)rangeEnd,
        (unsigned long)totalSize);

    if (hLen <= 0 || hLen >= (int)sizeof(header)) return false;

    // AT+CIPSEND=<len>
    char cipsend[24];
    snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%d", hLen);
    if (!esp.sendCmd(cipsend, ">", 3000)) return false;

    // Send header
    esp.sendData(reinterpret_cast<const uint8_t*>(header), hLen, 3000);
    return esp.waitFor("SEND OK", 5000);
}

// ---------------------------------------------------------------------------
// Stream file body in chunks
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::streamFileBody(Esp8266& esp, FIL* fp, uint32_t fileSize) {
    uint8_t* chunkBuf = atsstorage::AtsStorageServiceImpl::getReadCache();
    static const uint16_t TX_CHUNK = 2048;  // DMA reads stable at 2KB

    printf("[UPL] stream %luB\r\n", (unsigned long)fileSize);
    io::IoServiceImpl::getInstance().armCancel();
    uint32_t sent = 0;
    while (sent < fileSize) {
        uint32_t remaining = fileSize - sent;
        uint16_t chunkLen = (remaining > TX_CHUNK) ? TX_CHUNK : (uint16_t)remaining;

        // DMA read from SD with retry
        UINT br = 0;
        FRESULT fr = FR_DISK_ERR;
        for (int retry = 0; retry < 3; retry++) {
            SDIO->DCTRL = 0;
            SDIO->ICR = 0xFFFFFFFF;
            br = 0;
            fr = f_read(fp, chunkBuf, chunkLen, &br);
            if (fr == FR_OK && br > 0) break;
            sdio_force_reinit();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (fr != FR_OK || br == 0) {
            printf("[UPL] read FAIL fr=%d br=%u at %lu\r\n",
                   (int)fr, (unsigned)br, (unsigned long)sent);
            return false;
        }

        // Transparent mode: send directly to TCP (no AT overhead)
        if (!esp.sendData(chunkBuf, br, 5000)) {
            printf("[UPL] SEND FAIL at %lu\r\n", (unsigned long)sent);
            return false;
        }

        sent += br;
        g_uploadProgress.bytesSent = sent;

        // Progress log + LCD update + cancel check every ~20KB
        if (sent % (TX_CHUNK * 10) == 0 || sent == fileSize) {
            printf("[UPL] %lu/%lu\r\n", (unsigned long)sent, (unsigned long)fileSize);
            // LCD toast: "Upload 1/7 23%" — static buffer (toast stores pointer)
            uint8_t pct = (uint8_t)(sent * 100ULL / fileSize);
            static char msg[24];
            snprintf(msg, sizeof(msg), "Upload %u/%u %u%%",
                     g_uploadProgress.currentFile,
                     g_uploadProgress.totalFiles, pct);
            display::toast(msg, 30000, (uint32_t)xTaskGetTickCount(),
                           display::colors::WHITE, 0x001F);  // blue bg

            // Cancel check — don't clear flag here, let retry loop see it
            if (io::IoServiceImpl::getInstance().isCancelRequested()) {
                printf("[UPL] cancelled by KEY2\r\n");
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Wait for HTTP response
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::waitHttpResponse(Esp8266& esp) {
    // Wait for server response (HTTP 200)
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!esp.waitFor("+IPD", 10000)) return false;

    // Check for "200" in response
    return esp.responseContains("200") || esp.responseContains("complete");
}

} // namespace arcana
