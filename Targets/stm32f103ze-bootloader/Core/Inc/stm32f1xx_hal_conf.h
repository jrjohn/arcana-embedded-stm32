/**
 * @file stm32f1xx_hal_conf.h
 * @brief Minimal HAL configuration for OTA bootloader
 *
 * Only enables modules needed: RCC, GPIO, Flash, SD, UART, DMA, PWR, Cortex
 */
#ifndef __STM32F1xx_HAL_CONF_H
#define __STM32F1xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Module Selection — minimal for bootloader */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SD_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* Oscillator Values */
#if !defined(HSE_VALUE)
  #define HSE_VALUE    8000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif
#if !defined(HSI_VALUE)
  #define HSI_VALUE    8000000U
#endif
#if !defined(LSI_VALUE)
  #define LSI_VALUE    40000U
#endif
#if !defined(LSE_VALUE)
  #define LSE_VALUE    32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    5000U
#endif

/* System Configuration */
#define VDD_VALUE                    3300U
#define TICK_INT_PRIORITY            0U   /* Highest — no RTOS */
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U

#define USE_SPI_CRC                  0U

/* No register callbacks */
#define USE_HAL_SD_REGISTER_CALLBACKS   0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U

/* No assertions in bootloader */
#define assert_param(expr) ((void)0U)

/* Module Includes */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f1xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f1xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f1xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f1xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f1xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f1xx_hal_pwr.h"
#endif
#ifdef HAL_SD_MODULE_ENABLED
#include "stm32f1xx_hal_sd.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f1xx_hal_uart.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_CONF_H */
