/**
 * @file bl_uart.c
 * @brief Bootloader USART1 debug output (minimal, no printf)
 */

#include "bl_uart.h"
#include "stm32f1xx_hal.h"

static UART_HandleTypeDef huart1;

void bl_uart_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

void bl_print(const char* str)
{
    while (*str) {
        while (!(USART1->SR & USART_SR_TXE)) {}
        USART1->DR = (uint8_t)*str++;
    }
    /* Wait for transmission complete */
    while (!(USART1->SR & USART_SR_TC)) {}
}

static const char hex_chars[] = "0123456789ABCDEF";

void bl_print_hex(const char* prefix, unsigned int val)
{
    bl_print(prefix);
    char buf[11]; /* "0x" + 8 hex + '\0' */
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_chars[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    bl_print(buf);
}

/* Retarget _write for any accidental printf usage */
int _write(int fd, char* ptr, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        while (!(USART1->SR & USART_SR_TXE)) {}
        USART1->DR = (uint8_t)ptr[i];
    }
    return len;
}
