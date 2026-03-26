#include "RegistrationServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "Credentials.hpp"
#include "Log.hpp"
#include "registration.pb.h"
#include "Crc16.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstdio>
#include <cstring>

namespace arcana {
namespace reg {

static const char* REG_SERVER = "arcana.boo";
static const uint16_t REG_PORT = 8088;

// FrameCodec constants (same as Shared/Inc/FrameCodec.hpp)
static const uint8_t FRAME_MAGIC[] = {0xAC, 0xDA};
static const uint8_t FRAME_VER = 0x01;
static const uint8_t SID_REGISTER = 0x10;

RegistrationServiceImpl::RegistrationServiceImpl()
    : mCreds{}
    , mDeviceId{}
    , mDeviceKey{}
{
    // Derive device ID (8-char hex of first 4 UID bytes)
    uint8_t uid[12];
    crypto::DeviceKey::getUID(uid);
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 4; i++) {
        mDeviceId[i * 2]     = hex[uid[i] >> 4];
        mDeviceId[i * 2 + 1] = hex[uid[i] & 0x0F];
    }
    mDeviceId[8] = '\0';

    // Derive device key (32 bytes) — used as "public key" for TOFU registration
    crypto::DeviceKey::deriveKey(mDeviceKey);
}

RegistrationServiceImpl& RegistrationServiceImpl::getInstance() {
    static RegistrationServiceImpl sInstance;
    return sInstance;
}

// ---------------------------------------------------------------------------
// Load/Save credentials from device.ats CONFIG channel
// ---------------------------------------------------------------------------

bool RegistrationServiceImpl::loadCredentials() {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());

    // Wait for device DB to be ready
    for (int i = 0; i < 10; i++) {
        if (storage.isReady()) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Query device.ats CONFIG ch3 for stored credentials
    // Record format: [mqttUser:32][mqttPass:32][uploadToken:64][topicPrefix:32] = 160 bytes
    uint8_t buf[160];
    // TODO: implement device.ats ch3 query when AtsStorageServiceImpl supports it
    // For now, check a simple flag in the credential struct
    mCreds.valid = false;
    return false;  // Not implemented yet — will always register
}

bool RegistrationServiceImpl::saveCredentials() {
    // TODO: save to device.ats CONFIG ch3
    printf("[REG] credentials saved (in-memory only for now)\r\n");
    return true;
}

// ---------------------------------------------------------------------------
// Registration via HTTP POST
// ---------------------------------------------------------------------------

bool RegistrationServiceImpl::doRegistration() {
    if (mCreds.valid) return true;  // already registered

    // Try loading from storage first
    if (loadCredentials() && mCreds.valid) return true;

    // Need to register — get ESP8266
    Esp8266& esp = Esp8266::getInstance();
    return httpRegister(esp);
}

bool RegistrationServiceImpl::httpRegister(Esp8266& esp) {
    printf("[REG] registering %s...\r\n", mDeviceId);

    // --- Encode RegisterRequest protobuf ---
    arcana_RegisterRequest req = arcana_RegisterRequest_init_zero;
    strncpy(req.device_id, mDeviceId, sizeof(req.device_id) - 1);
    // Use first 32 bytes of device key as "public key" (padded to 64 for P-256 compat)
    memcpy(req.public_key.bytes, mDeviceKey, 32);
    memset(req.public_key.bytes + 32, 0, 32);  // pad remaining 32 bytes
    req.public_key.size = 64;
    req.firmware_ver = 0x0100;  // v1.0

    uint8_t pbBuf[128];
    pb_ostream_t stream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
    if (!pb_encode(&stream, arcana_RegisterRequest_fields, &req)) {
        printf("[REG] pb encode fail\r\n");
        return false;
    }
    uint16_t pbLen = (uint16_t)stream.bytes_written;

    // --- Wrap in FrameCodec ---
    // [magic:2][ver:1][flags:1][sid:1][len:2][payload:N][crc:2]
    uint8_t frame[256];
    uint16_t fIdx = 0;
    frame[fIdx++] = FRAME_MAGIC[0];
    frame[fIdx++] = FRAME_MAGIC[1];
    frame[fIdx++] = FRAME_VER;
    frame[fIdx++] = 0x01;  // FLAG_FIN
    frame[fIdx++] = SID_REGISTER;
    frame[fIdx++] = (uint8_t)(pbLen & 0xFF);
    frame[fIdx++] = (uint8_t)(pbLen >> 8);
    memcpy(frame + fIdx, pbBuf, pbLen);
    fIdx += pbLen;
    // CRC-16 over header + payload
    uint16_t crc = crc16(0,frame, fIdx);
    frame[fIdx++] = (uint8_t)(crc & 0xFF);
    frame[fIdx++] = (uint8_t)(crc >> 8);

    uint16_t frameLen = fIdx;

    // --- TCP connect ---
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", REG_SERVER, REG_PORT);
    if (!esp.sendCmd(cmd, "OK", 10000)) {
        if (!esp.responseContains("ALREADY CONNECTED")) {
            printf("[REG] TCP FAIL\r\n");
            return false;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // --- HTTP POST header ---
    char header[256];
    int hLen = snprintf(header, sizeof(header),
        "POST /api/register HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %u\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n"
        "\r\n",
        REG_SERVER, frameLen);

    // Send header + body
    uint16_t totalLen = (uint16_t)(hLen + frameLen);
    char cipsend[24];
    snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%u", totalLen);
    if (!esp.sendCmd(cipsend, ">", 3000)) {
        printf("[REG] CIPSEND FAIL\r\n");
        esp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        return false;
    }

    esp.sendData(reinterpret_cast<const uint8_t*>(header), hLen, 3000);
    esp.sendData(frame, frameLen, 3000);

    if (!esp.waitFor("SEND OK", 5000)) {
        printf("[REG] SEND FAIL\r\n");
        esp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        return false;
    }

    // --- Wait for response (headers + body may come in separate +IPD chunks) ---
    esp.clearRx();
    esp.setIpdPassthrough(true);

    // Wait for first +IPD (HTTP headers)
    esp.waitFor("+IPD", 8000);
    // Wait for second +IPD (HTTP body with FrameCodec frame)
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp.waitFor("+IPD", 3000);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Use mRxPos (actual bytes received) not mRxLen (last idle snapshot)
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(esp.getResponse());
    uint16_t respLen = esp.getResponseLen();
    // Check if more bytes in buffer than reported by idle
    // (mRxPos may be ahead of mRxLen)
    if (respLen < 200) {
        // Probably missing body — wait more
        vTaskDelay(pdMS_TO_TICKS(2000));
        respLen = esp.getResponseLen();
    }
    printf("[REG] resp %u bytes\r\n", respLen);

    // Search for FrameCodec magic 0xAC 0xDA 0x01 in response (after HTTP headers)
    bool found = false;
    for (uint16_t i = 0; i + 9 <= respLen; i++) {
        if (raw[i] == 0xAC && raw[i + 1] == 0xDA && raw[i + 2] == FRAME_VER) {
            uint16_t pLen = raw[i + 5] | (raw[i + 6] << 8);
            uint16_t totalFrame = 7 + pLen + 2;
            if (i + totalFrame <= respLen) {
                uint16_t expectedCrc = crc16(0, raw + i, 7 + pLen);
                uint16_t receivedCrc = raw[i + 7 + pLen] | (raw[i + 7 + pLen + 1] << 8);
                printf("[REG] frame @%u len=%u crc=%s\r\n",
                       i, pLen, expectedCrc == receivedCrc ? "OK" : "FAIL");
                if (expectedCrc == receivedCrc) {
                    found = parseResponse(raw + i + 7, pLen);
                }
            } else {
                printf("[REG] frame @%u truncated (need %u, have %u)\r\n",
                       i, i + totalFrame, respLen);
            }
            break;
        }
    }
    if (!found) {
        printf("[REG] no frame found in %u bytes\r\n", respLen);
    }

    esp.setIpdPassthrough(false);
    esp.sendCmd("AT+CIPCLOSE", "OK", 1000);

    if (found && mCreds.valid) {
        printf("[REG] OK: user=%s topic=%s\r\n", mCreds.mqttUser, mCreds.topicPrefix);
        saveCredentials();
        return true;
    }

    printf("[REG] FAIL: response not parsed\r\n");
    return false;
}

bool RegistrationServiceImpl::parseResponse(const uint8_t* payload, uint16_t len) {
    // Decode RegisterResponse protobuf
    arcana_RegisterResponse resp = arcana_RegisterResponse_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, len);
    if (!pb_decode(&stream, arcana_RegisterResponse_fields, &resp)) {
        printf("[REG] pb decode fail: %s (len=%u)\r\n", PB_GET_ERROR(&stream), len);
        // Dump first 16 bytes of payload for debug
        printf("[REG] payload: ");
        for (uint16_t j = 0; j < (len < 16 ? len : 16); j++) printf("%02X ", payload[j]);
        printf("\r\n");
        return false;
    }

    if (!resp.success) {
        printf("[REG] server error: %s\r\n", resp.error);
        return false;
    }

    // Store credentials
    strncpy(mCreds.mqttUser, resp.mqtt_user, sizeof(mCreds.mqttUser) - 1);
    strncpy(mCreds.mqttPass, resp.mqtt_pass, sizeof(mCreds.mqttPass) - 1);
    strncpy(mCreds.uploadToken, resp.upload_token, sizeof(mCreds.uploadToken) - 1);
    strncpy(mCreds.topicPrefix, resp.topic_prefix, sizeof(mCreds.topicPrefix) - 1);
    mCreds.valid = true;

    return true;
}

} // namespace reg
} // namespace arcana
