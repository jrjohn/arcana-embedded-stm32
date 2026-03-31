#pragma once

#include "BleService.hpp"
#include "Hc08Ble.hpp"
#include "FrameCodec.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace ble {

/**
 * BLE transport — HC-08 transparent UART.
 * Receives bytes → FrameAssembler → submitFrame to CommandBridge.
 * Streams sensor JSON to phone at 1Hz.
 */
class BleServiceImpl : public BleService {
public:
    static BleService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    /** BLE send — used as TransportSendFn by CommandBridge TX task */
    static bool bleSendFn(const uint8_t* data, uint16_t len, void* ctx);

private:
    BleServiceImpl();
    ~BleServiceImpl();
    BleServiceImpl(const BleServiceImpl&);
    BleServiceImpl& operator=(const BleServiceImpl&);

    static void bleTask(void* param);
    void taskLoop();

    // Sensor Observable callbacks → JSON push + cache update
    static void onSensorData(SensorDataModel* model, void* ctx);
    static void onLightData(LightDataModel* model, void* ctx);
    void pushSensorJson();
    void pushSensorEncrypted();
#ifdef ARCANA_CMD_CRYPTO
    void pushSensorEncryptedCcm();
#endif

    // Task (256 words = 1KB — enough for AES-CCM on small payloads)
    static const uint16_t TASK_STACK_SIZE = 256;
    StaticTask_t mTaskBuf;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Cached latest readings (for JSON push)
    float mTemp;
    int16_t mAx, mAy, mAz;
    uint16_t mAls, mPs;
    volatile bool mSensorDirty;

    // Reference to BLE driver
    Hc08Ble& mBle;
};

} // namespace ble
} // namespace arcana
