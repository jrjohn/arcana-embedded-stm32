#include "SimpleAdcSimulator.hpp"
#include "SdStorageServiceImpl.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>

namespace arcana {
namespace sdstorage {

SimpleAdcSimulator& SimpleAdcSimulator::getInstance() {
    static SimpleAdcSimulator sInstance;
    return sInstance;
}

SimpleAdcSimulator::SimpleAdcSimulator()
    : mStorage(nullptr)
    , mTaskHandle(nullptr)
    , mTaskBuffer()
    , mTaskStack{}
    , mRunning(false)
    , mHz(10)
{
}

SimpleAdcSimulator::~SimpleAdcSimulator() {
    stop();
}

void SimpleAdcSimulator::init(SdStorageServiceImpl* storage) {
    mStorage = storage;
}

void SimpleAdcSimulator::start(uint16_t hz) {
    if (mRunning || !mStorage) return;
    
    mHz = hz;
    mRunning = true;
    
    mTaskHandle = xTaskCreateStatic(
        taskEntry,
        "SimpleAdc",
        sizeof(mTaskStack) / sizeof(StackType_t),
        this,
        tskIDLE_PRIORITY + 2,  // Same as SdStorage
        mTaskStack,
        &mTaskBuffer
    );
}

void SimpleAdcSimulator::stop() {
    mRunning = false;
    if (mTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        mTaskHandle = nullptr;
    }
}

void SimpleAdcSimulator::taskEntry(void* param) {
    SimpleAdcSimulator* self = static_cast<SimpleAdcSimulator*>(param);
    self->taskLoop();
    vTaskDelete(nullptr);
}

void SimpleAdcSimulator::taskLoop() {
    // Wait for SD card ready
    extern volatile uint8_t g_exfat_ready;
    int waitCount = 0;
    while (!g_exfat_ready && mRunning && waitCount < 300) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
    }
    
    if (!mRunning) return;
    
    // Wait for SdStorage to initialize FlashDB
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    printf("[SimpleAdc] Started at %d Hz\n", mHz);
    
    const uint32_t intervalMs = 1000 / mHz;
    uint32_t lastTick = xTaskGetTickCount();
    uint32_t batchCounter = 0;
    
    while (mRunning) {
        uint32_t now = xTaskGetTickCount();
        
        if ((now - lastTick) >= pdMS_TO_TICKS(intervalMs)) {
            writeDummyAdcBatch();
            lastTick = now;
            batchCounter++;
            
            if (batchCounter % 10 == 0) {
                printf("[SimpleAdc] Written %lu batches\n", batchCounter);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void SimpleAdcSimulator::writeDummyAdcBatch() {
    if (!mStorage) return;
    
    // Create dummy ADC batch data (8 channels, 10 samples)
    uint8_t samples[24 * 10];  // 8ch * 3 bytes * 10 samples = 240 bytes
    memset(samples, 0, sizeof(samples));
    
    // Fill with dummy pattern
    for (int s = 0; s < 10; s++) {
        for (int ch = 0; ch < 8; ch++) {
            int idx = (s * 8 + ch) * 3;
            samples[idx] = (uint8_t)(ch + s);      // MSB
            samples[idx + 1] = (uint8_t)(s << 4); // Middle
            samples[idx + 2] = (uint8_t)(ch);      // LSB
        }
    }
    
    // Use SdStorage's ADC batch write method
    // Note: We need to add a public method to SdStorageServiceImpl for this
    // For now, just print
    printf("[SimpleAdc] Writing batch of 10 samples\n");
}

} // namespace sdstorage
} // namespace arcana
