/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
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
#include "timer_drv.h"
#include "wdg_drv.h"

/** @addtogroup AT32F426_periph_examples
  * @{
  */

/** @addtogroup 426_CAN_communication_mode CAN_communication_mode
  * @{
  */



/**
  * @brief  main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  system_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  timer_drv_init();      /* initialize software timer (SysTick 1ms) */
  wdg_drv_init();        /* initialize independent watchdog (~1000ms) */

  /* example: create a 100ms periodic timer (e.g. for life-cycle broadcast) */
  /* uint8_t tmr_broadcast = timer_create(100, broadcast_callback, 1); */
  /* timer_start(tmr_broadcast); */

  while(1)
  {
    timer_poll();          /* software timer polling */
    wdg_drv_refresh();     /* feed the watchdog */
  }
}

/**
  * @}
  */

/**
  * @}
  */
