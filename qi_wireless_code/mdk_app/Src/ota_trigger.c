/**
  **************************************************************************
  * @file     ota_trigger.c
  * @brief    OTA trigger module for APP firmware
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
#include "ota_trigger.h"
#include <string.h>

/* private define ------------------------------------------------------------*/

/** @brief  byte offset of the crc32 field within ota_metadata_t */
#define META_CRC32_OFFSET    (sizeof(ota_metadata_t) - sizeof(uint32_t))

/* private functions ---------------------------------------------------------*/

/**
 * @brief  validate metadata structure: check magic, version, and CRC32
 * @param  meta: pointer to metadata to validate
 * @retval 0 if valid, -1 if invalid
 */
static int8_t meta_validate(const ota_metadata_t *meta)
{
  uint32_t computed_crc;
  uint32_t stored_crc;

  if (meta->magic != OTA_META_MAGIC)
  {
    return -1;
  }

  if (meta->version != OTA_META_VERSION)
  {
    return -1;
  }

  stored_crc   = meta->crc32;
  computed_crc = ota_crc32((const void *)meta, META_CRC32_OFFSET);

  if (computed_crc != stored_crc)
  {
    return -1;
  }

  return 0;
}

/**
 * @brief  fill metadata with default values
 * @param  meta: pointer to metadata to initialize
 * @retval none
 */
static void meta_fill_defaults(ota_metadata_t *meta)
{
  memset((void *)meta, 0, sizeof(ota_metadata_t));

  meta->magic             = OTA_META_MAGIC;
  meta->version           = OTA_META_VERSION;
  meta->active_slot       = OTA_SLOT_A;
  meta->pending_slot      = OTA_SLOT_NONE;
  meta->slot_a_valid      = 0;
  meta->slot_b_valid      = 0;
  meta->slot_a_crc32      = 0;
  meta->slot_b_crc32      = 0;
  meta->trial_state       = 0;  /* TRIAL_STATE_IDLE */
  meta->trial_slot        = OTA_SLOT_A;
  meta->trial_retry_count = 0;
  meta->trial_max_retries = 3;
  meta->trial_timeout_sec = 10;
  meta->reserved1         = 0;
  meta->rollback_count    = 0;
  meta->last_boot_reason  = 0;
  meta->ota_state         = OTA_STATE_IDLE;
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  compute CRC32 (IEEE 802.3, polynomial 0xEDB88320)
 * @param  data: pointer to data
 * @param  length: number of bytes
 * @retval CRC32 value
 */
uint32_t ota_crc32(const void *data, uint32_t length)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t i;
  uint32_t j;
  uint32_t bit;

  for (i = 0; i < length; i++)
  {
    crc ^= (uint32_t)p[i];
    for (j = 0; j < 8; j++)
    {
      bit = crc & 1U;
      crc >>= 1;
      if (bit != 0U)
      {
        crc ^= 0xEDB88320U;
      }
    }
  }

  return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief  read metadata from primary flash location
 * @param  meta: pointer to metadata structure to fill
 * @retval 0 on success (valid metadata), -1 on failure (invalid or read error)
 */
int8_t ota_metadata_read(ota_metadata_t *meta)
{
  const ota_metadata_t *primary;
  const ota_metadata_t *backup;

  /* try primary metadata */
  primary = (const ota_metadata_t *)OTA_META_PRIMARY_ADDR;
  if (meta_validate(primary) == 0)
  {
    memcpy((void *)meta, (const void *)primary, sizeof(ota_metadata_t));
    return 0;
  }

  /* primary invalid, try backup */
  backup = (const ota_metadata_t *)OTA_META_BACKUP_ADDR;
  if (meta_validate(backup) == 0)
  {
    memcpy((void *)meta, (const void *)backup, sizeof(ota_metadata_t));
    return 0;
  }

  /* both invalid, use defaults */
  meta_fill_defaults(meta);
  return -1;
}

/**
 * @brief  save metadata to primary flash location only
 * @note   writes only to OTA_META_PRIMARY_ADDR to avoid destroying NVM config
 *         at OTA_META_BACKUP_ADDR (shared area).
 * @param  meta: pointer to metadata structure to save
 * @retval 0 on success, -1 on flash write error
 */
int8_t ota_metadata_save(const ota_metadata_t *meta)
{
  const uint32_t *src;
  uint32_t words;
  uint32_t i;
  flash_status_type status;
  ota_metadata_t meta_copy;

  /* make a mutable copy to compute CRC */
  memcpy((void *)&meta_copy, (const void *)meta, sizeof(ota_metadata_t));
  meta_copy.crc32 = ota_crc32((const void *)&meta_copy, META_CRC32_OFFSET);

  /* erase primary metadata page (8KB = 4 sectors of 2KB) */
  flash_unlock();
  for (i = 0; i < (OTA_META_PAGE_SIZE / OTA_FLASH_SECTOR_SIZE); i++)
  {
    status = flash_sector_erase(OTA_META_PRIMARY_ADDR + (i * OTA_FLASH_SECTOR_SIZE));
    if (status != FLASH_OPERATE_DONE)
    {
      flash_lock();
      return -1;
    }
  }

  /* write metadata word by word */
  src   = (const uint32_t *)&meta_copy;
  words = sizeof(ota_metadata_t) / sizeof(uint32_t);

  for (i = 0; i < words; i++)
  {
    status = flash_word_program(OTA_META_PRIMARY_ADDR + (i * 4U), src[i]);
    if (status != FLASH_OPERATE_DONE)
    {
      flash_lock();
      return -1;
    }
  }

  flash_lock();
  return 0;
}

/**
 * @brief  trigger OTA upgrade mode
 * @note   reads current metadata, sets ota_state to OTA_STATE_DOWNLOADING,
 *         saves metadata to primary flash, then performs a system reset.
 *         this function does NOT return.
 * @param  none
 * @retval none (does not return)
 */
void ota_trigger_request(void)
{
  ota_metadata_t meta;

  /* read current metadata */
  ota_metadata_read(&meta);

  /* set OTA state to downloading */
  meta.ota_state = OTA_STATE_DOWNLOADING;

  /* save metadata to primary flash */
  ota_metadata_save(&meta);

  /* perform system reset - bootloader will enter safe mode for download */
  NVIC_SystemReset();

  /* should never reach here */
  while (1)
  {
  }
}
