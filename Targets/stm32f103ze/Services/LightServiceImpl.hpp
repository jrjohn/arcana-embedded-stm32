#pragma once

#include "LightService.hpp"
#include "Ap3216cSensor.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace light {

class LightServiceImpl : public LightService {
public:
    static LightService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    LightServiceImpl();
    ~LightServiceImpl();
    LightServiceImpl(const LightServiceImpl&);
    LightServiceImpl& operator=(const LightServiceImpl&);

    static void lightTask(void* param);

    static const uint32_t READ_INTERVAL_MS = 1000;
    static const uint16_t TASK_STACK_SIZE = 256;

    Observable<LightDataModel> mDataObs;
    LightDataModel mLightData;
    Ap3216cSensor mSensor;

    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;
};

} // namespace light
} // namespace arcana
