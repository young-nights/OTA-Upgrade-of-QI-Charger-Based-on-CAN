/**
  **************************************************************************
  * @file     can_protocol.h
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

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_PROTOCOL_H
#define __CAN_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* ========================================================================== */
/*  CAN identifiers                                                          */
/* ========================================================================== */

/** @brief  CAN IDs for UDS communication */
#define CAN_PROTO_UDS_REQUEST        0x18DA0D03U  /*!< UDS request  (tester -> ECU) */
#define CAN_PROTO_UDS_RESPONSE       0x18DA030DU  /*!< UDS response (ECU -> tester) */

/** @brief  Lifecycle broadcast CAN ID (J1939 PDU2, GE=0x26, SA=0x0D) */
#define CAN_ID_LIFECYCLE_BROADCAST   0x18FF260DU

/* ========================================================================== */
/*  UDS service identifiers                                                  */
/* ========================================================================== */

#define UDS_SID_DIAG_SESSION_CTRL    0x10U        /*!< DiagnosticSessionControl */
#define UDS_SID_ECU_RESET            0x11U        /*!< ECUReset */
#define UDS_SID_READ_DATA_BY_ID      0x22U        /*!< ReadDataByIdentifier */
#define UDS_SID_WRITE_DATA_BY_ID     0x2EU        /*!< WriteDataByIdentifier */
#define UDS_SID_SECURITY_ACCESS      0x27U        /*!< SecurityAccess */
#define UDS_SID_ROUTINE_CONTROL      0x31U        /*!< RoutineControl */
#define UDS_SID_REQUEST_DOWNLOAD     0x34U        /*!< RequestDownload */
#define UDS_SID_TRANSFER_DATA        0x36U        /*!< TransferData (boot safe mode only) */
#define UDS_SID_TRANSFER_EXIT        0x37U        /*!< RequestTransferExit (boot safe mode only) */
#define UDS_SID_TRANSFER_SIGNATURE   0x38U        /*!< TransferSignature (boot safe mode only) */ */
#define UDS_SID_TESTER_KEEPALIVE     0x3EU        /*!< TesterPresent (keepalive) */

/* ========================================================================== */
/*  UDS response codes                                                       */
/* ========================================================================== */

#define UDS_NEGATIVE_RESPONSE        0x7FU        /*!< negative response indicator */
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U        /*!< positive response offset */

/* ========================================================================== */
/*  UDS negative response codes (NRC)                                        */
/* ========================================================================== */

#define UDS_NRC_SERVICE_NOT_SUPPORTED       0x11U  /*!< service not supported */
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED   0x12U  /*!< sub-function not supported */
#define UDS_NRC_INCORRECT_MESSAGE_LENGTH    0x13U  /*!< incorrect message length or invalid format */
#define UDS_NRC_CONDITIONS_NOT_CORRECT      0x22U  /*!< conditions not correct */
#define UDS_NRC_REQUEST_OUT_OF_RANGE        0x31U  /*!< request out of range */
#define UDS_NRC_SECURITY_ACCESS_DENIED      0x33U  /*!< security access denied */
#define UDS_NRC_INVALID_KEY                 0x35U  /*!< invalid key */
#define UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS 0x36U  /*!< exceeded number of attempts */
#define UDS_NRC_REQUIRED_TIME_DELAY         0x37U  /*!< required time delay not expired */
#define UDS_NRC_REQUEST_SEQUENCE_ERROR      0x24U  /*!< request sequence error */
#define UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED 0x70U /*!< upload/download not accepted */
#define UDS_NRC_TRANSFER_DATA_SUSPENDED     0x71U  /*!< transfer data suspended */
#define UDS_NRC_GENERAL_PROGRAMMING_FAILURE  0x72U /*!< general programming failure */
#define UDS_NRC_WRONG_BLOCK_SEQUENCE        0x73U  /*!< wrong block sequence counter */
#define UDS_NRC_RESPONSE_PENDING            0x78U  /*!< request correctly received, response pending */

/* ========================================================================== */
/*  UDS response timing constants (P2 / P2*)                                 */
/* ========================================================================== */

#define UDS_P2_TIMEOUT_MS          50U    /*!< P2: max server response time (ms) */
#define UDS_P2_STAR_TIMEOUT_MS     5000U  /*!< P2*: extended timeout after NRC 0x78 (ms) */

/* ========================================================================== */
/*  DID definitions (General + Device-specific)                               */
/* ========================================================================== */

/** @brief  Standard identifier DIDs (per 通用CAN协议规范 8.) */
#define DID_SW_VERSION              0xF189U   /*!< software version, UTF-8 string "MAJOR.MINOR.PATCH" */
#define DID_SERIAL_NUMBER           0xF18CU   /*!< serial number, UTF-8 string */
#define DID_BOOTLOADER_VERSION      0xF18DU   /*!< bootloader version, UTF-8 string */
#define DID_HW_VERSION              0xF191U   /*!< hardware version, UTF-8 string */

/** @brief  Firmware management DIDs */
#define DID_FW_TYPE                 0x2010U   /*!< firmware type, uint8, read/write */

/** @brief  Device-specific DIDs (defined in peripheral SRS) */
#define DID_OTA_STATE               0x2112U   /*!< OTA state from metadata */
#define DID_ACTIVE_SLOT             0x2113U   /*!< active firmware slot */
#define DID_PENDING_SLOT            0x2114U   /*!< pending firmware slot */
#define DID_LAST_BOOT_REASON        0x2115U   /*!< last boot reason */
#define DID_ROLLBACK_COUNT          0x2116U   /*!< rollback counter */

/* ========================================================================== */
/*  Session management constants                                             */
/* ========================================================================== */

#define SESSION_DEFAULT             0x01U     /*!< default session */
#define SESSION_PROGRAMMING         0x02U     /*!< programming session */
#define SESSION_EXTENDED            0x03U     /*!< extended session */

#define SESSION_TIMEOUT_MS          5000U     /*!< TesterPresent timeout in ms */

/** @brief  Suppress positive response bit (bit 7 of sub-function byte) */
#define UDS_SUBFUNC_SUPPRESS_POS_RESP  0x80U
#define UDS_SUBFUNC_MASK               0x7FU

/* ========================================================================== */
/*  Firmware type values (DID 0x2010)                                        */
/* ========================================================================== */

#define FW_TYPE_APP                 0x01U     /*!< APP firmware */
#define FW_TYPE_RESOURCE            0x02U     /*!< resource package */
#define FW_TYPE_BOOTLOADER          0x03U     /*!< bootloader */

/* ========================================================================== */
/*  RoutineControl routine IDs                                               */
/* ========================================================================== */

#define ROUTINE_ERASE_MEMORY        0xFF00U   /*!< erase memory routine (bootloader only) */

/* ========================================================================== */
/*  Exported functions                                                       */
/* ========================================================================== */

/**
 * @brief  initialize CAN protocol module
 * @note   registers CAN RX callback with the CAN driver.
 *         must be called after can_driver_init().
 * @param  none
 * @retval none
 */
void can_protocol_init(void);

/**
 * @brief  poll session timeout (S3) and ISO-TP N_Cr
 * @note   call from the main loop.
 */
void can_protocol_poll(void);

/**
 * @brief  get current diagnostic session
 * @retval SESSION_DEFAULT, SESSION_PROGRAMMING, or SESSION_EXTENDED
 */
uint8_t can_protocol_get_session(void);

/**
 * @brief  check if security access Level 1 is unlocked
 * @retval 1 = unlocked, 0 = locked
 */
uint8_t can_protocol_is_security_unlocked(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H */
