/**
 * Test error paths in ObservableDispatcher (Observable.cpp)
 *
 * Separate executable: ObservableDispatcher uses static members,
 * so start() state persists across tests. This file runs WITHOUT
 * start() to test queue==nullptr paths, and uses a controllable
 * xQueueSendToBack stub to test queue-full paths.
 */
#include <gtest/gtest.h>

// Controllable stub behavior
static bool g_queueSendShouldFail = false;

// Override FreeRTOS stubs with controllable versions
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"
#include "cmsis_os.h"
#include <cstring>

extern "C" TickType_t xTaskGetTickCount(void) { return 0; }

extern "C" QueueHandle_t xQueueCreateStatic(UBaseType_t, UBaseType_t, uint8_t*, StaticQueue_t* q) {
    return (QueueHandle_t)q;
}
extern "C" BaseType_t xQueueSendToBack(QueueHandle_t, const void*, TickType_t) {
    return g_queueSendShouldFail ? pdFALSE : pdTRUE;
}
extern "C" BaseType_t xQueueSendToBackFromISR(QueueHandle_t, const void*, BaseType_t* p) {
    if (p) *p = pdFALSE;
    return g_queueSendShouldFail ? pdFALSE : pdTRUE;
}
extern "C" BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
extern "C" UBaseType_t uxQueueSpacesAvailable(QueueHandle_t) { return 4; }
extern "C" UBaseType_t uxQueueMessagesWaiting(QueueHandle_t) { return 0; }

extern "C" TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char*, uint32_t, void*,
                                           UBaseType_t, StackType_t*, StaticTask_t* t) {
    return (TaskHandle_t)t;
}
extern "C" void vTaskDelay(TickType_t) {}
extern "C" TimerHandle_t xTimerCreateStatic(const char*, TickType_t, UBaseType_t, void*,
                                             TimerCallbackFunction_t, StaticTimer_t* t) {
    return (TimerHandle_t)t;
}
extern "C" BaseType_t xTimerStart(TimerHandle_t, TickType_t) { return pdTRUE; }
extern "C" BaseType_t xTimerStop(TimerHandle_t, TickType_t)  { return pdTRUE; }
extern "C" void* pvTimerGetTimerID(TimerHandle_t) { return nullptr; }
extern "C" osStatus_t osDelay(uint32_t) { return 0; }

#include "Observable.hpp"

using namespace arcana;

// ── Queue nullptr tests (before start()) ─────────────────────────────────────

TEST(ObsErrorTest, EnqueueBeforeStartReturnsFalse) {
    // Static members start as nullptr — queue_ is null
    ObservableDispatcher::DispatchItem item{};
    item.observableName = "test-null";
    EXPECT_FALSE(ObservableDispatcher::enqueue(item));
}

TEST(ObsErrorTest, EnqueueHighPriorityBeforeStartReturnsFalse) {
    ObservableDispatcher::DispatchItem item{};
    item.observableName = "test-null-high";
    EXPECT_FALSE(ObservableDispatcher::enqueueHighPriority(item));
}

TEST(ObsErrorTest, EnqueueFromISRBeforeStartReturnsFalse) {
    ObservableDispatcher::DispatchItem item{};
    BaseType_t woken = pdFALSE;
    EXPECT_FALSE(ObservableDispatcher::enqueueFromISR(item, &woken));
}

TEST(ObsErrorTest, EnqueueHighPriorityFromISRBeforeStartReturnsFalse) {
    ObservableDispatcher::DispatchItem item{};
    BaseType_t woken = pdFALSE;
    EXPECT_FALSE(ObservableDispatcher::enqueueHighPriorityFromISR(item, &woken));
}

TEST(ObsErrorTest, ErrorCallbackOnQueueNotReady) {
    ObservableError lastError = ObservableError::None;
    ObservableDispatcher::setErrorCallback(
        [](ObservableError err, const char*, void* ctx) {
            *static_cast<ObservableError*>(ctx) = err;
        }, &lastError);

    ObservableDispatcher::DispatchItem item{};
    item.observableName = "test-err";
    ObservableDispatcher::enqueue(item);
    EXPECT_EQ(lastError, ObservableError::QueueNotReady);

    lastError = ObservableError::None;
    ObservableDispatcher::enqueueHighPriority(item);
    EXPECT_EQ(lastError, ObservableError::QueueNotReady);

    ObservableDispatcher::setErrorCallback(nullptr);
}

// ── Queue full tests (after start(), with failing send) ──────────────────────

TEST(ObsErrorTest, EnqueueQueueFullReturnsFalse) {
    ObservableDispatcher::start();  // create queues
    g_queueSendShouldFail = true;
    ObservableDispatcher::resetStats();

    ObservableDispatcher::DispatchItem item{};
    item.observableName = "test-full";
    EXPECT_FALSE(ObservableDispatcher::enqueue(item));
    EXPECT_EQ(ObservableDispatcher::getStats().overflowCount, 1u);

    g_queueSendShouldFail = false;
}

TEST(ObsErrorTest, EnqueueHighPriorityQueueFullReturnsFalse) {
    g_queueSendShouldFail = true;
    ObservableDispatcher::resetStats();

    ObservableDispatcher::DispatchItem item{};
    item.observableName = "test-full-high";
    EXPECT_FALSE(ObservableDispatcher::enqueueHighPriority(item));
    EXPECT_EQ(ObservableDispatcher::getStats().overflowHighCount, 1u);

    g_queueSendShouldFail = false;
}

TEST(ObsErrorTest, EnqueueFromISRQueueFullReturnsFalse) {
    g_queueSendShouldFail = true;

    ObservableDispatcher::DispatchItem item{};
    BaseType_t woken = pdFALSE;
    EXPECT_FALSE(ObservableDispatcher::enqueueFromISR(item, &woken));

    g_queueSendShouldFail = false;
}

TEST(ObsErrorTest, EnqueueHighPriorityFromISRQueueFullReturnsFalse) {
    g_queueSendShouldFail = true;

    ObservableDispatcher::DispatchItem item{};
    BaseType_t woken = pdFALSE;
    EXPECT_FALSE(ObservableDispatcher::enqueueHighPriorityFromISR(item, &woken));

    g_queueSendShouldFail = false;
}

TEST(ObsErrorTest, ErrorCallbackOnQueueFull) {
    ObservableError lastError = ObservableError::None;
    ObservableDispatcher::setErrorCallback(
        [](ObservableError err, const char*, void* ctx) {
            *static_cast<ObservableError*>(ctx) = err;
        }, &lastError);

    g_queueSendShouldFail = true;

    ObservableDispatcher::DispatchItem item{};
    item.observableName = "test-full-cb";
    ObservableDispatcher::enqueue(item);
    EXPECT_EQ(lastError, ObservableError::QueueFull);

    lastError = ObservableError::None;
    ObservableDispatcher::enqueueHighPriority(item);
    EXPECT_EQ(lastError, ObservableError::QueueFull);

    g_queueSendShouldFail = false;
    ObservableDispatcher::setErrorCallback(nullptr);
}
