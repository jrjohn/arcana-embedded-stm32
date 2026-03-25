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

    // List pending files
    atsstorage::AtsStorageServiceImpl::PendingFile pending[atsstorage::AtsStorageServiceImpl::MAX_PENDING];
    uint8_t count = storage.listPendingUploads(pending, atsstorage::AtsStorageServiceImpl::MAX_PENDING);

    LOG_I(ats::ErrorSource::System, 0x0070, (uint32_t)count);  // pending file count

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
    if (uploaded > 0 || count == 0) {
        uploadFile(esp, "device.ats", deviceId);
    }

    return uploaded;
}

// ---------------------------------------------------------------------------
// Upload single file
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::uploadFile(Esp8266& esp, const char* filename,
                                    const char* deviceId) {
    // Open file
    static FIL fp;  // static — too large for stack
    FRESULT fr = f_open(&fp, filename, FA_READ);
    if (fr != FR_OK) return false;

    uint32_t fileSize = (uint32_t)f_size(&fp);
    if (fileSize == 0) { f_close(&fp); return false; }

    // SSL connect
    if (!sslConnect(esp)) {
        f_close(&fp);
        return false;
    }

    // Send HTTP header
    if (!sendHttpHeader(esp, filename, deviceId, fileSize)) {
        sslClose(esp);
        f_close(&fp);
        return false;
    }

    // Stream file body
    bool ok = streamFileBody(esp, &fp, fileSize);
    f_close(&fp);

    if (ok) {
        ok = waitHttpResponse(esp);
    }

    sslClose(esp);
    return ok;
}

// ---------------------------------------------------------------------------
// SSL connection
// ---------------------------------------------------------------------------

bool HttpUploadServiceImpl::sslConnect(Esp8266& esp) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"SSL\",\"%s\",%u", SERVER, PORT);
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

    uint32_t sent = 0;
    while (sent < fileSize) {
        uint32_t remaining = fileSize - sent;
        uint16_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : (uint16_t)remaining;

        // Read from SD
        UINT br = 0;
        if (f_read(fp, chunkBuf, chunkLen, &br) != FR_OK || br == 0) {
            return false;
        }

        // AT+CIPSEND=<len>
        char cipsend[24];
        snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%u", (unsigned)br);
        if (!esp.sendCmd(cipsend, ">", 3000)) return false;

        // Send chunk
        esp.sendData(chunkBuf, br, 5000);
        if (!esp.waitFor("SEND OK", 10000)) return false;

        sent += br;
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
