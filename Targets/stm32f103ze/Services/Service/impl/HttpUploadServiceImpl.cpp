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

extern "C" void sdio_force_reinit(void);

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

    // Reset SDIO — long DMA writes degrade polling read (known issue)
    sdio_force_reinit();

    // List pending files (safe now — ATS task suspended)
    atsstorage::AtsStorageServiceImpl::PendingFile pending[atsstorage::AtsStorageServiceImpl::MAX_PENDING];
    uint8_t count = storage.listPendingUploads(pending, atsstorage::AtsStorageServiceImpl::MAX_PENDING);

    LOG_I(ats::ErrorSource::System, 0x0070, (uint32_t)count);  // pending file count

    if (count == 0) {
        storage.resumeRecording();
        return 0;
    }

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

    // SSL connect
    printf("[UPL] SSL connecting...\r\n");
    if (!sslConnect(esp)) {
        printf("[UPL] SSL FAIL\r\n");
        f_close(&fp);
        return false;
    }

    printf("[UPL] SSL OK. Sending header...\r\n");
    // Send HTTP header
    if (!sendHttpHeader(esp, filename, deviceId, fileSize)) {
        printf("[UPL] Header FAIL\r\n");
        sslClose(esp);
        f_close(&fp);
        return false;
    }

    // Stream file body
    bool ok = streamFileBody(esp, &fp, fileSize);
    f_close(&fp);

    if (ok) {
        ok = waitHttpResponse(esp);
        if (!ok) LOG_W(ats::ErrorSource::System, 0x0077);  // HTTP response failed
    } else {
        LOG_W(ats::ErrorSource::System, 0x0078);  // Stream body failed
    }

    sslClose(esp);
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

bool HttpUploadServiceImpl::sendHttpHeader(Esp8266& esp, const char* filename,
                                        const char* deviceId, uint32_t fileSize) {
    // Build HTTP header
    char header[256];
    int hLen = snprintf(header, sizeof(header),
        "POST /upload/%s/%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %lu\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n"
        "\r\n",
        deviceId, filename, SERVER, (unsigned long)fileSize);

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
    // Reuse AtsStorage's 4KB read cache (same task, sequential access)
    uint8_t* chunkBuf = atsstorage::AtsStorageServiceImpl::getReadCache();

    printf("[UPL] stream %luB\r\n", (unsigned long)fileSize);
    uint32_t sent = 0;
    while (sent < fileSize) {
        uint32_t remaining = fileSize - sent;
        uint16_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : (uint16_t)remaining;

        // Read from SD
        UINT br = 0;
        FRESULT fr = f_read(fp, chunkBuf, chunkLen, &br);
        if (fr != FR_OK || br == 0) {
            printf("[UPL] read FAIL fr=%d br=%u\r\n", (int)fr, (unsigned)br);
            return false;
        }

        // AT+CIPSEND=<len>
        char cipsend[24];
        snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%u", (unsigned)br);
        if (!esp.sendCmd(cipsend, ">", 5000)) {
            printf("[UPL] CIPSEND FAIL at %lu\r\n", (unsigned long)sent);
            return false;
        }

        // Send chunk
        esp.sendData(chunkBuf, br, 5000);
        if (!esp.waitFor("SEND OK", 10000)) {
            printf("[UPL] SEND FAIL at %lu\r\n", (unsigned long)sent);
            return false;
        }

        sent += br;

        // Periodic SDIO reinit — polling read degrades after ~100 consecutive reads
        if (sent % (CHUNK_SIZE * 64) == 0) {
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
