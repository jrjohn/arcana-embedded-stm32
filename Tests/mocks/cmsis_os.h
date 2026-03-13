#pragma once
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

typedef uint32_t osStatus_t;
#define osPriorityAboveNormal  ((UBaseType_t)3)
#define osPriorityNormal       ((UBaseType_t)2)

#ifdef __cplusplus
extern "C" {
#endif
osStatus_t osDelay(uint32_t millisec);
#ifdef __cplusplus
}
#endif
