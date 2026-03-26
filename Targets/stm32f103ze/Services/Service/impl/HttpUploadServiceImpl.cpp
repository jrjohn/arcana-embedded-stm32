#include "stm32f1xx_hal.h"
#include "HttpUploadServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "Credentials.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdio>
#include <cstring>

extern "C" {
    void sdio_force_reinit(void);
    void sd_card_full_reinit(void);
}

namespace arcana {

const char* HttpUploadServiceImpl::SERVER = UPLOAD_SERVER_VALUE;
const uint16_t HttpUploadServiceImpl::PORT = UPLOAD_PORT_VALUE;

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
    if (!storage.isReady()) return 0;

    // Pause ATS recording cooperatively — FatFS not thread-safe
    storage.pauseRecording();
    vTaskDelay(pdMS_TO_TICKS(500));  // let ATS task finish current write + enter yield loop

    // Full SDIO + FatFS reinit before file access
    sd_card_full_reinit();
    vTaskDelay(pdMS_TO_TICKS(500));

    // List pending files (safe now — ATS task suspended)
    atsstorage::AtsStorageServiceImpl::PendingFile pending[atsstorage::AtsStorageServiceImpl::MAX_PENDING];
    uint8_t count = storage.listPendingUploads(pending, atsstorage::AtsStorageServiceImpl::MAX_PENDING);

    LOG_I(ats::ErrorSource::System, 0x0070, (uint32_t)count);  // pending file count

    if (count == 0) {
        storage.resumeRecording();
        return 0;
    }

    // Enable IPD passthrough so +IPD stays in mRxBuf (not intercepted as MQTT)
    esp.setIpdPassthrough(true);

    uint8_t uploaded = 0;
    for (uint8_t i = 0; i < count; i++) {
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

    // --- Retry loop: resume from server offset on each attempt ---
    // Safety valve: give up if 3 consecutive retries make no progress
    static const int MAX_ATTEMPTS = 200;
    static const int MAX_STALL = 3;
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

        // HTTP header with Content-Range for resume
        if (!sendHttpHeader(esp, filename, deviceId, remainSize, resumeOffset, fileSize)) {
            printf("[UPL] Header FAIL\r\n");
            sslClose(esp);
            continue;  // retry
        }

        // Stream file body (AT mode — reliable per-chunk acknowledgment)
        ok = streamFileBody(esp, &fp, remainSize);

        if (ok) {
            ok = waitHttpResponse(esp);
            if (!ok) LOG_W(ats::ErrorSource::System, 0x0077);
        } else {
            LOG_W(ats::ErrorSource::System, 0x0078);
        }

        sslClose(esp);

        if (ok) break;  // success!

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

    printf("[UPL] stream %luB\r\n", (unsigned long)fileSize);
    uint32_t sent = 0;
    while (sent < fileSize) {
        uint32_t remaining = fileSize - sent;
        uint16_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : (uint16_t)remaining;

        // Read from SD with retry + SDIO reinit on failure
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

        // AT+CIPSEND=<len>
        char cipsend[24];
        snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%u", (unsigned)br);
        if (!esp.sendCmd(cipsend, ">", 5000)) {
            printf("[UPL] CIPSEND FAIL at %lu\r\n", (unsigned long)sent);
            return false;
        }

        esp.sendData(chunkBuf, br, 5000);
        if (!esp.waitFor("SEND OK", 10000)) {
            printf("[UPL] SEND FAIL at %lu\r\n", (unsigned long)sent);
            return false;
        }

        sent += br;

        // Proactive SDIO reinit every 32KB
        if (sent % 32768 == 0) {
            sdio_force_reinit();
        }

        if (sent % (CHUNK_SIZE * 10) == 0 || sent == fileSize) {
            printf("[UPL] %lu/%lu\r\n", (unsigned long)sent, (unsigned long)fileSize);
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
