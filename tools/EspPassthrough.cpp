/**
 * @file EspPassthrough.cpp
 * @brief UART passthrough: USART1 (USB) <-> USART3 (ESP8266)
 *
 * Used for:
 * - Checking ESP8266 AT version (AT+GMR)
 * - Flashing ESP8266 firmware via esptool.py
 *
 * Build: replace F103App.cpp with this file in the build system
 * (or temporarily rename F103App.cpp and this file)
 *
 * Usage:
 * 1. Flash this to STM32
 * 2. For AT commands: just open serial 115200
 * 3. For ESP8266 flash: ground GPIO0 on ESP8266 module, then use esptool.py
 *
 * LED indicators:
 * - Green ON:  passthrough active
 * - Red blink: data flowing USART1→USART3
 * - Blue blink: data flowing USART3→USART1
 */

#include "App.hpp"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdio>

// USART3 handle (ESP8266)
static UART_HandleTypeDef huart3;

// Ring buffers for ISR → task forwarding
static volatile uint8_t u1_buf[512];  // USART1 RX → USART3 TX
static volatile uint16_t u1_head = 0, u1_tail = 0;
static volatile uint8_t u3_buf[512];  // USART3 RX → USART1 TX
static volatile uint16_t u3_head = 0, u3_tail = 0;

// Forward: USART3 ISR
extern "C" void USART3_IRQHandler(void) {
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) {
        uint8_t b = (uint8_t)(huart3.Instance->DR & 0xFF);
        uint16_t next = (u3_head + 1) % sizeof(u3_buf);
        if (next != u3_tail) {
            u3_buf[u3_head] = b;
            u3_head = next;
        }
    }
    // Clear overrun if any
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE)) {
        volatile uint32_t tmp = huart3.Instance->SR;
        tmp = huart3.Instance->DR;
        (void)tmp;
    }
}

// Forward: USART1 RX byte (called from existing USART1 ISR)
// We hook into the existing __io_putchar / IRQ mechanism
extern UART_HandleTypeDef huart1;
extern "C" void USART1_IRQHandler(void) {
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        uint8_t b = (uint8_t)(huart1.Instance->DR & 0xFF);
        uint16_t next = (u1_head + 1) % sizeof(u1_buf);
        if (next != u1_tail) {
            u1_buf[u1_head] = b;
            u1_head = next;
        }
    }
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE)) {
        volatile uint32_t tmp = huart1.Instance->SR;
        tmp = huart1.Instance->DR;
        (void)tmp;
    }
}

static void initUsart3() {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};

    // PB10 = USART3_TX
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    // PB11 = USART3_RX
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    // PG13 = CH_PD (enable ESP8266)
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);

    // PG14 = RST
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);

    // USART3 @ 115200
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart3);

    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);

    // Enable USART1 RX interrupt (for passthrough)
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void espPower(bool on) {
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void espReset() {
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// LED helpers (active-low on 野火霸道)
static void ledGreen(bool on) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

namespace arcana {

void App_Init() {}

void App_Run() {
    printf("\r\n=== ESP8266 UART Passthrough ===\r\n");
    printf("USART1 (USB 115200) <-> USART3 (ESP8266 115200)\r\n");
    printf("GPIO: PG13=CH_PD, PG14=RST\r\n\r\n");

    initUsart3();

    // Power on ESP8266
    espPower(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    espReset();

    printf("[PT] ESP8266 powered on + reset\r\n");
    printf("[PT] Ready — type AT commands or use esptool.py\r\n\r\n");

    ledGreen(true);

    // Main loop: forward bytes between USART1 and USART3
    while (true) {
        // USART1 RX → USART3 TX (Mac → ESP8266)
        while (u1_head != u1_tail) {
            uint8_t b = u1_buf[u1_tail];
            u1_tail = (u1_tail + 1) % sizeof(u1_buf);
            while (!(USART3->SR & USART_SR_TXE)) {}
            USART3->DR = b;
        }

        // USART3 RX → USART1 TX (ESP8266 → Mac)
        while (u3_head != u3_tail) {
            uint8_t b = u3_buf[u3_tail];
            u3_tail = (u3_tail + 1) % sizeof(u3_buf);
            while (!(USART1->SR & USART_SR_TXE)) {}
            USART1->DR = b;
        }

        vTaskDelay(1);  // 1 tick (~1ms) yield
    }
}

} // namespace arcana
