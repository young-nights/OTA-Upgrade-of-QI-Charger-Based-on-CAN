/**
  **************************************************************************
  * @file     boot_safe_mode.c
  * @brief    Safe mode implementation: UDS OTA download via CAN
  **************************************************************************
  *
  * @note    Current implementation supports single-frame UDS only (8 bytes per CAN frame).
  *          For 48KB firmware, ~8192 frames are needed. ISO-TP multi-frame support
  *          can be added in a future version for improved throughput.
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
#include "boot_safe_mode.h"
#include "boot_metadata.h"
#include "boot_verify.h"
#include "boot_trial.h"
#include "can_driver.h"
#include "timer_drv.h"
#include "wdg_drv.h"
#include <string.h>

/* private define ------------------------------------------------------------*/

/** @brief  safe mode CAN IDs for OTA download */
#define SAFE_MODE_CAN_ID_REQUEST    0x18DA0D03U  /*!< UDS request ID */
#define SAFE_MODE_CAN_ID_RESPONSE   0x18DA030DU  /*!< UDS response ID */

/** @brief  UDS service IDs supported in safe mode */
#define UDS_DIAG_SESSION_CTRL       0x10U
#define UDS_SECURITY_ACCESS         0x27U
#define UDS_REQUEST_DOWNLOAD        0x34U
#define UDS_TRANSFER_DATA           0x36U
#define UDS_REQUEST_TRANSFER_EXIT   0x37U
#define UDS_ECU_RESET               0x11U

/** @brief  UDS negative response code */
#define UDS_NEGATIVE_RESPONSE       0x7FU
#define UDS_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U

/* private variables ---------------------------------------------------------*/

/** @brief  safe mode flag */
static uint8_t g_safe_mode = 0;

/** @brief  download state variables */
static uint32_t g_dl_write_addr    = 0;
static uint32_t g_dl_bytes_written = 0;
static uint8_t  g_dl_block_seq     = 0;
static uint8_t  g_dl_active        = 0;

/** @brief  maximum image size (APP_A size minus header) */
#define MAX_IMAGE_SIZE    (APP_A_SIZE - IMAGE_HEADER_SIZE)

/* private functions ---------------------------------------------------------*/

/**
 * @brief  send a CAN response frame
 * @param  data: pointer to response data
 * @param  len: data length
 * @retval none
 */
static void safe_mode_send_response(uint8_t *data, uint8_t len)
{
  can_driver_send(SAFE_MODE_CAN_ID_RESPONSE, data, len);
}

/**
 * @brief  send UDS negative response
 * @param  service_id: the rejected service ID
 * @param  nrc: negative response code
 * @retval none
 */
static void safe_mode_send_nrc(uint8_t service_id, uint8_t nrc)
{
  uint8_t resp[3];
  resp[0] = UDS_NEGATIVE_RESPONSE;
  resp[1] = service_id;
  resp[2] = nrc;
  safe_mode_send_response(resp, 3);
}

/**
 * @brief  handle UDS request in safe mode
 * @param  id: CAN frame ID
 * @param  data: pointer to frame data
 * @param  len: frame data length
 * @retval none
 */
static void safe_mode_can_rx_handler(uint32_t id, uint8_t *data, uint8_t len)
{
  uint8_t service_id;
  uint8_t resp[8];

  (void)id;

  if (len == 0)
  {
    return;
  }

  service_id = data[0];

  switch (service_id)
  {
    case UDS_DIAG_SESSION_CTRL:
      /* DiagnosticSessionControl: positive response */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = data[1];
      safe_mode_send_response(resp, 2);
      break;

    case UDS_SECURITY_ACCESS:
      /* SecurityAccess: accept any seed/key for bootloader (simplified) */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = data[1];
      safe_mode_send_response(resp, 2);
      break;

    case UDS_REQUEST_DOWNLOAD:
    {
      uint32_t sector_addr;
      uint8_t erase_err = 0;

      /* erase APP_A flash area */
      flash_unlock();
      for (sector_addr = APP_A_BASE_ADDR;
           sector_addr < (APP_A_BASE_ADDR + APP_A_SIZE);
           sector_addr += 0x800U)  /* 2KB sectors for AT32F426 */
      {
        if (flash_sector_erase(sector_addr) != FLASH_OPERATE_DONE)
        {
          erase_err = 1;
          break;
        }
      }
      flash_lock();

      if (erase_err)
      {
        safe_mode_send_nrc(service_id, 0x72U);
        break;
      }

      /* initialize download state */
      g_dl_write_addr    = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
      g_dl_bytes_written = 0;
      g_dl_block_seq     = 0;
      g_dl_active        = 1;

      /* maxNumberOfBlockLength = maximum firmware size (not per-frame size).
       * actual per-frame payload is limited to 6 bytes (8-byte CAN - SID - blockSeq). */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = 0x20;
      resp[2] = 0x00;
      resp[3] = (uint8_t)((APP_A_SIZE >> 8) & 0xFFU);
      resp[4] = (uint8_t)(APP_A_SIZE & 0xFFU);
      safe_mode_send_response(resp, 5);
      break;
    }

    case UDS_TRANSFER_DATA:
    {
      uint8_t block_seq;
      uint8_t data_len;
      uint8_t i;
      flash_status_type flash_status;

      if (!g_dl_active)
      {
        safe_mode_send_nrc(service_id, 0x71U);  /* transferDataAborted */
        break;
      }

      if (len < 3U)
      {
        safe_mode_send_nrc(service_id, 0x13U);  /* incorrectMessageLengthOrInvalidFormat */
        break;
      }

      block_seq = data[1];
      g_dl_block_seq++;
      if (block_seq != g_dl_block_seq)
      {
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, 0x71U);
        break;
      }

      data_len = len - 2U;

      if ((g_dl_bytes_written + (uint32_t)data_len) > MAX_IMAGE_SIZE)
      {
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, 0x14U);  /* responseTooLong */
        break;
      }

      /* write data to flash */
      flash_unlock();
      for (i = 0; i < data_len; i += 4U)
      {
        uint32_t word_data = 0xFFFFFFFFU;
        uint8_t  remaining = data_len - i;
        uint8_t  copy_len  = (remaining > 4U) ? 4U : remaining;
        uint8_t  k;

        for (k = 0; k < copy_len; k++)
        {
          word_data &= ~((uint32_t)0xFFU << (k * 8U));
          word_data |= ((uint32_t)data[2U + i + k] << (k * 8U));
        }

        flash_status = flash_word_program(g_dl_write_addr + (uint32_t)i, word_data);
        if (flash_status != FLASH_OPERATE_DONE)
        {
          flash_lock();
          g_dl_active = 0;
          safe_mode_send_nrc(service_id, 0x72U);  /* generalProgrammingFailure */
          break;
        }
      }
      flash_lock();

      if (!g_dl_active) break;

      g_dl_write_addr    += (uint32_t)data_len;
      g_dl_bytes_written += (uint32_t)data_len;

      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = block_seq;
      safe_mode_send_response(resp, 2);
      break;
    }

    case UDS_REQUEST_TRANSFER_EXIT:
    {
      uint32_t computed_crc;
      uint32_t image_data_addr;
      image_header_t header;
      const uint32_t *hdr_words;
      uint32_t hdr_word_count;
      uint32_t w;

      if (!g_dl_active)
      {
        safe_mode_send_nrc(service_id, 0x71U);
        break;
      }

      g_dl_active = 0;

      /* compute CRC32 of downloaded image */
      image_data_addr = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
      computed_crc = boot_crc32((const void *)image_data_addr, g_dl_bytes_written);

      /* prepare and write image header */
      memset((void *)&header, 0xFF, sizeof(image_header_t));
      header.magic        = IMAGE_MAGIC;
      header.image_length = g_dl_bytes_written;
      header.crc32        = computed_crc;

      flash_unlock();
      hdr_words      = (const uint32_t *)&header;
      hdr_word_count = sizeof(image_header_t) / sizeof(uint32_t);
      for (w = 0; w < hdr_word_count; w++)
      {
        flash_word_program(APP_A_BASE_ADDR + (w * 4U), hdr_words[w]);
      }
      flash_lock();

      /* update metadata to mark slot A as valid */
      g_meta.slot_a_valid = 1;
      g_meta.slot_a_crc32 = computed_crc;
      g_meta.ota_state    = OTA_STATE_IDLE;
      boot_metadata_save(&g_meta);

      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      safe_mode_send_response(resp, 1);
      break;
    }

    case UDS_ECU_RESET:
      /* ECUReset: acknowledge then reset via watchdog */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      safe_mode_send_response(resp, 1);
      /* let watchdog reset us */
      while(1)
      {
        /* wait for watchdog reset */
      }

    default:
      /* unsupported service */
      safe_mode_send_nrc(service_id, UDS_SERVICE_NOT_SUPPORTED);
      break;
  }
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  enter safe mode: initialize CAN and wait for OTA download
 * @note   called when both application slots are invalid.
 *         runs a minimal event loop with CAN polling and watchdog refresh.
 * @param  none
 * @retval none (does not return unless watchdog resets)
 */
void enter_safe_mode(void)
{
  g_safe_mode = 1;

  /* initialize CAN for safe mode communication */
  can_driver_init();
  can_driver_register_rx_callback(safe_mode_can_rx_handler);

  /* safe mode event loop */
  while (1)
  {
    timer_poll();
    can_driver_poll();
    wdg_drv_refresh();
  }
}
