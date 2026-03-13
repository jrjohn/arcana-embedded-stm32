#include "Controller.hpp"
#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LcdServiceImpl.hpp"
#include "LightServiceImpl.hpp"
#include "StorageServiceImpl.hpp"
#include "SdBenchmarkServiceImpl.hpp"
#include "SdStorageServiceImpl.hpp"
#include "WifiServiceImpl.hpp"
#include "MqttServiceImpl.hpp"
// AdcSimulatorService disabled - using simple direct write instead
// #include "AdcSimulatorService.hpp"
#include "RtcDriver.hpp"
#include "SystemClock.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {

Controller::Controller()
    : mTimer(0)
    , mLed(0)
    , mSensor(0)
    , mLcd(0)
    , mLight(0)
    , mStorage(0)
    , mSdBench(0)
    , mSdStorage(0)
    , mWifi(0)
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
    mTimer     = &timer::TimerServiceImpl::getInstance();
    mLed       = &led::LedServiceImpl::getInstance();
    mSensor    = &sensor::SensorServiceImpl::getInstance();
    mLcd       = &lcd::LcdServiceImpl::getInstance();
    mLight     = &light::LightServiceImpl::getInstance();
    mStorage   = &storage::StorageServiceImpl::getInstance();
    mSdBench   = &sdbench::SdBenchmarkServiceImpl::getInstance();
    mSdStorage = &sdstorage::SdStorageServiceImpl::getInstance();
    mWifi      = &wifi::WifiServiceImpl::getInstance();
    mMqtt      = &mqtt::MqttServiceImpl::getInstance();
    // mAdcSim disabled - using enableStressTest instead

    // Wire LED <- Timer (base tick for 1-second color cycling)
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire LCD <- Sensor (display temperature from MPU6050)
    mLcd->input.SensorData = mSensor->output.DataEvents;

    // Wire LCD <- Light (display ambient light from AP3216C)
    mLcd->input.LightData = mLight->output.DataEvents;

    // littlefs StorageService disabled (SD card TSDB replaces it)
    // mStorage->input.SensorData = mSensor->output.DataEvents;

    // Wire SdStorage <- Sensor (encrypt + append to TSDB on SD card)
    mSdStorage->input.SensorData = mSensor->output.DataEvents;

    // Wire SdStorage <- ADC Simulator disabled
    // Using enableStressTest() instead for testing

    // Wire LCD <- SdStorage stats (display record count + write rate)
    mLcd->input.StorageStats = mSdStorage->output.StatsEvents;

    // Wire LCD <- BaseTimer (1-second clock display)
    mLcd->input.BaseTimer = mTimer->output.BaseTimer;

    // Wire LCD <- SD Benchmark (display write speed — unused now but keep wiring)
    mLcd->input.SdBenchmark = mSdBench->output.StatsEvents;

    // Wire MQTT <- WiFi + Sensor + Light
    mMqtt->input.Wifi       = mWifi;
    mMqtt->input.SensorData = mSensor->output.DataEvents;
    mMqtt->input.LightData  = mLight->output.DataEvents;
}

void Controller::initHAL() {
    // Init RTC first — seed SystemClock from backup battery before anything else
    RtcDriver& rtc = RtcDriver::getInstance();
    if (rtc.init()) {
        uint32_t epoch = rtc.read();
        if (RtcDriver::isValidEpoch(epoch)) {
            SystemClock::getInstance().sync(epoch);
            printf("[Controller] SystemClock seeded from RTC: %lu\n",
                   (unsigned long)epoch);
        }
    }

    mTimer->initHAL();
    mLed->initHAL();
    mSensor->initHAL();      // Initializes shared I2C bus
    mLight->initHAL();
    mLcd->initHAL();
    // mStorage->initHAL();  // littlefs storage disabled
    mSdBench->initHAL();     // Initializes SDIO + SD card
    mSdStorage->initHAL();   // Derives per-device encryption key
    mWifi->initHAL();        // Initializes USART3 + ESP8266 GPIO
    mMqtt->initHAL();
    // mAdcSim->initHAL();  // Disabled
}

void Controller::initServices() {
    mTimer->init();
    mLed->init();
    mSensor->init();          // Initializes MPU6050
    mLight->init();           // Initializes AP3216C
    mLcd->init();
    // mStorage->init();      // littlefs storage disabled
    mSdBench->init();
    mSdStorage->init();       // Creates semaphores
    mWifi->init();
    mMqtt->init();
    
    // Use ADC stress test: 1000 SPS, 100 samples/batch = 10 writes/sec
    mSdStorage->enableAdcStressTest(1000, 100);
}

void Controller::startServices() {
    mTimer->start();
    mLed->start();
    mSensor->start();
    mLight->start();
    mLcd->start();
    // mStorage->start();     // littlefs storage disabled
    mSdBench->start();        // exFAT format+mount must succeed first
    mSdStorage->start();      // Waits for g_exfat_ready, then inits FlashDB
    mWifi->start();
    mMqtt->start();           // MQTT task waits for g_exfat_ready flag
    
    // ADC simulator disabled - using enableStressTest(10) instead
}

} // namespace arcana
