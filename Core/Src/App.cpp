/**
 * @file App.cpp
 * @brief Application entry point implementation
 */

#include "App.hpp"
#include "Observable.hpp"
#include "TimerService.hpp"
#include "CounterService.hpp"
#include "TimeDisplayService.hpp"
#include "cmsis_os.h"

using namespace arcana;

/* Error statistics for monitoring */
static volatile uint32_t queueOverflowCount = 0;

/**
 * @brief Error callback for Observable queue overflow
 */
static void onObservableError(ObservableError error, const char* observableName, void* context) {
    (void)context;
    (void)observableName;

    switch (error) {
        case ObservableError::QueueFull:
            queueOverflowCount++;
            /* Could trigger LED blink, log to UART, etc. */
            break;
        case ObservableError::QueueNotReady:
            /* Dispatcher not started - programming error */
            break;
        default:
            break;
    }
}

extern "C" void App_Init(void) {
    // 1. Set error callback before starting dispatcher
    ObservableDispatcher::setErrorCallback(onObservableError, nullptr);

    // 2. Start Observable dispatcher
    ObservableDispatcher::start();

    // 3. Initialize services
    timerService.init(100);  // 100ms period
    counterService.init(&timerService.observable);
    timeDisplayService.init(&timerService.observable);

    // 4. Start timer
    timerService.start();
}

extern "C" void App_Run(void) {
    // Example: Get current time string from TimeDisplayService
    // const char* timeStr = timeDisplayService.getTimeString();
    // Could send via UART, display on LCD, etc.

    osDelay(1000);
}

/**
 * @brief Get local overflow count (from error callback)
 * @return Number of queue overflow events
 */
extern "C" uint32_t App_GetOverflowCount(void) {
    return queueOverflowCount;
}

/**
 * @brief Get queue space available
 * @return Number of free queue slots
 */
extern "C" uint8_t App_GetQueueSpaceAvailable(void) {
    return ObservableDispatcher::getQueueSpaceAvailable();
}

/**
 * @brief Get dispatcher publish count
 * @return Total publish attempts
 */
extern "C" uint32_t App_GetPublishCount(void) {
    return ObservableDispatcher::getStats().publishCount;
}

/**
 * @brief Get dispatcher dispatch count
 * @return Successfully dispatched events
 */
extern "C" uint32_t App_GetDispatchCount(void) {
    return ObservableDispatcher::getStats().dispatchCount;
}

/**
 * @brief Get queue high water mark (normal priority)
 * @return Peak queue usage
 */
extern "C" uint8_t App_GetQueueHighWaterMark(void) {
    return ObservableDispatcher::getStats().queueHighWaterMark;
}

/**
 * @brief Get high priority queue space available
 * @return Number of free slots (0-4)
 */
extern "C" uint8_t App_GetHighQueueSpaceAvailable(void) {
    return ObservableDispatcher::getHighQueueSpaceAvailable();
}

/**
 * @brief Get high priority publish count
 * @return Total high priority publish attempts
 */
extern "C" uint32_t App_GetHighPublishCount(void) {
    return ObservableDispatcher::getStats().publishHighCount;
}

/**
 * @brief Get high priority dispatch count
 * @return Successfully dispatched high priority events
 */
extern "C" uint32_t App_GetHighDispatchCount(void) {
    return ObservableDispatcher::getStats().dispatchHighCount;
}
