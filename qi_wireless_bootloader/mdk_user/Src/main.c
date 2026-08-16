/**
  **************************************************************************
  * @file     main.c
  * @brief    Bootloader main program for QI Charger OTA upgrade
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
#include "at32f422_426_clock.h"
#include "at32f422_426_conf.h"
#include "timer_drv.h"
#include "wdg_drv.h"
#include "boot_metadata.h"
#include "boot_safe_mode.h"
#include "boot_trial.h"

/* exported functions --------------------------------------------------------*/

/**
 * @brief  bootloader main entry point
 * @note   boot sequence:
 *         1. system clock configuration
 *         2. initialize timer and watchdog
 *         3. load OTA metadata
 *         4. detect boot reason
 *         5. process trial boot state machine
 *         6. select and verify boot slot
 *         7. jump to application or enter safe mode
 * @param  none
 * @retval none (should never return)
 */
int main(void)
{
  uint8_t boot_slot;
  uint8_t other_slot;
  uint8_t trial_tmr_id;
  int8_t  boot_result;

  /* step 1: configure system clock (180MHz from HEXT via PLL) */
  system_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  /* step 2: initialize drivers */
  timer_drv_init();
  wdg_drv_init();

  /* step 3: load and validate OTA metadata */
  boot_metadata_init(&g_meta);

  /* step 4: detect and record boot reason */
  g_meta.last_boot_reason = detect_boot_reason();

  /* step 5: process trial boot state machine */
  process_trial_state(&g_meta);

  /* step 6: select boot slot */
  if (select_boot_slot(&g_meta, &boot_slot) != 0)
  {
    /* no valid slot selected, enter safe mode */
    enter_safe_mode();
    /* does not return */
  }

  /* if trial is active, start trial timeout timer */
  if (g_meta.trial_state == TRIAL_STATE_ACTIVE)
  {
    trial_tmr_id = timer_create(TRIAL_TIMER_PERIOD_MS, trial_timer_callback, 1);
    if (trial_tmr_id != TIMER_INVALID_ID)
    {
      timer_start(trial_tmr_id);
    }
  }

  /* step 7: try to boot from selected slot */
  boot_result = try_boot_slot(boot_slot, &g_meta);

  if (boot_result != 0)
  {
    /* selected slot failed, try the other slot */
    other_slot = (boot_slot == SLOT_A) ? SLOT_B : SLOT_A;
    boot_result = try_boot_slot(other_slot, &g_meta);

    if (boot_result == 0)
    {
      /* update active slot to the one that worked */
      g_meta.active_slot = other_slot;
      boot_metadata_save(&g_meta);
    }
  }

  /* step 8: if both slots failed, enter safe mode */
  if (boot_result != 0)
  {
    enter_safe_mode();
    /* does not return */
  }

  /* should never reach here (boot_jump_to_app does not return) */
  while (1)
  {
    wdg_drv_refresh();
  }
}
