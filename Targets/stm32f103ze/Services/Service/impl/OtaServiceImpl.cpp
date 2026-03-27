/**
 * @file OtaServiceImpl.cpp
 * @brief OTA firmware download via ESP8266 HTTP GET → SD card → CRC → flag → reset
 *
 * Flow:
 * 1. SSL/TLS connect to server
 * 2. HTTPS GET firmware binary
 * 3. Stream +IPD data → f_write to firmware.bin on SD
 * 4. CRC-32 verify entire file
 * 5. Write ota_meta.bin
 * 6. Set BKP DR2/DR3 OTA flag
 * 7. NVIC_SystemReset()
 */

#include "OtaServiceImpl.hpp"
#include "ota_header.h"
#include "Crc32.hpp"
#include "ff.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "Log.hpp"
#include "EventCodes.hpp"
#include <cstring>
#include <cstdio>

namespace arcana {

OtaServiceImpl::OtaServiceImpl()
    : mProgress(0)
    , mActive(false)
{
}

OtaServiceImpl& OtaServiceImpl::getInstance() {
    static OtaServiceImpl sInstance;
    return sInstance;
}

bool OtaServiceImpl::startUpdate(const char* host, uint16_t port,
                                  const char* path, uint32_t expectedSize,
                                  uint32_t expectedCrc32)
{
    if (!input.esp) return false;
    if (mActive) return false;

    mActive = true;
    mProgress = 0;

    LOG_I(ats::ErrorSource::Ota, evt::OTA_START, expectedSize);

    /* 1. TCP connect + HTTP GET */
    if (!httpGet(host, port, path)) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_HTTP_FAIL);
        mActive = false;
        return false;
    }

    /* 2. Receive body → firmware.bin */
    if (!receiveToFile(expectedSize)) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_DOWNLOAD_FAIL);
        input.esp->sendCmd("AT+CIPCLOSE", "OK", 1000);
        input.esp->setIpdPassthrough(false);
        mActive = false;
        return false;
    }

    input.esp->setIpdPassthrough(false);

    /* 3. Verify CRC */
    if (!verifyCrc(expectedCrc32, expectedSize)) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_VERIFY_FAIL, expectedCrc32);
        mActive = false;
        return false;
    }

    /* 4. Write ota_meta.bin */
    if (!writeOtaMeta(expectedSize, expectedCrc32, "ota")) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_META_FAIL);
        mActive = false;
        return false;
    }

    /* 5. Set OTA flag + reset */
    LOG_I(ats::ErrorSource::Ota, evt::OTA_RESETTING);
    setOtaFlag();

    vTaskDelay(pdMS_TO_TICKS(100));  /* Flush UART */
    NVIC_SystemReset();

    return true;  /* Never reached */
}

/* ---- HTTP GET ---- */
bool OtaServiceImpl::httpGet(const char* host, uint16_t port, const char* path)
{
    Esp8266& esp = *input.esp;

    /* Close any existing connection */
    esp.sendCmd("AT+CIPCLOSE", "OK", 1000);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* SSL/TLS connect (prevents MITM on firmware download) */
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"SSL\",\"%s\",%u", host, port);
    if (!esp.sendCmd(cmd, "OK", 10000)) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_TCP_FAIL);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Build HTTP request */
    char req[256];
    int reqLen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    /* Send via AT+CIPSEND */
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", reqLen);
    if (!esp.sendCmd(cmd, ">", 2000)) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_CIPSEND_FAIL);
        return false;
    }

    esp.sendData((const uint8_t*)req, reqLen, 2000);
    if (!esp.waitFor("SEND OK", 5000)) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_SEND_FAIL);
        return false;
    }

    /* Enable IPD passthrough for large chunk handling */
    esp.setIpdPassthrough(true);
    esp.clearRx();

    return true;
}

/* ---- Receive HTTP response → file ---- */
bool OtaServiceImpl::receiveToFile(uint32_t expectedSize)
{
    Esp8266& esp = *input.esp;
    FIL f;

    /* Open firmware.bin for writing */
    if (f_open(&f, OTA_FW_FILENAME, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        LOG_E(ats::ErrorSource::Ota, evt::OTA_FILE_CREATE_FAIL);
        return false;
    }

    uint32_t bytesWritten = 0;
    bool headersParsed = false;
    uint32_t noDataCount = 0;

    while (bytesWritten < expectedSize) {
        /* Wait for +IPD frame */
        if (!esp.waitFor("+IPD,", 10000)) {
            /* Check for CLOSED (server done) */
            if (esp.responseContains("CLOSED")) {
                LOG_W(ats::ErrorSource::Ota, evt::OTA_CONN_CLOSED, bytesWritten);
                break;
            }
            noDataCount++;
            if (noDataCount > 3) {
                LOG_E(ats::ErrorSource::Ota, evt::OTA_DATA_TIMEOUT, bytesWritten);
                f_close(&f);
                return false;
            }
            continue;
        }
        noDataCount = 0;

        /* Parse +IPD,<len>:<data> from mRxBuf */
        const char* buf = esp.getResponse();
        uint16_t bufLen = esp.getResponseLen();

        /* Find +IPD, in buffer */
        const char* ipd = nullptr;
        for (uint16_t i = 0; i + 5 <= bufLen; i++) {
            if (memcmp(buf + i, "+IPD,", 5) == 0) {
                ipd = buf + i;
                break;
            }
        }
        if (!ipd) {
            esp.clearRx();
            continue;
        }

        /* Parse length */
        uint32_t ipdLen = 0;
        const char* p = ipd + 5;
        while (*p >= '0' && *p <= '9') {
            ipdLen = ipdLen * 10 + (*p - '0');
            p++;
        }
        if (*p != ':' || ipdLen == 0) {
            esp.clearRx();
            continue;
        }
        p++;  /* Skip ':' */

        const uint8_t* payload = (const uint8_t*)p;
        uint16_t available = bufLen - (uint16_t)(p - buf);
        uint16_t dataLen = (available < ipdLen) ? available : (uint16_t)ipdLen;

        if (!headersParsed) {
            /* First +IPD contains HTTP headers + body start */
            /* Find \r\n\r\n (end of headers) */
            const uint8_t* bodyStart = nullptr;
            for (uint16_t i = 0; i + 4 <= dataLen; i++) {
                if (payload[i] == '\r' && payload[i+1] == '\n' &&
                    payload[i+2] == '\r' && payload[i+3] == '\n') {
                    bodyStart = payload + i + 4;
                    break;
                }
            }

            if (!bodyStart) {
                /* Headers didn't fit in one +IPD — unlikely for our small GET */
                LOG_E(ats::ErrorSource::Ota, evt::OTA_HDR_TOO_LARGE);
                f_close(&f);
                return false;
            }

            /* Check HTTP status */
            if (memcmp(payload, "HTTP/1.", 7) != 0) {
                LOG_E(ats::ErrorSource::Ota, evt::OTA_NOT_HTTP);
                f_close(&f);
                return false;
            }

            /* Find status code (after "HTTP/1.x ") */
            const char* status = (const char*)payload + 9;
            if (status[0] != '2') {
                LOG_E(ats::ErrorSource::Ota, evt::OTA_HTTP_ERROR, (uint32_t)(status[0] - '0') * 100 + (status[1] - '0') * 10 + (status[2] - '0'));
                f_close(&f);
                return false;
            }

            headersParsed = true;

            /* Write body portion of first chunk */
            uint16_t bodyLen = dataLen - (uint16_t)(bodyStart - payload);
            if (bodyLen > 0) {
                UINT bw;
                f_write(&f, bodyStart, bodyLen, &bw);
                bytesWritten += bw;
            }
        } else {
            /* Subsequent +IPDs: pure body data */
            UINT bw;
            f_write(&f, payload, dataLen, &bw);
            bytesWritten += bw;
        }

        /* Update progress */
        if (expectedSize > 0) {
            mProgress = (uint8_t)((bytesWritten * 100UL) / expectedSize);
        }

        /* Log progress every 10KB */
        if ((bytesWritten % 10240) < 512) {
            LOG_D(ats::ErrorSource::Ota, evt::OTA_PROGRESS, bytesWritten);
        }

        esp.clearRx();
    }

    f_close(&f);
    mProgress = 100;

    LOG_I(ats::ErrorSource::Ota, evt::OTA_DL_COMPLETE, bytesWritten);
    return (bytesWritten >= expectedSize);
}

/* ---- CRC-32 verification ---- */
bool OtaServiceImpl::verifyCrc(uint32_t expectedCrc32, uint32_t fileSize)
{
    LOG_I(ats::ErrorSource::Ota, evt::OTA_CRC_START);

    FIL f;
    if (f_open(&f, OTA_FW_FILENAME, FA_READ) != FR_OK) return false;

    uint8_t buf[256];
    uint32_t crc = 0xFFFFFFFF;
    uint32_t total = 0;

    while (total < fileSize) {
        UINT br;
        uint32_t chunk = fileSize - total;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);

        if (f_read(&f, buf, chunk, &br) != FR_OK || br == 0) break;
        crc = crc32_calc(crc, buf, br);
        total += br;
    }
    f_close(&f);

    crc = ~crc;  /* Standard IEEE finalization */
    LOG_I(ats::ErrorSource::Ota, evt::OTA_CRC_RESULT, crc);

    return (crc == expectedCrc32);
}

/* ---- Write ota_meta.bin ---- */
bool OtaServiceImpl::writeOtaMeta(uint32_t fwSize, uint32_t crc32, const char* version)
{
    ota_meta_t meta;
    memset(&meta, 0, sizeof(meta));

    meta.magic = OTA_META_MAGIC;
    meta.version = OTA_META_VERSION;
    meta.fw_size = fwSize;
    meta.crc32 = crc32;
    meta.target_addr = APP_FLASH_BASE;
    strncpy(meta.fw_version, version, sizeof(meta.fw_version) - 1);
    meta.timestamp = 0;  /* No RTC epoch available here */

    /* Calculate self-CRC over first 40 bytes */
    meta.meta_crc = ~crc32_calc(0xFFFFFFFF, (const uint8_t*)&meta, OTA_META_CRC_OFFSET);

    FIL f;
    if (f_open(&f, OTA_META_FILENAME, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        return false;
    }

    UINT bw;
    FRESULT res = f_write(&f, &meta, sizeof(meta), &bw);
    f_close(&f);

    return (res == FR_OK && bw == sizeof(meta));
}

/* ---- Set BKP OTA flags ---- */
void OtaServiceImpl::setOtaFlag()
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    BKP->DR2 = OTA_FLAG_DR2_VALUE;
    BKP->DR3 = OTA_FLAG_DR3_VALUE;

    LOG_I(ats::ErrorSource::Ota, evt::OTA_FLAG_SET, (uint32_t)BKP->DR2);
}

} // namespace arcana
