#include "CommandBridge.hpp"
#include "FrameCodec.hpp"
#include "Crc16.hpp"
#include "stm32f1xx_hal.h"
#include "Commands.hpp"
#include <cstring>
#include <cstdio>

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
    , mTxQueue(0)
    , mTxQueueBuf()
    , mTxQueueStorage{}
    , mBridgeTaskBuf()
    , mBridgeStack{}
    , mTxTaskBuf()
    , mTxStack{}
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
    mTxQueue = xQueueCreateStatic(TX_QUEUE_LEN, sizeof(TxItem),
                                   mTxQueueStorage, &mTxQueueBuf);

    printf("[CMD] %u cmds registered\r\n", (unsigned)mCommandCount);
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
    xTaskCreateStatic(bridgeTask, "CmdRx", BRIDGE_STACK_SIZE,
                      this, tskIDLE_PRIORITY + 2,
                      mBridgeStack, &mBridgeTaskBuf);

    xTaskCreateStatic(txTask, "CmdTx", TX_STACK_SIZE,
                      this, tskIDLE_PRIORITY + 2,
                      mTxStack, &mTxTaskBuf);
}

void CommandBridge::bridgeTask(void* param) {
    CommandBridge* self = static_cast<CommandBridge*>(param);
    printf("[CMD] Bridge task started\r\n");

    while (true) {
        CmdFrameItem frame;
        if (xQueueReceive(self->mRxQueue, &frame, portMAX_DELAY) == pdTRUE) {
            // Deframe
            const uint8_t* payload = nullptr;
            size_t payloadLen = 0;
            uint8_t flags = 0, streamId = 0;

            if (!FrameCodec::deframe(frame.data, frame.len,
                                      payload, payloadLen, flags, streamId)) {
                printf("[CMD] Bad frame (%u bytes)\r\n", frame.len);
                continue;
            }

            // Decode command request
            if (payloadLen < 3) continue;
            CommandRequest req;
            req.key.cluster = static_cast<Cluster>(payload[0]);
            req.key.commandId = payload[1];
            req.paramsLength = payload[2];
            if (req.paramsLength > 8) req.paramsLength = 8;
            if (payloadLen >= 3u + req.paramsLength) {
                memcpy(req.params, payload + 3, req.paramsLength);
            }

            printf("[CMD] %02X:%02X sid=%u\r\n",
                   (unsigned)req.key.cluster, (unsigned)req.key.commandId,
                   (unsigned)streamId);

            // Execute
            CommandResponseModel rsp;
            rsp.key = req.key;
            ICommand* cmd = self->findCommand(req.key);
            if (cmd) {
                cmd->execute(req, rsp);
            } else {
                rsp.status = CommandStatus::NotFound;
            }

            // Encode response payload
            uint8_t rspPayload[28];
            rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
            rspPayload[1] = rsp.key.commandId;
            rspPayload[2] = static_cast<uint8_t>(rsp.status);
            rspPayload[3] = rsp.dataLength;
            if (rsp.dataLength > 0) {
                memcpy(rspPayload + 4, rsp.data, rsp.dataLength);
            }
            size_t rspPayloadLen = 4 + rsp.dataLength;

            // Frame response — echo the request's streamId
            uint8_t frameBuf[40];
            size_t frameLen = 0;
            if (!FrameCodec::frame(rspPayload, rspPayloadLen,
                                    FrameCodec::kFlagFin, streamId,
                                    frameBuf, sizeof(frameBuf), frameLen)) {
                continue;
            }

            // Push to TX queue
            TxItem tx;
            memcpy(tx.data, frameBuf, frameLen);
            tx.len = static_cast<uint16_t>(frameLen);
            tx.target = frame.source;
            xQueueSend(self->mTxQueue, &tx, pdMS_TO_TICKS(50));

            printf("[CMD] RSP status=%u data=%u\r\n",
                   (unsigned)rsp.status, (unsigned)rsp.dataLength);
        }
    }
}

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

// ---------------------------------------------------------------------------
// Legacy processFrame — direct call (kept for compatibility)
// ---------------------------------------------------------------------------

void CommandBridge::processFrame(const uint8_t* data, uint16_t len,
                                  ResponseCallback respCb, void* ctx) {
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, streamId = 0;

    if (!FrameCodec::deframe(data, len, payload, payloadLen, flags, streamId)) {
        printf("[CMD] Bad frame (%u bytes)\r\n", len);
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

    printf("[CMD] %02X:%02X\r\n",
           (unsigned)req.key.cluster, (unsigned)req.key.commandId);

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

    printf("[CMD] RSP status=%u data=%u\r\n",
           (unsigned)rsp.status, (unsigned)rsp.dataLength);
}

} // namespace arcana
