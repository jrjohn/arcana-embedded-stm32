#include "SensorServiceImpl.hpp"

namespace arcana {
namespace sensor {

SensorServiceImpl::SensorServiceImpl()
    : mDataObs("SensorSvc Data")
    , mSensorData()
    , mDht()
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
{
    output.DataEvents = &mDataObs;
}

SensorServiceImpl::~SensorServiceImpl() {
    stop();
}

SensorService& SensorServiceImpl::getInstance() {
    static SensorServiceImpl sInstance;
    return sInstance;
}

ServiceStatus SensorServiceImpl::initHAL() {
    mDht.initHAL();
    return ServiceStatus::OK;
}

ServiceStatus SensorServiceImpl::init() {
    return ServiceStatus::OK;
}

ServiceStatus SensorServiceImpl::start() {
    mRunning = true;

    mTaskHandle = xTaskCreateStatic(
        sensorTask,
        "sensor",
        TASK_STACK_SIZE,
        this,
        tskIDLE_PRIORITY + 2,
        mTaskStack,
        &mTaskBuffer
    );

    if (!mTaskHandle) {
        return ServiceStatus::Error;
    }

    return ServiceStatus::OK;
}

void SensorServiceImpl::stop() {
    mRunning = false;
}

void SensorServiceImpl::sensorTask(void* param) {
    SensorServiceImpl* self = static_cast<SensorServiceImpl*>(param);

    // Initial delay to let DHT11 stabilize (1s after power-on)
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (self->mRunning) {
        DhtReading reading = self->mDht.read();

        if (reading.valid) {
            self->mSensorData.temperature = reading.temperature;
            self->mSensorData.humidity = reading.humidity;
            self->mSensorData.quality = 100;
            self->mSensorData.updateTimestamp();
            self->mDataObs.publish(&self->mSensorData);
        }

        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }

    vTaskDelete(0);
}

} // namespace sensor
} // namespace arcana
