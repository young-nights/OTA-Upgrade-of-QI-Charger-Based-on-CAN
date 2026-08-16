/**
  **************************************************************************
  * @file     boot_safe_mode.c
  * @brief    Safe mode implementation: UDS OTA download via CAN
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
#include "boot_safe_mode.h"
#include "can_driver.h"
#include "timer_drv.h"
#include "wdg_drv.h"

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
      /* RequestDownload: acknowledge (actual flash write handled by transfer) */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = 0x20;  /* lengthFormatIdentifier */
      resp[2] = 0x00;
      resp[3] = 0x00;
      resp[4] = 0x10;
      safe_mode_send_response(resp, 5);
      break;

    case UDS_TRANSFER_DATA:
      /* TransferData: acknowledge data block */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = data[1];  /* blockSequenceCounter */
      safe_mode_send_response(resp, 2);
      break;

    case UDS_REQUEST_TRANSFER_EXIT:
      /* RequestTransferExit: acknowledge */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      safe_mode_send_response(resp, 1);
      break;

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
  uint8_t tmr_id;

  g_safe_mode = 1;

  /* initialize CAN for safe mode communication */
  can_driver_init();
  can_driver_register_rx_callback(safe_mode_can_rx_handler);

  /* create a periodic timer for safe mode housekeeping (optional) */
  tmr_id = timer_create(500, (void (*)(void))0, 1);
  if (tmr_id != TIMER_INVALID_ID)
  {
    timer_start(tmr_id);
  }

  /* safe mode event loop */
  while (1)
  {
    timer_poll();
    can_driver_poll();
    wdg_drv_refresh();
  }
}
