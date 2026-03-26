#pragma once

#include "Esp8266.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "ff.h"
#include <cstdint>

namespace arcana {

/** Global upload progress — read by LCD, written by upload task */
struct UploadProgress {
    volatile uint8_t  currentFile;  // 1-based, 0 = not uploading
    volatile uint8_t  totalFiles;
    volatile uint32_t bytesSent;     // session bytes sent
    volatile uint32_t totalBytes;    // total file size
    volatile uint32_t resumeOffset;  // resume start point
};
extern UploadProgress g_uploadProgress;

/**
 * HTTPS file upload service — uploads .ats files to cloud server.
 *
 * Stateless utility: call uploadPendingFiles() from MQTT task
 * after disconnecting MQTT (ESP8266 single-connection mode).
 *
 * Flow:
 *   AT+CIPSTART="SSL" → HTTP POST header → stream file body → AT+CIPCLOSE
 *   Repeat for each pending file + device.ats blackbox.
 */
class HttpUploadServiceImpl {
public:
    /**
     * Upload all pending .ats files + device.ats.
     * @param esp   ESP8266 instance (MQTT must be disconnected)
     * @return number of files successfully uploaded
     */
    static uint8_t uploadPendingFiles(Esp8266& esp);

    /**
     * Upload a single file via HTTPS POST.
     * @param esp       ESP8266 instance
     * @param filename  File on SD card (e.g. "20260325.ats" or "device.ats")
     * @param deviceId  Device identifier for server path
     * @return true on success
     */
    static bool uploadFile(Esp8266& esp, const char* filename, const char* deviceId);

private:
    static bool sslConnect(Esp8266& esp);
    static void sslClose(Esp8266& esp);
    static uint32_t queryServerOffset(Esp8266& esp, const char* filename,
                                       const char* deviceId);
    static bool sendHttpHeader(Esp8266& esp, const char* filename,
                               const char* deviceId, uint32_t bodySize,
                               uint32_t rangeStart = 0, uint32_t totalSize = 0);
    static bool streamFileBody(Esp8266& esp, FIL* fp, uint32_t fileSize);
    static bool waitHttpResponse(Esp8266& esp);

    // Config
    static const char* SERVER;
    static const uint16_t PORT;
    static const uint16_t CHUNK_SIZE = 512;  // max stable with SDIO polling read
};

} // namespace arcana
