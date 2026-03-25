#include "CommandBridge.hpp"
#include "FrameCodec.hpp"
#include "Crc16.hpp"
#include "stm32f1xx_hal.h"
#include "Commands.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#ifdef ARCANA_CMD_CRYPTO
#include "CryptoEngine.hpp"
#include "DeviceKey.hpp"  // needs stm32f1xx_hal.h (included above) for UID_BASE
#include "arcana_cmd.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#endif
#include <cstring>

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
// Lifecycle
// ---------------------------------------------------------------------------

CommandBridge::CommandBridge()
    : mCommands{}
    , mCommandCount(0)
    , mSensorCache()
    , mRxQueue(0)
    , mRxQueueBuf()
    , mRxQueueStorage{}
#ifdef ARCANA_CMD_CRYPTO
    , mTxQueue(0)
    , mTxQueueBuf()
    , mTxQueueStorage{}
#endif
    , mBridgeTaskBuf()
    , mBridgeStack{}
#ifdef ARCANA_CMD_CRYPTO
    , mTxTaskBuf()
    , mTxStack{}
#endif
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

    // Sensor commands need cache pointer
    sTempCmd.cache  = &mSensorCache;
    sAccelCmd.cache = &mSensorCache;
    sLightCmd.cache = &mSensorCache;
    registerCommand(&sTempCmd);
    registerCommand(&sAccelCmd);
    registerCommand(&sLightCmd);

    // Create queues
    mRxQueue = xQueueCreateStatic(RX_QUEUE_LEN, sizeof(CmdFrameItem),
                                   mRxQueueStorage, &mRxQueueBuf);
#ifdef ARCANA_CMD_CRYPTO
    mTxQueue = xQueueCreateStatic(TX_QUEUE_LEN, sizeof(TxItem),
                                   mTxQueueStorage, &mTxQueueBuf);
#endif

#ifdef ARCANA_CMD_CRYPTO
    // Derive command PSK from flash KeyStore + device UID (no hardcoded key in source)
    {
        uint8_t cmdKey[CryptoEngine::kKeyLen];
        crypto::DeviceKey::deriveKey(cmdKey);  // KeyStore fleet master + UID → per-device key
        mEncryptionEnabled = mCrypto.init(cmdKey);
        if (mEncryptionEnabled) {
            mKeyExchange.init(cmdKey);
        }
    }
#endif

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
// Tasks
// ---------------------------------------------------------------------------

void CommandBridge::startTasks() {
    xTaskCreateStatic(bridgeTask, "CmdBr", BRIDGE_STACK_SIZE,
                      this, tskIDLE_PRIORITY + 2,
                      mBridgeStack, &mBridgeTaskBuf);

#ifdef ARCANA_CMD_CRYPTO
    xTaskCreateStatic(txTask, "CmdTx", TX_STACK_SIZE,
                      this, tskIDLE_PRIORITY + 2,
                      mTxStack, &mTxTaskBuf);
#endif
}

void CommandBridge::bridgeTask(void* param) {
    CommandBridge* self = static_cast<CommandBridge*>(param);
    LOG_I(ats::ErrorSource::Cmd, evt::CMD_BRIDGE_START);

    while (true) {
        CmdFrameItem frame;
        if (xQueueReceive(self->mRxQueue, &frame, portMAX_DELAY) == pdTRUE) {
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
#ifdef ARCANA_CMD_CRYPTO
            // Track source for session key lookup
            uint8_t cmdSource = static_cast<uint8_t>(frame.source);
            arcana_CmdRequest msg = arcana_CmdRequest_init_zero;
            {
                const uint8_t* pbData = payload;
                size_t pbLen = payloadLen;
                uint8_t plainBuf[arcana_CmdRequest_size];

                if (self->mEncryptionEnabled) {
                    size_t plainLen = 0;
                    bool decrypted = false;

                    // Try session key first
                    decrypted = self->mKeyExchange.decryptWithSession(
                        cmdSource, 0, payload, payloadLen,
                        plainBuf, sizeof(plainBuf), plainLen);

                    // Fallback to PSK
                    if (!decrypted) {
                        if (!self->mCrypto.decrypt(payload, payloadLen,
                                                    plainBuf, sizeof(plainBuf), plainLen)) {
                            LOG_W(ats::ErrorSource::Cmd, evt::CMD_BAD_FRAME, 0xDE);
                            continue;
                        }
                    }
                    pbData = plainBuf;
                    pbLen = plainLen;
                }

                // Decode protobuf
                pb_istream_t stream = pb_istream_from_buffer(pbData, pbLen);
                if (!pb_decode(&stream, arcana_CmdRequest_fields, &msg)) {
                    LOG_W(ats::ErrorSource::Cmd, evt::CMD_BAD_FRAME, 0xFB);
                    continue;
                }
                if (msg.cluster > 0xFF || msg.command > 0xFF) continue;

                req.key.cluster = static_cast<Cluster>(msg.cluster);
                req.key.commandId = static_cast<uint8_t>(msg.command);
                req.paramsLength = (msg.payload.size <= 8) ? (uint8_t)msg.payload.size : 8;
                if (req.paramsLength > 0) {
                    memcpy(req.params, msg.payload.bytes, req.paramsLength);
                }
            }
#else
            if (payloadLen < 3) continue;
            req.key.cluster = static_cast<Cluster>(payload[0]);
            req.key.commandId = payload[1];
            req.paramsLength = payload[2];
            if (req.paramsLength > 8) req.paramsLength = 8;
            if (payloadLen >= 3u + req.paramsLength) {
                memcpy(req.params, payload + 3, req.paramsLength);
            }
#endif

            LOG_D(ats::ErrorSource::Cmd, evt::CMD_RX,
                  ((uint32_t)req.key.cluster << 8) | req.key.commandId);

            // Execute
            CommandResponseModel rsp;
            rsp.key = req.key;

#ifdef ARCANA_CMD_CRYPTO
            // KeyExchange: handle directly (payload too large for ICommand)
            // KeyExchange response buffer (96B = serverPub:64 + authTag:32)
            static uint8_t kePayload[96];
            uint8_t kePayloadLen = 0;

            if (req.key.cluster == Cluster::Security &&
                req.key.commandId == SecurityCommand::KeyExchange) {
                if (msg.payload.size == KeyExchangeManager::kPubKeyLen) {
                    uint8_t* serverPub = kePayload;
                    uint8_t* authTag = kePayload + 64;
                    if (self->mKeyExchange.performKeyExchange(
                            cmdSource, 0, msg.payload.bytes, serverPub, authTag)) {
                        kePayloadLen = 96;
                        rsp.status = CommandStatus::Success;
                    } else {
                        rsp.status = CommandStatus::Error;
                    }
                } else {
                    rsp.status = CommandStatus::InvalidParam;
                }
            } else
#endif
            {
                ICommand* cmd = self->findCommand(req.key);
                if (cmd) {
                    cmd->execute(req, rsp);
                } else {
                    rsp.status = CommandStatus::NotFound;
                }
            }

            // Encode response
            uint8_t frameBuf[TxItem::MAX_DATA];
            size_t frameLen = 0;

#ifdef ARCANA_CMD_CRYPTO
            {
                bool isKeyExchangeOk = (rsp.key.cluster == Cluster::Security &&
                    rsp.key.commandId == SecurityCommand::KeyExchange &&
                    rsp.status == CommandStatus::Success);

                // Protobuf encode
                arcana_CmdResponse rspMsg = arcana_CmdResponse_init_zero;
                rspMsg.cluster = static_cast<uint32_t>(rsp.key.cluster);
                rspMsg.command = static_cast<uint32_t>(rsp.key.commandId);
                rspMsg.status = static_cast<uint32_t>(rsp.status);

                if (isKeyExchangeOk && kePayloadLen > 0) {
                    // KeyExchange: full 96 bytes (serverPub:64 + authTag:32)
                    rspMsg.payload.size = kePayloadLen;
                    memcpy(rspMsg.payload.bytes, kePayload, kePayloadLen);
                } else {
                    rspMsg.payload.size = rsp.dataLength;
                    if (rsp.dataLength > 0) {
                        memcpy(rspMsg.payload.bytes, rsp.data, rsp.dataLength);
                    }
                }

                uint8_t pbBuf[arcana_CmdResponse_size];
                pb_ostream_t ostream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
                if (!pb_encode(&ostream, arcana_CmdResponse_fields, &rspMsg)) continue;
                size_t pbLen = ostream.bytes_written;

                // Encrypt: KE response always uses PSK, others try session first
                uint8_t innerBuf[arcana_CmdResponse_size + CryptoEngine::kOverhead];
                size_t innerLen = 0;
                if (self->mEncryptionEnabled) {
                    bool encrypted = false;

                    if (!isKeyExchangeOk) {
                        // Try session key first
                        encrypted = self->mKeyExchange.encryptWithSession(
                            cmdSource, 0, pbBuf, pbLen,
                            innerBuf, sizeof(innerBuf), innerLen);
                    }
                    // PSK fallback (or always for KE response)
                    if (!encrypted) {
                        if (!self->mCrypto.encrypt(pbBuf, pbLen,
                                                    innerBuf, sizeof(innerBuf), innerLen)) continue;
                    }

                    // Install session AFTER encrypting KE response with PSK
                    if (isKeyExchangeOk) {
                        self->mKeyExchange.installPendingSession(cmdSource, 0);
                    }
                } else {
                    memcpy(innerBuf, pbBuf, pbLen);
                    innerLen = pbLen;
                }

                // Frame
                if (!FrameCodec::frame(innerBuf, innerLen,
                                        FrameCodec::kFlagFin, streamId,
                                        frameBuf, sizeof(frameBuf), frameLen)) continue;
            }
#else
            {
                uint8_t rspPayload[28];
                rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
                rspPayload[1] = rsp.key.commandId;
                rspPayload[2] = static_cast<uint8_t>(rsp.status);
                rspPayload[3] = rsp.dataLength;
                if (rsp.dataLength > 0) {
                    memcpy(rspPayload + 4, rsp.data, rsp.dataLength);
                }
                size_t rspPayloadLen = 4 + rsp.dataLength;

                if (!FrameCodec::frame(rspPayload, rspPayloadLen,
                                        FrameCodec::kFlagFin, streamId,
                                        frameBuf, sizeof(frameBuf), frameLen)) continue;
            }
#endif

#ifdef ARCANA_CMD_CRYPTO
            // Push to TX queue (TX task sends)
            TxItem tx;
            memcpy(tx.data, frameBuf, frameLen);
            tx.len = static_cast<uint16_t>(frameLen);
            tx.target = frame.source;
            xQueueSend(self->mTxQueue, &tx, pdMS_TO_TICKS(50));
#else
            // Direct send (no TX task on small-RAM boards)
            if (frame.source == CmdFrameItem::BLE && self->mBleSend) {
                self->mBleSend(frameBuf, (uint16_t)frameLen, self->mBleCtx);
            } else if (frame.source == CmdFrameItem::MQTT && self->mMqttSend) {
                self->mMqttSend(frameBuf, (uint16_t)frameLen, self->mMqttCtx);
            }
#endif

            LOG_D(ats::ErrorSource::Cmd, evt::CMD_RSP, (uint32_t)rsp.status);
        }
    }
}

#ifdef ARCANA_CMD_CRYPTO
void CommandBridge::txTask(void* param) {
    CommandBridge* self = static_cast<CommandBridge*>(param);

    while (true) {
        TxItem tx;
        if (xQueueReceive(self->mTxQueue, &tx, portMAX_DELAY) == pdTRUE) {
            if (tx.target == CmdFrameItem::BLE && self->mBleSend) {
                self->mBleSend(tx.data, tx.len, self->mBleCtx);
            } else if (tx.target == CmdFrameItem::MQTT && self->mMqttSend) {
                self->mMqttSend(tx.data, tx.len, self->mMqttCtx);
            }
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// Legacy processFrame — direct call (kept for compatibility)
// ---------------------------------------------------------------------------

void CommandBridge::processFrame(const uint8_t* data, uint16_t len,
                                  ResponseCallback respCb, void* ctx) {
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, streamId = 0;

    if (!FrameCodec::deframe(data, len, payload, payloadLen, flags, streamId)) {
        LOG_W(ats::ErrorSource::Cmd, evt::CMD_BAD_FRAME, (uint32_t)len);
        return;
    }

    if (payloadLen < 3) return;
    CommandRequest req;
    req.key.cluster = static_cast<Cluster>(payload[0]);
    req.key.commandId = payload[1];
    req.paramsLength = payload[2];
    if (req.paramsLength > 8) req.paramsLength = 8;
    if (payloadLen >= 3u + req.paramsLength) {
        memcpy(req.params, payload + 3, req.paramsLength);
    }

    LOG_D(ats::ErrorSource::Cmd, evt::CMD_RX,
          ((uint32_t)req.key.cluster << 8) | req.key.commandId);

    CommandResponseModel rsp;
    rsp.key = req.key;
    ICommand* cmd = findCommand(req.key);
    if (cmd) {
        cmd->execute(req, rsp);
    } else {
        rsp.status = CommandStatus::NotFound;
    }

    uint8_t rspPayload[28];
    rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
    rspPayload[1] = rsp.key.commandId;
    rspPayload[2] = static_cast<uint8_t>(rsp.status);
    rspPayload[3] = rsp.dataLength;
    if (rsp.dataLength > 0) {
        memcpy(rspPayload + 4, rsp.data, rsp.dataLength);
    }
    size_t rspPayloadLen = 4 + rsp.dataLength;

    // Echo streamId in response
    uint8_t frameBuf[40];
    size_t frameLen = 0;
    if (!FrameCodec::frame(rspPayload, rspPayloadLen,
                           FrameCodec::kFlagFin, streamId,
                           frameBuf, sizeof(frameBuf), frameLen)) {
        return;
    }

    if (respCb) {
        respCb(frameBuf, (uint16_t)frameLen, ctx);
    }

    LOG_D(ats::ErrorSource::Cmd, evt::CMD_RSP, (uint32_t)rsp.status);
}

} // namespace arcana
