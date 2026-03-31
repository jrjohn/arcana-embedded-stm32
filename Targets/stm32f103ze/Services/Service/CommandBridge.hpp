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

// Forward
struct FrameItem;

/**
 * Shared frame item for RX queue.
 * MAX_DATA=80 fits encrypted commands: [nonce:12][cipher:28max][hmac:32] = 72B
 */
struct CmdFrameItem {
    static constexpr uint16_t MAX_DATA = 80;
    uint8_t data[MAX_DATA];
    uint16_t len;
    enum Transport : uint8_t { BLE = 0, MQTT = 1 } source;
};

/**
 * Lightweight ChaCha20 session — replaces CryptoEngine + KeyExchangeManager.
 * 41 bytes per session vs ~430 bytes per CryptoEngine instance.
 */
struct ChaChaSession {
    uint8_t key[32] = {};
    uint32_t txCounter = 0;
    uint32_t rxCounter = 0;
    bool active = false;
};

/**
 * CommandBridge — shared command registry + frame processing.
 * Transport-agnostic: BLE and MQTT share the same crypto path.
 *
 * Crypto: uECC (ECDH) + ChaCha20 (encrypt) + HMAC-SHA256 (auth)
 * Zero mbedtls dependency.
 *
 * StreamId routing:
 *   0x00 = plaintext binary command (backward compatible)
 *   0x10 = ChaCha20 + HMAC encrypted command
 *   0x20 = ChaCha20 encrypted sensor stream (BleServiceImpl)
 */
class CommandBridge {
public:
    static constexpr uint8_t SID_PLAINTEXT = 0x00;
    static constexpr uint8_t SID_ENCRYPTED = 0x10;
    static constexpr uint8_t SID_SENSOR    = 0x20;

    /** Transport send function */
    typedef bool (*TransportSendFn)(const uint8_t* data, uint16_t len, void* ctx);

    static CommandBridge& getInstance();

    /** Register a command handler (max 16) */
    bool registerCommand(ICommand* cmd);

    /** Submit a complete frame for processing (called from BLE/MQTT) */
    bool submitFrame(const uint8_t* data, uint16_t len, CmdFrameItem::Transport source);

    /** Register transport send functions */
    void setBleSend(TransportSendFn fn, void* ctx) { mBleSend = fn; mBleCtx = ctx; }
    void setMqttSend(TransportSendFn fn, void* ctx) { mMqttSend = fn; mMqttCtx = ctx; }

    /** Shared sensor cache — commands read from here */
    SensorDataCache& getSensorCache() { return mSensorCache; }

    /** Start bridge task */
    void startTasks();

    /**
     * Legacy: process a frame directly (callback-based, no queue).
     */
    void processFrame(const uint8_t* data, uint16_t len,
                      void (*respCb)(const uint8_t*, uint16_t, void*), void* ctx);

    uint8_t getCommandCount() const { return mCommandCount; }

    /** Check if session is active for a transport source */
    bool hasSession(uint8_t source) const { return mSessions[source & 1].active; }

private:
    CommandBridge();

    static void bridgeTask(void* param);

    // Command registry
    static const uint8_t MAX_COMMANDS = 16;
    ICommand* mCommands[MAX_COMMANDS];
    uint8_t mCommandCount;
    ICommand* findCommand(CommandKey key);

    // Shared sensor cache
    SensorDataCache mSensorCache;

    // RX frame queue
    static const uint8_t RX_QUEUE_LEN = 4;
    QueueHandle_t mRxQueue;
    StaticQueue_t mRxQueueBuf;
    uint8_t mRxQueueStorage[RX_QUEUE_LEN * sizeof(CmdFrameItem)];

    // Lightweight crypto sessions (1 BLE + 1 MQTT)
    static const uint8_t MAX_SESSIONS = 2;
    ChaChaSession mSessions[MAX_SESSIONS];
    uint8_t mDeviceKey[32];  // for KE auth tag verification

    // Bridge task — 512 words for uECC ECDH (~700B peak + frame processing)
    static const uint16_t BRIDGE_STACK_SIZE = 512;
    StaticTask_t mBridgeTaskBuf;
    StackType_t mBridgeStack[BRIDGE_STACK_SIZE];

    // Transport send functions
    TransportSendFn mBleSend;
    void* mBleCtx;
    TransportSendFn mMqttSend;
    void* mMqttCtx;

    // Internal helpers
    bool handleKeyExchange(uint8_t source, const uint8_t* clientPub,
                           uint8_t* serverPub, uint8_t* authTag);
    bool encryptAndFrame(uint8_t source, uint8_t streamId,
                         const uint8_t* plain, size_t plainLen,
                         uint8_t* frameBuf, size_t frameBufSize, size_t& frameLen);
    bool decryptAndVerify(uint8_t source,
                          const uint8_t* payload, size_t payloadLen,
                          uint8_t* plain, size_t plainBufSize, size_t& plainLen);
};

} // namespace arcana
