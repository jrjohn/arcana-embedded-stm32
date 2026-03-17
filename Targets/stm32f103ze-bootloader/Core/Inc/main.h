/**
 * @file main.h
 * @brief Bootloader main header
 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* LED pins — 野火霸道 V2 (active-low, common anode) */
#define LED_R_PIN    GPIO_PIN_5
#define LED_R_PORT   GPIOB
#define LED_G_PIN    GPIO_PIN_0
#define LED_G_PORT   GPIOB
#define LED_B_PIN    GPIO_PIN_1
#define LED_B_PORT   GPIOB

/* KEY1 — PA0 (active-low with external pull-up) */
#define KEY1_PIN     GPIO_PIN_0
#define KEY1_PORT    GPIOA

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
