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

ErrorCallback ObservableDispatcher::errorCallback_ = nullptr;
void* ObservableDispatcher::errorContext_ = nullptr;
DispatcherStats ObservableDispatcher::stats_ = {};

void ObservableDispatcher::dispatcherTask(void* pvParameters) {
    (void)pvParameters;
    DispatchItem item;

    for (;;) {
        if (xQueueReceive(queue_, &item, portMAX_DELAY) == pdTRUE) {
            if (item.notifyFunc != nullptr && item.observable != nullptr) {
                item.notifyFunc(item.observable, item.model);
                stats_.dispatchCount++;
            }

            /* Update high water mark */
            UBaseType_t messagesWaiting = uxQueueMessagesWaiting(queue_);
            if (messagesWaiting > stats_.queueHighWaterMark) {
                stats_.queueHighWaterMark = static_cast<uint8_t>(messagesWaiting);
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
    stats_.publishCount++;

    if (queue_ == nullptr) {
        /* Dispatcher not started */
        if (errorCallback_ != nullptr) {
            errorCallback_(ObservableError::QueueNotReady, item.observableName, errorContext_);
        }
        return false;
    }

    if (xQueueSend(queue_, &item, 0) != pdTRUE) {
        /* Queue full - overflow */
        stats_.overflowCount++;

        if (errorCallback_ != nullptr) {
            errorCallback_(ObservableError::QueueFull, item.observableName, errorContext_);
        }
        return false;
    }

    return true;
}

bool ObservableDispatcher::enqueueFromISR(const DispatchItem& item, BaseType_t* pxHigherPriorityTaskWoken) {
    /* Note: Cannot safely update stats from ISR without atomic ops */

    if (queue_ == nullptr) {
        return false;
    }

    if (xQueueSendFromISR(queue_, &item, pxHigherPriorityTaskWoken) != pdTRUE) {
        /* Queue full - cannot call error callback from ISR */
        return false;
    }

    return true;
}

} // namespace arcana
