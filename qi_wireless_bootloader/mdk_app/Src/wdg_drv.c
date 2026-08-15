/**
  **************************************************************************
  * @file     wdg_drv.c
  * @brief    IWDG (Independent Watchdog) driver implementation
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
#include "at32f422_426_conf.h"

/* private define ------------------------------------------------------------*/
/**
 * @brief  IWDG timeout calculation (LSI = 40kHz typical):
 *         - LSI clock = 40000 Hz
 *         - WDT_CLK_DIV_128 -> 40000 / 128 = 312.5 Hz -> 3.2 ms per tick
 *         - reload = 312 -> timeout = 312 * 3.2 ms = 998.4 ms ~ 1000 ms
 */
#define WDG_DRV_DIVIDER               WDT_CLK_DIV_128
#define WDG_DRV_RELOAD_VALUE          312

/* exported functions --------------------------------------------------------*/

/**
 * @brief  initialize IWDG with ~1000ms timeout
 * @note   IWDG uses LSI clock (~40kHz). Once enabled, it cannot be disabled.
 *         Initialization sequence: unlock -> set divider -> set reload -> enable -> lock
 * @param  none
 * @retval none
 */
void wdg_drv_init(void)
{
  /* enable register write access (unlock) */
  wdt_register_write_enable(TRUE);

  /* set clock divider: LSI/128 = 312.5 Hz (3.2ms per tick) */
  wdt_divider_set(WDG_DRV_DIVIDER);

  /* set reload value: 312 -> timeout ~ 998ms */
  wdt_reload_value_set(WDG_DRV_RELOAD_VALUE);

  /* enable IWDG (once enabled, it cannot be disabled - hardware limitation) */
  wdt_enable();

  /* reload counter to start fresh */
  wdt_counter_reload();

  /* disable register write access (lock) */
  wdt_register_write_enable(FALSE);
}

/**
 * @brief  refresh (feed) the IWDG to prevent reset
 * @note   must be called periodically within the watchdog timeout window
 * @param  none
 * @retval none
 */
void wdg_drv_refresh(void)
{
  wdt_counter_reload();
}
