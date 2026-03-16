#pragma once

#include "BleService.hpp"
#include "Hc08Ble.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace ble {

/**
 * BLE transport — HC-08 transparent UART.
 * Receives framed commands → delegates to shared CommandBridge.
 * Streams sensor JSON to phone at 1Hz.
 */
class BleServiceImpl : public BleService {
public:
    static BleService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    BleServiceImpl();
    ~BleServiceImpl();
    BleServiceImpl(const BleServiceImpl&);
    BleServiceImpl& operator=(const BleServiceImpl&);

    static void bleTask(void* param);
    void taskLoop();

    // BLE → CommandBridge response callback
    static void onBleResponse(const uint8_t* frameBuf, uint16_t frameLen, void* ctx);

    // Sensor Observable callbacks → JSON push
    static void onSensorData(SensorDataModel* model, void* ctx);
    static void onLightData(LightDataModel* model, void* ctx);
    void pushSensorJson();

    // Task
    static const uint16_t TASK_STACK_SIZE = 256;
    StaticTask_t mTaskBuf;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Cached latest readings
    float mTemp;
    int16_t mAx, mAy, mAz;
    uint16_t mAls, mPs;
    volatile bool mSensorDirty;

    // Reference to BLE driver
    Hc08Ble& mBle;
};

} // namespace ble
} // namespace arcana
