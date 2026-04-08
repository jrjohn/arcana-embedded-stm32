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
extern GPIO_TypeDef* const GPIOA;
extern GPIO_TypeDef* const GPIOB;
extern GPIO_TypeDef* const GPIOC;
extern GPIO_TypeDef* const GPIOD;
extern GPIO_TypeDef* const GPIOE;
extern GPIO_TypeDef* const GPIOF;
extern GPIO_TypeDef* const GPIOG;
#define GPIO_PIN_0       ((uint16_t)0x0001)
#define GPIO_PIN_1       ((uint16_t)0x0002)
#define GPIO_PIN_2       ((uint16_t)0x0004)
#define GPIO_PIN_3       ((uint16_t)0x0008)
#define GPIO_PIN_4       ((uint16_t)0x0010)
#define GPIO_PIN_5       ((uint16_t)0x0020)
#define GPIO_PIN_6       ((uint16_t)0x0040)
#define GPIO_PIN_7       ((uint16_t)0x0080)
#define GPIO_PIN_8       ((uint16_t)0x0100)
#define GPIO_PIN_9       ((uint16_t)0x0200)
#define GPIO_PIN_10      ((uint16_t)0x0400)
#define GPIO_PIN_11      ((uint16_t)0x0800)
#define GPIO_PIN_12      ((uint16_t)0x1000)
#define GPIO_PIN_13      ((uint16_t)0x2000)
#define GPIO_PIN_14      ((uint16_t)0x4000)
#define GPIO_PIN_15      ((uint16_t)0x8000)
#define GPIO_PIN_RESET   0
#define GPIO_PIN_SET     1
typedef int GPIO_PinState;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state);

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

/* USART surface — Hc08Ble + Esp8266 drivers use HAL_UART for transmit
 * and read raw SR/DR for ISR-driven receive. Stub provides minimal
 * type-compatible registers. */
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;
extern USART_TypeDef* const USART1;
extern USART_TypeDef* const USART2;
extern USART_TypeDef* const USART3;
#define USART_SR_RXNE  ((uint32_t)0x0020)
#define USART_SR_IDLE  ((uint32_t)0x0010)
#define USART_SR_TC    ((uint32_t)0x0040)
#define USART_SR_TXE   ((uint32_t)0x0080)
#define USART_SR_ORE   ((uint32_t)0x0008)
#define USART_CR1_UE   ((uint32_t)0x2000)
#define USART_FLAG_RXNE USART_SR_RXNE
#define USART_FLAG_IDLE USART_SR_IDLE

/* CoreDebug — I2cBus enables DWT cycle counter for delayUs */
typedef struct {
    volatile uint32_t DHCSR;
    volatile uint32_t DCRSR;
    volatile uint32_t DCRDR;
    volatile uint32_t DEMCR;
} CoreDebug_Type;
extern CoreDebug_Type* const CoreDebug;
#define CoreDebug_DEMCR_TRCENA_Msk ((uint32_t)0x01000000)
#define DWT_CTRL_CYCCNTENA_Msk     ((uint32_t)1)

/* GPIO_MODE_OUTPUT_OD — open-drain mode used by I2cBus */
#define GPIO_MODE_OUTPUT_OD ((uint32_t)0x11)

typedef struct UART_InitType {
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
    uint32_t Mode;
    uint32_t HwFlowCtl;
    uint32_t OverSampling;
} UART_InitTypeDef;

typedef struct {
    USART_TypeDef* Instance;
    UART_InitTypeDef Init;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B   0x0000U
#define UART_STOPBITS_1      0x0000U
#define UART_PARITY_NONE     0x0000U
#define UART_MODE_TX_RX      0x000CU
#define UART_HWCONTROL_NONE  0x0000U
#define UART_OVERSAMPLING_16 0x0000U
#define UART_IT_RXNE         ((uint32_t)1)
#define UART_IT_IDLE         ((uint32_t)2)

typedef int HAL_StatusTypeDef;
#define HAL_OK     0
#define HAL_ERROR  1
#define HAL_BUSY   2
#define HAL_TIMEOUT 3

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* huart);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef* huart);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef* huart, uint8_t* data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef* huart, uint8_t* data, uint16_t len, uint32_t timeout);

#define __HAL_UART_ENABLE_IT(...) ((void)0)
#define __HAL_UART_DISABLE_IT(...) ((void)0)
#define __HAL_UART_GET_FLAG(...) (0)
#define __HAL_UART_CLEAR_FLAG(...) ((void)0)

/* GPIO init / NVIC stubs */
typedef struct {
    uint16_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;
#define GPIO_MODE_OUTPUT_PP ((uint32_t)0x01)
#define GPIO_MODE_AF_PP     ((uint32_t)0x02)
#define GPIO_MODE_INPUT     ((uint32_t)0x00)
#define GPIO_NOPULL         ((uint32_t)0x00)
#define GPIO_PULLUP         ((uint32_t)0x01)
#define GPIO_SPEED_FREQ_HIGH ((uint32_t)0x03)
#define GPIO_SPEED_HIGH      GPIO_SPEED_FREQ_HIGH
void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);

#define __HAL_RCC_GPIOA_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_GPIOD_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_GPIOE_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_GPIOG_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_USART1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART3_CLK_ENABLE() ((void)0)

typedef int IRQn_Type;
#define USART1_IRQn 37
#define USART2_IRQn 38
#define USART3_IRQn 39
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t pri, uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);

/* portYIELD_FROM_ISR — host no-op */
#define portYIELD_FROM_ISR(woken) ((void)(woken))

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

/* RTC surface — production SystemClock.hpp reads/writes RTC->CNTH/CNTL +
 * uses RTC->CRL config bits. The host stub backs this with a single uint32_t
 * "counter" stored in test_hal_stub.cpp; CNTH/CNTL projections of that
 * counter let the production register-aliasing code work unchanged. */
typedef struct {
    volatile uint16_t CRL;
    uint16_t _pad_crl;
    volatile uint16_t CRH;
    uint16_t _pad_crh;
    volatile uint16_t PRLH;
    uint16_t _pad_prlh;
    volatile uint16_t PRLL;
    uint16_t _pad_prll;
    volatile uint16_t DIVH;
    uint16_t _pad_divh;
    volatile uint16_t DIVL;
    uint16_t _pad_divl;
    volatile uint16_t CNTH;
    uint16_t _pad_cnth;
    volatile uint16_t CNTL;
    uint16_t _pad_cntl;
    volatile uint16_t ALRH;
    uint16_t _pad_alrh;
    volatile uint16_t ALRL;
    uint16_t _pad_alrl;
} RTC_TypeDef;
extern RTC_TypeDef* const RTC;
#define RTC_CRL_RTOFF   ((uint16_t)0x0020)
#define RTC_CRL_CNF     ((uint16_t)0x0010)
#define RTC_CRL_RSF     ((uint16_t)0x0008)

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
