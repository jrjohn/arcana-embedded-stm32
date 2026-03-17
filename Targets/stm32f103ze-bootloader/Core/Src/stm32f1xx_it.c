/**
 * @file stm32f1xx_it.c
 * @brief Bootloader interrupt handlers (minimal, bare-metal)
 */

#include "main.h"

void NMI_Handler(void)       { while (1) {} }
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void)  { while (1) {} }
void UsageFault_Handler(void){ while (1) {} }
void SVC_Handler(void)       { }
void DebugMon_Handler(void)  { }
void PendSV_Handler(void)    { }

void SysTick_Handler(void)
{
    HAL_IncTick();
}
