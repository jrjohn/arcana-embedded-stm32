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
void         vTaskDelete(TaskHandle_t xTaskToDelete);
void         vTaskDelayUntil(TickType_t* pxPreviousWakeTime,
                             TickType_t xTimeIncrement);
BaseType_t   xTaskNotifyGive(TaskHandle_t xTaskToNotify);
uint32_t     ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
#ifdef __cplusplus
}
#endif

/* Critical section + ISR mask helpers — host stubs are no-ops */
#define taskENTER_CRITICAL()                  ((void)0)
#define taskEXIT_CRITICAL()                   ((void)0)
#define portSET_INTERRUPT_MASK_FROM_ISR()     ((uint32_t)0)
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)  ((void)(x))
