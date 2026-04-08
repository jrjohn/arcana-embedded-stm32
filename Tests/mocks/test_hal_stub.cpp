/**
 * @file test_hal_stub.cpp
 * @brief Host-side definitions backing stm32f1xx_hal.h stub.
 *
 * Provides storage for the fake hardware UID and the SysTick/DWT/GPIO
 * register surfaces referenced by code-under-test (CommandBridgeImpl,
 * DeviceKey, Commands, AtsStorageServiceImpl).
 */
#include "stm32f1xx_hal.h"

extern "C" {

/* 12-byte fake UID — deterministic so tests get repeatable derived keys */
const uint8_t arcana_test_fake_uid[12] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02,
    0x03, 0x04, 0xC0, 0xFF, 0xEE, 0x00,
};

/* SysTick / DWT register storage — values are read by ueccRng() but never
 * required to be unique; zero is fine because the seed already mixes
 * caller-controlled tick + counter. */
static SysTick_Type sSysTickStorage = {0, 0, 0, 0};
static DWT_Type     sDwtStorage     = {0, 0};
SysTick_Type* const SysTick = &sSysTickStorage;
DWT_Type*     const DWT     = &sDwtStorage;

/* CoreDebug — I2cBus enables DWT cycle counter via DEMCR.TRCENA */
static CoreDebug_Type sCoreDebugStorage = {};
CoreDebug_Type* const CoreDebug = &sCoreDebugStorage;

/* GPIO port stubs — reads return "not pressed" (RESET = 0). */
static GPIO_TypeDef sGpioaStorage = {0};
static GPIO_TypeDef sGpiobStorage = {0};
static GPIO_TypeDef sGpiocStorage = {0};
static GPIO_TypeDef sGpiodStorage = {0};
static GPIO_TypeDef sGpioeStorage = {0};
static GPIO_TypeDef sGpiofStorage = {0};
static GPIO_TypeDef sGpiogStorage = {0};
GPIO_TypeDef* const GPIOA = &sGpioaStorage;
GPIO_TypeDef* const GPIOB = &sGpiobStorage;
GPIO_TypeDef* const GPIOC = &sGpiocStorage;
GPIO_TypeDef* const GPIOD = &sGpiodStorage;
GPIO_TypeDef* const GPIOE = &sGpioeStorage;
GPIO_TypeDef* const GPIOF = &sGpiofStorage;
GPIO_TypeDef* const GPIOG = &sGpiogStorage;

void HAL_GPIO_WritePin(GPIO_TypeDef* /*port*/, uint16_t /*pin*/, GPIO_PinState /*state*/) {
    /* no-op — Led + other services call this; tests don't inspect GPIO state */
}

/* ADC1 stub — DR field reads as 0; only used as entropy source mix-in. */
static ADC_TypeDef sAdc1Storage = {};
ADC_TypeDef* const ADC1 = &sAdc1Storage;

/* SDIO peripheral stub — HttpUploadServiceImpl writes DCTRL/ICR. */
static SDIO_TypeDef sSdioStorage = {};
SDIO_TypeDef* const SDIO = &sSdioStorage;

/* BKP peripheral stub — OtaServiceImpl::setOtaFlag writes DR2/DR3.
 * NVIC_SystemReset would normally reboot the MCU; on host we use a flag
 * + setjmp that the test can inspect. */
static BKP_TypeDef sBkpStorage = {};
BKP_TypeDef* const BKP = &sBkpStorage;

/* USART peripheral stubs — Hc08Ble + Esp8266 drivers reference USART1/2/3
 * directly. Provide register storage so SR/DR access doesn't segfault.
 * Initialise SR with TXE | TC set so EspFlasher's busy-wait spin loops
 * (`while (!(SR & TXE)) {}`) terminate immediately. */
static USART_TypeDef sUsart1Storage = { USART_SR_TXE | USART_SR_TC, 0, 0, 0, 0, 0, 0 };
static USART_TypeDef sUsart2Storage = { USART_SR_TXE | USART_SR_TC, 0, 0, 0, 0, 0, 0 };
static USART_TypeDef sUsart3Storage = { USART_SR_TXE | USART_SR_TC, 0, 0, 0, 0, 0, 0 };
USART_TypeDef* const USART1 = &sUsart1Storage;
USART_TypeDef* const USART2 = &sUsart2Storage;
USART_TypeDef* const USART3 = &sUsart3Storage;

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* /*huart*/) { return HAL_OK; }
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef* /*huart*/) { return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef* /*huart*/,
                                    uint8_t* /*data*/, uint16_t /*len*/,
                                    uint32_t /*timeout*/) { return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef* /*huart*/,
                                   uint8_t* /*data*/, uint16_t /*len*/,
                                   uint32_t /*timeout*/) { return HAL_OK; }

void HAL_GPIO_Init(GPIO_TypeDef* /*port*/, GPIO_InitTypeDef* /*init*/) {}
void HAL_NVIC_SetPriority(IRQn_Type /*irq*/, uint32_t /*pri*/, uint32_t /*sub*/) {}
void HAL_NVIC_EnableIRQ(IRQn_Type /*irq*/) {}
void HAL_NVIC_DisableIRQ(IRQn_Type /*irq*/) {}

/* RTC peripheral stub — production SystemClock.hpp R/Ws CNTH/CNTL via the
 * register-aliasing pattern. Initialise CRL with the "always-ready" bits
 * set so the rtcEnterConfig/rtcExitConfig spin loops never block. */
static RTC_TypeDef sRtcStorage = {
    /*CRL*/   RTC_CRL_RTOFF | RTC_CRL_RSF, 0,
    /*CRH*/   0, 0,
    /*PRLH*/  0, 0, /*PRLL*/ 0, 0,
    /*DIVH*/  0, 0, /*DIVL*/ 0, 0,
    /*CNTH*/  0, 0, /*CNTL*/ 0, 0,
    /*ALRH*/  0, 0, /*ALRL*/ 0, 0,
};
RTC_TypeDef* const RTC = &sRtcStorage;

void HAL_PWR_EnableBkUpAccess(void) {}

/* Make NVIC_SystemReset a controllable abort point: tests can throw via the
 * exception path to verify the call site is reached without actually
 * resetting the host process. The default no-op behavior lets unit tests
 * proceed past the call (since NVIC_SystemReset is the LAST thing in
 * production startUpdate). */
void NVIC_SystemReset(void) {}

/* HAL_GPIO_ReadPin abort hook — tests can install a counter that throws
 * a sentinel int after N calls so they can break out of taskLoop's
 * infinite for(;;). Default: 0 (disabled) → never throws. Pin override
 * vector lets a test return GPIO_PIN_RESET ("pressed") for specific
 * (port, pin) tuples to drive KEY1/KEY2 branches. */
int      g_hal_gpio_read_abort_after = 0;
int      g_hal_gpio_read_call_count  = 0;
GPIO_PinState (*g_hal_gpio_read_override)(GPIO_TypeDef*, uint16_t) = nullptr;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin) {
    if (g_hal_gpio_read_abort_after > 0 &&
        ++g_hal_gpio_read_call_count >= g_hal_gpio_read_abort_after) {
        throw 1;
    }
    if (g_hal_gpio_read_override) return g_hal_gpio_read_override(port, pin);
    return GPIO_PIN_SET;  /* "released" — KEY2 high = idle */
}

/* Monotonically incrementing tick — different return values across calls
 * exercise nonce/RNG mixing in CommandBridge::encryptAndFrame and ueccRng. */
static uint32_t sFakeTick = 0;
uint32_t HAL_GetTick(void) {
    return ++sFakeTick;
}

} // extern "C"

/* Referenced by AtsStorageServiceImpl::appendRecord (DWT cycle math).
 * CMSIS declares SystemCoreClock without extern "C", so define it at C++
 * linkage to match the real header. */
uint32_t SystemCoreClock = 72000000u;
