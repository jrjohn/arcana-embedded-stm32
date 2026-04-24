#include "SensorServiceImpl.hpp"
#include "I2cBus.hpp"

namespace arcana {
namespace sensor {

SensorServiceImpl::SensorServiceImpl()
    : mDataObs("SensorSvc Data")
    , mSensorData()
    , mMpu()
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
    // Initialize shared I2C bus (used by both MPU6050 and AP3216C)
    I2cBus::getInstance().initHAL();
    return ServiceStatus::OK;
}

ServiceStatus SensorServiceImpl::init() {
    mMpu.init(&I2cBus::getInstance());
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

    if (!mTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void SensorServiceImpl::stop() {
    mRunning = false;
}

void SensorServiceImpl::sensorTask(void* param) {
    SensorServiceImpl* self = static_cast<SensorServiceImpl*>(param);

    vTaskDelay(pdMS_TO_TICKS(100));

    while (self->mRunning) {
        Mpu6050Reading reading = self->mMpu.read();

        if (reading.valid) {
            self->mSensorData.temperature = reading.temperature;
            self->mSensorData.accelX = reading.accelX;
            self->mSensorData.accelY = reading.accelY;
            self->mSensorData.accelZ = reading.accelZ;
            self->mSensorData.updateTimestamp();
            self->mDataObs.publish(&self->mSensorData);
        }

        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }

    vTaskDelete(0);
}

} // namespace sensor
} // namespace arcana
