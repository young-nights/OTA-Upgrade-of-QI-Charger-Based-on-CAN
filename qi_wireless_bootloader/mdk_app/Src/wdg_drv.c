/**
  **************************************************************************
  * @file     wdg_drv.c
  * @brief    IWDG (Independent Watchdog) driver — DISABLED (stub only)
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
#include "wdg_drv.h"

/* exported functions --------------------------------------------------------*/

/**
 * @brief  IWDG init — no-op (watchdog removed from bootloader)
 * @param  none
 * @retval none
 */
void wdg_drv_init(void)
{
  /* intentionally empty — watchdog is disabled */
}

/**
 * @brief  IWDG refresh — no-op (watchdog removed from bootloader)
 * @param  none
 * @retval none
 */
void wdg_drv_refresh(void)
{
  /* intentionally empty — watchdog is disabled */
}
