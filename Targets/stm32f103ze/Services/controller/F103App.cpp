#include "App.hpp"
#include "Observable.hpp"
#include "Controller.hpp"
#include "cmsis_os2.h"

using namespace arcana;

static void onDispatcherError(ObservableError error, const char* name, void* ctx) {
    (void)error;
    (void)name;
    (void)ctx;
}

extern "C" void App_Init(void) {
    ObservableDispatcher::setErrorCallback(onDispatcherError);
    ObservableDispatcher::start();
    Controller::getInstance().run();
}

extern "C" void App_Run(void) {
    osDelay(1000);
}

extern "C" uint32_t App_GetOverflowCount(void) {
    return ObservableDispatcher::getStats().overflowCount;
}

extern "C" uint32_t App_GetQueueSpace(void) {
    return ObservableDispatcher::getQueueSpaceAvailable();
}

extern "C" uint32_t App_GetPublishCount(void) {
    return ObservableDispatcher::getStats().publishCount;
}

extern "C" uint32_t App_GetDispatchCount(void) {
    return ObservableDispatcher::getStats().dispatchCount;
}
