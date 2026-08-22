/**
  **************************************************************************
  * @file     isotp.c
  * @brief    ISO 15765-2 (ISO-TP) transport layer for CAN UDS communication
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
#include "isotp.h"
#include "can_driver.h"
#include "timer_drv.h"
#include "wdg_drv.h"

/* private variables ---------------------------------------------------------*/

/** @brief  ISO-TP receiver context (single instance) */
static isotp_rx_ctx_t rx_ctx;

/** @brief  completion callback pointer */
static isotp_complete_cb_t complete_callback = (isotp_complete_cb_t)0;

/* private functions ---------------------------------------------------------*/

/**
 * @brief  send a Flow Control frame via CAN
 * @param  can_id: CAN identifier for the FC frame
 * @param  status: FC status (CTS / Wait / Overflow)
 * @param  bs:     block size (number of CF frames before next FC)
 * @param  stmin:  minimum separation time between CF frames (ms)
 * @retval none
 */
static void isotp_send_fc(uint32_t can_id, uint8_t status, uint8_t bs, uint8_t stmin)
{
  uint8_t fc_data[ISOTP_CAN_FRAME_SIZE];

  fc_data[0] = ISOTP_PCI_TYPE_FC | (status & 0x0FU);
  fc_data[1] = bs;
  fc_data[2] = stmin;
  /* pad remaining bytes with 0xCC (unused, per ISO 15765-2 recommendation) */
  fc_data[3] = 0xCCU;
  fc_data[4] = 0xCCU;
  fc_data[5] = 0xCCU;
  fc_data[6] = 0xCCU;
  fc_data[7] = 0xCCU;

  can_driver_send(can_id, fc_data, ISOTP_CAN_FRAME_SIZE);
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  initialize the ISO-TP receiver
 * @param  cb: callback invoked when a complete message is reassembled
 * @retval none
 */
void isotp_init(isotp_complete_cb_t cb)
{
  rx_ctx.state        = ISOTP_RX_STATE_IDLE;
  rx_ctx.total_len    = 0;
  rx_ctx.received_len = 0;
  rx_ctx.expected_sn  = 0;
  complete_callback   = cb;
}

/**
 * @brief  process an incoming CAN frame through ISO-TP
 * @note   handles SF, FF, CF, and FC frame types.
 *         SF triggers the callback immediately.
 *         FF/CF accumulate data; the callback fires when all bytes arrive.
 *         FC frames from the tester are acknowledged but no action is taken
 *         because this MCU currently transmits single-segment messages only.
 * @param  data: pointer to CAN frame data (up to 8 bytes)
 * @param  len:  CAN frame data length
 * @retval none
 */
void isotp_rx_process(uint8_t *data, uint8_t len)
{
  uint8_t pci_type;
  uint16_t payload_len;
  uint16_t copy_len;
  uint16_t i;

  if ((data == (uint8_t *)0) || (len == 0U))
  {
    return;
  }

  /* extract PCI frame type from upper nibble of first byte */
  pci_type = data[0] & ISOTP_PCI_TYPE_MASK;

  switch (pci_type)
  {
    /* ------------------------------------------------------------------ */
    /* Single Frame: PCI = 0x0N, N = payload length (1..7 for SF with     */
    /* classic addressing; lower nibble of byte 0 holds the length).      */
    /* SF with length 0 in the PCI means SF_DL is in byte 1 (ISO 15765   */
    /* 2016 extension), but this project uses classic CAN, so we only     */
    /* handle the simple case.                                             */
    /* ------------------------------------------------------------------ */
    case ISOTP_PCI_TYPE_SF:
    {
      uint8_t sf_dl = data[0] & 0x0FU;

      /* validation: SF_DL must be > 0 and fit within the CAN frame */
      if ((sf_dl == 0U) || (sf_dl > (uint8_t)(len - 1U)))
      {
        return;  /* malformed SF, discard */
      }

      /* deliver UDS payload (bytes 1..sf_dl) directly */
      if (complete_callback != (isotp_complete_cb_t)0)
      {
        complete_callback(&data[1], (uint16_t)sf_dl);
      }
      break;
    }

    /* ------------------------------------------------------------------ */
    /* First Frame: PCI bytes 0-1 = 0x1NNN where NNN is 12-bit length.   */
    /* Byte 0: upper nibble = 0x1, lower nibble = length bits [11:8].    */
    /* Byte 1: length bits [7:0].                                         */
    /* Bytes 2..7: first data bytes of the payload.                       */
    /* ------------------------------------------------------------------ */
    case ISOTP_PCI_TYPE_FF:
    {
      if (len < 2U)
      {
        return;  /* FF must have at least 2 PCI bytes */
      }

      payload_len = ((uint16_t)(data[0] & 0x0FU) << 8) | (uint16_t)data[1];

      /* payload length must be > 7 (otherwise it should have been SF) */
      if (payload_len <= 7U)
      {
        return;
      }

      /* reject if payload exceeds our buffer */
      if (payload_len > ISOTP_MAX_PAYLOAD)
      {
        isotp_send_fc(CAN_ID_UDS_RESPONSE, ISOTP_FC_STATUS_OVERFLOW, 0U, 0U);
        return;
      }

      /* start a new reception */
      rx_ctx.total_len    = payload_len;
      rx_ctx.received_len = 0;
      rx_ctx.expected_sn  = 1;  /* first CF will have SN = 1 */
      rx_ctx.state        = ISOTP_RX_STATE_RX_IN_PROGRESS;

      /* copy first data bytes from the FF (bytes 2..len-1) */
      if (len > 2U)
      {
        copy_len = (uint16_t)(len - 2U);
        if (copy_len > payload_len)
        {
          copy_len = payload_len;
        }
        for (i = 0; i < copy_len; i++)
        {
          rx_ctx.buf[i] = data[2 + i];
        }
        rx_ctx.received_len = copy_len;
      }

      /* send Flow Control (CTS) to tell the sender to continue */
      /* uses CAN_ID_UDS_RESPONSE from can_driver.h as the FC source ID */
      isotp_send_fc(CAN_ID_UDS_RESPONSE, ISOTP_FC_STATUS_CTS,
                     ISOTP_FC_DEFAULT_BS, ISOTP_FC_DEFAULT_STMIN);
      break;
    }

    /* ------------------------------------------------------------------ */
    /* Consecutive Frame: PCI = 0x2N where N is the sequence number       */
    /* (0..15, wrapping).  Bytes 1..7 are payload data.                   */
    /* ------------------------------------------------------------------ */
    case ISOTP_PCI_TYPE_CF:
    {
      if (rx_ctx.state != ISOTP_RX_STATE_RX_IN_PROGRESS)
      {
        return;  /* unexpected CF, no reception in progress */
      }

      {
        uint8_t sn = data[0] & 0x0FU;

        /* verify sequence number */
        if (sn != rx_ctx.expected_sn)
        {
          /* sequence number mismatch — abort reception */
          rx_ctx.state = ISOTP_RX_STATE_IDLE;
          return;
        }
      }

      /* calculate how many payload bytes are in this CF */
      copy_len = (uint16_t)(len - 1U);
      if (copy_len > (rx_ctx.total_len - rx_ctx.received_len))
      {
        copy_len = rx_ctx.total_len - rx_ctx.received_len;
      }

      /* copy data bytes into reassembly buffer */
      for (i = 0; i < copy_len; i++)
      {
        rx_ctx.buf[rx_ctx.received_len + i] = data[1 + i];
      }
      rx_ctx.received_len += copy_len;

      /* advance expected sequence number (wraps at 16) */
      rx_ctx.expected_sn = (rx_ctx.expected_sn + 1U) & 0x0FU;

      /* check if reception is complete */
      if (rx_ctx.received_len >= rx_ctx.total_len)
      {
        rx_ctx.state = ISOTP_RX_STATE_IDLE;

        if (complete_callback != (isotp_complete_cb_t)0)
        {
          complete_callback(rx_ctx.buf, rx_ctx.total_len);
        }
      }
      break;
    }

    /* ------------------------------------------------------------------ */
    /* Flow Control: received from the tester when MCU is the sender.     */
    /* In the current design, MCU transmits only single-frame responses,  */
    /* so FC frames are acknowledged but no action is taken.              */
    /* ------------------------------------------------------------------ */
    case ISOTP_PCI_TYPE_FC:
    {
      /* no action required for current single-frame TX model */
      break;
    }

    default:
      /* unknown PCI type — discard */
      break;
  }
}

/**
 * @brief  send a UDS payload via ISO-TP segmentation
 * @note   for payloads <= 7 bytes, sends a Single Frame.
 *         for payloads > 7 bytes, sends FF followed by CFs.
 *         does NOT wait for FC from the tester (sends all at once).
 * @param  can_id:  CAN identifier to use for transmission
 * @param  payload: pointer to UDS payload data
 * @param  len:     payload length in bytes (1..4095)
 * @retval 0 on success, -1 on failure
 */
static void isotp_delay_ms(uint32_t ms)
{
  uint32_t start = timer_get_tick();
  while ((timer_get_tick() - start) < ms)
  {
    wdg_drv_refresh();
  }
}

static int8_t isotp_wait_cts(void)
{
  uint32_t start = timer_get_tick();
  uint32_t id;
  uint8_t data[ISOTP_CAN_FRAME_SIZE];
  uint8_t n;

  while ((timer_get_tick() - start) < ISOTP_N_BS_TIMEOUT_MS)
  {
    wdg_drv_refresh();
    if (can_driver_recv(&id, data, &n) != 0)
    {
      continue;
    }
    if ((n == 0U) || ((data[0] & ISOTP_PCI_TYPE_MASK) != ISOTP_PCI_TYPE_FC))
    {
      continue;
    }
    if ((data[0] & 0x0FU) == ISOTP_FC_STATUS_CTS)
    {
      return 0;
    }
    if ((data[0] & 0x0FU) == ISOTP_FC_STATUS_OVERFLOW)
    {
      return -1;
    }
    /* FC Wait: restart N_Bs */
    start = timer_get_tick();
  }
  return -1;
}

int8_t isotp_tx_send(uint32_t can_id, uint8_t *payload, uint16_t len)
{
  uint8_t frame[ISOTP_CAN_FRAME_SIZE];
  uint16_t offset;
  uint8_t sn;
  uint8_t copy_len;

  if ((payload == (uint8_t *)0) || (len == 0U) || (len > ISOTP_MAX_PAYLOAD))
  {
    return -1;
  }

  if (len <= 7U)
  {
    frame[0] = ISOTP_PCI_TYPE_SF | (uint8_t)len;
    {
      uint16_t i;
      for (i = 0; i < len; i++)
      {
        frame[1 + i] = payload[i];
      }
    }
    {
      uint16_t i;
      for (i = 1 + len; i < ISOTP_CAN_FRAME_SIZE; i++)
      {
        frame[i] = 0xCCU;
      }
    }
    return can_driver_send(can_id, frame, ISOTP_CAN_FRAME_SIZE);
  }

  frame[0] = ISOTP_PCI_TYPE_FF | (uint8_t)((len >> 8) & 0x0FU);
  frame[1] = (uint8_t)(len & 0xFFU);
  {
    uint8_t i;
    for (i = 0; i < 6U; i++)
    {
      frame[2 + i] = payload[i];
    }
  }
  if (can_driver_send(can_id, frame, ISOTP_CAN_FRAME_SIZE) != 0)
  {
    return -1;
  }

  /* ISO 15765-2: wait for Flow Control before Consecutive Frames */
  if (isotp_wait_cts() != 0)
  {
    return -1;
  }

  offset = 6U;
  sn = 1U;

  while (offset < len)
  {
    isotp_delay_ms(ISOTP_FC_DEFAULT_STMIN);

    frame[0] = ISOTP_PCI_TYPE_CF | (sn & 0x0FU);

    copy_len = (uint8_t)(len - offset);
    if (copy_len > 7U)
    {
      copy_len = 7U;
    }

    {
      uint8_t i;
      for (i = 0; i < copy_len; i++)
      {
        frame[1 + i] = payload[offset + i];
      }
    }
    {
      uint8_t i;
      for (i = 1 + copy_len; i < ISOTP_CAN_FRAME_SIZE; i++)
      {
        frame[i] = 0xCCU;
      }
    }

    if (can_driver_send(can_id, frame, ISOTP_CAN_FRAME_SIZE) != 0)
    {
      return -1;
    }

    offset += copy_len;
    sn = (sn + 1U) & 0x0FU;
  }

  return 0;
}
