#include "CommandBridge.hpp"
#include "FrameCodec.hpp"
#include "Crc16.hpp"
#include "ChaCha20.hpp"
#include "Sha256.hpp"
#include "stm32f1xx_hal.h"
#include "Commands.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#include "uECC.h"
#include <cstring>

// DeviceKey needs UID_BASE from the HAL header above
#include "DeviceKey.hpp"

namespace arcana {

// ---------------------------------------------------------------------------
// Built-in commands (static instances)
// ---------------------------------------------------------------------------

class PingCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::System, SystemCommand::Ping };
    }
    void execute(const CommandRequest& req, CommandResponseModel& rsp) override {
        (void)req;
        uint32_t tick = xTaskGetTickCount();
        rsp.setUint32(tick);
        rsp.status = CommandStatus::Success;
    }
};

static PingCommand              sPingCmd;
static GetFwVersionCommand      sFwVerCmd;
static GetCompileTimeCommand    sCompileCmd;
static GetDeviceModelCommand    sModelCmd;
static GetSerialNumberCommand   sSerialCmd;
static GetTemperatureCommand    sTempCmd;
static GetAccelCommand          sAccelCmd;
static GetLightCommand          sLightCmd;

// ---------------------------------------------------------------------------
// ChaCha20 CSPRNG for uECC (same as RegistrationServiceImpl)
// ---------------------------------------------------------------------------

static int ueccRng(uint8_t* dest, unsigned size) {
    uint8_t seed[32];
    const uint32_t* uid = reinterpret_cast<const uint32_t*>(UID_BASE);
    uint32_t* s = reinterpret_cast<uint32_t*>(seed);
    for (int i = 0; i < 8; i++) {
        s[i] = uid[i % 3] ^ SysTick->VAL ^ DWT->CYCCNT;
        volatile uint32_t dummy = 0;
        for (int j = 0; j < 10; j++) dummy += SysTick->VAL;
        (void)dummy;
    }

    static uint32_t sRngCounter = 0;
    uint8_t nonce[12] = {};
    uint32_t tick = HAL_GetTick();
    memcpy(nonce, &tick, 4);
    memcpy(nonce + 4, &sRngCounter, 4);
    sRngCounter++;

    memset(dest, 0, size);
    crypto::ChaCha20::crypt(seed, nonce, 0, dest, size);
    return 1;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CommandBridge::CommandBridge()
    : mCommands{}
    , mCommandCount(0)
    , mSensorCache()
    , mRxQueue(0)
    , mRxQueueBuf()
    , mRxQueueStorage{}
    , mSessions{}
    , mDeviceKey{}
    , mBridgeTaskBuf()
    , mBridgeStack{}
    , mBleSend(nullptr)
    , mBleCtx(nullptr)
    , mMqttSend(nullptr)
    , mMqttCtx(nullptr)
{
    // Register all commands
    registerCommand(&sPingCmd);
    registerCommand(&sFwVerCmd);
    registerCommand(&sCompileCmd);
    registerCommand(&sModelCmd);
    registerCommand(&sSerialCmd);

    sTempCmd.cache  = &mSensorCache;
    sAccelCmd.cache = &mSensorCache;
    sLightCmd.cache = &mSensorCache;
    registerCommand(&sTempCmd);
    registerCommand(&sAccelCmd);
    registerCommand(&sLightCmd);

    // Create RX queue
    mRxQueue = xQueueCreateStatic(RX_QUEUE_LEN, sizeof(CmdFrameItem),
                                   mRxQueueStorage, &mRxQueueBuf);

    // Derive device key for key exchange auth
    crypto::DeviceKey::deriveKey(mDeviceKey);

    LOG_I(ats::ErrorSource::Cmd, evt::CMD_REGISTERED, (uint32_t)mCommandCount);
}

CommandBridge& CommandBridge::getInstance() {
    static CommandBridge sInstance;
    return sInstance;
}

bool CommandBridge::registerCommand(ICommand* cmd) {
    if (mCommandCount >= MAX_COMMANDS) return false;
    mCommands[mCommandCount++] = cmd;
    return true;
}

ICommand* CommandBridge::findCommand(CommandKey key) {
    for (uint8_t i = 0; i < mCommandCount; i++) {
        if (mCommands[i]->getKey() == key) return mCommands[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Submit frame from any transport
// ---------------------------------------------------------------------------

bool CommandBridge::submitFrame(const uint8_t* data, uint16_t len,
                                 CmdFrameItem::Transport source) {
    if (len > CmdFrameItem::MAX_DATA) return false;

    CmdFrameItem item;
    memcpy(item.data, data, len);
    item.len = len;
    item.source = source;
    return xQueueSend(mRxQueue, &item, 0) == pdTRUE;
}

// ---------------------------------------------------------------------------
// Crypto helpers
// ---------------------------------------------------------------------------

bool CommandBridge::handleKeyExchange(uint8_t source, const uint8_t* clientPub,
                                       uint8_t* serverPub, uint8_t* authTag) {
    const uECC_Curve_t* curve = uECC_secp256r1();
    uECC_set_rng(ueccRng);

    // Generate server ephemeral keypair
    uint8_t serverPriv[32];
    if (!uECC_make_key(serverPub, serverPriv, curve)) return false;

    // ECDH shared secret
    uint8_t shared[32];
    if (!uECC_shared_secret(clientPub, serverPriv, shared, curve)) return false;

    // Session key = HKDF-SHA256(shared, device_key, "ARCANA-SESSION")
    uint8_t sessionKey[32];
    crypto::Sha256::hkdf(shared, 32, mDeviceKey, 32,
                          (const uint8_t*)"ARCANA-SESSION", 14,
                          sessionKey, 32);

    // Auth tag = HMAC-SHA256(device_key, serverPub || clientPub)
    uint8_t authData[128];
    memcpy(authData, serverPub, 64);
    memcpy(authData + 64, clientPub, 64);
    crypto::Sha256::hmac(mDeviceKey, 32, authData, 128, authTag);

    // Install session
    uint8_t idx = source & 1;
    memcpy(mSessions[idx].key, sessionKey, 32);
    mSessions[idx].txCounter = 0;
    mSessions[idx].rxCounter = 0;
    mSessions[idx].active = true;

    // Clear ephemeral private key (PFS)
    memset(serverPriv, 0, sizeof(serverPriv));
    memset(shared, 0, sizeof(shared));

    return true;
}

bool CommandBridge::encryptAndFrame(uint8_t source, uint8_t streamId,
                                     const uint8_t* plain, size_t plainLen,
                                     uint8_t* frameBuf, size_t frameBufSize,
                                     size_t& frameLen) {
    uint8_t idx = source & 1;
    ChaChaSession& sess = mSessions[idx];
    if (!sess.active) return false;

    // Build nonce: [counter:4 LE][tick:4 LE][zeros:4]
    uint8_t nonce[12] = {};
    uint32_t counter = sess.txCounter++;
    uint32_t tick = xTaskGetTickCount();
    memcpy(nonce, &counter, 4);
    memcpy(nonce + 4, &tick, 4);

    // Payload: [nonce:12][ciphertext:N][hmac:32]
    uint8_t payload[80];
    size_t payloadLen = 12 + plainLen + 32;
    if (payloadLen > sizeof(payload)) return false;

    memcpy(payload, nonce, 12);
    memcpy(payload + 12, plain, plainLen);

    // ChaCha20 encrypt ciphertext portion in-place
    crypto::ChaCha20::crypt(sess.key, nonce, 0, payload + 12, plainLen);

    // HMAC-SHA256 over nonce + ciphertext
    crypto::Sha256::hmac(sess.key, 32, payload, 12 + plainLen,
                          payload + 12 + plainLen);

    return FrameCodec::frame(payload, payloadLen, FrameCodec::kFlagFin,
                              streamId, frameBuf, frameBufSize, frameLen);
}

bool CommandBridge::decryptAndVerify(uint8_t source,
                                      const uint8_t* payload, size_t payloadLen,
                                      uint8_t* plain, size_t plainBufSize,
                                      size_t& plainLen) {
    uint8_t idx = source & 1;
    ChaChaSession& sess = mSessions[idx];
    if (!sess.active) return false;
    if (payloadLen < 12 + 1 + 32) return false; // nonce + min 1B cipher + hmac

    size_t cipherLen = payloadLen - 12 - 32;
    if (cipherLen > plainBufSize) return false;

    const uint8_t* nonce = payload;
    const uint8_t* cipher = payload + 12;
    const uint8_t* hmac = payload + 12 + cipherLen;

    // Verify HMAC-SHA256(key, nonce || ciphertext)
    uint8_t expectedHmac[32];
    crypto::Sha256::hmac(sess.key, 32, payload, 12 + cipherLen, expectedHmac);
    if (memcmp(hmac, expectedHmac, 32) != 0) return false;

    // Replay protection: nonce counter must increase
    uint32_t rxCounter;
    memcpy(&rxCounter, nonce, 4);
    if (sess.rxCounter > 0 && rxCounter <= sess.rxCounter) return false;
    sess.rxCounter = rxCounter;

    // ChaCha20 decrypt
    memcpy(plain, cipher, cipherLen);
    crypto::ChaCha20::crypt(sess.key, nonce, 0, plain, cipherLen);
    plainLen = cipherLen;
    return true;
}

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------

void CommandBridge::startTasks() {
    xTaskCreateStatic(bridgeTask, "CmdBr", BRIDGE_STACK_SIZE,
                      this, tskIDLE_PRIORITY + 2,
                      mBridgeStack, &mBridgeTaskBuf);
}

void CommandBridge::bridgeTask(void* param) {
    CommandBridge* self = static_cast<CommandBridge*>(param);
    LOG_I(ats::ErrorSource::Cmd, evt::CMD_BRIDGE_START);

    while (true) {
        CmdFrameItem frame;
        if (xQueueReceive(self->mRxQueue, &frame, portMAX_DELAY) != pdTRUE) continue;

        // Deframe
        const uint8_t* payload = nullptr;
        size_t payloadLen = 0;
        uint8_t flags = 0, streamId = 0;

        if (!FrameCodec::deframe(frame.data, frame.len,
                                  payload, payloadLen, flags, streamId)) {
            LOG_W(ats::ErrorSource::Cmd, evt::CMD_BAD_FRAME, (uint32_t)frame.len);
            continue;
        }

        // Decode command request
        CommandRequest req;
        bool encrypted = false;

        if (streamId == SID_ENCRYPTED) {
            // ── Encrypted path: verify HMAC → ChaCha20 decrypt → binary decode ──
            uint8_t plainBuf[32];
            size_t plainLen = 0;
            if (!self->decryptAndVerify(static_cast<uint8_t>(frame.source),
                                         payload, payloadLen,
                                         plainBuf, sizeof(plainBuf), plainLen)) {
                LOG_W(ats::ErrorSource::Cmd, evt::CMD_BAD_FRAME, 0xDE);
                continue;
            }
            if (plainLen < 3) continue;
            req.key.cluster = static_cast<Cluster>(plainBuf[0]);
            req.key.commandId = plainBuf[1];
            req.paramsLength = plainBuf[2];
            if (req.paramsLength > 8) req.paramsLength = 8;
            if (plainLen >= 3u + req.paramsLength) {
                memcpy(req.params, plainBuf + 3, req.paramsLength);
            }
            encrypted = true;

        } else {
            // ── Plaintext path (sid=0x00): binary decode directly ──
            if (payloadLen < 3) continue;
            req.key.cluster = static_cast<Cluster>(payload[0]);
            req.key.commandId = payload[1];
            req.paramsLength = payload[2];
            if (req.paramsLength > 8) req.paramsLength = 8;
            if (payloadLen >= 3u + req.paramsLength) {
                memcpy(req.params, payload + 3, req.paramsLength);
            }
        }

        LOG_D(ats::ErrorSource::Cmd, evt::CMD_RX,
              ((uint32_t)req.key.cluster << 8) | req.key.commandId);

        // Execute
        CommandResponseModel rsp;
        rsp.key = req.key;

        bool isKeRequest = (req.key.cluster == Cluster::Security &&
                            req.key.commandId == SecurityCommand::KeyExchange);

        if (isKeRequest && req.paramsLength == 0 && payloadLen >= 3 + 64) {
            // KeyExchange: client sends 64-byte public key in params area
            // For KE, the 64B pub key is in the payload after the 3-byte header
            const uint8_t* clientPub = (streamId == SID_ENCRYPTED)
                ? nullptr  // can't KE over encrypted channel (chicken-egg)
                : payload + 3;

            if (clientPub) {
                uint8_t serverPub[64];
                uint8_t authTag[32];
                if (self->handleKeyExchange(static_cast<uint8_t>(frame.source),
                                             clientPub, serverPub, authTag)) {
                    // Response: [serverPub:64][authTag:32] = 96 bytes
                    memcpy(rsp.data, serverPub, 24);  // first 24 bytes in data field
                    rsp.dataLength = 24;
                    rsp.status = CommandStatus::Success;

                    // KE response is special: send full 96B directly (too big for rsp.data[24])
                    uint8_t keRsp[4 + 96]; // binary response header + KE payload
                    keRsp[0] = static_cast<uint8_t>(rsp.key.cluster);
                    keRsp[1] = rsp.key.commandId;
                    keRsp[2] = static_cast<uint8_t>(CommandStatus::Success);
                    keRsp[3] = 96;
                    memcpy(keRsp + 4, serverPub, 64);
                    memcpy(keRsp + 4 + 64, authTag, 32);

                    uint8_t frameBuf[112];
                    size_t frameLen = 0;
                    if (FrameCodec::frame(keRsp, 100, FrameCodec::kFlagFin,
                                           streamId, frameBuf, sizeof(frameBuf), frameLen)) {
                        if (frame.source == CmdFrameItem::BLE && self->mBleSend) {
                            self->mBleSend(frameBuf, (uint16_t)frameLen, self->mBleCtx);
                        } else if (frame.source == CmdFrameItem::MQTT && self->mMqttSend) {
                            self->mMqttSend(frameBuf, (uint16_t)frameLen, self->mMqttCtx);
                        }
                    }
                    LOG_I(ats::ErrorSource::Cmd, 0x0B1E, (uint32_t)frame.source);
                    continue; // KE response already sent
                } else {
                    rsp.status = CommandStatus::Error;
                }
            } else {
                rsp.status = CommandStatus::InvalidParam;
            }
        } else if (encrypted && !isKeRequest) {
            // Session gate: encrypted non-KE command requires active session
            // (session was verified in decryptAndVerify, so we're good)
            ICommand* cmd = self->findCommand(req.key);
            if (cmd) {
                cmd->execute(req, rsp);
            } else {
                rsp.status = CommandStatus::NotFound;
            }
        } else if (!encrypted && !isKeRequest) {
            // Plaintext command (no session required)
            ICommand* cmd = self->findCommand(req.key);
            if (cmd) {
                cmd->execute(req, rsp);
            } else {
                rsp.status = CommandStatus::NotFound;
            }
        } else {
            rsp.status = CommandStatus::InvalidParam;
        }

        // Encode response
        uint8_t rspPayload[32];
        rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
        rspPayload[1] = rsp.key.commandId;
        rspPayload[2] = static_cast<uint8_t>(rsp.status);
        rspPayload[3] = rsp.dataLength;
        if (rsp.dataLength > 0) {
            memcpy(rspPayload + 4, rsp.data, rsp.dataLength);
        }
        size_t rspPayloadLen = 4 + rsp.dataLength;

        uint8_t frameBuf[112];
        size_t frameLen = 0;

        if (encrypted) {
            // Encrypt response with session key
            if (!self->encryptAndFrame(static_cast<uint8_t>(frame.source),
                                        SID_ENCRYPTED, rspPayload, rspPayloadLen,
                                        frameBuf, sizeof(frameBuf), frameLen)) {
                continue;
            }
        } else {
            // Plaintext frame
            if (!FrameCodec::frame(rspPayload, rspPayloadLen,
                                    FrameCodec::kFlagFin, streamId,
                                    frameBuf, sizeof(frameBuf), frameLen)) {
                continue;
            }
        }

        // Direct send (no TX task needed)
        if (frame.source == CmdFrameItem::BLE && self->mBleSend) {
            self->mBleSend(frameBuf, (uint16_t)frameLen, self->mBleCtx);
        } else if (frame.source == CmdFrameItem::MQTT && self->mMqttSend) {
            self->mMqttSend(frameBuf, (uint16_t)frameLen, self->mMqttCtx);
        }

        LOG_D(ats::ErrorSource::Cmd, evt::CMD_RSP, (uint32_t)rsp.status);
    }
}

// ---------------------------------------------------------------------------
// Legacy processFrame — direct call (kept for compatibility)
// ---------------------------------------------------------------------------

void CommandBridge::processFrame(const uint8_t* data, uint16_t len,
                                  void (*respCb)(const uint8_t*, uint16_t, void*),
                                  void* ctx) {
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, streamId = 0;

    if (!FrameCodec::deframe(data, len, payload, payloadLen, flags, streamId)) return;
    if (payloadLen < 3) return;

    CommandRequest req;
    req.key.cluster = static_cast<Cluster>(payload[0]);
    req.key.commandId = payload[1];
    req.paramsLength = payload[2];
    if (req.paramsLength > 8) req.paramsLength = 8;
    if (payloadLen >= 3u + req.paramsLength) {
        memcpy(req.params, payload + 3, req.paramsLength);
    }

    CommandResponseModel rsp;
    rsp.key = req.key;
    ICommand* cmd = findCommand(req.key);
    if (cmd) cmd->execute(req, rsp);
    else rsp.status = CommandStatus::NotFound;

    uint8_t rspPayload[28];
    rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
    rspPayload[1] = rsp.key.commandId;
    rspPayload[2] = static_cast<uint8_t>(rsp.status);
    rspPayload[3] = rsp.dataLength;
    if (rsp.dataLength > 0) memcpy(rspPayload + 4, rsp.data, rsp.dataLength);

    uint8_t frameBuf[40];
    size_t frameLen = 0;
    if (!FrameCodec::frame(rspPayload, 4 + rsp.dataLength,
                           FrameCodec::kFlagFin, streamId,
                           frameBuf, sizeof(frameBuf), frameLen)) return;

    if (respCb) respCb(frameBuf, (uint16_t)frameLen, ctx);
}

} // namespace arcana
