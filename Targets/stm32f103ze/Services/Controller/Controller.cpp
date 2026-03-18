#include "Controller.hpp"
#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LcdServiceImpl.hpp"
#include "LightServiceImpl.hpp"
#include "SdBenchmarkServiceImpl.hpp"
#include "AtsStorageServiceImpl.hpp"
#include "WifiServiceImpl.hpp"
#include "MqttServiceImpl.hpp"
#include "BleServiceImpl.hpp"
#include "OtaServiceImpl.hpp"
#include "LcdViewModel.hpp"
#include "MainView.hpp"
#include "Hc08Ble.hpp"
#include "EspFlasher.hpp"
#include <cstdio>

namespace arcana {

Controller::Controller()
    : mTimer(0)
    , mLed(0)
    , mSensor(0)
    , mLcd(0)
    , mLight(0)
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
    mLight     = &light::LightServiceImpl::getInstance();
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
    mMqtt->input.LightData  = mLight->output.DataEvents;

    // Wire BLE <- Sensor + Light (JSON streaming)
    mBle->input.SensorData = mSensor->output.DataEvents;
    mBle->input.LightData  = mLight->output.DataEvents;
}

void Controller::wireViews() {
    // ViewModel ← Service outputs (ViewModel subscribes to data sources)
    sViewModel.input.SensorData   = mSensor->output.DataEvents;
    sViewModel.input.LightData    = mLight->output.DataEvents;
    sViewModel.input.StorageStats = mSdStorage->output.StatsEvents;
    sViewModel.input.SdBenchmark  = mSdBench->output.StatsEvents;
    sViewModel.input.BaseTimer    = mTimer->output.BaseTimer;

    // View ← ViewModel + LCD hardware
    sMainView.input.viewModel = &sViewModel;
    sMainView.input.lcd       = &mLcd->getLcd();
}

void Controller::initHAL() {
    mTimer->initHAL();
    mLed->initHAL();
    mSensor->initHAL();
    mLight->initHAL();
    mLcd->initHAL();          // LCD hardware init
    mSdBench->initHAL();
    mSdStorage->initHAL();
    mWifi->initHAL();
    mMqtt->initHAL();

    // BLE (HC-08 on USART2)
    Hc08Ble::getInstance().initHAL();
}

void Controller::initServices() {
    mTimer->init();
    mLed->init();
    mSensor->init();
    mLight->init();
    mSdBench->init();
    mSdStorage->init();
    mWifi->init();
    mMqtt->init();
    mBle->init();

    // View layer init (mutex, queues)
    sMainView.init();
}

void Controller::startServices() {
    mTimer->start();
    mLed->start();
    mSensor->start();
    mLight->start();
    mSdBench->start();
    mSdStorage->start();
    mWifi->start();
    mMqtt->start();
    mBle->start();

    // View layer start (render task + ViewModel subscriptions)
    sMainView.start();
    sViewModel.init(sMainView.renderTaskHandle());

    // ESP8266 flasher — must run AFTER ViewModel.init() to not block SD stats event
    EspFlasher::run();
}

} // namespace arcana
