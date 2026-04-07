/**
 * @file stm32f1xx_hal.h (host test stub)
 * @brief Minimal CMSIS/HAL surface needed by command-bridge / device-key host tests.
 *
 * Real header lives under STM32CubeIDE; on host we substitute this so files
 * such as DeviceKey.hpp, Commands.hpp, CommandBridgeImpl.cpp can compile.
 *
 * Provides:
 *   - UID_BASE pointing at a 12-byte fake UID buffer (host-resident)
 *   - SysTick / DWT register stubs (used by ueccRng)
 *   - HAL_GetTick prototype
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 12-byte fake hardware UID lives in test_hal_stub.cpp */
extern const uint8_t arcana_test_fake_uid[12];

/* CMSIS-style register block stubs (only the fields the code reads) */
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_Type;

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t CYCCNT;
} DWT_Type;

extern SysTick_Type* const SysTick;
extern DWT_Type*     const DWT;

uint32_t HAL_GetTick(void);

/* GPIO surface — referenced by other targets but not by CommandBridgeImpl;
 * keep it minimal so AtsStorageServiceImpl could later compile against this. */
typedef struct {
    volatile uint32_t IDR;
} GPIO_TypeDef;
extern GPIO_TypeDef* const GPIOC;
#define GPIO_PIN_13      ((uint16_t)0x2000)
#define GPIO_PIN_RESET   0
#define GPIO_PIN_SET     1
typedef int GPIO_PinState;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin);

/* BKP (backup register) — OtaServiceImpl::setOtaFlag writes DR2/DR3
 * to signal the bootloader on next reset. */
typedef struct {
    volatile uint32_t RTCCR;
    volatile uint32_t CR;
    volatile uint32_t CSR;
    volatile uint16_t DR1;
    uint16_t _pad1;
    volatile uint16_t DR2;
    uint16_t _pad2;
    volatile uint16_t DR3;
    uint16_t _pad3;
    volatile uint16_t DR4;
    uint16_t _pad4;
    /* … other DRn omitted; not used by code under test */
} BKP_TypeDef;
extern BKP_TypeDef* const BKP;

/* HAL macros + functions referenced by OtaServiceImpl::setOtaFlag */
#define __HAL_RCC_PWR_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_BKP_CLK_ENABLE()  ((void)0)
void HAL_PWR_EnableBkUpAccess(void);
void NVIC_SystemReset(void);

/* SDIO surface — HttpUploadServiceImpl::streamFileBody pokes DCTRL/ICR
 * directly between f_read calls (DMA reset). Stub fields = 0. */
typedef struct {
    volatile uint32_t POWER;
    volatile uint32_t CLKCR;
    volatile uint32_t ARG;
    volatile uint32_t CMD;
    volatile uint32_t RESPCMD;
    volatile uint32_t RESP1;
    volatile uint32_t RESP2;
    volatile uint32_t RESP3;
    volatile uint32_t RESP4;
    volatile uint32_t DTIMER;
    volatile uint32_t DLEN;
    volatile uint32_t DCTRL;
    volatile uint32_t DCOUNT;
    volatile uint32_t STA;
    volatile uint32_t ICR;
    volatile uint32_t MASK;
    volatile uint32_t FIFOCNT;
    volatile uint32_t FIFO;
} SDIO_TypeDef;
extern SDIO_TypeDef* const SDIO;

/* ADC surface — RegistrationServiceImpl::ueccRng() XORs ADC1->DR for entropy */
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SMPR1;
    volatile uint32_t SMPR2;
    volatile uint32_t JOFR1;
    volatile uint32_t JOFR2;
    volatile uint32_t JOFR3;
    volatile uint32_t JOFR4;
    volatile uint32_t HTR;
    volatile uint32_t LTR;
    volatile uint32_t SQR1;
    volatile uint32_t SQR2;
    volatile uint32_t SQR3;
    volatile uint32_t JSQR;
    volatile uint32_t JDR1;
    volatile uint32_t JDR2;
    volatile uint32_t JDR3;
    volatile uint32_t JDR4;
    volatile uint32_t DR;
} ADC_TypeDef;
extern ADC_TypeDef* const ADC1;

#ifdef __cplusplus
}
#endif

/* ── UID_BASE: must look like a (uintptr_t)-castable address. ────────────── */
/* Real CMSIS header defines as 0x1FFFF7E8U; on host we point at the fake
 * UID array so reinterpret_cast<const uint8_t*>(UID_BASE) is dereferenceable. */
#ifndef UID_BASE
#define UID_BASE ((uintptr_t)arcana_test_fake_uid)
#endif

/* SystemCoreClock — declared at C++ linkage to match CMSIS system header */
extern uint32_t SystemCoreClock;
