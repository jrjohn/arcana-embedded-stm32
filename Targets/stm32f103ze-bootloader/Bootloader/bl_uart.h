/**
 * @file bl_uart.h
 * @brief Bootloader USART1 debug output
 */
#ifndef BL_UART_H
#define BL_UART_H

/**
 * @brief Initialize USART1 at 115200 8N1
 */
void bl_uart_init(void);

/**
 * @brief Print string to USART1 (blocking)
 */
void bl_print(const char* str);

/**
 * @brief Print string + hex32 value
 */
void bl_print_hex(const char* prefix, unsigned int val);

#endif /* BL_UART_H */
