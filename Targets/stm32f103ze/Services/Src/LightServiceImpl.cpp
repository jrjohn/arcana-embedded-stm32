#include "LightServiceImpl.hpp"
#include "I2cBus.hpp"

namespace arcana {
namespace light {

LightServiceImpl::LightServiceImpl()
    : mDataObs("LightSvc Data")
    , mLightData()
    , mSensor()
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
{
    output.DataEvents = &mDataObs;
}

LightServiceImpl::~LightServiceImpl() {
    stop();
}

LightService& LightServiceImpl::getInstance() {
    static LightServiceImpl sInstance;
    return sInstance;
}

ServiceStatus LightServiceImpl::initHAL() {
    // I2C bus already initialized by SensorService
    return ServiceStatus::OK;
}

ServiceStatus LightServiceImpl::init() {
    mSensor.init(&I2cBus::getInstance());
    return ServiceStatus::OK;
}

ServiceStatus LightServiceImpl::start() {
    mRunning = true;

    mTaskHandle = xTaskCreateStatic(
        lightTask,
        "light",
        TASK_STACK_SIZE,
        this,
        tskIDLE_PRIORITY + 2,
        mTaskStack,
        &mTaskBuffer
    );

    if (!mTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void LightServiceImpl::stop() {
    mRunning = false;
}

void LightServiceImpl::lightTask(void* param) {
    LightServiceImpl* self = static_cast<LightServiceImpl*>(param);

    // Wait for AP3216C first conversion (~112ms for ALS)
    vTaskDelay(pdMS_TO_TICKS(200));

    while (self->mRunning) {
        Ap3216cReading reading = self->mSensor.read();

        if (reading.valid) {
            self->mLightData.ambientLight = reading.ambientLight;
            self->mLightData.proximity = reading.proximity;
            self->mLightData.updateTimestamp();
            self->mDataObs.publish(&self->mLightData);
        }

        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }

    vTaskDelete(0);
}

} // namespace light
} // namespace arcana
