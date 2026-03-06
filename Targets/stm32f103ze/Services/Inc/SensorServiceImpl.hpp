#pragma once

#include "SensorService.hpp"
#include "DhtSensor.hpp"
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

private:
    SensorServiceImpl();
    ~SensorServiceImpl();
    SensorServiceImpl(const SensorServiceImpl&);
    SensorServiceImpl& operator=(const SensorServiceImpl&);

    static void sensorTask(void* param);

    static const uint32_t READ_INTERVAL_MS = 2000;
    static const uint16_t TASK_STACK_SIZE = 256;

    Observable<SensorDataModel> mDataObs;
    SensorDataModel mSensorData;
    DhtSensor mDht;

    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;
};

} // namespace sensor
} // namespace arcana
