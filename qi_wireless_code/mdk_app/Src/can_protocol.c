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
#include "isotp.h"
#include "timer_drv.h"

/* ========================================================================== */
/*  Version string constants (UTF-8, max 16 bytes including null terminator)  */
/* ========================================================================== */

static const char SW_VERSION_STR[]   = "1.0.0";    /*!< software version */
static const char SERIAL_NUMBER_STR[] = "N/A";      /*!< serial number placeholder */
static const char BOOTLOADER_VER_STR[] = "1.0.0";   /*!< bootloader version */
static const char HW_VERSION_STR[]   = "1.0.0";     /*!< hardware version */

/* ========================================================================== */
/*  Session management state                                                 */
/* ========================================================================== */

static uint8_t  current_session           = SESSION_DEFAULT;
static uint8_t  security_unlocked         = 0;
static uint32_t last_tester_present_tick  = 0;

/* ========================================================================== */
/*  Private helper functions                                                 */
/* ========================================================================== */

/**
 * @brief  send a UDS response frame
 * @param  data: pointer to response data
 * @param  len: data length
 * @retval none
 */
static void proto_send_response(uint8_t *data, uint8_t len)
{
  (void)isotp_tx_send(CAN_PROTO_UDS_RESPONSE, data, (uint16_t)len);
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
 * @brief  reset session to default and clear security state
 * @note   called on session timeout or switch to default session
 * @retval none
 */
static void session_reset_to_default(void)
{
  current_session   = SESSION_DEFAULT;
  security_unlocked = 0;
}

/**
 * @brief  handle session switch rules per spec section 7.3
 * @param  new_session: requested session type
 * @retval none
 */
static void session_switch(uint8_t new_session)
{
  if (new_session == SESSION_DEFAULT)
  {
    /* entering default: abort any firmware transfer, clear security */
    /* (OTA transfer is not active in APP, but clear state anyway) */
    session_reset_to_default();
  }
  else if (current_session != SESSION_DEFAULT && new_session != current_session)
  {
    /* non-default -> different non-default: clear security */
    security_unlocked = 0;
    current_session = new_session;
  }
  else
  {
    /* default -> non-default, or same session: just update */
    current_session = new_session;
  }
}


/**
 * @brief  copy string into response buffer (including null terminator)
 * @param  dst: destination buffer
 * @param  src: source string
 * @retval number of bytes written (including null terminator)
 */
static uint8_t str_copy_to_resp(uint8_t *dst, const char *src)
{
  uint8_t i = 0;
  while ((i < 16U) && (src[i] != '\0'))
  {
    dst[i] = (uint8_t)src[i];
    i++;
  }
  dst[i] = '\0';
  return (uint8_t)(i + 1U);
}

/* ========================================================================== */
/*  UDS service handlers                                                     */
/* ========================================================================== */

/**
 * @brief  DiagnosticSessionControl (0x10)
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_diag_session_ctrl(uint8_t *data, uint16_t len)
{
  uint8_t resp[8];
  uint8_t sub_func;
  uint8_t suppress;
  uint8_t session_type;

  if (len < 2U)
  {
    proto_send_nrc(UDS_SID_DIAG_SESSION_CTRL, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  sub_func     = data[1];
  suppress     = sub_func & UDS_SUBFUNC_SUPPRESS_POS_RESP;
  session_type = sub_func & UDS_SUBFUNC_MASK;

  /* validate session type */
  if ((session_type != SESSION_DEFAULT) &&
      (session_type != SESSION_PROGRAMMING) &&
      (session_type != SESSION_EXTENDED))
  {
    proto_send_nrc(UDS_SID_DIAG_SESSION_CTRL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    return;
  }

  /* perform session switch */
  session_switch(session_type);

  /* update tester present tick on session control */
  last_tester_present_tick = timer_get_tick();

  /* send positive response unless suppressed */
  if (!suppress)
  {
    resp[0] = UDS_SID_DIAG_SESSION_CTRL + UDS_POSITIVE_RESPONSE_OFFSET;
    resp[1] = session_type;
    proto_send_response(resp, 2);
  }
}

/**
 * @brief  ECUReset (0x11)
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_ecu_reset(uint8_t *data, uint16_t len)
{
  uint8_t resp[8];
  uint8_t sub_func;
  uint8_t suppress;
  uint8_t enter_ota;

  if (len < 2U)
  {
    proto_send_nrc(UDS_SID_ECU_RESET, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  sub_func = data[1] & UDS_SUBFUNC_MASK;
  suppress = data[1] & UDS_SUBFUNC_SUPPRESS_POS_RESP;

  if (sub_func != 0x01U)
  {
    proto_send_nrc(UDS_SID_ECU_RESET, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    return;
  }

  /* programming session + hardReset: enter bootloader download */
  enter_ota = (current_session == SESSION_PROGRAMMING) ? 1U : 0U;

  if (enter_ota)
  {
    if (ota_trigger_prepare() != 0)
    {
      proto_send_nrc(UDS_SID_ECU_RESET, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
      return;
    }
  }

  if (!suppress)
  {
    resp[0] = UDS_SID_ECU_RESET + UDS_POSITIVE_RESPONSE_OFFSET;
    resp[1] = sub_func;
    proto_send_response(resp, 2);
  }

  (void)can_driver_wait_tx_idle(5U);
  NVIC_SystemReset();
}

/**
 * @brief  ReadDataByIdentifier (0x22)
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_read_data_by_id(uint8_t *data, uint16_t len)
{
  uint8_t resp[20];
  uint8_t nrc = 0x00U;

  if (len < 3U)
  {
    proto_send_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  /* support single DID read: SID + DID_H + DID_L */
  {
    uint16_t did = ((uint16_t)data[1] << 8) | (uint16_t)data[2];
    uint8_t pos;

    resp[0] = UDS_SID_READ_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET;
    resp[1] = data[1];  /* DID high byte */
    resp[2] = data[2];  /* DID low byte */
    pos = 3;

    switch (did)
    {
      case DID_SW_VERSION:
        pos += str_copy_to_resp(&resp[pos], SW_VERSION_STR);
        proto_send_response(resp, pos);
        break;

      case DID_SERIAL_NUMBER:
        pos += str_copy_to_resp(&resp[pos], SERIAL_NUMBER_STR);
        proto_send_response(resp, pos);
        break;

      case DID_BOOTLOADER_VERSION:
        pos += str_copy_to_resp(&resp[pos], BOOTLOADER_VER_STR);
        proto_send_response(resp, pos);
        break;

      case DID_HW_VERSION:
        pos += str_copy_to_resp(&resp[pos], HW_VERSION_STR);
        proto_send_response(resp, pos);
        break;

      case DID_FW_TYPE:
        resp[pos] = FW_TYPE_APP;
        proto_send_response(resp, (uint8_t)(pos + 1U));
        break;

      case DID_OTA_STATE:
      {
        ota_metadata_t meta;
        if (ota_metadata_read(&meta) == 0)
        {
          resp[pos] = meta.ota_state;
        }
        else
        {
          resp[pos] = 0xFFU;  /* unknown */
        }
        proto_send_response(resp, (uint8_t)(pos + 1U));
        break;
      }

      case DID_ACTIVE_SLOT:
      {
        ota_metadata_t meta;
        if (ota_metadata_read(&meta) == 0)
        {
          resp[pos] = meta.active_slot;
        }
        else
        {
          resp[pos] = 0xFFU;  /* unknown */
        }
        proto_send_response(resp, (uint8_t)(pos + 1U));
        break;
      }

      case DID_LAST_BOOT_REASON:
      {
        ota_metadata_t meta;
        if (ota_metadata_read(&meta) == 0)
        {
          resp[pos] = meta.last_boot_reason;
        }
        else
        {
          resp[pos] = 0xFFU;  /* unknown */
        }
        proto_send_response(resp, (uint8_t)(pos + 1U));
        break;
      }

      case DID_ROLLBACK_COUNT:
      {
        ota_metadata_t meta;
        if (ota_metadata_read(&meta) == 0)
        {
          resp[pos] = (uint8_t)(meta.rollback_count & 0xFFU);
        }
        else
        {
          resp[pos] = 0x00U;
        }
        proto_send_response(resp, (uint8_t)(pos + 1U));
        break;
      }

      default:
        nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
        break;
    }
  }

  if (nrc != 0x00U)
  {
    proto_send_nrc(UDS_SID_READ_DATA_BY_ID, nrc);
  }
}

/**
 * @brief  WriteDataByIdentifier (0x2E)
 * @note   requires programming session + SecurityAccess Level 1
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_write_data_by_id(uint8_t *data, uint16_t len)
{
  uint8_t resp[4];

  /* minimum: SID + DID_H + DID_L + 1 byte data */
  if (len < 4U)
  {
    proto_send_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  /* check programming session */
  if (current_session != SESSION_PROGRAMMING)
  {
    proto_send_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    return;
  }

  /* check security access */
  if (!security_unlocked)
  {
    proto_send_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SECURITY_ACCESS_DENIED);
    return;
  }

  {
    uint16_t did = ((uint16_t)data[1] << 8) | (uint16_t)data[2];

    switch (did)
    {
      case DID_FW_TYPE:
      {
        uint8_t fw_type = data[3];
        if ((fw_type < FW_TYPE_APP) || (fw_type > FW_TYPE_BOOTLOADER))
        {
          proto_send_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
          return;
        }
        /* accept the firmware type write (value stored/used during transfer) */
        resp[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET;
        resp[1] = data[1];  /* DID high byte */
        resp[2] = data[2];  /* DID low byte */
        proto_send_response(resp, 3);
        break;
      }

      default:
        /* DID not writable */
        proto_send_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        break;
    }
  }
}

/**
 * @brief  SecurityAccess (0x27)
 * @note   APP side does not support SecurityAccess (done in bootloader safe mode).
 *         Return NRC 0x11 to indicate this service is not available in APP.
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_security_access(uint8_t *data, uint16_t len)
{
  (void)data;
  (void)len;
  proto_send_nrc(UDS_SID_SECURITY_ACCESS, UDS_NRC_SERVICE_NOT_SUPPORTED);
}

/**
 * @brief  RoutineControl (0x31)
 * @note   APP side only reports NRC 0x11 for all routines
 *         (erase and other routines are handled by bootloader)
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_routine_control(uint8_t *data, uint16_t len)
{
  (void)data;
  (void)len;
  /* All routines are handled by bootloader, not APP */
  proto_send_nrc(UDS_SID_ROUTINE_CONTROL, UDS_NRC_SERVICE_NOT_SUPPORTED);
}

/**
 * @brief  TesterPresent (0x3E)
 * @param  data: UDS payload
 * @param  len:  payload length
 * @retval none
 */
static void handle_tester_present(uint8_t *data, uint16_t len)
{
  uint8_t resp[8];
  uint8_t sub_func;
  uint8_t suppress;

  /* update session keepalive timestamp */
  last_tester_present_tick = timer_get_tick();

  if (len >= 2U)
  {
    sub_func = data[1];
    suppress = sub_func & UDS_SUBFUNC_SUPPRESS_POS_RESP;

    if (!suppress)
    {
      resp[0] = UDS_SID_TESTER_KEEPALIVE + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = sub_func & UDS_SUBFUNC_MASK;
      proto_send_response(resp, 2);
    }
  }
  else
  {
    /* no sub-function: send positive response */
    resp[0] = UDS_SID_TESTER_KEEPALIVE + UDS_POSITIVE_RESPONSE_OFFSET;
    proto_send_response(resp, 1);
  }
}

/* ========================================================================== */
/*  Main UDS message dispatcher                                              */
/* ========================================================================== */

/**
 * @brief  process a complete UDS message (ISO-TP payload already extracted)
 * @note   called from isotp_message_received() after reassembly.
 * @param  data: pointer to UDS payload (first byte is SID)
 * @param  len:  UDS payload length
 * @retval none
 */
static void uds_process_message(uint8_t *data, uint16_t len)
{
  uint8_t service_id;

  if ((data == (uint8_t *)0) || (len == 0U))
  {
    return;
  }

  service_id = data[0];

  switch (service_id)
  {
    case UDS_SID_DIAG_SESSION_CTRL:
      handle_diag_session_ctrl(data, len);
      break;

    case UDS_SID_ECU_RESET:
      handle_ecu_reset(data, len);
      break;

    case UDS_SID_READ_DATA_BY_ID:
      handle_read_data_by_id(data, len);
      break;

    case UDS_SID_WRITE_DATA_BY_ID:
      handle_write_data_by_id(data, len);
      break;

    case UDS_SID_SECURITY_ACCESS:
      handle_security_access(data, len);
      break;

    case UDS_SID_ROUTINE_CONTROL:
      handle_routine_control(data, len);
      break;

    case UDS_SID_REQUEST_DOWNLOAD:
      /* APP does not handle download - done by boot safe mode */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;

    case UDS_SID_TRANSFER_SIGNATURE:
      /* APP does not handle signature transfer - done in boot safe mode */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;

    case UDS_SID_TESTER_KEEPALIVE:
      handle_tester_present(data, len);
      break;

    default:
      /* unsupported service */
      proto_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;
  }
}

/* ========================================================================== */
/*  ISO-TP callback and CAN RX handler                                       */
/* ========================================================================== */

/**
 * @brief  ISO-TP completion callback
 * @note   invoked when a complete UDS message has been reassembled.
 *         checks session timeout before forwarding to uds_process_message().
 * @param  data: pointer to complete UDS payload
 * @param  len:  payload length in bytes
 * @retval none
 */
static void isotp_message_received(uint8_t *data, uint16_t len)
{
  uint32_t now;

  /* session timeout check: if in non-default session and no TesterPresent
   * received within SESSION_TIMEOUT_MS, fall back to default session */
  if (current_session != SESSION_DEFAULT)
  {
    now = timer_get_tick();
    if ((now - last_tester_present_tick) >= SESSION_TIMEOUT_MS)
    {
      session_reset_to_default();
    }
  }

  uds_process_message(data, len);
}

/**
 * @brief  CAN RX callback for UDS protocol handling
 * @note   called from can_driver_poll() in main loop context.
 * @param  id:   29-bit extended identifier of received frame
 * @param  data: pointer to received data buffer
 * @param  len:  data length (0~8)
 * @retval none
 */
static void can_protocol_rx_handler(uint32_t id, uint8_t *data, uint8_t len)
{
  /* only accept UDS request ID */
  if (id != CAN_PROTO_UDS_REQUEST)
  {
    return;
  }

  if (len == 0)
  {
    return;
  }

  /* pass to ISO-TP for reassembly */
  isotp_rx_process(data, len);
}

/* ========================================================================== */
/*  Exported functions                                                       */
/* ========================================================================== */

/**
 * @brief  initialize CAN protocol module
 * @param  none
 * @retval none
 */
void can_protocol_init(void)
{
  current_session          = SESSION_DEFAULT;
  security_unlocked        = 0;
  last_tester_present_tick = timer_get_tick();

  isotp_init(isotp_message_received);
  can_driver_register_rx_callback(can_protocol_rx_handler);
}

/**
 * @brief  get current diagnostic session
 * @retval SESSION_DEFAULT, SESSION_PROGRAMMING, or SESSION_EXTENDED
 */
uint8_t can_protocol_get_session(void)
{
  return current_session;
}

/**
 * @brief  check if security access Level 1 is unlocked
 * @retval 1 = unlocked, 0 = locked
 */
uint8_t can_protocol_is_security_unlocked(void)
{
  return security_unlocked;
}
