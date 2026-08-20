/**
  **************************************************************************
  * @file     ota_trigger.h
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

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __OTA_TRIGGER_H
#define __OTA_TRIGGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* exported constants --------------------------------------------------------*/

/** @brief  Flash layout constants (must match bootloader definitions) */
#define OTA_APP_A_BASE_ADDR     0x08004000U   /*!< application A start address */
#define OTA_APP_A_SIZE          0xC000U       /*!< application A size: 48KB */
#define OTA_APP_B_BASE_ADDR     0x08010000U   /*!< application B start address */
#define OTA_APP_B_SIZE          0xC000U       /*!< application B size: 48KB */
#define OTA_META_PRIMARY_ADDR   0x0801C000U   /*!< primary metadata address */
#define OTA_META_BACKUP_ADDR    0x0801E000U   /*!< backup metadata address */
#define OTA_META_PAGE_SIZE      0x2000U       /*!< metadata page size: 8KB */
#define OTA_IMAGE_HEADER_SIZE   256U          /*!< image header size in bytes */

/** @brief  OTA metadata magic and version */
#define OTA_META_MAGIC          0x4F54414DU   /*!< "MATO" */
#define OTA_META_VERSION        1U            /*!< metadata format version */

/** @brief  Slot definitions */
#define OTA_SLOT_NONE           0xFEU         /*!< no pending slot */
#define OTA_SLOT_A              0U            /*!< slot A index */
#define OTA_SLOT_B              1U            /*!< slot B index */

/** @brief  OTA state codes */
#define OTA_STATE_IDLE          0x00U         /*!< no OTA in progress */
#define OTA_STATE_DOWNLOADING   0x01U         /*!< OTA download in progress */

/** @brief  Flash sector size for AT32F426 */
#define OTA_FLASH_SECTOR_SIZE   0x800U        /*!< 2KB per sector */

/* exported types ------------------------------------------------------------*/

/**
 * @brief  OTA metadata structure (must match bootloader definition)
 * @note   stored in Flash at OTA_META_PRIMARY_ADDR and OTA_META_BACKUP_ADDR.
 *         CRC32 covers all fields except the trailing crc32 field itself.
 */
typedef struct
{
  uint32_t magic;               /*!< 0x4F54414D "MATO" */
  uint32_t version;             /*!< metadata format version = 1 */
  uint8_t  active_slot;         /*!< 0=A, 1=B */
  uint8_t  pending_slot;        /*!< 0=A, 1=B, 0xFE=none */
  uint8_t  slot_a_valid;        /*!< 1=slot A image valid */
  uint8_t  slot_b_valid;        /*!< 1=slot B image valid */
  uint32_t slot_a_crc32;        /*!< CRC32 of slot A image */
  uint32_t slot_b_crc32;        /*!< CRC32 of slot B image */
  uint8_t  trial_state;         /*!< 0=IDLE, 1=PENDING, 2=ACTIVE, 3=CONFIRMED */
  uint8_t  trial_slot;          /*!< slot under trial (0=A, 1=B) */
  uint8_t  trial_retry_count;   /*!< current retry count */
  uint8_t  trial_max_retries;   /*!< max retries (default 3) */
  uint16_t trial_timeout_sec;   /*!< trial timeout in seconds (default 10) */
  uint16_t reserved1;           /*!< reserved for alignment */
  uint32_t rollback_count;      /*!< number of rollbacks performed */
  uint8_t  last_boot_reason;    /*!< 0x00=power-on, 0x02=WDG, 0x03=OTA, 0x04=rollback */
  uint8_t  ota_state;           /*!< 0x00=idle, 0x01=downloading */
  uint8_t  reserved2[2];        /*!< reserved */
  uint8_t  padding[232];        /*!< padding to 276 bytes (must match bootloader) */
  uint32_t crc32;               /*!< CRC32 of all above fields */
} ota_metadata_t;

/* exported functions -------------------------------------------------------*/

/**
 * @brief  trigger OTA upgrade mode
 * @note   reads current metadata from primary flash, sets ota_state to
 *         OTA_STATE_DOWNLOADING, saves metadata, then performs a system reset.
 *         this function does NOT return.
 * @param  none
 * @retval none (does not return)
 */
void ota_trigger_request(void);

/**
 * @brief  read metadata from primary flash location
 * @param  meta: pointer to metadata structure to fill
 * @retval 0 on success (valid metadata), -1 on failure (invalid or read error)
 */
int8_t ota_metadata_read(ota_metadata_t *meta);

/**
 * @brief  save metadata to primary flash location only
 * @note   writes only to OTA_META_PRIMARY_ADDR to avoid destroying NVM config
 *         at OTA_META_BACKUP_ADDR (shared area).
 * @param  meta: pointer to metadata structure to save
 * @retval 0 on success, -1 on flash write error
 */
int8_t ota_metadata_save(const ota_metadata_t *meta);

/**
 * @brief  compute CRC32 (IEEE 802.3, polynomial 0xEDB88320)
 * @param  data: pointer to data
 * @param  length: number of bytes
 * @retval CRC32 value
 */
uint32_t ota_crc32(const void *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_TRIGGER_H */
