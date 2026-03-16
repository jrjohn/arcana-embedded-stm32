#include "CommandBridge.hpp"
#include "FrameCodec.hpp"
#include "Crc16.hpp"
#include <cstring>
#include <cstdio>

namespace arcana {

// ---------------------------------------------------------------------------
// Built-in commands
// ---------------------------------------------------------------------------

class PingCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::System, 0x01 };
    }
    void execute(const CommandRequest& req, CommandResponseModel& rsp) override {
        (void)req;
        uint32_t tick = xTaskGetTickCount();
        rsp.setUint32(tick);
        rsp.status = CommandStatus::Success;
    }
};

static PingCommand sPingCmd;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CommandBridge::CommandBridge()
    : mCommands{}
    , mCommandCount(0)
{
    registerCommand(&sPingCmd);
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
// Frame processing — shared by BLE and MQTT transports
// ---------------------------------------------------------------------------

void CommandBridge::processFrame(const uint8_t* data, uint16_t len,
                                  ResponseCallback respCb, void* ctx) {
    // 1. Deframe
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, streamId = 0;

    if (!FrameCodec::deframe(data, len, payload, payloadLen, flags, streamId)) {
        printf("[CMD] Bad frame (%u bytes)\r\n", len);
        return;
    }

    // 2. Decode command request
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

    // 3. Execute
    CommandResponseModel rsp;
    rsp.key = req.key;
    ICommand* cmd = findCommand(req.key);
    if (cmd) {
        cmd->execute(req, rsp);
    } else {
        rsp.status = CommandStatus::NotFound;
    }

    // 4. Encode response
    uint8_t rspPayload[20];
    rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
    rspPayload[1] = rsp.key.commandId;
    rspPayload[2] = static_cast<uint8_t>(rsp.status);
    rspPayload[3] = rsp.dataLength;
    if (rsp.dataLength > 0) {
        memcpy(rspPayload + 4, rsp.data, rsp.dataLength);
    }
    size_t rspPayloadLen = 4 + rsp.dataLength;

    // 5. Frame response
    uint8_t frameBuf[32];
    size_t frameLen = 0;
    if (!FrameCodec::frame(rspPayload, rspPayloadLen,
                           FrameCodec::kFlagFin, FrameCodec::kSidNone,
                           frameBuf, sizeof(frameBuf), frameLen)) {
        return;
    }

    // 6. Send back via originating transport
    if (respCb) {
        respCb(frameBuf, (uint16_t)frameLen, ctx);
    }

    printf("[CMD] RSP status=%u data=%u\r\n",
           (unsigned)rsp.status, (unsigned)rsp.dataLength);
}

} // namespace arcana
