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
#include "ota_trigger.h"
#include "nvm_drv.h"
#include "qi_uart.h"
#include <string.h>

/* private define ------------------------------------------------------------*/

/** @brief  APP base address (start of slot A region in flash) */
#define APP_BASE_ADDR           0x08004000U

/** @brief  Image header size written by bootloader before firmware data.
 *          The actual APP code starts at APP_BASE_ADDR + IMAGE_HEADER_SIZE,
 *          which is also where the bootloader sets the jump target.
 *          VTOR must point to the real vector table at that offset. */
#define IMAGE_HEADER_SIZE       256U



/** @brief  OTA metadata trial states */
#define TRIAL_STATE_ACTIVE      2U
#define TRIAL_STATE_CONFIRMED   3U

/* private variables ---------------------------------------------------------*/

/** @brief  broadcast timer ID */
static uint8_t g_broadcast_timer_id = 0xFF;

/** @brief  broadcast flag (set from timer callback) */
static volatile uint8_t g_broadcast_flag = 0;

/* private functions ---------------------------------------------------------*/

/**
 * @brief  broadcast timer callback (called every 100ms)
 * @param  none
 * @retval none
 */
static void broadcast_timer_callback(void)
{
  g_broadcast_flag = 1;
}

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
 * @brief  send lifecycle broadcast (0x18FF260D)
 * @note   simplified version: sends lifecycle=OPERATIONAL + status bytes
 * @param  none
 * @retval none
 */
static void send_broadcast(void)
{
  uint8_t data[8];

  memset(data, 0, sizeof(data));

  /* byte 0: lifecycle = OPERATIONAL (0x03) */
  data[0] = 0x03U;
  /* byte 1-7: reserved, all zeros */

  can_driver_send(0x18FF260DU, data, 8);
}

/**
 * @brief  main function.
 * @param  none
 * @retval none
 */
int main(void)
{
  /* set vector table to real code start (skip image header written by bootloader).
   * NOTE: Keil IROM start must also be set to 0x08004100 in the project options. */
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

  /* confirm trial boot image if needed (must be after flash init) */
  ota_confirm_if_needed();

  /* initialize CAN protocol module (registers UDS handler) */
  can_protocol_init();

  /* create 100ms periodic broadcast timer */
  g_broadcast_timer_id = timer_create(100, broadcast_timer_callback, 1);
  timer_start(g_broadcast_timer_id);

  /* send BOOTUP broadcast */
  send_broadcast();

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

    /* send periodic lifecycle broadcast */
    if (g_broadcast_flag)
    {
      g_broadcast_flag = 0;
      send_broadcast();
    }

    /* TODO: poll UART for Qi chip data */
    /* TODO: run charging state machine */
  }
}

/**
  * @}
  */

/**
  * @}
  */
