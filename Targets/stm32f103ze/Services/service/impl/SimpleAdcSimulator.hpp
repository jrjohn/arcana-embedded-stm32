#pragma once

#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace sdstorage {

// 前向聲明
class SdStorageServiceImpl;

/**
 * @brief Simplified ADC Simulator that writes directly to SdStorage
 * Similar to enableStressTest() but for ADC batch data
 */
class SimpleAdcSimulator {
public:
    static SimpleAdcSimulator& getInstance();

    void init(SdStorageServiceImpl* storage);
    void start(uint16_t hz);
    void stop();

private:
    SimpleAdcSimulator();
    ~SimpleAdcSimulator();

    static void taskEntry(void* param);
    void taskLoop();
    void writeDummyAdcBatch();

    SdStorageServiceImpl* mStorage;
    TaskHandle_t mTaskHandle;
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[512];
    bool mRunning;
    uint16_t mHz;
};

} // namespace sdstorage
} // namespace arcana
