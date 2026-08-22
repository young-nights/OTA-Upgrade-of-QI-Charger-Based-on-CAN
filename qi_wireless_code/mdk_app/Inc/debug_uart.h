/**
  **************************************************************************
  * @file     debug_uart.h
  * @brief    USART1 debug heartbeat (PB6=TX, PB7=RX)
  **************************************************************************
  */

#ifndef __DEBUG_UART_H
#define __DEBUG_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f422_426.h"

#define DEBUG_UART_BAUDRATE       115200U
#define DEBUG_UART_PERIOD_MS      500U

/**
 * @brief  initialize USART1 on PB6/PB7 and print a start line
 * @param  tag: short ASCII id, e.g. "APP" or "BOOT" (may be NULL)
 */
void debug_uart_init(const char *tag);

/**
 * @brief  send a NUL-terminated string (timeout-protected, not from ISR)
 */
void debug_uart_puts(const char *s);

/**
 * @brief  periodic heartbeat from the main loop
 * @note   call from while(1). If this stops, the main loop is stuck.
 *         Do not call from SysTick — that would still run when main is dead.
 */
void debug_uart_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEBUG_UART_H */
