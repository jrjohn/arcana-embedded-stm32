#include "Controller.hpp"
#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LcdServiceImpl.hpp"

namespace arcana {

Controller::Controller()
    : mTimer(0)
    , mLed(0)
    , mSensor(0)
    , mLcd(0)
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

    // Wire LED <- Timer (base tick for 1-second color cycling)
    mLed->input.TimerEvents = mTimer->output.BaseTimer;

    // Wire LCD <- Sensor (display temperature/humidity)
    mLcd->input.SensorData = mSensor->output.DataEvents;
}

void Controller::initHAL() {
    mTimer->initHAL();
    mLed->initHAL();
    mSensor->initHAL();
    mLcd->initHAL();
}

void Controller::initServices() {
    mTimer->init();
    mLed->init();
    mLcd->init();
    mSensor->init();
}

void Controller::startServices() {
    mTimer->start();
    mLed->start();
    mSensor->start();
    mLcd->start();
}

} // namespace arcana
