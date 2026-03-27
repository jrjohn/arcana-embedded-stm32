#include "Controller.hpp"
#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LcdServiceImpl.hpp"
#ifdef ARCANA_LIGHT_SENSOR
#include "LightServiceImpl.hpp"
#endif
#include "SdBenchmarkServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "WifiServiceImpl.hpp"
#include "MqttServiceImpl.hpp"
#include "BleServiceImpl.hpp"
#include "OtaServiceImpl.hpp"
#include "IoServiceImpl.hpp"
#include "LcdViewModel.hpp"
#include "MainView.hpp"
#include "IDisplay.hpp"
#include "Hc08Ble.hpp"
#include "EspFlasher.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"

namespace arcana {

// Global thread-safe display for ad-hoc status callers
namespace display { IDisplay* g_display = nullptr; }

Controller::Controller()
    : mTimer(0)
    , mLed(0)
    , mSensor(0)
    , mLcd(0)
#ifdef ARCANA_LIGHT_SENSOR
    , mLight(0)
#endif
    , mSdBench(0)
    , mSdStorage(0)
    , mWifi(0)
    , mMqtt(0)
    , mBle(0)
    , mOta(0)
{
}

Controller::~Controller() {}

Controller& Controller::getInstance() {
    static Controller sInstance;
    return sInstance;
}

// Static MVVM instances (owned by Controller scope)
static lcd::LcdViewModel sViewModel;
static lcd::MainView     sMainView;

// Global pointer for cross-module ECG push (AtsStorageService → MainView)
lcd::MainView* g_mainView = &sMainView;

void Controller::run() {
    wireServices();
    wireViews();
    initHAL();
    initServices();
    startServices();
}

void Controller::wireServices() {
    mTimer     = &timer::TimerServiceImpl::getInstance();
    mLed       = &led::LedServiceImpl::getInstance();
    mSensor    = &sensor::SensorServiceImpl::getInstance();
    mLcd       = &lcd::LcdServiceImpl::getInstance();
#ifdef ARCANA_LIGHT_SENSOR
    mLight     = &light::LightServiceImpl::getInstance();
#endif
    mSdBench   = &sdbench::SdBenchmarkServiceImpl::getInstance();
    mSdStorage = &atsstorage::AtsStorageServiceImpl::getInstance();
    mWifi      = &wifi::WifiServiceImpl::getInstance();
    mMqtt      = &mqtt::MqttServiceImpl::getInstance();
    mBle       = &ble::BleServiceImpl::getInstance();
    mOta       = &OtaServiceImpl::getInstance();

    // Wire OTA <- ESP8266
    static_cast<OtaServiceImpl*>(mOta)->input.esp = &Esp8266::getInstance();

    // Wire LED <- Timer
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire SdStorage <- Sensor
    mSdStorage->input.SensorData = mSensor->output.DataEvents;

    // Wire MQTT <- WiFi + Sensor + Light
    mMqtt->input.Wifi       = mWifi;
    mMqtt->input.SensorData = mSensor->output.DataEvents;
#ifdef ARCANA_LIGHT_SENSOR
    mMqtt->input.LightData  = mLight->output.DataEvents;
#endif

    // Wire BLE <- Sensor + Light (JSON streaming)
    mBle->input.SensorData = mSensor->output.DataEvents;
#ifdef ARCANA_LIGHT_SENSOR
    mBle->input.LightData  = mLight->output.DataEvents;
#endif
}

void Controller::wireViews() {
    // ViewModel ← Service outputs (ViewModel subscribes to data sources)
    sViewModel.input.SensorData   = mSensor->output.DataEvents;
#ifdef ARCANA_LIGHT_SENSOR
    sViewModel.input.LightData    = mLight->output.DataEvents;
#endif
    sViewModel.input.StorageStats = mSdStorage->output.StatsEvents;
    sViewModel.input.SdBenchmark  = mSdBench->output.StatsEvents;
    sViewModel.input.BaseTimer    = mTimer->output.BaseTimer;
    sViewModel.input.MqttConn     = mMqtt->output.ConnectionStatus;

    // View ← ViewModel + LCD hardware
    sMainView.input.viewModel = &sViewModel;
    sMainView.input.lcd       = &mLcd->getDisplay();
}

void Controller::initHAL() {
    mTimer->initHAL();
    mLed->initHAL();
    mSensor->initHAL();
#ifdef ARCANA_LIGHT_SENSOR
    mLight->initHAL();
#endif
    mLcd->initHAL();          // LCD hardware init

    // Global display for ad-hoc status callers (statusLine, headerBar)
    // No MutexDisplay — saves 88B RAM. Ad-hoc callers previously had no
    // mutex either (throwaway Ili9341Lcd instances). Enable MutexDisplay
    // when RAM budget allows.
    display::g_display = &mLcd->getDisplay();
    mSdBench->initHAL();
    mSdStorage->initHAL();
#ifndef ESP_FLASH_MODE
    mWifi->initHAL();
    mMqtt->initHAL();
    Hc08Ble::getInstance().initHAL();
#else
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g={};g.Mode=GPIO_MODE_OUTPUT_PP;g.Speed=GPIO_SPEED_FREQ_HIGH;
    g.Pin=GPIO_PIN_13;HAL_GPIO_Init(GPIOG,&g);HAL_GPIO_WritePin(GPIOG,GPIO_PIN_13,GPIO_PIN_SET);
    g.Pin=GPIO_PIN_14;HAL_GPIO_Init(GPIOG,&g);HAL_GPIO_WritePin(GPIOG,GPIO_PIN_14,GPIO_PIN_SET);
#endif
}

void Controller::initServices() {
    mTimer->init();
    mLed->init();
    mSensor->init();
#ifdef ARCANA_LIGHT_SENSOR
    mLight->init();
#endif
    mSdBench->init();
    mSdStorage->init();
    mWifi->init();
    mMqtt->init();
    mBle->init();

    // View layer init (mutex, queues)
    sMainView.init();
}

void Controller::startServices() {
#ifdef ESP_FLASH_MODE
    LOG_I(ats::ErrorSource::System, evt::SYS_ESP_FLASH_MODE);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    mTimer->start();
    mLed->start();

    mSdBench->start();
    mSensor->start();
#ifdef ARCANA_LIGHT_SENSOR
    mLight->start();
#endif
    mSdStorage->start();
    mWifi->start();
    mMqtt->start();
    mBle->start();

    // IO key service (independent KEY1/KEY2 polling task)
    io::IoServiceImpl::getInstance().start();

    // View layer start (render task + ViewModel subscriptions)
    sMainView.start();
    sViewModel.init(sMainView.renderTaskHandle());

    // ESP8266 flasher — runs if esp_fw/ exists on SD card
    EspFlasher::run();
}

} // namespace arcana
