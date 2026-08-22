/**
  **************************************************************************
  * @file     main.c
  * @brief    QI Charger APP main program
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
#include "can_driver.h"
#include "can_protocol.h"
#include "lifecycle.h"
#include "ota_trigger.h"
#include "nvm_drv.h"
#include "qi_uart.h"
#include <string.h>

/* private define ------------------------------------------------------------*/

/** @brief  APP base address (start of slot A region in flash) */
#define APP_BASE_ADDR           0x08005000U

/** @brief  Image header size written by bootloader before firmware data.
 *          The actual APP code starts at APP_BASE_ADDR + IMAGE_HEADER_SIZE,
 *          which is also where the bootloader sets the jump target.
 *          VTOR must point to the real vector table at that offset. */
#define IMAGE_HEADER_SIZE       256U

/** @brief  OTA metadata trial states */
#define TRIAL_STATE_ACTIVE      2U
#define TRIAL_STATE_CONFIRMED   3U

/* private functions ---------------------------------------------------------*/

/**
 * @brief  confirm new image after trial boot
 * @note   if the bootloader started this APP in trial mode (trial_state=ACTIVE),
 *         this function confirms the image by setting trial_state=CONFIRMED.
 *         must be called after core initialization is complete.
 * @param  none
 * @retval none
 */
static void ota_confirm_if_needed(void)
{
  ota_metadata_t meta;

  /* read metadata from flash */
  if (ota_metadata_read(&meta) != 0)
  {
    return;  /* invalid metadata, nothing to confirm */
  }

  /* check if we are in active trial */
  if (meta.trial_state == TRIAL_STATE_ACTIVE)
  {
    /* confirm the image */
    meta.trial_state = TRIAL_STATE_CONFIRMED;

    /* save to primary flash */
    ota_metadata_save(&meta);
  }
}

/**
 * @brief  main function.
 * @param  none
 * @retval none
 */
int main(void)
{
  /* vector table is at slot base + 256B image header (IROM 0x08005100). */
  SCB->VTOR = APP_BASE_ADDR + IMAGE_HEADER_SIZE;

  /* configure system clock to 180MHz */
  system_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  /* initialize drivers */
  timer_drv_init();
  wdg_drv_init();
  nvm_drv_init();
  can_driver_init();
  qi_uart_init();

  /* initialize CAN protocol module (registers UDS handler) */
  can_protocol_init();

  /* initialize lifecycle broadcast (sends BOOTUP) */
  lifecycle_init();

  /* report OPERATIONAL after core init is complete */
  lifecycle_set_state(LIFECYCLE_OPERATIONAL);

  /* confirm trial only after CAN/Qi/lifecycle are up */
  ota_confirm_if_needed();

  /* main loop */
  while (1)
  {
    /* poll software timers */
    timer_poll();

    /* poll CAN for received frames (triggers protocol handler callbacks) */
    can_driver_poll();

    /* poll Qi UART for chip data */
    qi_uart_poll();

    /* feed watchdog */
    wdg_drv_refresh();

    /* poll lifecycle for periodic broadcast (1 Hz in OPERATIONAL/DEGRADED) */
    lifecycle_poll();
  }
}

/**
  * @}
  */

/**
  * @}
  */
