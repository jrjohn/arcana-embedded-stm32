/**
 * @file Observable.cpp
 * @brief Observable Dispatcher implementation
 */

#include "Observable.hpp"
#include "cmsis_os.h"

namespace arcana {

/* Static member definitions */
StaticQueue_t ObservableDispatcher::queueBuffer_;
uint8_t ObservableDispatcher::queueStorage_[DISPATCHER_QUEUE_SIZE * sizeof(DispatchItem)];
QueueHandle_t ObservableDispatcher::queue_ = nullptr;

StaticTask_t ObservableDispatcher::taskBuffer_;
StackType_t ObservableDispatcher::taskStack_[DISPATCHER_STACK_SIZE];
TaskHandle_t ObservableDispatcher::taskHandle_ = nullptr;

void ObservableDispatcher::dispatcherTask(void* pvParameters) {
    (void)pvParameters;
    DispatchItem item;

    for (;;) {
        if (xQueueReceive(queue_, &item, portMAX_DELAY) == pdTRUE) {
            if (item.notifyFunc != nullptr && item.observable != nullptr) {
                item.notifyFunc(item.observable, item.model);
            }
        }
    }
}

void ObservableDispatcher::start() {
    if (queue_ != nullptr) return;  // Already started

    // Create static queue
    queue_ = xQueueCreateStatic(
        DISPATCHER_QUEUE_SIZE,
        sizeof(DispatchItem),
        queueStorage_,
        &queueBuffer_
    );

    // Create static task
    taskHandle_ = xTaskCreateStatic(
        dispatcherTask,
        "ObsDisp",
        DISPATCHER_STACK_SIZE,
        nullptr,
        osPriorityAboveNormal,
        taskStack_,
        &taskBuffer_
    );
}

bool ObservableDispatcher::enqueue(const DispatchItem& item) {
    if (queue_ == nullptr) return false;
    return xQueueSend(queue_, &item, 0) == pdTRUE;
}

} // namespace arcana
