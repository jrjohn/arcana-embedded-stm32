#include "Controller.hpp"
#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LcdServiceImpl.hpp"
#include "LightServiceImpl.hpp"

namespace arcana {

Controller::Controller()
    : mTimer(0)
    , mLed(0)
    , mSensor(0)
    , mLcd(0)
    , mLight(0)
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
    mTimer  = &timer::TimerServiceImpl::getInstance();
    mLed    = &led::LedServiceImpl::getInstance();
    mSensor = &sensor::SensorServiceImpl::getInstance();
    mLcd    = &lcd::LcdServiceImpl::getInstance();
    mLight  = &light::LightServiceImpl::getInstance();

    // Wire LED <- Timer (base tick for 1-second color cycling)
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire LCD <- Sensor (display temperature from MPU6050)
    mLcd->input.SensorData = mSensor->output.DataEvents;

    // Wire LCD <- Light (display ambient light from AP3216C)
    mLcd->input.LightData = mLight->output.DataEvents;
}

void Controller::initHAL() {
    mTimer->initHAL();
    mLed->initHAL();
    mSensor->initHAL();   // Initializes shared I2C bus
    mLight->initHAL();
    mLcd->initHAL();
}

void Controller::initServices() {
    mTimer->init();
    mLed->init();
    mSensor->init();      // Initializes MPU6050
    mLight->init();       // Initializes AP3216C
    mLcd->init();
}

void Controller::startServices() {
    mTimer->start();
    mLed->start();
    mSensor->start();
    mLight->start();
    mLcd->start();
}

} // namespace arcana
