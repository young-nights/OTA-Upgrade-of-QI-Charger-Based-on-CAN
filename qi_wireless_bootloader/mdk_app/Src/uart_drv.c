/**
  **************************************************************************
  * @file     uart_drv.c
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

/* includes ------------------------------------------------------------------*/
#include "uart_drv.h"

/* private variables ---------------------------------------------------------*/

/** @brief  RX callback function pointer */
static volatile uart_rx_callback_t rx_callback = (uart_rx_callback_t)0;

/* exported functions --------------------------------------------------------*/

/**
 * @brief  initialize USART1 for Qi chip communication
 * @note   configures PB6(TX) and PB7(RX) with AF7 mux,
 *         baud rate 115200, 8 data bits, no parity, 1 stop bit,
 *         enables RX interrupt for byte-by-byte reception.
 * @param  none
 * @retval none
 */
void uart_drv_init(void)
{
  gpio_init_type gpio_init_struct;

  /* enable peripheral clocks */
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);

  /* configure PB6 (USART1_TX) as alternate function push-pull */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_6;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE6, GPIO_MUX_7);

  /* configure PB7 (USART1_RX) as input with pull-up */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_7;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE7, GPIO_MUX_7);

  /* reset USART peripheral */
  usart_reset(USART1);

  /* configure USART1: 115200 baud, 8 data bits, 1 stop bit, no parity */
  usart_init(USART1, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_parity_selection_config(USART1, USART_PARITY_NONE);

  /* enable transmitter and receiver */
  usart_transmitter_enable(USART1, TRUE);
  usart_receiver_enable(USART1, TRUE);

  /* enable RX buffer full interrupt */
  usart_interrupt_enable(USART1, USART_RDBF_INT, TRUE);

  /* configure NVIC for USART1 */
  nvic_irq_enable(USART1_IRQn, 1, 0);

  /* enable USART1 */
  usart_enable(USART1, TRUE);

  /* reset callback */
  rx_callback = (uart_rx_callback_t)0;
}

/**
 * @brief  transmit data via USART1 (blocking)
 * @param  data: pointer to data buffer to transmit
 * @param  len:  number of bytes to transmit
 * @retval none
 */
void uart_drv_send(uint8_t *data, uint16_t len)
{
  uint16_t i;

  if (data == (uint8_t *)0)
  {
    return;
  }

  for (i = 0; i < len; i++)
  {
    /* wait until transmit data buffer is empty */
    while (usart_flag_get(USART1, USART_TDBE_FLAG) == RESET)
    {
    }

    /* send byte */
    usart_data_transmit(USART1, (uint16_t)data[i]);
  }

  /* wait until transmit is complete */
  while (usart_flag_get(USART1, USART_TDC_FLAG) == RESET)
  {
  }
}

/**
 * @brief  register a callback for received bytes
 * @note   the callback is invoked directly from USART1_IRQHandler
 * @param  cb: callback function pointer, or NULL to unregister
 * @retval none
 */
void uart_drv_register_rx_callback(uart_rx_callback_t cb)
{
  rx_callback = cb;
}

/**
 * @brief  USART1 RX interrupt handler (called from USART1_IRQHandler)
 * @note   reads received byte and invokes registered callback.
 * @param  none
 * @retval none
 */
void uart_drv_rx_irq_handler(void)
{
  uint16_t data;

  /* check RX buffer full flag */
  if (usart_flag_get(USART1, USART_RDBF_FLAG) != RESET)
  {
    /* read received data (also clears the flag) */
    data = usart_data_receive(USART1);

    /* invoke callback with received byte */
    if (rx_callback != (uart_rx_callback_t)0)
    {
      rx_callback((uint8_t)(data & 0xFF));
    }
  }
}
