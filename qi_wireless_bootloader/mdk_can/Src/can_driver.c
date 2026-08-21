/**
  **************************************************************************
  * @file     can_driver.c
  * @brief    CAN 2.0B extended frame driver for AT32F426
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
#include "can_driver.h"

/* private define ------------------------------------------------------------*/

/**
 * @brief  CAN bit timing calculation
 * @note   APB1 clock = 180 MHz
 *         bittime_div = 10, CAN clock = 180MHz / 10 = 18 MHz
 *         bit_time = 1 + BTS1 + BTS2 = 1 + 54 + 17 = 72 Tq
 *         bitrate = 18MHz / 72 = 250 kbps
 *         SJW = 1 Tq (minimal for stable communication)
 */
#define CAN_BITTIME_DIV                 10U
#define CAN_BITTIME_SJW                 1U
#define CAN_BITTIME_BTS1                54U
#define CAN_BITTIME_BTS2                17U

/* private variables ---------------------------------------------------------*/

/** @brief  software RX FIFO ring buffer */
static volatile can_rx_frame_t rx_fifo[CAN_DRIVER_RX_FIFO_SIZE];
static volatile uint8_t rx_fifo_head = 0;   /*!< write index (ISR context) */
static volatile uint8_t rx_fifo_tail = 0;   /*!< read index  (main context) */
static volatile uint8_t rx_fifo_count = 0;  /*!< number of pending frames */

/** @brief  RX callback function pointer */
static volatile can_rx_callback_t rx_callback = (can_rx_callback_t)0;

/* private functions ---------------------------------------------------------*/

/**
 * @brief  check if software RX FIFO is full
 * @retval 1 = full, 0 = not full
 */
static uint8_t rx_fifo_is_full(void)
{
  return (rx_fifo_count >= CAN_DRIVER_RX_FIFO_SIZE) ? 1U : 0U;
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  initialize CAN1 peripheral in extended frame mode at 250kbps
 * @note   configures PA11(CAN_RX) and PA12(CAN_TX) with AF mux,
 *         sets up acceptance filter to accept all extended frames,
 *         enables RX and error interrupts.
 * @param  none
 * @retval none
 */
void can_driver_init(void)
{
  gpio_init_type gpio_init_struct;
  can_bittime_type can_bittime_struct;
  can_filter_config_type can_filter_struct;

  /* enable peripheral clocks */
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_CAN1_PERIPH_CLOCK, TRUE);

  /* configure PA11 (CAN_RX) as input with pull-up */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_11;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE11, GPIO_MUX_4);

  /* configure PA12 (CAN_TX) as alternate function push-pull */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_12;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE12, GPIO_MUX_4);

  /* reset CAN peripheral */
  can_reset(CAN1);

  /* set CAN to normal communication mode */
  can_mode_set(CAN1, CAN_MODE_COMMUNICATE);

  /* configure bit timing for 250 kbps */
  can_bittime_default_para_init(&can_bittime_struct);
  can_bittime_struct.bittime_div  = CAN_BITTIME_DIV;
  can_bittime_struct.ac_rsaw_size = CAN_BITTIME_SJW;
  can_bittime_struct.ac_bts1_size = CAN_BITTIME_BTS1;
  can_bittime_struct.ac_bts2_size = CAN_BITTIME_BTS2;
  can_bittime_set(CAN1, &can_bittime_struct);

  /* configure filters to accept only diagnostic requests addressed to this
   * module (physical 0x18DA0D03) and functional broadcast (0x18DB33xx).
   *
   * Filter 0 (mask mode): physical addressing
   *   code = 0x18DA0D03, mask = 0x0000FF00
   *   match: byte SA ignored, TA must be 0x0D => 0x18DA0Dxx
   *
   * Filter 1 (mask mode): functional addressing
   *   code = 0x18DB3300, mask = 0x1FFFFF00
   *   match: SA ignored, TA must be 0x33 => 0x18DB33xx
   */
  can_filter_default_para_init(&can_filter_struct);
  can_filter_struct.code_para.id         = 0x18DA0D03U;
  can_filter_struct.code_para.id_type    = CAN_ID_EXTENDED;
  can_filter_struct.code_para.frame_type = CAN_FRAME_DATA;
  can_filter_struct.mask_para.id         = 0x0000FF00U;  /*!< mask: compare TA byte only */
  can_filter_struct.mask_para.id_type    = TRUE;         /*!< care: extended */
  can_filter_struct.mask_para.frame_type = TRUE;         /*!< care: data frame */
  can_filter_struct.mask_para.data_length = 0U;          /*!< 0: don't care DLC */
  can_filter_struct.mask_para.recv_frame = FALSE;        /*!< don't care RX mode */
  can_filter_set(CAN1, CAN_FILTER_NUM_0, &can_filter_struct);
  can_filter_enable(CAN1, CAN_FILTER_NUM_0, TRUE);

  /* Filter 1: functional addressing (0x18DB33xx, TA=0x33) */
  can_filter_default_para_init(&can_filter_struct);
  can_filter_struct.code_para.id         = 0x18DB3300U;
  can_filter_struct.code_para.id_type    = CAN_ID_EXTENDED;
  can_filter_struct.code_para.frame_type = CAN_FRAME_DATA;
  can_filter_struct.mask_para.id         = 0x1FFFFF00U;  /*!< mask: compare PF+TA, ignore SA */
  can_filter_struct.mask_para.id_type    = TRUE;
  can_filter_struct.mask_para.frame_type = TRUE;
  can_filter_struct.mask_para.data_length = 0U;
  can_filter_struct.mask_para.recv_frame = FALSE;
  can_filter_set(CAN1, CAN_FILTER_NUM_1, &can_filter_struct);
  can_filter_enable(CAN1, CAN_FILTER_NUM_1, TRUE);

  /* enable RX interrupt and error interrupt */
  can_interrupt_enable(CAN1, CAN_RIE_INT, TRUE);
  can_interrupt_enable(CAN1, CAN_EIE_INT, TRUE);

  /* configure NVIC for CAN1 RX */
  nvic_irq_enable(CAN1_RX_IRQn, 1, 0);

  /* configure NVIC for CAN1 Error */
  nvic_irq_enable(CAN1_ERR_IRQn, 2, 0);

  /* reset software FIFO */
  rx_fifo_head  = 0;
  rx_fifo_tail  = 0;
  rx_fifo_count = 0;
  rx_callback   = (can_rx_callback_t)0;
}

/**
 * @brief  transmit a CAN extended frame
 * @param  id:   29-bit extended identifier
 * @param  data: pointer to transmit data buffer
 * @param  len:  data length (0~8)
 * @retval 0 on success, -1 on failure (bus busy or invalid parameter)
 */
int8_t can_driver_send(uint32_t id, uint8_t *data, uint8_t len)
{
  can_txbuf_type tx_buf;
  can_txbuf_select_type txbuf_sel;
  can_stb_status_type stb_status;
  can_transmit_status_type tx_status;
  uint8_t i;

  /* validate parameters */
  if ((data == (uint8_t *)0) || (len > CAN_DRIVER_MAX_DATA_LEN))
  {
    return -1;
  }

  /* prepare TX buffer */
  tx_buf.id          = id;
  tx_buf.id_type     = CAN_ID_EXTENDED;
  tx_buf.frame_type  = CAN_FRAME_DATA;
  tx_buf.handle      = 0;
  tx_buf.tx_timestamp = FALSE;

  /* set data length code */
  switch (len)
  {
    case 0: tx_buf.data_length = CAN_DLC_BYTES_0; break;
    case 1: tx_buf.data_length = CAN_DLC_BYTES_1; break;
    case 2: tx_buf.data_length = CAN_DLC_BYTES_2; break;
    case 3: tx_buf.data_length = CAN_DLC_BYTES_3; break;
    case 4: tx_buf.data_length = CAN_DLC_BYTES_4; break;
    case 5: tx_buf.data_length = CAN_DLC_BYTES_5; break;
    case 6: tx_buf.data_length = CAN_DLC_BYTES_6; break;
    case 7: tx_buf.data_length = CAN_DLC_BYTES_7; break;
    default: tx_buf.data_length = CAN_DLC_BYTES_8; break;
  }

  /* copy data */
  for (i = 0; i < len; i++)
  {
    tx_buf.data[i] = data[i];
  }

  /* try primary transmit buffer (PTB) first for higher priority */
  can_transmit_status_get(CAN1, &tx_status);
  if (tx_status.current_tstat == 0)  /* PTB empty */
  {
    txbuf_sel = CAN_TXBUF_PTB;
  }
  else
  {
    /* check secondary transmit buffer (STB) */
    stb_status = can_stb_status_get(CAN1);
    if (stb_status == CAN_STB_STATUS_FULL)
    {
      return -1;  /* both buffers full */
    }
    txbuf_sel = CAN_TXBUF_STB;
  }

  /* write to transmit buffer */
  if (can_txbuf_write(CAN1, txbuf_sel, &tx_buf) != SUCCESS)
  {
    return -1;
  }

  /* trigger transmission */
  if (txbuf_sel == CAN_TXBUF_PTB)
  {
    can_txbuf_transmit(CAN1, CAN_TRANSMIT_PTB);
  }
  else
  {
    can_txbuf_transmit(CAN1, CAN_TRANSMIT_STB_ONE);
  }

  return 0;
}

/**
 * @brief  register a callback for received CAN frames
 * @note   the callback is invoked from can_driver_poll() in main loop context
 * @param  cb: callback function pointer, or NULL to unregister
 * @retval none
 */
void can_driver_register_rx_callback(can_rx_callback_t cb)
{
  rx_callback = cb;
}

/**
 * @brief  process received CAN frames from software FIFO
 * @note   must be called periodically from main loop.
 *         invokes registered callback for each pending frame.
 * @param  none
 * @retval none
 */
void can_driver_poll(void)
{
  can_rx_frame_t frame;

  while (rx_fifo_count > 0)
  {
    /* disable IRQ to protect the entire FIFO read+advance sequence from ISR race */
    __disable_irq();

    /* copy frame from FIFO */
    frame.id  = rx_fifo[rx_fifo_tail].id;
    frame.len = rx_fifo[rx_fifo_tail].len;
    {
      uint8_t i;
      for (i = 0; i < frame.len; i++)
      {
        frame.data[i] = rx_fifo[rx_fifo_tail].data[i];
      }
    }

    /* advance tail pointer and decrement count */
    rx_fifo_tail = (rx_fifo_tail + 1) % CAN_DRIVER_RX_FIFO_SIZE;
    rx_fifo_count--;

    __enable_irq();

    /* invoke callback */
    if (rx_callback != (can_rx_callback_t)0)
    {
      rx_callback(frame.id, frame.data, frame.len);
    }
  }
}

/**
 * @brief  CAN1 RX interrupt handler (called from CAN1_RX_IRQHandler)
 * @note   reads frame from hardware RX buffer into software FIFO,
 *         clears interrupt flag.
 * @param  none
 * @retval none
 */
void can_driver_rx_irq_handler(void)
{
  can_rxbuf_type rx_buf;
  uint8_t i;

  /* check RX interrupt flag */
  if (can_flag_get(CAN1, CAN_RIF_FLAG) != RESET)
  {
    /* read frame from hardware RX buffer */
    if (can_rxbuf_read(CAN1, &rx_buf) == SUCCESS)
    {
      /* store in software FIFO if not full */
      if (!rx_fifo_is_full())
      {
        rx_fifo[rx_fifo_head].id  = rx_buf.id;
        rx_fifo[rx_fifo_head].len = (uint8_t)(rx_buf.data_length & 0x0F);
        for (i = 0; i < rx_fifo[rx_fifo_head].len; i++)
        {
          rx_fifo[rx_fifo_head].data[i] = rx_buf.data[i];
        }
        rx_fifo_head = (rx_fifo_head + 1) % CAN_DRIVER_RX_FIFO_SIZE;
        rx_fifo_count++;
      }
    }

    /* release RX buffer */
    can_rxbuf_release(CAN1);

    /* clear RX interrupt flag */
    can_flag_clear(CAN1, CAN_RIF_FLAG);
  }

  /* handle RX overflow */
  if (can_flag_get(CAN1, CAN_ROIF_FLAG) != RESET)
  {
    can_flag_clear(CAN1, CAN_ROIF_FLAG);
  }
}

/**
 * @brief  CAN1 error interrupt handler (called from CAN1_ERR_IRQHandler)
 * @note   clears error flags and performs error recovery if bus-off.
 * @param  none
 * @retval none
 */
void can_driver_err_irq_handler(void)
{
  /* check error warning flag */
  if (can_flag_get(CAN1, CAN_EIF_FLAG) != RESET)
  {
    /* check for bus-off condition */
    if (can_busoff_get(CAN1) != RESET)
    {
      /* recover from bus-off by requesting recovery */
      can_busoff_reset(CAN1);
    }

    /* clear error interrupt flag */
    can_flag_clear(CAN1, CAN_EIF_FLAG);
  }

  /* clear bus error flag if set */
  if (can_flag_get(CAN1, CAN_BEIF_FLAG) != RESET)
  {
    can_flag_clear(CAN1, CAN_BEIF_FLAG);
  }

  /* clear arbitration lost flag if set */
  if (can_flag_get(CAN1, CAN_ALIF_FLAG) != RESET)
  {
    can_flag_clear(CAN1, CAN_ALIF_FLAG);
  }

  /* clear error passive flag if set */
  if (can_flag_get(CAN1, CAN_EPIF_FLAG) != RESET)
  {
    can_flag_clear(CAN1, CAN_EPIF_FLAG);
  }
}
