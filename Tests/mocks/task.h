#pragma once
#include "FreeRTOS.h"

typedef void* TaskHandle_t;
typedef struct tskTaskControlBlock {} StaticTask_t;

typedef void (*TaskFunction_t)(void*);

#define tskIDLE_PRIORITY ((UBaseType_t)0)

#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode, const char* pcName,
                                uint32_t ulStackDepth, void* pvParameters,
                                UBaseType_t uxPriority, StackType_t* puxStackBuffer,
                                StaticTask_t* pxTaskBuffer);
void         vTaskDelay(TickType_t xTicksToDelay);
#ifdef __cplusplus
}
#endif
