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

/* exported constants --------------------------------------------------------*/

/** @brief  CAN IDs for UDS communication */
#define CAN_PROTO_UDS_REQUEST       0x18DA0D03U  /*!< UDS request  (tester -> ECU) */
#define CAN_PROTO_UDS_RESPONSE      0x18DA030DU  /*!< UDS response (ECU -> tester) */

/** @brief  UDS service IDs */
#define UDS_SID_DIAG_SESSION_CTRL   0x10U        /*!< DiagnosticSessionControl */
#define UDS_SID_ECU_RESET           0x11U        /*!< ECUReset */
#define UDS_SID_REQUEST_DOWNLOAD    0x34U        /*!< RequestDownload */
#define UDS_SID_TESTER_KEEPALIVE    0x3EU        /*!< TesterPresent (keepalive) */
#define UDS_SID_TRANSFER_SIGNATURE  0x38U        /*!< TransferSignature (boot safe mode only) */

/** @brief  UDS response codes */
#define UDS_NEGATIVE_RESPONSE       0x7FU        /*!< negative response indicator */
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U       /*!< positive response offset */

/** @brief  UDS negative response codes (NRC) */
#define UDS_NRC_SERVICE_NOT_SUPPORTED  0x11U     /*!< service not supported */
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12U  /*!< sub-function not supported */

/* exported functions -------------------------------------------------------*/

/**
 * @brief  initialize CAN protocol module
 * @note   registers CAN RX callback with the CAN driver.
 *         must be called after can_driver_init().
 * @param  none
 * @retval none
 */
void can_protocol_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H */
