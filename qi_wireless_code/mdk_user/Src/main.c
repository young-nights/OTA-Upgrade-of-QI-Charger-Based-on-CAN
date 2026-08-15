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
#include "nvm_drv.h"
#include <string.h>

/* private define ------------------------------------------------------------*/

/** @brief  APP base address (set by bootloader before jump) */
#define APP_BASE_ADDR           0x08004000U

/** @brief  OTA metadata addresses (shared with bootloader) */
#define META_PRIMARY_ADDR       0x0801C000U
#define META_MAGIC              0x4F54414DU   /* "MATO" */

/** @brief  OTA metadata trial states */
#define TRIAL_STATE_ACTIVE      2U
#define TRIAL_STATE_CONFIRMED   3U

/* private types -------------------------------------------------------------*/

/**
 * @brief  minimal OTA metadata structure (must match bootloader definition)
 * @note   only the fields needed by APP are included
 */
typedef struct
{
  uint32_t magic;              /* 0x4F54414D "MATO" */
  uint32_t version;            /* metadata format version = 1 */
  uint8_t  active_slot;        /* 0=A, 1=B */
  uint8_t  pending_slot;       /* 0=A, 1=B, 0xFE=none */
  uint8_t  slot_a_valid;
  uint8_t  slot_b_valid;
  uint32_t slot_a_crc32;
  uint32_t slot_b_crc32;
  uint8_t  trial_state;        /* 0=IDLE, 1=PENDING, 2=ACTIVE, 3=CONFIRMED */
  uint8_t  trial_slot;
  uint8_t  trial_retry_count;
  uint8_t  trial_max_retries;
  uint16_t trial_timeout_sec;
  uint16_t reserved1;
  uint32_t rollback_count;
  uint8_t  last_boot_reason;
  uint8_t  ota_state;
  uint8_t  reserved2[2];
  uint8_t  padding[488];       /* fill to 512 bytes */
  uint32_t crc32;
} ota_metadata_t;

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
  uint32_t *src;
  uint32_t *dst;
  uint32_t i;
  uint32_t word_count = sizeof(ota_metadata_t) / 4;

  /* read metadata from primary location */
  src = (uint32_t *)META_PRIMARY_ADDR;
  dst = (uint32_t *)&meta;
  for (i = 0; i < word_count; i++)
  {
    dst[i] = src[i];
  }

  /* check if we are in active trial */
  if (meta.magic == META_MAGIC && meta.trial_state == TRIAL_STATE_ACTIVE)
  {
    /* confirm the image */
    meta.trial_state = TRIAL_STATE_CONFIRMED;

    /* save back to flash (both primary and backup) */
    flash_unlock();

    /* erase primary metadata page */
    flash_sector_erase(META_PRIMARY_ADDR);

    /* write confirmed metadata to primary */
    src = (uint32_t *)&meta;
    for (i = 0; i < word_count; i++)
    {
      flash_word_program(META_PRIMARY_ADDR + (i * 4), src[i]);
    }

    /* erase backup metadata page */
    flash_sector_erase(META_PRIMARY_ADDR + 0x2000U);

    /* write confirmed metadata to backup */
    for (i = 0; i < word_count; i++)
    {
      flash_word_program(META_PRIMARY_ADDR + 0x2000U + (i * 4), src[i]);
    }

    flash_lock();
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
  /* set vector table to APP base address */
  SCB->VTOR = APP_BASE_ADDR;

  /* configure system clock to 180MHz */
  system_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  /* initialize drivers */
  timer_drv_init();
  wdg_drv_init();
  nvm_drv_init();
  can_driver_init();

  /* confirm trial boot image if needed (must be after flash init) */
  ota_confirm_if_needed();

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

    /* feed watchdog */
    wdg_drv_refresh();

    /* send periodic lifecycle broadcast */
    if (g_broadcast_flag)
    {
      g_broadcast_flag = 0;
      send_broadcast();
    }

    /* TODO: poll CAN for UDS requests */
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
