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

/* GPIOC stub — reads return "not pressed" (RESET = 0). The CommandBridge
 * doesn't poll GPIO, but linking AtsStorageServiceImpl later will need it. */
static GPIO_TypeDef sGpiocStorage = {0};
GPIO_TypeDef* const GPIOC = &sGpiocStorage;

/* ADC1 stub — DR field reads as 0; only used as entropy source mix-in. */
static ADC_TypeDef sAdc1Storage = {};
ADC_TypeDef* const ADC1 = &sAdc1Storage;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* /*port*/, uint16_t /*pin*/) {
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
