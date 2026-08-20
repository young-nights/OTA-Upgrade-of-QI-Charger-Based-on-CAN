/**
  **************************************************************************
  * @file     boot_verify.c
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

/* includes ------------------------------------------------------------------*/
#include "boot_verify.h"
#include "boot_metadata.h"
#include "uECC.h"
#include "sha256.h"

/* private constants ---------------------------------------------------------*/

/** @brief  ECDSA P-256 public key (uncompressed SEC1: 04 || x || y)
 *          Stored in a dedicated .rodata section.  The linker will place this
 *          in the bootloader flash region (0x08000000..0x08003FFF).
 *          A magic marker is appended after the key for corruption detection.
 *          Generated via: openssl ecparam -genkey -name prime256v1
 *          Private key stored at: docs/keys/private.pem (for host-side signing)
 */
__attribute__((section(".ecdsa_pubkey"), used))
const uint8_t g_ecdsa_public_key[65] = {
  0x04,  /* uncompressed point tag */
  /* 32-byte X coordinate */
  0x6a, 0x10, 0xf4, 0x3f, 0xb8, 0x9f, 0x45, 0x5b,
  0x3b, 0xd4, 0x9c, 0xa4, 0x87, 0x1b, 0xa0, 0x78,
  0xd0, 0xbb, 0x8f, 0x49, 0x14, 0x86, 0x91, 0xaa,
  0xf4, 0xf4, 0x85, 0x24, 0xc5, 0x5b, 0x94, 0x9d,
  /* 32-byte Y coordinate */
  0x07, 0xeb, 0x0a, 0x32, 0xae, 0xb8, 0xd8, 0x61,
  0x11, 0xc4, 0x91, 0x80, 0xc5, 0x00, 0xdc, 0x2e,
  0xe6, 0x0f, 0x28, 0x9f, 0xfb, 0x69, 0x77, 0x59,
  0x92, 0xc0, 0xd0, 0xd7, 0x2f, 0x0c, 0xe0, 0x43
};

/** @brief  Magic marker immediately after the public key (for corruption check) */
__attribute__((section(".ecdsa_pubkey"), used))
static const uint32_t g_pubkey_magic = ECDSA_PUBKEY_MAGIC;

/* exported functions --------------------------------------------------------*/

/**
 * @brief  get pointer to the ECDSA public key
 * @note   uses symbol address (not hard-coded), safe against linker movement
 * @retval pointer to 65-byte uncompressed SEC1 public key
 */
const uint8_t *boot_verify_get_public_key(void)
{
  /* verify magic marker to detect Flash corruption (e.g. code overwrite) */
  if (g_pubkey_magic != ECDSA_PUBKEY_MAGIC)
  {
    /* public key area corrupted — return NULL to force verification failure */
    return (const uint8_t *)0;
  }
  return g_ecdsa_public_key;
}

/**
 * @brief  get pointer to image header at a given base address
 * @param  base_addr: start address of the image slot
 * @retval pointer to image_header_t (memory-mapped)
 */
const image_header_t *boot_verify_get_header(uint32_t base_addr)
{
  return (const image_header_t *)base_addr;
}

/**
 * @brief  verify an application image at the given base address
 * @note   checks: (1) header magic, (2) image_length within slot bounds,
 *         (3) CRC32 of image data. signature verification is a placeholder.
 * @param  base_addr: start address of the image slot (header is at this address)
 * @param  slot_size: total size of the slot in bytes
 * @retval 0 if image is valid, -1 if verification fails
 */
int8_t boot_verify_image(uint32_t base_addr, uint32_t slot_size)
{
  const image_header_t *header;
  const uint8_t *image_data;
  uint32_t computed_crc;
  uint32_t max_image_len;

  /* cast base address to header pointer */
  header = (const image_header_t *)base_addr;

  /* check 1: verify magic number */
  if (header->magic != IMAGE_MAGIC)
  {
    return -1;
  }

  /* check 2: verify image_length is within slot bounds */
  /* slot_size minus header size is the maximum allowed image length */
  max_image_len = slot_size - IMAGE_HEADER_SIZE;
  if ((header->image_length == 0) || (header->image_length > max_image_len))
  {
    return -1;
  }

  /* check 3: verify CRC32 of image data (data follows the header) */
  image_data   = (const uint8_t *)(base_addr + IMAGE_HEADER_SIZE);
  computed_crc = boot_crc32((const void *)image_data, header->image_length);

  if (computed_crc != header->crc32)
  {
    return -1;
  }

  /* check 4: ECDSA P-256 signature verification */
  {
    const uint8_t *public_key;
    uint8_t image_hash[32];
    int verify_result;

    /* read the pre-provisioned public key (symbol-based, no hard-coded address) */
    public_key = boot_verify_get_public_key();
    if (public_key == (const uint8_t *)0)
    {
      return -1;
    }

    /* compute SHA-256 hash of the image data (excluding header) */
    sha256_hash(image_data, header->image_length, image_hash);

    /* verify ECDSA P-256 signature
     * public_key : 65-byte uncompressed SEC1 point (04 || x || y)
     * image_hash : 32-byte SHA-256 digest
     * signature  : 64-byte IEEE P1363 (R || S, each 32 bytes) */
    verify_result = uECC_verify(public_key, image_hash, header->signature);

    if (verify_result != 1)
    {
      return -1;
    }
  }

  return 0;
}
