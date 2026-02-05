/**
 * @file App.cpp
 * @brief Application entry point implementation
 */

#include "App.hpp"
#include "Observable.hpp"
#include "TimerService.hpp"
#include "CounterService.hpp"
#include "cmsis_os.h"

using namespace arcana;

extern "C" void App_Init(void) {
    // 1. Start Observable dispatcher
    ObservableDispatcher::start();

    // 2. Initialize services
    timerService.init(100);  // 100ms period
    counterService.init(&timerService.observable);

    // 3. Start timer
    timerService.start();
}

extern "C" void App_Run(void) {
    // Optional: Add periodic checks or status updates here
    // Example: check counterService.getCount()
    osDelay(1000);
}
