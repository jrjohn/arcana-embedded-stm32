#pragma once

#include "CommandTypes.hpp"
#include "ICommand.hpp"
#include "FrameCodec.hpp"
#include "SensorDataCache.hpp"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <cstdint>

namespace arcana {

// Forward — defined in Hc08Ble.hpp but avoid circular include
struct FrameItem;

/**
 * Shared frame item for RX queue — used by both BLE and MQTT.
 * Re-declared here so CommandBridge owns the queue type.
 */
struct CmdFrameItem {
    static constexpr uint16_t MAX_DATA = 64;
    uint8_t data[MAX_DATA];
    uint16_t len;
    enum Transport : uint8_t { BLE = 0, MQTT = 1 } source;
};

/** TX item — response routed back to originating transport */
struct TxItem {
    uint8_t data[64];
    uint16_t len;
    CmdFrameItem::Transport target;
};

/**
 * CommandBridge — shared command registry + frame processing.
 * Owns RX frame queue (BLE/MQTT push) and TX queue (responses).
 * Two FreeRTOS tasks: bridgeTask processes RX, txTask sends TX.
 */
class CommandBridge {
public:
    /** Response callback — transport sends this back to the peer */
    typedef void (*ResponseCallback)(const uint8_t* frameBuf, uint16_t frameLen, void* ctx);

    /** Transport send function — called by TX task */
    typedef bool (*TransportSendFn)(const uint8_t* data, uint16_t len, void* ctx);

    static CommandBridge& getInstance();

    /** Register a command handler (max 16) */
    bool registerCommand(ICommand* cmd);

    /** Submit a complete frame for processing (called from BLE/MQTT) */
    bool submitFrame(const uint8_t* data, uint16_t len, CmdFrameItem::Transport source);

    /** Register transport send functions (called during init) */
    void setBleSend(TransportSendFn fn, void* ctx) { mBleSend = fn; mBleCtx = ctx; }
    void setMqttSend(TransportSendFn fn, void* ctx) { mMqttSend = fn; mMqttCtx = ctx; }

    /** Shared sensor cache — commands read from here */
    SensorDataCache& getSensorCache() { return mSensorCache; }

    /** Start bridge + TX tasks */
    void startTasks();

    /**
     * Process a received frame: deframe → decode → execute → encode → callback.
     * @param data     Raw bytes from transport (FrameCodec framed)
     * @param len      Length of raw bytes
     * @param respCb   Callback to send response back via originating transport
     * @param ctx      Context pointer passed to callback
     */
    void processFrame(const uint8_t* data, uint16_t len,
                      ResponseCallback respCb, void* ctx);

    uint8_t getCommandCount() const { return mCommandCount; }

private:
    CommandBridge();

    static void bridgeTask(void* param);
    static void txTask(void* param);

    static const uint8_t MAX_COMMANDS = 16;
    ICommand* mCommands[MAX_COMMANDS];
    uint8_t mCommandCount;

    ICommand* findCommand(CommandKey key);

    // Shared sensor cache
    SensorDataCache mSensorCache;

    // RX frame queue
    static const uint8_t RX_QUEUE_LEN = 8;
    QueueHandle_t mRxQueue;
    StaticQueue_t mRxQueueBuf;
    uint8_t mRxQueueStorage[RX_QUEUE_LEN * sizeof(CmdFrameItem)];

    // TX response queue
    static const uint8_t TX_QUEUE_LEN = 8;
    QueueHandle_t mTxQueue;
    StaticQueue_t mTxQueueBuf;
    uint8_t mTxQueueStorage[TX_QUEUE_LEN * sizeof(TxItem)];

    // Bridge task
    static const uint16_t BRIDGE_STACK_SIZE = 256;
    StaticTask_t mBridgeTaskBuf;
    StackType_t mBridgeStack[BRIDGE_STACK_SIZE];

    // TX task
    static const uint16_t TX_STACK_SIZE = 256;
    StaticTask_t mTxTaskBuf;
    StackType_t mTxStack[TX_STACK_SIZE];

    // Transport send functions
    TransportSendFn mBleSend;
    void* mBleCtx;
    TransportSendFn mMqttSend;
    void* mMqttCtx;
};

} // namespace arcana
