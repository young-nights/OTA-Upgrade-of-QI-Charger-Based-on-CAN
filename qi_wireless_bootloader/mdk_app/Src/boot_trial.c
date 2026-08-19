/**
  **************************************************************************
  * @file     boot_trial.c
  * @brief    Trial boot state machine implementation
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
#include "boot_trial.h"
#include "boot_verify.h"
#include "boot_jump.h"
#include "timer_drv.h"
#include "at32f422_426_conf.h"

/* private variables ---------------------------------------------------------*/

/** @brief  OTA metadata instance */
ota_metadata_t g_meta;

/** @brief  trial boot elapsed seconds counter */
static volatile uint32_t g_trial_elapsed_sec = 0;

/** @brief  trial timer expired flag (set from timer callback) */
static volatile uint8_t g_trial_timer_flag = 0;

/* private functions ---------------------------------------------------------*/

/**
 * @brief  trial boot timer callback (called every 1 second)
 * @param  none
 * @retval none
 */
void trial_timer_callback(void)
{
  g_trial_elapsed_sec++;
  g_trial_timer_flag = 1;

  /* Check if trial has timed out */
  if (g_meta.trial_state == TRIAL_STATE_ACTIVE &&
      g_trial_elapsed_sec >= (uint32_t)g_meta.trial_timeout_sec)
  {
    /* Timeout: increment retry count and rollback to previous slot */
    g_meta.trial_retry_count++;
    g_meta.trial_state = TRIAL_STATE_IDLE;
    g_meta.active_slot = (g_meta.trial_slot == SLOT_A) ? SLOT_B : SLOT_A;
    boot_metadata_save(&g_meta);

    /* Force watchdog reset to apply rollback */
    while (1)
    {
      /* wait for watchdog reset */
    }
  }
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  detect reset source and return boot reason code
 * @param  none
 * @retval boot reason code
 */
uint8_t detect_boot_reason(void)
{
  /* check RCC reset status register */
  if (crm_flag_get(CRM_WDT_RESET_FLAG) != RESET)
  {
    crm_flag_clear(CRM_WDT_RESET_FLAG);
    return BOOT_REASON_WDG;
  }

  /* default: power-on reset */
  return BOOT_REASON_POWER_ON;
}

/**
 * @brief  select the slot to boot from based on metadata
 * @param  meta: pointer to metadata
 * @param  slot: output, selected slot index (0=A, 1=B)
 * @retval 0 on success (valid slot found), -1 if no valid slot available
 */
int8_t select_boot_slot(const ota_metadata_t *meta, uint8_t *slot)
{
  /* check if there is a pending trial */
  if (meta->trial_state == TRIAL_STATE_PENDING)
  {
    /* use the trial slot */
    *slot = meta->trial_slot;
    return 0;
  }

  /* NOTE: TRIAL_STATE_PENDING is the only state that routes to a specific trial slot.
   * All other states (IDLE, ACTIVE, CONFIRMED) fall through to the active slot below.
   * This means ACTIVE and CONFIRMED states are handled by the default active_slot path,
   * which is correct: the trial slot IS the active slot once the trial is activated. */

  /* use the active slot */
  *slot = meta->active_slot;
  return 0;
}

/**
 * @brief  perform trial boot state machine processing
 * @param  meta: pointer to metadata (mutable)
 * @retval none
 */
void process_trial_state(ota_metadata_t *meta)
{
  uint8_t other_slot;

  switch (meta->trial_state)
  {
    case TRIAL_STATE_IDLE:
      /* no trial in progress, nothing to do */
      break;

    case TRIAL_STATE_PENDING:
      /* trial requested: activate it */
      meta->trial_state       = TRIAL_STATE_ACTIVE;
      meta->trial_retry_count++;
      meta->last_boot_reason  = BOOT_REASON_OTA_ACT;
      boot_metadata_save(meta);
      break;

    case TRIAL_STATE_ACTIVE:
      /* trial is active: check timeout and retry count */
      if (meta->trial_retry_count > meta->trial_max_retries)
      {
        /* max retries exceeded, rollback */
        meta->rollback_count++;
        meta->trial_state       = TRIAL_STATE_IDLE;
        meta->trial_retry_count = 0;
        meta->last_boot_reason  = BOOT_REASON_ROLLBACK;

        /* switch to the other slot */
        other_slot = (meta->trial_slot == SLOT_A) ? SLOT_B : SLOT_A;
        if ((other_slot == SLOT_A && meta->slot_a_valid) ||
            (other_slot == SLOT_B && meta->slot_b_valid))
        {
          meta->active_slot = other_slot;
        }

        boot_metadata_save(meta);
      }
      break;

    case TRIAL_STATE_CONFIRMED:
      /* trial passed, clear trial state */
      meta->trial_state       = TRIAL_STATE_IDLE;
      meta->trial_retry_count = 0;
      boot_metadata_save(meta);
      break;

    default:
      /* invalid state, reset to idle */
      meta->trial_state = TRIAL_STATE_IDLE;
      boot_metadata_save(meta);
      break;
  }
}

/**
 * @brief  attempt to boot from a given slot
 * @param  slot: slot index (0=A, 1=B)
 * @param  meta: pointer to metadata
 * @retval 0 on success (jumped), -1 on failure
 */
int8_t try_boot_slot(uint8_t slot, ota_metadata_t *meta)
{
  uint32_t slot_addr;
  uint32_t slot_size;
  uint8_t  *valid_flag;

  slot_addr = boot_metadata_slot_addr(slot);
  slot_size = boot_metadata_slot_size(slot);

  if ((slot_addr == 0) || (slot_size == 0))
  {
    return -1;
  }

  /* verify image */
  if (boot_verify_image(slot_addr, slot_size) == 0)
  {
    /* image valid, update validity flag */
    if (slot == SLOT_A)
    {
      valid_flag = &meta->slot_a_valid;
    }
    else
    {
      valid_flag = &meta->slot_b_valid;
    }

    if (*valid_flag == 0)
    {
      *valid_flag = 1;
      boot_metadata_save(meta);
    }

    /* jump to application */
    boot_jump_to_app(slot_addr + IMAGE_HEADER_SIZE);

    /* should not reach here */
    return 0;
  }

  return -1;
}
