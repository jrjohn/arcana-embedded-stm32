#pragma once
/* Included after portmacro.h, so BaseType_t is already defined */
#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE
/* Production code uses pdMS_TO_TICKS(ms) — host treats 1 tick == 1 ms. */
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
