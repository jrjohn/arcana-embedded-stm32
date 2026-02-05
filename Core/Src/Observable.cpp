/**
 * @file Observable.cpp
 * @brief Observable Dispatcher implementation
 */

#include "Observable.hpp"
#include "cmsis_os.h"

namespace arcana {

/* Static member definitions - Normal priority queue */
StaticQueue_t ObservableDispatcher::queueBuffer_;
uint8_t ObservableDispatcher::queueStorage_[DISPATCHER_QUEUE_SIZE_NORMAL * sizeof(DispatchItem)];
QueueHandle_t ObservableDispatcher::queue_ = nullptr;

/* Static member definitions - High priority queue */
StaticQueue_t ObservableDispatcher::queueHighBuffer_;
uint8_t ObservableDispatcher::queueHighStorage_[DISPATCHER_QUEUE_SIZE_HIGH * sizeof(DispatchItem)];
QueueHandle_t ObservableDispatcher::queueHigh_ = nullptr;

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
        /* Priority 1: Check high priority queue first (non-blocking) */
        while (xQueueReceive(queueHigh_, &item, 0) == pdTRUE) {
            if (item.notifyFunc != nullptr && item.observable != nullptr) {
                item.notifyFunc(item.observable, item.model);
                stats_.dispatchHighCount++;
            }

            /* Update high priority queue high water mark */
            UBaseType_t highMessages = uxQueueMessagesWaiting(queueHigh_);
            if (highMessages > stats_.queueHighHighWaterMark) {
                stats_.queueHighHighWaterMark = static_cast<uint8_t>(highMessages);
            }
        }

        /* Priority 2: Check normal queue (with short timeout to re-check high priority) */
        if (xQueueReceive(queue_, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (item.notifyFunc != nullptr && item.observable != nullptr) {
                item.notifyFunc(item.observable, item.model);
                stats_.dispatchCount++;
            }

            /* Update normal queue high water mark */
            UBaseType_t normalMessages = uxQueueMessagesWaiting(queue_);
            if (normalMessages > stats_.queueHighWaterMark) {
                stats_.queueHighWaterMark = static_cast<uint8_t>(normalMessages);
            }
        }
    }
}

void ObservableDispatcher::start() {
    if (queue_ != nullptr) return;  // Already started

    // Create static normal priority queue
    queue_ = xQueueCreateStatic(
        DISPATCHER_QUEUE_SIZE_NORMAL,
        sizeof(DispatchItem),
        queueStorage_,
        &queueBuffer_
    );

    // Create static high priority queue
    queueHigh_ = xQueueCreateStatic(
        DISPATCHER_QUEUE_SIZE_HIGH,
        sizeof(DispatchItem),
        queueHighStorage_,
        &queueHighBuffer_
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

bool ObservableDispatcher::enqueueHighPriority(const DispatchItem& item) {
    stats_.publishHighCount++;

    if (queueHigh_ == nullptr) {
        /* Dispatcher not started */
        if (errorCallback_ != nullptr) {
            errorCallback_(ObservableError::QueueNotReady, item.observableName, errorContext_);
        }
        return false;
    }

    if (xQueueSend(queueHigh_, &item, 0) != pdTRUE) {
        /* Queue full - overflow */
        stats_.overflowHighCount++;

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

bool ObservableDispatcher::enqueueHighPriorityFromISR(const DispatchItem& item, BaseType_t* pxHigherPriorityTaskWoken) {
    /* Note: Cannot safely update stats from ISR without atomic ops */

    if (queueHigh_ == nullptr) {
        return false;
    }

    if (xQueueSendFromISR(queueHigh_, &item, pxHigherPriorityTaskWoken) != pdTRUE) {
        /* Queue full - cannot call error callback from ISR */
        return false;
    }

    return true;
}

} // namespace arcana
