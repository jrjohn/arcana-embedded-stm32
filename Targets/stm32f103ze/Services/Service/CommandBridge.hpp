#pragma once

#include "CommandTypes.hpp"
#include "ICommand.hpp"
#include "FrameCodec.hpp"
#ifdef ARCANA_CMD_CRYPTO
#include "CryptoEngine.hpp"
#include "KeyExchangeManager.hpp"
#endif
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
 * When ARCANA_CMD_CRYPTO is defined, MAX_DATA is larger to accommodate
 * encrypted protobuf frames (max 143 pb + 12 crypto + 9 frame = 164).
 */
struct CmdFrameItem {
#ifdef ARCANA_CMD_CRYPTO
    static constexpr uint16_t MAX_DATA = 176;
#else
    static constexpr uint16_t MAX_DATA = 64;
#endif
    uint8_t data[MAX_DATA];
    uint16_t len;
    enum Transport : uint8_t { BLE = 0, MQTT = 1 } source;
};

/** TX item — response routed back to originating transport */
struct TxItem {
#ifdef ARCANA_CMD_CRYPTO
    static constexpr uint16_t MAX_DATA = 300;
#else
    static constexpr uint16_t MAX_DATA = 64;
#endif
    uint8_t data[MAX_DATA];
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
#ifdef ARCANA_CMD_CRYPTO
    static void txTask(void* param);
#endif

    static const uint8_t MAX_COMMANDS = 16;
    ICommand* mCommands[MAX_COMMANDS];
    uint8_t mCommandCount;

    ICommand* findCommand(CommandKey key);

    // Shared sensor cache
    SensorDataCache mSensorCache;

    // RX frame queue (depth 2 with crypto — commands are low-frequency)
#ifdef ARCANA_CMD_CRYPTO
    static const uint8_t RX_QUEUE_LEN = 4;
    static const uint8_t TX_QUEUE_LEN = 2;
#else
    static const uint8_t RX_QUEUE_LEN = 8;
    static const uint8_t TX_QUEUE_LEN = 8;
#endif
    QueueHandle_t mRxQueue;
    StaticQueue_t mRxQueueBuf;
    uint8_t mRxQueueStorage[RX_QUEUE_LEN * sizeof(CmdFrameItem)];

#ifdef ARCANA_CMD_CRYPTO
    // TX response queue — only with TX task
    QueueHandle_t mTxQueue;
    StaticQueue_t mTxQueueBuf;
    uint8_t mTxQueueStorage[TX_QUEUE_LEN * sizeof(TxItem)];
#endif

#ifdef ARCANA_CMD_CRYPTO
public:
    // CryptoEngine for command encryption (PSK-based, AES-256-CCM)
    // Public: MqttServiceImpl needs direct access for KeyExchange in MQTT task context
    CryptoEngine mCrypto;
    KeyExchangeManager mKeyExchange;
    bool mEncryptionEnabled;
private:

    // Larger bridge stack for protobuf + crypto buffers on stack
    static const uint16_t BRIDGE_STACK_SIZE = 512;
#else
    static const uint16_t BRIDGE_STACK_SIZE = 256;
#endif
    StaticTask_t mBridgeTaskBuf;
    StackType_t mBridgeStack[BRIDGE_STACK_SIZE];

#ifdef ARCANA_CMD_CRYPTO
    // TX task — only on boards with enough RAM
    static const uint16_t TX_STACK_SIZE = 256;
    StaticTask_t mTxTaskBuf;
    StackType_t mTxStack[TX_STACK_SIZE];
#endif

    // Transport send functions
    TransportSendFn mBleSend;
    void* mBleCtx;
    TransportSendFn mMqttSend;
    void* mMqttCtx;
};

} // namespace arcana
