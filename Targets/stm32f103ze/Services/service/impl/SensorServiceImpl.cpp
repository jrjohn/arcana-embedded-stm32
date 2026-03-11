#include "SensorServiceImpl.hpp"
#include "I2cBus.hpp"
#include "task.h"
#include <cmath>

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
    , mAdcSimMode(false)
    , mAdcSampleRateHz(10)
    , mAdcSimCounter(0)
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

void SensorServiceImpl::enableAdcSimulation(bool enable, uint16_t sampleRateHz) {
    mAdcSimMode = enable;
    mAdcSampleRateHz = sampleRateHz;
}

void SensorServiceImpl::generateSimulatedData() {
    // Generate fake ADC-like data using sine wave patterns
    // This simulates high-frequency sensor data similar to ADS1298
    
    float angle = 2.0f * 3.14159f * (mAdcSimCounter % 100) / 100.0f;
    
    // Simulate temperature as sine wave
    mSensorData.temperature = 25.0f + 5.0f * sinf(angle);
    
    // Simulate accelerometer as different frequency components
    mSensorData.accelX = (int16_t)(1000.0f * sinf(angle));
    mSensorData.accelY = (int16_t)(1000.0f * sinf(angle * 2.0f));
    mSensorData.accelZ = (int16_t)(1000.0f * cosf(angle));
    
    mSensorData.updateTimestamp();
    mDataObs.publish(&mSensorData);
    
    mAdcSimCounter++;
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

    if (self->mAdcSimMode) {
        // ADC simulation mode: generate high-frequency fake data
        uint32_t intervalMs = 1000 / self->mAdcSampleRateHz;
        if (intervalMs < 10) intervalMs = 10; // Minimum 10ms for safety
        
        while (self->mRunning) {
            self->generateSimulatedData();
            vTaskDelay(pdMS_TO_TICKS(intervalMs));
        }
    } else {
        // Normal mode: read from real MPU6050 sensor
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
    }

    vTaskDelete(0);
}

} // namespace sensor
} // namespace arcana
