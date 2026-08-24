/**
  **************************************************************************
  * @file     wdg_drv.h
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

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __WDG_DRV_H
#define __WDG_DRV_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* exported functions -------------------------------------------------------*/

/**
 * @brief  IWDG init — no-op (watchdog removed from bootloader)
 * @note   The IWDG is no longer used. This stub exists only for
 *         backward compatibility with code that still calls it.
 * @param  none
 * @retval none
 */
void wdg_drv_init(void);

/**
 * @brief  IWDG refresh — no-op (watchdog removed from bootloader)
 * @note   The IWDG is no longer used. This stub exists only for
 *         backward compatibility with code that still calls it.
 * @param  none
 * @retval none
 */
void wdg_drv_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __WDG_DRV_H */
