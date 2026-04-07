#pragma once
/* Host-test mock for FreeRTOS semphr.h
 *
 * Single-threaded host tests don't need real mutex semantics — Take/Give are
 * no-ops, but we keep a counter so test code can assert balanced lock/unlock.
 * Mutex creation returns a non-null cookie so callers see "success".
 */
#include "FreeRTOS.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* SemaphoreHandle_t;

typedef struct StaticSemaphore_t {
    uint8_t  opaque[8];
} StaticSemaphore_t;

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* pxSemaphoreBuffer);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* pxSemaphoreBuffer);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void              vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

#ifdef __cplusplus
}
#endif
