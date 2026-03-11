#pragma once

#include "SensorService.hpp"
#include "Mpu6050Sensor.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace sensor {

class SensorServiceImpl : public SensorService {
public:
    static SensorService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    /**
     * Enable ADC simulation mode for high-frequency testing.
     * In this mode, fake high-frequency data is generated instead of reading from MPU6050.
     * @param enable true to enable simulation, false for real sensor (default)
     * @param sampleRateHz Sample rate in Hz (e.g., 10 for 10 samples/sec)
     */
    void enableAdcSimulation(bool enable, uint16_t sampleRateHz = 10);

private:
    SensorServiceImpl();
    ~SensorServiceImpl();
    SensorServiceImpl(const SensorServiceImpl&);
    SensorServiceImpl& operator=(const SensorServiceImpl&);

    static void sensorTask(void* param);
    void generateSimulatedData();

    static const uint32_t READ_INTERVAL_MS = 1000;
    static const uint16_t TASK_STACK_SIZE = 256;

    Observable<SensorDataModel> mDataObs;
    SensorDataModel mSensorData;
    Mpu6050Sensor mMpu;

    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // ADC simulation mode
    bool mAdcSimMode;
    uint16_t mAdcSampleRateHz;
    uint32_t mAdcSimCounter;
};

} // namespace sensor
} // namespace arcana
