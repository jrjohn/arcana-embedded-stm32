#pragma once

#include "CommandTypes.hpp"
#include "ICommand.hpp"
#include "FrameCodec.hpp"
#include <cstdint>

namespace arcana {

/**
 * CommandBridge — shared command registry + frame processing.
 * Both BLE and MQTT transports call processFrame() with raw bytes.
 * Response is returned via callback to the originating transport.
 */
class CommandBridge {
public:
    /** Response callback — transport sends this back to the peer */
    typedef void (*ResponseCallback)(const uint8_t* frameBuf, uint16_t frameLen, void* ctx);

    static CommandBridge& getInstance();

    /** Register a command handler (max 8) */
    bool registerCommand(ICommand* cmd);

    /**
     * Process a received frame: deframe → decode → execute → encode → callback.
     * @param data     Raw bytes from transport (FrameCodec framed)
     * @param len      Length of raw bytes
     * @param respCb   Callback to send response back via originating transport
     * @param ctx      Context pointer passed to callback
     */
    void processFrame(const uint8_t* data, uint16_t len,
                      ResponseCallback respCb, void* ctx);

private:
    CommandBridge();

    static const uint8_t MAX_COMMANDS = 8;
    ICommand* mCommands[MAX_COMMANDS];
    uint8_t mCommandCount;

    ICommand* findCommand(CommandKey key);
};

} // namespace arcana
