#pragma once
#include "FreeRTOS.h"

typedef struct QueueDefinition {} StaticQueue_t;
typedef void* QueueHandle_t;

#ifdef __cplusplus
extern "C" {
#endif
QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength, UBaseType_t uxItemSize,
                                  uint8_t* pucQueueStorageBuffer, StaticQueue_t* pxStaticQueue);
BaseType_t    xQueueSendToBack(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait);
BaseType_t    xQueueSendToBackFromISR(QueueHandle_t xQueue, const void* pvItemToQueue, BaseType_t* pxHigherPriorityTaskWoken);
BaseType_t    xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait);
UBaseType_t   uxQueueSpacesAvailable(QueueHandle_t xQueue);
#ifdef __cplusplus
}
#endif

/* Additional queue functions needed by Observable.cpp */
#ifdef __cplusplus
extern "C" {
#endif
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);
#ifdef __cplusplus
}
#endif

/* FreeRTOS aliases — commonly defined as macros in real headers */
#define xQueueSend(q, i, t)           xQueueSendToBack((q), (i), (t))
#define xQueueSendFromISR(q, i, p)    xQueueSendToBackFromISR((q), (i), (p))
#define xQueueSendToFront(q, i, t)    xQueueSendToBack((q), (i), (t))
#define xQueueMessagesWaiting(q)      uxQueueMessagesWaiting((q))
