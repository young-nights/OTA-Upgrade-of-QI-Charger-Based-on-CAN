/**
  **************************************************************************
  * @file     boot_verify.h
  * @brief    Image verification for bootloader (CRC32 + signature placeholder)
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
#ifndef __BOOT_VERIFY_H
#define __BOOT_VERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* exported constants --------------------------------------------------------*/

/** @brief  image header magic number: "XATO" */
#define IMAGE_MAGIC   0x4F544158U

/* exported types ------------------------------------------------------------*/

/**
 * @brief  application image header (256 bytes)
 * @note   placed at the start of each application slot.
 *         the actual application code follows immediately after the header.
 */
typedef struct
{
  uint32_t magic;              /*!< 0x4F544158 "XATO" */
  uint32_t image_length;       /*!< valid image size in bytes (excluding header) */
  uint32_t crc32;              /*!< CRC32 of image data (excluding header) */
  uint8_t  signature[64];      /*!< ECDSA P-256 R||S signature (placeholder) */
  char     version[16];        /*!< "MAJOR.MINOR.PATCH\0" */
  uint32_t build_timestamp;    /*!< Unix timestamp of build */
  uint8_t  reserved[156];      /*!< padding to 256 bytes */
} image_header_t;

/* exported functions -------------------------------------------------------*/

/**
 * @brief  verify an application image at the given base address
 * @note   checks: (1) header magic, (2) image_length within slot bounds,
 *         (3) CRC32 of image data. signature is currently a placeholder.
 * @param  base_addr: start address of the image slot (header is at this address)
 * @param  slot_size: total size of the slot in bytes
 * @retval 0 if image is valid, -1 if verification fails
 */
int8_t boot_verify_image(uint32_t base_addr, uint32_t slot_size);

/**
 * @brief  get pointer to image header at a given base address
 * @param  base_addr: start address of the image slot
 * @retval pointer to image_header_t (memory-mapped)
 */
const image_header_t *boot_verify_get_header(uint32_t base_addr);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_VERIFY_H */
