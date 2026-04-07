#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"
#include "cmsis_os.h"
#include <cstring>
#include <cstdlib>

/* ── TickCount ──────────────────────────────────────────────────────────── */
extern "C" TickType_t xTaskGetTickCount(void) { return 0; }

/* ── Queue stubs ────────────────────────────────────────────────────────── */
extern "C" QueueHandle_t xQueueCreateStatic(UBaseType_t, UBaseType_t, uint8_t*, StaticQueue_t* q) {
    return (QueueHandle_t)q;
}
/* Test override hooks: tests that need to inject queue items (e.g.
 * test_command_bridge driving CommandBridge::bridgeTask) install a function
 * pointer here. Default behavior preserves the legacy stub semantics
 * (Send always succeeds, Receive always fails) so existing tests are unaffected. */
typedef BaseType_t (*XQueueSendFn)(QueueHandle_t, const void*, TickType_t);
typedef BaseType_t (*XQueueReceiveFn)(QueueHandle_t, void*, TickType_t);
XQueueSendFn    g_xQueueSendOverride    = nullptr;
XQueueReceiveFn g_xQueueReceiveOverride = nullptr;

extern "C" BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t t) {
    if (g_xQueueSendOverride) return g_xQueueSendOverride(q, item, t);
    return pdTRUE;
}
extern "C" BaseType_t xQueueSendToBackFromISR(QueueHandle_t, const void*, BaseType_t* p) {
    if (p) *p = pdFALSE; return pdTRUE;
}
extern "C" BaseType_t xQueueReceive(QueueHandle_t q, void* buf, TickType_t t) {
    if (g_xQueueReceiveOverride) return g_xQueueReceiveOverride(q, buf, t);
    return pdFALSE;
}
extern "C" UBaseType_t uxQueueSpacesAvailable(QueueHandle_t) { return 4; }
extern "C" UBaseType_t uxQueueMessagesWaiting(QueueHandle_t) { return 0; }

/* ── Task stubs ─────────────────────────────────────────────────────────── */
extern "C" TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char*, uint32_t, void*,
                                           UBaseType_t, StackType_t*, StaticTask_t* t) {
    return (TaskHandle_t)t;
}
extern "C" void vTaskDelay(TickType_t) {}
extern "C" void vTaskDelete(TaskHandle_t) {}
extern "C" void vTaskDelayUntil(TickType_t* prev, TickType_t inc) {
    if (prev) *prev += inc;
}
extern "C" BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdTRUE; }
extern "C" uint32_t   ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }

/* ── Timer stubs (store callback so tests can invoke it) ────────────────── */
static TimerCallbackFunction_t s_timer_cb = nullptr;
static void*                   s_timer_id = nullptr;
static TimerHandle_t           s_timer_handle = nullptr;

extern "C" TimerHandle_t xTimerCreateStatic(const char*, TickType_t, UBaseType_t,
                                             void* pvTimerID,
                                             TimerCallbackFunction_t pxCallbackFunction,
                                             StaticTimer_t* pxTimerBuffer) {
    s_timer_cb     = pxCallbackFunction;
    s_timer_id     = pvTimerID;
    s_timer_handle = (TimerHandle_t)pxTimerBuffer;
    return s_timer_handle;
}
extern "C" BaseType_t xTimerStart(TimerHandle_t, TickType_t) { return pdTRUE; }
extern "C" BaseType_t xTimerStop(TimerHandle_t, TickType_t)  { return pdTRUE; }
extern "C" void*      pvTimerGetTimerID(TimerHandle_t)       { return s_timer_id; }

/* ── Test helper: fire the stored timer callback ───────────────────────── */
void test_fire_timer_callback() {
    if (s_timer_cb && s_timer_handle) {
        s_timer_cb(s_timer_handle);
    }
}

/* ── CMSIS-OS stub ──────────────────────────────────────────────────────── */
extern "C" osStatus_t osDelay(uint32_t) { return 0; }

/* ── Semaphore stubs (single-threaded host: take/give are no-ops) ─────── */
#include "semphr.h"

static int s_lock_balance = 0;

extern "C" SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* p) {
    return (SemaphoreHandle_t)p;
}
extern "C" SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* p) {
    return (SemaphoreHandle_t)p;
}
extern "C" SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static int dummy = 0;
    return (SemaphoreHandle_t)&dummy;
}
extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
    ++s_lock_balance;
    return pdTRUE;
}
extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
    --s_lock_balance;
    return pdTRUE;
}
extern "C" void vSemaphoreDelete(SemaphoreHandle_t) {}
