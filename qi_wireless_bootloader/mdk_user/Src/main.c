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
#include "can_driver.h"
#include "boot_metadata.h"
#include "boot_verify.h"
#include "boot_jump.h"
#include <string.h>

/* private define ------------------------------------------------------------*/

/** @brief  safe mode CAN IDs for OTA download */
#define SAFE_MODE_CAN_ID_REQUEST    0x18DA0D03U  /*!< UDS request ID */
#define SAFE_MODE_CAN_ID_RESPONSE   0x18DA030DU  /*!< UDS response ID */

/** @brief  UDS service IDs supported in safe mode */
#define UDS_DIAG_SESSION_CTRL       0x10U
#define UDS_SECURITY_ACCESS         0x27U
#define UDS_REQUEST_DOWNLOAD        0x34U
#define UDS_TRANSFER_DATA           0x36U
#define UDS_REQUEST_TRANSFER_EXIT   0x37U
#define UDS_ECU_RESET               0x11U

/** @brief  UDS negative response code */
#define UDS_NEGATIVE_RESPONSE       0x7FU
#define UDS_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U

/** @brief  trial boot timer period (1 second) */
#define TRIAL_TIMER_PERIOD_MS       1000U

/* private variables ---------------------------------------------------------*/

/** @brief  OTA metadata instance */
static ota_metadata_t g_meta;

/** @brief  trial boot elapsed seconds counter */
static volatile uint32_t g_trial_elapsed_sec = 0;

/** @brief  trial timer expired flag (set from timer callback) */
static volatile uint8_t g_trial_timer_flag = 0;

/** @brief  safe mode flag */
static uint8_t g_safe_mode = 0;

/* private functions ---------------------------------------------------------*/

/**
 * @brief  trial boot timer callback (called every 1 second)
 * @param  none
 * @retval none
 */
static void trial_timer_callback(void)
{
  g_trial_elapsed_sec++;
  g_trial_timer_flag = 1;
}

/**
 * @brief  detect reset source and return boot reason code
 * @param  none
 * @retval boot reason code
 */
static uint8_t detect_boot_reason(void)
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
static int8_t select_boot_slot(const ota_metadata_t *meta, uint8_t *slot)
{
  /* check if there is a pending trial */
  if (meta->trial_state == TRIAL_STATE_PENDING)
  {
    /* use the trial slot */
    *slot = meta->trial_slot;
    return 0;
  }

  /* use the active slot */
  *slot = meta->active_slot;
  return 0;
}

/**
 * @brief  perform trial boot state machine processing
 * @param  meta: pointer to metadata (mutable)
 * @retval none
 */
static void process_trial_state(ota_metadata_t *meta)
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
 * @brief  send a CAN response frame
 * @param  data: pointer to response data
 * @param  len: data length
 * @retval none
 */
static void safe_mode_send_response(uint8_t *data, uint8_t len)
{
  can_driver_send(SAFE_MODE_CAN_ID_RESPONSE, data, len);
}

/**
 * @brief  send UDS negative response
 * @param  service_id: the rejected service ID
 * @param  nrc: negative response code
 * @retval none
 */
static void safe_mode_send_nrc(uint8_t service_id, uint8_t nrc)
{
  uint8_t resp[3];
  resp[0] = UDS_NEGATIVE_RESPONSE;
  resp[1] = service_id;
  resp[2] = nrc;
  safe_mode_send_response(resp, 3);
}

/**
 * @brief  handle UDS request in safe mode
 * @param  id: CAN frame ID
 * @param  data: pointer to frame data
 * @param  len: frame data length
 * @retval none
 */
static void safe_mode_can_rx_handler(uint32_t id, uint8_t *data, uint8_t len)
{
  uint8_t service_id;
  uint8_t resp[8];

  (void)id;

  if (len == 0)
  {
    return;
  }

  service_id = data[0];

  switch (service_id)
  {
    case UDS_DIAG_SESSION_CTRL:
      /* DiagnosticSessionControl: positive response */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = data[1];
      safe_mode_send_response(resp, 2);
      break;

    case UDS_SECURITY_ACCESS:
      /* SecurityAccess: accept any seed/key for bootloader (simplified) */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = data[1];
      safe_mode_send_response(resp, 2);
      break;

    case UDS_REQUEST_DOWNLOAD:
      /* RequestDownload: acknowledge (actual flash write handled by transfer) */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = 0x20;  /* lengthFormatIdentifier */
      resp[2] = 0x00;
      resp[3] = 0x00;
      resp[4] = 0x10;
      safe_mode_send_response(resp, 5);
      break;

    case UDS_TRANSFER_DATA:
      /* TransferData: acknowledge data block */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = data[1];  /* blockSequenceCounter */
      safe_mode_send_response(resp, 2);
      break;

    case UDS_REQUEST_TRANSFER_EXIT:
      /* RequestTransferExit: acknowledge */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      safe_mode_send_response(resp, 1);
      break;

    case UDS_ECU_RESET:
      /* ECUReset: acknowledge then reset via watchdog */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      safe_mode_send_response(resp, 1);
      /* let watchdog reset us */
      while(1)
      {
        /* wait for watchdog reset */
      }

    default:
      /* unsupported service */
      safe_mode_send_nrc(service_id, UDS_SERVICE_NOT_SUPPORTED);
      break;
  }
}

/**
 * @brief  enter safe mode: initialize CAN and wait for OTA download
 * @note   called when both application slots are invalid.
 *         runs a minimal event loop with CAN polling and watchdog refresh.
 * @param  none
 * @retval none (does not return unless watchdog resets)
 */
static void enter_safe_mode(void)
{
  uint8_t tmr_id;

  g_safe_mode = 1;

  /* initialize CAN for safe mode communication */
  can_driver_init();
  can_driver_register_rx_callback(safe_mode_can_rx_handler);

  /* create a periodic timer for safe mode housekeeping (optional) */
  tmr_id = timer_create(500, (void (*)(void))0, 1);
  if (tmr_id != TIMER_INVALID_ID)
  {
    timer_start(tmr_id);
  }

  /* safe mode event loop */
  while (1)
  {
    timer_poll();
    can_driver_poll();
    wdg_drv_refresh();
  }
}

/**
 * @brief  attempt to boot from a given slot
 * @param  slot: slot index (0=A, 1=B)
 * @param  meta: pointer to metadata
 * @retval 0 on success (jumped), -1 on failure
 */
static int8_t try_boot_slot(uint8_t slot, ota_metadata_t *meta)
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
