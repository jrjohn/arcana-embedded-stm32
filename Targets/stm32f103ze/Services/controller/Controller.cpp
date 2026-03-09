#include "Controller.hpp"
#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LcdServiceImpl.hpp"
#include "LightServiceImpl.hpp"
#include "StorageServiceImpl.hpp"
#include "SdBenchmarkServiceImpl.hpp"
#include "WifiMqttServiceImpl.hpp"

namespace arcana {

Controller::Controller()
    : mTimer(0)
    , mLed(0)
    , mSensor(0)
    , mLcd(0)
    , mLight(0)
    , mStorage(0)
    , mSdBench(0)
    , mMqtt(0)
{
}

Controller::~Controller() {}

Controller& Controller::getInstance() {
    static Controller sInstance;
    return sInstance;
}

void Controller::run() {
    wireServices();
    initHAL();
    initServices();
    startServices();
}

void Controller::wireServices() {
    mTimer   = &timer::TimerServiceImpl::getInstance();
    mLed     = &led::LedServiceImpl::getInstance();
    mSensor  = &sensor::SensorServiceImpl::getInstance();
    mLcd     = &lcd::LcdServiceImpl::getInstance();
    mLight   = &light::LightServiceImpl::getInstance();
    mStorage = &storage::StorageServiceImpl::getInstance();
    mSdBench = &sdbench::SdBenchmarkServiceImpl::getInstance();
    mMqtt    = &mqtt::WifiMqttServiceImpl::getInstance();

    // Wire LED <- Timer (base tick for 1-second color cycling)
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire LCD <- Sensor (display temperature from MPU6050)
    mLcd->input.SensorData = mSensor->output.DataEvents;

    // Wire LCD <- Light (display ambient light from AP3216C)
    mLcd->input.LightData = mLight->output.DataEvents;

    // StorageService disabled in benchmark mode (SD card used by SdBenchmarkService)
    // mStorage->input.SensorData = mSensor->output.DataEvents;
    // mLcd->input.StorageStats = mStorage->output.StatsEvents;

    // Wire LCD <- SD Benchmark (display write speed)
    mLcd->input.SdBenchmark = mSdBench->output.StatsEvents;

    // Wire MQTT <- Sensor + Light (publish all sensor data to MQTT broker)
    mMqtt->input.SensorData = mSensor->output.DataEvents;
    mMqtt->input.LightData  = mLight->output.DataEvents;
}

void Controller::initHAL() {
    mTimer->initHAL();
    mLed->initHAL();
    mSensor->initHAL();   // Initializes shared I2C bus
    mLight->initHAL();
    mLcd->initHAL();
    // mStorage->initHAL();
    mSdBench->initHAL();  // Initializes SDIO + SD card
    mMqtt->initHAL();     // Initializes USART3 + ESP8266 GPIO
}

void Controller::initServices() {
    mTimer->init();
    mLed->init();
    mSensor->init();      // Initializes MPU6050
    mLight->init();       // Initializes AP3216C
    mLcd->init();
    // mStorage->init();
    mSdBench->init();     // Fills benchmark buffer
    mMqtt->init();        // Subscribes to sensor observable
}

void Controller::startServices() {
    mTimer->start();
    mLed->start();
    mSensor->start();
    mLight->start();
    mLcd->start();
    // mStorage->start();
    mMqtt->start();       // Starts WiFi + MQTT connection task
    mSdBench->start();    // Start after MQTT (SDIO DMA interferes with UART)
}

} // namespace arcana
