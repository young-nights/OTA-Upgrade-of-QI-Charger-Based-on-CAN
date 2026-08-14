/**
  **************************************************************************
  * @file     ota_trigger.c
  * @brief    APP-side OTA trigger implementation
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
#include "ota_trigger.h"
#include "at32f422_426_flash.h"
#include "at32f422_426_crm.h"

/** @addtogroup APP_OTA
  * @{
  */

/** @defgroup OTA_trigger
  * @brief    OTA trigger implementation
  * @{
  */

/** @defgroup OTA_trigger_private_constants
  * @{
  */

/* Flash sector size for AT32F426 */
#define FLASH_SECTOR_SIZE            ((uint32_t)0x800)

/**
  * @}
  */

/** @defgroup OTA_trigger_exported_functions
  * @{
  */

/**
  * @brief  Request OTA upgrade
  * @note   This function:
  *         1. Erases the OTA flag sector
  *         2. Writes OTA_FLAG_MAGIC_REQUEST to the flag area
  *         3. Triggers a system reset via NVIC
  *         After reset, the bootloader will detect the flag and enter OTA mode.
  * @retval none (never returns, triggers system reset)
  */
void ota_trigger_request(void)
{
  flash_status_type status;

  /* unlock flash for writing */
  flash_unlock();

  /* erase OTA flag sector */
  status = flash_sector_erase(OTA_FLAG_START_ADDR);
  if (status != FLASH_OPERATE_DONE)
  {
    /* erase failed - lock flash and return without reset */
    flash_lock();
    return;
  }

  /* write OTA request magic value */
  status = flash_word_program(OTA_FLAG_START_ADDR, OTA_FLAG_MAGIC_REQUEST);
  if (status != FLASH_OPERATE_DONE)
  {
    /* write failed - lock flash and return without reset */
    flash_lock();
    return;
  }

  /* lock flash */
  flash_lock();

  /* trigger system reset */
  /* Use NVIC_SystemReset() from CMSIS */
  NVIC_SystemReset();

  /* should never reach here */
  while (1)
  {
  }
}

/**
  * @brief  Check if OTA flag area is clean (no pending request)
  * @retval 1 = clean (no OTA pending), 0 = OTA flag exists
  */
uint8_t ota_trigger_is_clean(void)
{
  uint32_t flag = *((volatile uint32_t *)OTA_FLAG_START_ADDR);

  /* erased flash reads as 0xFFFFFFFF, anything else is a flag */
  if (flag == OTA_FLAG_MAGIC_REQUEST)
  {
    return 0;  /* OTA request pending */
  }

  return 1;  /* clean */
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
