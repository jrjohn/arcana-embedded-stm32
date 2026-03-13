#pragma once
#include <stdint.h>
#include <stddef.h>

/* Basic types */
typedef long          BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t      TickType_t;

#define pdTRUE    ((BaseType_t)1)
#define pdFALSE   ((BaseType_t)0)
#define pdPASS    pdTRUE
#define pdFAIL    pdFALSE
#define portMAX_DELAY ((TickType_t)0xFFFFFFFF)

#define configASSERT(x) (void)(x)

/* TickCount stub */
#ifdef __cplusplus
extern "C" {
#endif
TickType_t xTaskGetTickCount(void);
#ifdef __cplusplus
}
#endif
