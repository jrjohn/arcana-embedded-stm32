#pragma once
#include <stdint.h>
#include <stddef.h>
#include "portmacro.h"   /* defines StackType_t, BaseType_t, UBaseType_t, TickType_t */
#include "projdefs.h"    /* defines pdTRUE, pdFALSE, pdPASS, pdFAIL */
#include "FreeRTOSConfig.h" /* defines configTICK_RATE_HZ etc — production
                              FreeRTOS.h pulls this in transitively */

#define configASSERT(x) (void)(x)
#define portMAX_DELAY   ((TickType_t)0xFFFFFFFFUL)

#ifdef __cplusplus
extern "C" {
#endif
TickType_t xTaskGetTickCount(void);
#ifdef __cplusplus
}
#endif
