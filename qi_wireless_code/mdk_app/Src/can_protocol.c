/**
  **************************************************************************
  * @file     can_protocol.c
  * @brief    CAN UDS protocol handler for APP firmware
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
#include "can_protocol.h"
#include "can_driver.h"
#include "ota_trigger.h"

/* private constants ---------------------------------------------------------*/

/** @brief  DID definitions for ReadDataByIdentifier (0x22) */
#define DID_SOFTWARE_VERSION      0xF195U   /*!< software version (matches bootloader) */
#define DID_OTA_STATE             0x2112U   /*!< OTA state from metadata */
#define DID_ACTIVE_SLOT           0x2113U   /*!< active firmware slot */
#define DID_LAST_BOOT_REASON      0x2115U   /*!< last boot reason */
#define DID_ROLLBACK_COUNT        0x2116U   /*!< rollback counter */

/** @brief  Software version string (2 bytes, major.minor) */
#define SW_VERSION_MAJOR          0x01U
#define SW_VERSION_MINOR          0x00U

/* private functions ---------------------------------------------------------*/

/**
 * @brief  send a UDS response frame
 * @param  data: pointer to response data
 * @param  len: data length
 * @retval none
 */
static void proto_send_response(uint8_t *data, uint8_t len)
{
  can_driver_send(CAN_PROTO_UDS_RESPONSE, data, len);
}

/**
 * @brief  send UDS negative response
 * @param  service_id: the rejected service ID
 * @param  nrc: negative response code
 * @retval none
 */
static void proto_send_nrc(uint8_t service_id, uint8_t nrc)
{
  uint8_t resp[3];
  resp[0] = UDS_NEGATIVE_RESPONSE;
  resp[1] = service_id;
  resp[2] = nrc;
  proto_send_response(resp, 3);
}

/**
 * @brief  CAN RX callback for UDS protocol handling
 * @note   called from can_driver_poll() in main loop context.
 *         handles simplified UDS services for OTA triggering.
 * @param  id:   29-bit extended identifier of received frame
 * @param  data: pointer to received data buffer
 * @param  len:  data length (0~8)
 * @retval none
 */
static void can_protocol_rx_handler(uint32_t id, uint8_t *data, uint8_t len)
{
  uint8_t service_id;
  uint8_t resp[8];

  /* only accept UDS request ID */
  if (id != CAN_PROTO_UDS_REQUEST)
  {
    return;
  }

  if (len == 0)
  {
    return;
  }

  service_id = data[0];

  switch (service_id)
  {
    case UDS_SID_DIAG_SESSION_CTRL:
      /* DiagnosticSessionControl: positive response */
      /* byte 0: SID + 0x40 */
      /* byte 1: session type echo */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      if (len >= 2U)
      {
        resp[1] = data[1];
        proto_send_response(resp, 2);
      }
      else
      {
        resp[1] = 0x01U;  /* default session */
        proto_send_response(resp, 2);
      }
      break;

    case UDS_SID_ECU_RESET:
      /* ECUReset: acknowledge then reset */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      if (len >= 2U)
      {
        resp[1] = data[1];
        proto_send_response(resp, 2);
      }
      else
      {
        proto_send_response(resp, 1);
      }
      /* small delay to ensure CAN response is transmitted before reset */
      {
        volatile uint32_t d;
        for (d = 0; d < 36000; d++) { __NOP(); }  /* ~2ms at 180MHz */
      }
      /* trigger OTA mode: sets metadata flag and resets */
      ota_trigger_request();
      /* does not return */
      break;

    case UDS_SID_REQUEST_DOWNLOAD:
      /* APP does not handle download - this is done by boot safe mode */
      /* Return NRC to avoid misleading host with wrong maxNumberOfBlockLength */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;

    case UDS_SID_TRANSFER_SIGNATURE:
      /* APP does not handle signature transfer - this is done in boot safe mode */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;

    case UDS_SID_READ_DATA_BY_ID:
      /* ReadDataByIdentifier: requires at least SID + 2-byte DID */
      if (len < 3U)
      {
        proto_send_nrc(service_id, 0x13U);  /* incorrectMessageLengthOrInvalidFormat */
        break;
      }
      {
        uint16_t did = ((uint16_t)data[1] << 8) | (uint16_t)data[2];
        uint8_t nrc = 0x00U;

        switch (did)
        {
          case DID_SOFTWARE_VERSION:
            /* echo DID + 2-byte version */
            resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
            resp[1] = data[1];  /* DID high byte */
            resp[2] = data[2];  /* DID low byte */
            resp[3] = SW_VERSION_MAJOR;
            resp[4] = SW_VERSION_MINOR;
            proto_send_response(resp, 5);
            break;

          case DID_OTA_STATE:
          {
            ota_metadata_t meta;
            resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
            resp[1] = data[1];
            resp[2] = data[2];
            if (ota_metadata_read(&meta) == 0)
            {
              resp[3] = meta.ota_state;
            }
            else
            {
              resp[3] = 0xFFU;  /* unknown */
            }
            proto_send_response(resp, 4);
            break;
          }

          case DID_ACTIVE_SLOT:
          {
            ota_metadata_t meta;
            resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
            resp[1] = data[1];
            resp[2] = data[2];
            if (ota_metadata_read(&meta) == 0)
            {
              resp[3] = meta.active_slot;
            }
            else
            {
              resp[3] = 0xFFU;  /* unknown */
            }
            proto_send_response(resp, 4);
            break;
          }

          case DID_LAST_BOOT_REASON:
          {
            ota_metadata_t meta;
            resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
            resp[1] = data[1];
            resp[2] = data[2];
            if (ota_metadata_read(&meta) == 0)
            {
              resp[3] = meta.last_boot_reason;
            }
            else
            {
              resp[3] = 0xFFU;  /* unknown */
            }
            proto_send_response(resp, 4);
            break;
          }

          case DID_ROLLBACK_COUNT:
          {
            ota_metadata_t meta;
            resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
            resp[1] = data[1];
            resp[2] = data[2];
            if (ota_metadata_read(&meta) == 0)
            {
              resp[3] = (uint8_t)(meta.rollback_count & 0xFFU);
            }
            else
            {
              resp[3] = 0x00U;
            }
            proto_send_response(resp, 4);
            break;
          }

          default:
            nrc = 0x31U;  /* requestOutOfRange */
            break;
        }

        if (nrc != 0x00U)
        {
          proto_send_nrc(service_id, nrc);
        }
      }
      break;

    case UDS_SID_SECURITY_ACCESS:
      /* APP side does not support SecurityAccess (done in bootloader safe mode).
       * Return NRC 0x11 (serviceNotSupported) to indicate this service is
       * not available in the application session. */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;

    case UDS_SID_TESTER_KEEPALIVE:
      /* TesterPresent (keepalive): positive response */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      if (len >= 2U)
      {
        resp[1] = data[1];
        proto_send_response(resp, 2);
      }
      else
      {
        proto_send_response(resp, 1);
      }
      break;

    default:
      /* unsupported service */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;
  }
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  initialize CAN protocol module
 * @note   registers CAN RX callback with the CAN driver.
 *         must be called after can_driver_init().
 * @param  none
 * @retval none
 */
void can_protocol_init(void)
{
  can_driver_register_rx_callback(can_protocol_rx_handler);
}
