/**
  **************************************************************************
  * @file     uart_drv.h
  * @brief    USART1 driver for Qi chip communication (115200-8N1)
  **************************************************************************
  *
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __UART_DRV_H
#define __UART_DRV_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* exported constants --------------------------------------------------------*/

/**
 * @brief  UART frame format constants
 * @note   frame: 0x55 0xAA + LEN + CMD + DATA + SEQ + CS
 *         (header sync bytes, used by upper layer frame parser)
 */
#define UART_FRAME_HEADER_1             0x55U
#define UART_FRAME_HEADER_2             0xAAU

/* exported types ------------------------------------------------------------*/

/**
 * @brief  UART RX callback function type
 * @param  byte: received byte from USART1
 */
typedef void (*uart_rx_callback_t)(uint8_t byte);

/* exported functions -------------------------------------------------------*/

/**
 * @brief  initialize USART1 for Qi chip communication
 * @note   configures PB6(TX) and PB7(RX) with AF7 mux,
 *         baud rate 115200, 8 data bits, no parity, 1 stop bit,
 *         enables RX interrupt for byte-by-byte reception.
 * @param  none
 * @retval none
 */
void uart_drv_init(void);

/**
 * @brief  transmit data via USART1 (blocking)
 * @param  data: pointer to data buffer to transmit
 * @param  len:  number of bytes to transmit
 * @retval none
 */
void uart_drv_send(uint8_t *data, uint16_t len);

/**
 * @brief  register a callback for received bytes
 * @note   the callback is invoked directly from USART1_IRQHandler
 * @param  cb: callback function pointer, or NULL to unregister
 * @retval none
 */
void uart_drv_register_rx_callback(uart_rx_callback_t cb);

/**
 * @brief  USART1 RX interrupt handler (called from USART1_IRQHandler)
 * @note   reads received byte and invokes registered callback.
 * @param  none
 * @retval none
 */
void uart_drv_rx_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_DRV_H */
