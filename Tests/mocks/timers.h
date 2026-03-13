#pragma once
#include "FreeRTOS.h"
#include "task.h"

typedef void* TimerHandle_t;
typedef struct tmrTimerControl {} StaticTimer_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#ifdef __cplusplus
extern "C" {
#endif
TimerHandle_t xTimerCreateStatic(const char* pcTimerName, TickType_t xTimerPeriodInTicks,
                                  UBaseType_t uxAutoReload, void* pvTimerID,
                                  TimerCallbackFunction_t pxCallbackFunction,
                                  StaticTimer_t* pxTimerBuffer);
BaseType_t    xTimerStart(TimerHandle_t xTimer, TickType_t xBlockTime);
BaseType_t    xTimerStop(TimerHandle_t xTimer, TickType_t xBlockTime);
void*         pvTimerGetTimerID(TimerHandle_t xTimer);
#ifdef __cplusplus
}
#endif
