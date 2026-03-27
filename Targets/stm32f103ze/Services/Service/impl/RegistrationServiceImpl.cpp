#include "RegistrationServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "ChaCha20.hpp"
#include "Crc32.hpp"
#include "Credentials.hpp"
#include "ff.h"
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
// Credential persistence — device.ats ch2 (primary) or creds.enc (fallback)
// ---------------------------------------------------------------------------
// Encrypted format: [nonce:12][encrypted_payload:220]
// Payload: [user:36][pass:36][broker:36][port:2][token:72][prefix:36] = 218 + 2 pad
// creds.enc: [magic "CRD1":4][nonce:12][encrypted:220][crc32:4] = 240 bytes

static const uint16_t CRED_PLAIN_SIZE = 220;
static const uint8_t  CRED_MAGIC[4] = {'C','R','D','1'};
static const uint16_t CRED_FILE_SIZE = 4 + 12 + CRED_PLAIN_SIZE + 4;  // 240

extern "C" volatile uint8_t g_exfat_ready;

// --- Common: encrypt/decrypt + pack/unpack ---

// Validation magic at plaintext[218..219] — detects corrupt decryption
static const uint8_t CRED_VALID_MAGIC[2] = {0xCE, 0xED};

static void packCreds(uint8_t* plain, const RegistrationService::Credentials& c) {
    memset(plain, 0, CRED_PLAIN_SIZE);
    uint16_t off = 0;
    memcpy(plain + off, c.mqttUser,    36); off += 36;
    memcpy(plain + off, c.mqttPass,    36); off += 36;
    memcpy(plain + off, c.mqttBroker,  36); off += 36;
    memcpy(plain + off, &c.mqttPort,    2); off += 2;
    memcpy(plain + off, c.uploadToken, 72); off += 72;
    memcpy(plain + off, c.topicPrefix, 36); off += 36;
    // Validation magic in last 2 bytes (offset 218-219)
    plain[218] = CRED_VALID_MAGIC[0];
    plain[219] = CRED_VALID_MAGIC[1];
}

static bool unpackCreds(const uint8_t* plain, RegistrationService::Credentials& c) {
    // Check decryption validity first
    if (plain[218] != CRED_VALID_MAGIC[0] || plain[219] != CRED_VALID_MAGIC[1]) {
        return false;  // corrupt or wrong key
    }
    uint16_t off = 0;
    memcpy(c.mqttUser,    plain + off, 36); off += 36;
    memcpy(c.mqttPass,    plain + off, 36); off += 36;
    memcpy(c.mqttBroker,  plain + off, 36); off += 36;
    memcpy(&c.mqttPort,   plain + off,  2); off += 2;
    memcpy(c.uploadToken, plain + off, 72); off += 72;
    memcpy(c.topicPrefix, plain + off, 36);
    return c.mqttUser[0] != '\0' && c.mqttBroker[0] != '\0';
}

// --- Try device.ats ch2 ---

static bool loadFromDeviceAts(uint8_t* deviceKey, RegistrationService::Credentials& creds) {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());
    if (!storage.isReady()) return false;

    uint8_t data[232];
    uint16_t dataLen = 0;
    if (!storage.loadCredentials(data, sizeof(data), dataLen) || dataLen < 232)
        return false;

    uint8_t plain[CRED_PLAIN_SIZE];
    memcpy(plain, data + 12, CRED_PLAIN_SIZE);
    crypto::ChaCha20::crypt(deviceKey, data, 0, plain, CRED_PLAIN_SIZE);
    return unpackCreds(plain, creds);
}

static bool saveToDeviceAts(uint8_t* deviceKey, const RegistrationService::Credentials& creds) {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());
    if (!storage.isReady()) return false;

    uint8_t plain[CRED_PLAIN_SIZE];
    uint8_t data[232];
    uint8_t nonce[12];

    packCreds(plain, creds);
    uint32_t tick = xTaskGetTickCount();
    memcpy(nonce, &tick, 4);
    crypto::DeviceKey::getUID(nonce + 4);
    crypto::ChaCha20::crypt(deviceKey, nonce, 0, plain, CRED_PLAIN_SIZE);

    memcpy(data, nonce, 12);
    memcpy(data + 12, plain, CRED_PLAIN_SIZE);
    return storage.saveCredentials(data, sizeof(data));
}

// --- Fallback: creds.enc file ---

// Shared static FIL + buffer for creds.enc (saves ~800 bytes stack)
static FIL sCredFil;
static uint8_t sCredFileBuf[CRED_FILE_SIZE];

static bool loadFromFile(uint8_t* deviceKey, RegistrationService::Credentials& creds) {
    if (f_open(&sCredFil, "creds.enc", FA_READ) != FR_OK) return false;
    UINT br;
    FRESULT fr = f_read(&sCredFil, sCredFileBuf, CRED_FILE_SIZE, &br);
    f_close(&sCredFil);
    if (fr != FR_OK || br != CRED_FILE_SIZE) return false;
    if (memcmp(sCredFileBuf, CRED_MAGIC, 4) != 0) return false;

    uint32_t stored, calc;
    memcpy(&stored, sCredFileBuf + CRED_FILE_SIZE - 4, 4);
    calc = ~crc32(0xFFFFFFFF, sCredFileBuf, CRED_FILE_SIZE - 4);
    if (stored != calc) return false;

    uint8_t plain[CRED_PLAIN_SIZE];
    memcpy(plain, sCredFileBuf + 16, CRED_PLAIN_SIZE);
    crypto::ChaCha20::crypt(deviceKey, sCredFileBuf + 4, 0, plain, CRED_PLAIN_SIZE);
    return unpackCreds(plain, creds);
}

static bool saveToFile(uint8_t* deviceKey, const RegistrationService::Credentials& creds) {
    uint8_t plain[CRED_PLAIN_SIZE];
    uint8_t nonce[12];

    packCreds(plain, creds);
    uint32_t tick = xTaskGetTickCount();
    memcpy(nonce, &tick, 4);
    crypto::DeviceKey::getUID(nonce + 4);
    crypto::ChaCha20::crypt(deviceKey, nonce, 0, plain, CRED_PLAIN_SIZE);

    memcpy(sCredFileBuf, CRED_MAGIC, 4);
    memcpy(sCredFileBuf + 4, nonce, 12);
    memcpy(sCredFileBuf + 16, plain, CRED_PLAIN_SIZE);
    uint32_t crc = ~crc32(0xFFFFFFFF, sCredFileBuf, CRED_FILE_SIZE - 4);
    memcpy(sCredFileBuf + CRED_FILE_SIZE - 4, &crc, 4);

    if (f_open(&sCredFil, "creds.tmp", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT bw;
    FRESULT fr = f_write(&sCredFil, sCredFileBuf, CRED_FILE_SIZE, &bw);
    f_close(&sCredFil);
    if (fr != FR_OK || bw != CRED_FILE_SIZE) return false;
    f_unlink("creds.enc");
    f_rename("creds.tmp", "creds.enc");
    return true;
}

// --- Public API ---

bool RegistrationServiceImpl::loadCredentials() {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());

    // Wait for SD card + device.ats to be ready (ATS task races with MQTT task)
    for (int i = 0; i < 30; i++) {
        if (g_exfat_ready && storage.isReady()) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!g_exfat_ready) return false;

    // Try device.ats ch2 first (black box)
    if (storage.isReady() && loadFromDeviceAts(mDeviceKey, mCreds)) {
        // Validate: username must match this device's ID
        if (strcmp(mCreds.mqttUser, mDeviceId) == 0) {
            mCreds.valid = true;
            printf("[REG] loaded from device.ats ch2: user=%s\r\n", mCreds.mqttUser);
            return true;
        }
        printf("[REG] ch2 data corrupt (user=%s)\r\n", mCreds.mqttUser);
    }
    // Fallback: creds.enc (old boards without ch2)
    if (loadFromFile(mDeviceKey, mCreds)) {
        if (strcmp(mCreds.mqttUser, mDeviceId) == 0) {
            mCreds.valid = true;
            printf("[REG] loaded from creds.enc: user=%s\r\n", mCreds.mqttUser);
            return true;
        }
        printf("[REG] creds.enc data corrupt (user=%s)\r\n", mCreds.mqttUser);
    }
    mCreds.valid = false;
    return false;
}

bool RegistrationServiceImpl::saveCredentials() {
    bool ok = false;
    // Primary: device.ats ch2 (black box)
    if (saveToDeviceAts(mDeviceKey, mCreds)) {
        printf("[REG] saved to device.ats ch2\r\n");
        ok = true;
    } else {
        // Fallback: creds.enc (old boards without ch2)
        if (saveToFile(mDeviceKey, mCreds)) {
            printf("[REG] saved to creds.enc (ch2 unavailable)\r\n");
            ok = true;
        }
    }
    if (!ok) printf("[REG] credential save FAILED\r\n");
    return ok;
}

// ---------------------------------------------------------------------------
// Registration via HTTP POST
// ---------------------------------------------------------------------------

bool RegistrationServiceImpl::doRegistration() {
    if (mCreds.valid) return true;  // already registered

    // Try loading from storage (skip if invalidated — stale data)
    if (!mForceRegister && loadCredentials() && mCreds.valid) return true;
    mForceRegister = false;

    // Need to register — get ESP8266
    Esp8266& esp = Esp8266::getInstance();
    if (!httpRegister(esp)) return false;

    // Save AFTER httpRegister returns (frees 512B response buffer from stack)
    saveCredentials();
    return true;
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
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"SSL\",\"%s\",%u", REG_SERVER, REG_PORT);
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

    // --- Wait for response (headers + body in separate +IPD chunks) ---
    esp.clearRx();
    esp.setIpdPassthrough(true);

    // Wait for server to process + respond (3s) then collect all data
    vTaskDelay(pdMS_TO_TICKS(4000));

    // Multiple waitFor to drain semaphore for each +IPD chunk
    for (int i = 0; i < 5; i++) {
        if (!esp.waitFor("+IPD", 1000)) break;
    }
    vTaskDelay(pdMS_TO_TICKS(500));  // let final IDLE event fire

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(esp.getResponse());
    uint16_t respLen = esp.getResponseLen();
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
                    const uint8_t* framePayload = raw + i + 7;
                    // Try cleartext first
                    found = parseResponse(framePayload, pLen);
                    // If cleartext failed and payload long enough, try encrypted
                    if (!found && pLen > 12) {
                        uint8_t* enc = const_cast<uint8_t*>(framePayload + 12);
                        uint16_t encLen = pLen - 12;
                        crypto::ChaCha20::crypt(mDeviceKey, framePayload, 0, enc, encLen);
                        found = parseResponse(enc, encLen);
                    }
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
        // saveCredentials called by doRegistration after httpRegister returns
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
    strncpy(mCreds.mqttBroker, resp.mqtt_broker, sizeof(mCreds.mqttBroker) - 1);
    mCreds.mqttPort = (uint16_t)resp.mqtt_port;
    strncpy(mCreds.uploadToken, resp.upload_token, sizeof(mCreds.uploadToken) - 1);
    strncpy(mCreds.topicPrefix, resp.topic_prefix, sizeof(mCreds.topicPrefix) - 1);
    mCreds.valid = true;

    return true;
}

} // namespace reg
} // namespace arcana
