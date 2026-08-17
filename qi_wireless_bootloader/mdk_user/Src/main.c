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
#define UDS_GENERAL_PROGRAMMING_FAIL 0x72U
#define UDS_REQUEST_OUT_OF_RANGE     0x31U
#define UDS_TRANSFER_DATA_ABORTED    0x71U
#define UDS_UPLOAD_DOWNLOAD_NOT_ACCEPTED 0x70U

/** @brief  trial boot timer period (1 second) */
#define TRIAL_TIMER_PERIOD_MS       1000U

/** @brief  flash sector size for AT32F426 */
#define FLASH_SECTOR_SIZE           0x800U       /*!< 2KB per sector */

/** @brief  maximum image size (APP_A - header) */
#define MAX_IMAGE_SIZE              (APP_A_SIZE - IMAGE_HEADER_SIZE)

/** @brief  UDS TransferData max data payload per frame (8 - SID - blockSeq) */
#define UDS_TRANSFER_MAX_PAYLOAD    6U

/* private variables ---------------------------------------------------------*/

/** @brief  OTA metadata instance */
static ota_metadata_t g_meta;

/** @brief  trial boot elapsed seconds counter */
static volatile uint32_t g_trial_elapsed_sec = 0;

/** @brief  trial timer expired flag (set from timer callback) */
static volatile uint8_t g_trial_timer_flag = 0;

/** @brief  safe mode flag */
static uint8_t g_safe_mode = 0;

/** @brief  OTA download state */
static uint32_t g_dl_write_addr    = 0;  /*!< current flash write address */
static uint32_t g_dl_bytes_written = 0;  /*!< total bytes written */
static uint8_t  g_dl_active        = 0;  /*!< download in progress flag */
static uint8_t  g_dl_block_seq     = 0;  /*!< expected block sequence counter */

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
    {
      uint32_t sector_addr;
      uint8_t erase_err = 0;

      /* erase APP_A flash area (all sectors) */
      flash_unlock();
      for (sector_addr = APP_A_BASE_ADDR;
           sector_addr < (APP_A_BASE_ADDR + APP_A_SIZE);
           sector_addr += FLASH_SECTOR_SIZE)
      {
        if (flash_sector_erase(sector_addr) != FLASH_OPERATE_DONE)
        {
          erase_err = 1;
          break;
        }
      }
      flash_lock();

      if (erase_err)
      {
        safe_mode_send_nrc(service_id, UDS_GENERAL_PROGRAMMING_FAIL);
        break;
      }

      /* initialize download state */
      g_dl_write_addr    = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
      g_dl_bytes_written = 0;
      g_dl_block_seq     = 0;
      g_dl_active        = 1;

      /* positive response: SID + lengthFormatIdentifier + maxBlockLength */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = 0x20;  /* lengthFormatIdentifier: 2 bytes for max block length */
      resp[2] = 0x00;
      resp[3] = (uint8_t)((MAX_IMAGE_SIZE >> 8) & 0xFFU);
      resp[4] = (uint8_t)(MAX_IMAGE_SIZE & 0xFFU);
      safe_mode_send_response(resp, 5);
      break;
    }

    case UDS_TRANSFER_DATA:
    {
      uint8_t block_seq;
      uint8_t data_len;
      uint8_t i;
      flash_status_type flash_status;

      /* check if download is active */
      if (!g_dl_active)
      {
        safe_mode_send_nrc(service_id, UDS_TRANSFER_DATA_ABORTED);
        break;
      }

      /* validate minimum frame length: SID + blockSeq + at least 1 data byte */
      if (len < 3U)
      {
        safe_mode_send_nrc(service_id, UDS_REQUEST_OUT_OF_RANGE);
        break;
      }

      block_seq = data[1];

      /* verify block sequence counter */
      g_dl_block_seq++;
      if (block_seq != g_dl_block_seq)
      {
        /* sequence error: abort download */
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, UDS_TRANSFER_DATA_ABORTED);
        break;
      }

      /* calculate actual data length (frame length minus SID and blockSeq) */
      data_len = len - 2U;

      /* check bounds */
      if ((g_dl_bytes_written + (uint32_t)data_len) > MAX_IMAGE_SIZE)
      {
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, UDS_REQUEST_OUT_OF_RANGE);
        break;
      }

      /* write data to flash */
      flash_unlock();
      for (i = 0; i < data_len; i += 4U)
      {
        uint32_t word_data = 0xFFFFFFFFU;
        uint8_t  remaining = data_len - i;
        uint8_t  copy_len  = (remaining > 4U) ? 4U : remaining;
        uint8_t  k;

        /* pack up to 4 bytes into a word (little-endian) */
        for (k = 0; k < copy_len; k++)
        {
          word_data &= ~((uint32_t)0xFFU << (k * 8U));
          word_data |= ((uint32_t)data[2U + i + k] << (k * 8U));
        }

        flash_status = flash_word_program(g_dl_write_addr + (uint32_t)i, word_data);
        if (flash_status != FLASH_OPERATE_DONE)
        {
          flash_lock();
          g_dl_active = 0;
          safe_mode_send_nrc(service_id, UDS_GENERAL_PROGRAMMING_FAIL);
          break;
        }
      }
      flash_lock();

      if (!g_dl_active)
      {
        break;  /* flash write failed above */
      }

      /* advance write pointer and byte counter */
      g_dl_write_addr    += (uint32_t)data_len;
      g_dl_bytes_written += (uint32_t)data_len;

      /* positive response */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = block_seq;
      safe_mode_send_response(resp, 2);
      break;
    }

    case UDS_REQUEST_TRANSFER_EXIT:
    {
      uint32_t computed_crc;
      uint32_t image_data_addr;
      uint32_t sector_addr;
      image_header_t header;
      const uint32_t *hdr_words;
      uint32_t hdr_word_count;
      uint32_t w;

      if (!g_dl_active)
      {
        safe_mode_send_nrc(service_id, UDS_TRANSFER_DATA_ABORTED);
        break;
      }

      /* mark download as complete */
      g_dl_active = 0;

      /* compute CRC32 of the downloaded image data */
      image_data_addr = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
      computed_crc = boot_crc32((const void *)image_data_addr, g_dl_bytes_written);

      /* prepare image header */
      memset((void *)&header, 0xFF, sizeof(image_header_t));
      header.magic        = IMAGE_MAGIC;
      header.image_length = g_dl_bytes_written;
      header.crc32        = computed_crc;

      /* write image header to flash at APP_A_BASE_ADDR */
      flash_unlock();
      hdr_words     = (const uint32_t *)&header;
      hdr_word_count = sizeof(image_header_t) / sizeof(uint32_t);
      for (w = 0; w < hdr_word_count; w++)
      {
        flash_word_program(APP_A_BASE_ADDR + (w * 4U), hdr_words[w]);
      }
      flash_lock();

      /* update metadata: mark slot A as valid with correct CRC */
      g_meta.slot_a_valid = 1;
      g_meta.slot_a_crc32 = computed_crc;
      boot_metadata_save(&g_meta);

      /* positive response */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      safe_mode_send_response(resp, 1);
      break;
    }

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

  /* step 5.5: check if OTA download was requested by APP */
  if (g_meta.ota_state == OTA_STATE_DOWNLOADING)
  {
    /* clear the OTA state so we don't loop */
    g_meta.ota_state = OTA_STATE_IDLE;
    boot_metadata_save(&g_meta);

    /* enter safe mode to receive firmware via CAN */
    enter_safe_mode();
    /* does not return */
  }

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
