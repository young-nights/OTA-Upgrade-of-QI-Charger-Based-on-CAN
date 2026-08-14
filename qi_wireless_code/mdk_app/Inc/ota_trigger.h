/**
  **************************************************************************
  * @file     ota_trigger.h
  * @brief    APP-side OTA trigger: write flag, software reset
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
#ifndef __OTA_TRIGGER_H
#define __OTA_TRIGGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/** @addtogroup APP_OTA
  * @{
  */

/** @defgroup OTA_trigger
  * @brief    Functions for APP to request OTA upgrade
  * @{
  */

/* OTA flag addresses (must match bootloader ota_config.h) */
#define OTA_FLAG_START_ADDR          ((uint32_t)0x0801C000)
#define OTA_FLAG_MAGIC_REQUEST       ((uint32_t)0x544F4152)  /* "RAOT" */

/**
  * @brief  Request OTA upgrade
  * @note   This function writes the OTA request flag to flash
  *         and triggers a software reset. The bootloader will
  *         detect the flag and enter OTA mode.
  * @retval none (never returns, triggers system reset)
  */
void ota_trigger_request(void);

/**
  * @brief  Check if OTA flag area is clean (no pending request)
  * @retval 1 = clean (no OTA pending), 0 = OTA flag exists
  */
uint8_t ota_trigger_is_clean(void);

/**
  * @}
  */

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __OTA_TRIGGER_H */
