#include "RegistrationServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "ChaCha20.hpp"
#include "CompanyKey.hpp"
#include "Crc32.hpp"
#include "Credentials.hpp"
#include "ff.h"
#include "Log.hpp"
#include "EventCodes.hpp"
#include "registration.pb.h"
#include "Crc16.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstdio>
#include <cstring>
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"

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

static const uint16_t CRED_PLAIN_SIZE = 256;
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
    memcpy(plain + off, c.topicPrefix, 36); off += 36;  // off=218
    // comm_key (32 bytes) at offset 218
    memcpy(plain + off, c.commKey,     32); off += 32;   // off=250
    plain[off] = c.hasCommKey ? 1 : 0; off += 1;        // off=251
    // Validation magic at offset 254-255
    plain[254] = CRED_VALID_MAGIC[0];
    plain[255] = CRED_VALID_MAGIC[1];
}

static bool unpackCreds(const uint8_t* plain, RegistrationService::Credentials& c) {
    // Check decryption validity — try new format first, then legacy
    bool newFormat = (plain[254] == CRED_VALID_MAGIC[0] && plain[255] == CRED_VALID_MAGIC[1]);
    bool legacyFormat = (plain[218] == CRED_VALID_MAGIC[0] && plain[219] == CRED_VALID_MAGIC[1]);
    if (!newFormat && !legacyFormat) {
        return false;  // corrupt or wrong key
    }

    uint16_t off = 0;
    memcpy(c.mqttUser,    plain + off, 36); off += 36;
    memcpy(c.mqttPass,    plain + off, 36); off += 36;
    memcpy(c.mqttBroker,  plain + off, 36); off += 36;
    memcpy(&c.mqttPort,   plain + off,  2); off += 2;
    memcpy(c.uploadToken, plain + off, 72); off += 72;
    memcpy(c.topicPrefix, plain + off, 36); off += 36;

    if (newFormat) {
        memcpy(c.commKey, plain + off, 32); off += 32;
        c.hasCommKey = (plain[off] == 1);
    } else {
        memset(c.commKey, 0, 32);
        c.hasCommKey = false;
    }

    return c.mqttUser[0] != '\0' && c.mqttBroker[0] != '\0';
}

// --- Try device.ats ch2 ---

static bool loadFromDeviceAts(uint8_t* deviceKey, RegistrationService::Credentials& creds) {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());
    if (!storage.isReady()) return false;

    uint8_t data[CRED_PLAIN_SIZE];
    uint16_t dataLen = 0;
    if (!storage.loadCredentials(data, sizeof(data), dataLen) || dataLen < CRED_PLAIN_SIZE)
        return false;

    // Block encryption protects the data — no app-level decrypt needed
    return unpackCreds(data, creds);
}

static bool saveToDeviceAts(uint8_t* deviceKey, const RegistrationService::Credentials& creds) {
    auto& storage = static_cast<atsstorage::AtsStorageServiceImpl&>(
        atsstorage::AtsStorageServiceImpl::getInstance());
    if (!storage.isReady()) return false;

    // Block encryption protects the data — store plaintext credentials
    uint8_t data[CRED_PLAIN_SIZE];
    packCreds(data, creds);
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
            LOG_I(ats::ErrorSource::Reg, evt::REG_LOADED_CH2);
            return true;
        }
        LOG_W(ats::ErrorSource::Reg, evt::REG_CH2_CORRUPT);
    }
    // Fallback: creds.enc (old boards without ch2)
    if (loadFromFile(mDeviceKey, mCreds)) {
        if (strcmp(mCreds.mqttUser, mDeviceId) == 0) {
            mCreds.valid = true;
            LOG_I(ats::ErrorSource::Reg, evt::REG_LOADED_ENC);
            return true;
        }
        LOG_W(ats::ErrorSource::Reg, evt::REG_ENC_CORRUPT);
    }
    mCreds.valid = false;
    return false;
}

bool RegistrationServiceImpl::saveCredentials() {
    bool ok = false;
    // Primary: device.ats ch2 (black box)
    if (saveToDeviceAts(mDeviceKey, mCreds)) {
        LOG_I(ats::ErrorSource::Reg, evt::REG_SAVED_CH2);
        ok = true;
    } else {
        // Fallback: creds.enc (old boards without ch2)
        if (saveToFile(mDeviceKey, mCreds)) {
            LOG_I(ats::ErrorSource::Reg, evt::REG_SAVED_ENC);
            ok = true;
        }
    }
    if (!ok) LOG_E(ats::ErrorSource::Reg, evt::REG_SAVE_FAIL);
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
    LOG_I(ats::ErrorSource::Reg, evt::REG_START);

    // --- Generate ephemeral EC P-256 keypair for ECDH ---
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_ecp_group grp;
    mbedtls_mpi devPriv;
    mbedtls_ecp_point devPub;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctrDrbg);
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&devPriv);
    mbedtls_ecp_point_init(&devPub);

    const char* pers = "arcana_reg_ecdh";
    if (mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                               reinterpret_cast<const unsigned char*>(pers),
                               strlen(pers)) != 0) {
        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
        mbedtls_ecp_gen_keypair(&grp, &devPriv, &devPub,
                                 mbedtls_ctr_drbg_random, &ctrDrbg) != 0) {
        mbedtls_ecp_point_free(&devPub);
        mbedtls_mpi_free(&devPriv);
        mbedtls_ecp_group_free(&grp);
        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    // Export dev_pub as raw x||y (64 bytes)
    uint8_t devPubBytes[64];
    mbedtls_mpi_write_binary(&devPub.MBEDTLS_PRIVATE(X), devPubBytes, 32);
    mbedtls_mpi_write_binary(&devPub.MBEDTLS_PRIVATE(Y), devPubBytes + 32, 32);

    // --- Encode RegisterRequest protobuf ---
    arcana_RegisterRequest req = arcana_RegisterRequest_init_zero;
    strncpy(req.device_id, mDeviceId, sizeof(req.device_id) - 1);
    memcpy(req.public_key.bytes, mDeviceKey, 32);
    memset(req.public_key.bytes + 32, 0, 32);
    req.public_key.size = 64;
    req.firmware_ver = 0x0100;
    // NEW: attach ephemeral ECDH public key
    memcpy(req.ecdh_pub.bytes, devPubBytes, 64);
    req.ecdh_pub.size = 64;

    uint8_t pbBuf[192];  // larger for ecdh_pub
    pb_ostream_t stream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
    if (!pb_encode(&stream, arcana_RegisterRequest_fields, &req)) {
        LOG_E(ats::ErrorSource::Reg, evt::REG_PB_FAIL);
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
            LOG_E(ats::ErrorSource::Reg, evt::REG_TCP_FAIL);
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
        LOG_E(ats::ErrorSource::Reg, evt::REG_CIPSEND_FAIL);
        esp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        return false;
    }

    esp.sendData(reinterpret_cast<const uint8_t*>(header), hLen, 3000);
    esp.sendData(frame, frameLen, 3000);

    if (!esp.waitFor("SEND OK", 5000)) {
        LOG_E(ats::ErrorSource::Reg, evt::REG_SEND_FAIL);
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
    LOG_D(ats::ErrorSource::Reg, evt::REG_RESP_BYTES, (uint32_t)respLen);

    // Search for FrameCodec magic 0xAC 0xDA 0x01 in response (after HTTP headers)
    bool found = false;
    for (uint16_t i = 0; i + 9 <= respLen; i++) {
        if (raw[i] == 0xAC && raw[i + 1] == 0xDA && raw[i + 2] == FRAME_VER) {
            uint16_t pLen = raw[i + 5] | (raw[i + 6] << 8);
            uint16_t totalFrame = 7 + pLen + 2;
            if (i + totalFrame <= respLen) {
                uint16_t expectedCrc = crc16(0, raw + i, 7 + pLen);
                uint16_t receivedCrc = raw[i + 7 + pLen] | (raw[i + 7 + pLen + 1] << 8);
                LOG_D(ats::ErrorSource::Reg, evt::REG_FRAME_FOUND, (uint32_t)i);
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
                LOG_W(ats::ErrorSource::Reg, evt::REG_FRAME_TRUNC);
            }
            break;
        }
    }
    if (!found) {
        LOG_W(ats::ErrorSource::Reg, evt::REG_NO_FRAME, (uint32_t)respLen);
    }

    esp.setIpdPassthrough(false);
    esp.sendCmd("AT+CIPCLOSE", "OK", 1000);

    bool result = false;

    if (found && mCreds.valid) {
        // --- ECDSA verify server_pub signature ---
        bool ecdsaOk = false;
        if (mServerPubLen == 64 && mEcdsaSigLen > 0) {
            // Hash: SHA256(server_pub + device_id)
            uint8_t hash[32];
            mbedtls_md_context_t md;
            mbedtls_md_init(&md);
            const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            if (mdInfo && mbedtls_md_setup(&md, mdInfo, 0) == 0) {
                mbedtls_md_starts(&md);
                mbedtls_md_update(&md, mServerPub, 64);
                mbedtls_md_update(&md, reinterpret_cast<const uint8_t*>(mDeviceId), 8);
                mbedtls_md_finish(&md, hash);
                mbedtls_md_free(&md);

                // Load company public key
                mbedtls_ecp_point companyPub;
                mbedtls_ecp_point_init(&companyPub);
                mbedtls_mpi r, s;
                mbedtls_mpi_init(&r);
                mbedtls_mpi_init(&s);

                if (mbedtls_mpi_read_binary(&companyPub.MBEDTLS_PRIVATE(X),
                                             crypto::kCompanyPub, 32) == 0 &&
                    mbedtls_mpi_read_binary(&companyPub.MBEDTLS_PRIVATE(Y),
                                             crypto::kCompanyPub + 32, 32) == 0 &&
                    mbedtls_mpi_lset(&companyPub.MBEDTLS_PRIVATE(Z), 1) == 0) {
                    // Parse DER signature to extract (r, s)
                    // DER: 0x30 <len> 0x02 <rlen> <r> 0x02 <slen> <s>
                    const uint8_t* p = mEcdsaSig;
                    if (mEcdsaSigLen > 6 && p[0] == 0x30) {
                        p += 2;  // skip SEQUENCE tag + len
                        if (p[0] == 0x02) {
                            uint8_t rLen = p[1]; p += 2;
                            mbedtls_mpi_read_binary(&r, p, rLen); p += rLen;
                            if (p[0] == 0x02) {
                                uint8_t sLen = p[1]; p += 2;
                                mbedtls_mpi_read_binary(&s, p, sLen);
                                ecdsaOk = (mbedtls_ecdsa_verify(&grp, hash, sizeof(hash),
                                                                 &companyPub, &r, &s) == 0);
                            }
                        }
                    }
                }
                mbedtls_mpi_free(&s);
                mbedtls_mpi_free(&r);
                mbedtls_ecp_point_free(&companyPub);
            }
        }

        // Skip ECDSA check if server didn't send signature (backward compat with old server)
        if (mEcdsaSigLen > 0 && !ecdsaOk) {
            LOG_W(ats::ErrorSource::Reg, 0x0D20);  // ECDSA verify failed
            mCreds.valid = false;
        } else {
            // --- ECDH: derive comm_key ---
            if (mServerPubLen == 64) {
                mbedtls_ecp_point srvPub;
                mbedtls_ecp_point_init(&srvPub);
                mbedtls_mpi shared;
                mbedtls_mpi_init(&shared);

                if (mbedtls_mpi_read_binary(&srvPub.MBEDTLS_PRIVATE(X), mServerPub, 32) == 0 &&
                    mbedtls_mpi_read_binary(&srvPub.MBEDTLS_PRIVATE(Y), mServerPub + 32, 32) == 0 &&
                    mbedtls_mpi_lset(&srvPub.MBEDTLS_PRIVATE(Z), 1) == 0 &&
                    mbedtls_ecdh_compute_shared(&grp, &shared, &srvPub, &devPriv,
                                                 mbedtls_ctr_drbg_random, &ctrDrbg) == 0) {
                    uint8_t sharedBytes[32];
                    mbedtls_mpi_write_binary(&shared, sharedBytes, 32);

                    // HKDF: comm_key = HKDF-SHA256(shared, device_id, "ARCANA-COMM")
                    // Simple HKDF: PRK = HMAC(salt=device_id, ikm=shared), OKM = HMAC(PRK, info+0x01)
                    uint8_t prk[32];
                    {
                        mbedtls_md_context_t hm;
                        mbedtls_md_init(&hm);
                        const mbedtls_md_info_t* mi = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
                        mbedtls_md_setup(&hm, mi, 1);
                        mbedtls_md_hmac_starts(&hm, reinterpret_cast<const uint8_t*>(mDeviceId), 8);
                        mbedtls_md_hmac_update(&hm, sharedBytes, 32);
                        mbedtls_md_hmac_finish(&hm, prk);
                        mbedtls_md_free(&hm);
                    }
                    {
                        const uint8_t info[] = "ARCANA-COMM";
                        uint8_t infoBlock[sizeof(info)];  // info + 0x01
                        memcpy(infoBlock, info, sizeof(info) - 1);
                        infoBlock[sizeof(info) - 1] = 0x01;

                        mbedtls_md_context_t hm;
                        mbedtls_md_init(&hm);
                        const mbedtls_md_info_t* mi = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
                        mbedtls_md_setup(&hm, mi, 1);
                        mbedtls_md_hmac_starts(&hm, prk, 32);
                        mbedtls_md_hmac_update(&hm, infoBlock, sizeof(info));
                        mbedtls_md_hmac_finish(&hm, mCreds.commKey);
                        mbedtls_md_free(&hm);
                    }
                    mCreds.hasCommKey = true;
                    LOG_I(ats::ErrorSource::Reg, 0x0D21);  // comm_key derived
                }
                mbedtls_mpi_free(&shared);
                mbedtls_ecp_point_free(&srvPub);
            }

            LOG_I(ats::ErrorSource::Reg, evt::REG_OK);
            result = true;
        }
    }

    if (!result && !mCreds.valid) {
        LOG_E(ats::ErrorSource::Reg, evt::REG_PARSE_FAIL);
    }

    // Cleanup ECDH resources
    mbedtls_ecp_point_free(&devPub);
    mbedtls_mpi_free(&devPriv);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);

    return result;
}

bool RegistrationServiceImpl::parseResponse(const uint8_t* payload, uint16_t len) {
    // Decode RegisterResponse protobuf
    arcana_RegisterResponse resp = arcana_RegisterResponse_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, len);
    if (!pb_decode(&stream, arcana_RegisterResponse_fields, &resp)) {
        LOG_E(ats::ErrorSource::Reg, evt::REG_PB_DECODE_FAIL, (uint32_t)len);
        return false;
    }

    if (!resp.success) {
        LOG_E(ats::ErrorSource::Reg, evt::REG_SERVER_ERROR);
        return false;
    }

    // Store credentials
    strncpy(mCreds.mqttUser, resp.mqtt_user, sizeof(mCreds.mqttUser) - 1);
    strncpy(mCreds.mqttPass, resp.mqtt_pass, sizeof(mCreds.mqttPass) - 1);
    strncpy(mCreds.mqttBroker, resp.mqtt_broker, sizeof(mCreds.mqttBroker) - 1);
    mCreds.mqttPort = (uint16_t)resp.mqtt_port;
    strncpy(mCreds.uploadToken, resp.upload_token, sizeof(mCreds.uploadToken) - 1);
    strncpy(mCreds.topicPrefix, resp.topic_prefix, sizeof(mCreds.topicPrefix) - 1);

    // Extract server_pub and ecdsa_sig (new fields)
    mServerPubLen = 0;
    mEcdsaSigLen = 0;
    if (resp.server_pub.size == 64) {
        memcpy(mServerPub, resp.server_pub.bytes, 64);
        mServerPubLen = 64;
    }
    if (resp.ecdsa_sig.size > 0 && resp.ecdsa_sig.size <= 72) {
        memcpy(mEcdsaSig, resp.ecdsa_sig.bytes, resp.ecdsa_sig.size);
        mEcdsaSigLen = (uint8_t)resp.ecdsa_sig.size;
    }

    mCreds.valid = true;
    return true;
}

} // namespace reg
} // namespace arcana
