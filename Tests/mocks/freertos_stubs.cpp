#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <cstring>
#include <cstdlib>

/* TickCount — always returns 0 in test environment */
extern "C" TickType_t xTaskGetTickCount(void) { return 0; }

extern "C" QueueHandle_t xQueueCreateStatic(UBaseType_t, UBaseType_t, uint8_t*, StaticQueue_t* q) {
    return (QueueHandle_t)q;
}
extern "C" BaseType_t xQueueSendToBack(QueueHandle_t, const void*, TickType_t) { return pdFALSE; }
extern "C" BaseType_t xQueueSendToBackFromISR(QueueHandle_t, const void*, BaseType_t* p) {
    if (p) *p = pdFALSE; return pdFALSE;
}
extern "C" BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
extern "C" UBaseType_t uxQueueSpacesAvailable(QueueHandle_t) { return 0; }
extern "C" TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, StackType_t*, StaticTask_t* t) {
    return (TaskHandle_t)t;
}
extern "C" void vTaskDelay(TickType_t) {}
