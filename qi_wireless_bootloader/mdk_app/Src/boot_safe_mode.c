/**
  **************************************************************************
  * @file     boot_safe_mode.c
  * @brief    Safe mode implementation: UDS OTA download via CAN
  **************************************************************************
  *
  * @note    Current implementation supports single-frame UDS only (8 bytes per CAN frame).
  *          For 48KB firmware, ~8192 frames are needed. ISO-TP multi-frame support
  *          can be added in a future version for improved throughput.
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
#include "boot_safe_mode.h"
#include "boot_metadata.h"
#include "boot_verify.h"
#include "boot_trial.h"
#include "can_driver.h"
#include "timer_drv.h"
#include "wdg_drv.h"
#include "uECC.h"
#include "sha256.h"
#include <string.h>

/** @brief  Software version string for DID 0xF195 */
#define SW_VERSION  "1.0.0"

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
#define UDS_TRANSFER_SIGNATURE      0x38U
#define UDS_ECU_RESET               0x11U
#define UDS_READ_DATA_BY_ID         0x22U
#define UDS_WRITE_DATA_BY_ID        0x2EU
#define UDS_ROUTINE_CONTROL         0x31U

/** @brief  UDS negative response code */
#define UDS_NEGATIVE_RESPONSE       0x7FU
#define UDS_NRC_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12U
#define UDS_NRC_INCORRECT_MSG_LENGTH  0x13U
#define UDS_NRC_RESPONSE_TOO_LONG     0x14U
#define UDS_NRC_TRANSFER_DATA_ABORTED 0x71U
#define UDS_NRC_GENERAL_PROGRAMMING_FAILURE 0x72U
#define UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS 0x35U
#define UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED 0x37U
#define UDS_NRC_SECURITY_ACCESS_DENIED 0x33U
#define UDS_NRC_REQUEST_OUT_OF_RANGE    0x31U
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U

/* private variables ---------------------------------------------------------*/

/** @brief  safe mode flag */
static uint8_t g_safe_mode = 0;

/** @brief  download state variables */
static uint32_t g_dl_write_addr    = 0;
static uint32_t g_dl_bytes_written = 0;
static uint8_t  g_dl_block_seq     = 0;
static uint8_t  g_dl_active        = 0;

/** @brief  maximum image size (APP_A size minus header) */
#define MAX_IMAGE_SIZE    (APP_A_SIZE - IMAGE_HEADER_SIZE)

/** @brief  signature transfer buffer (accumulated from 0x38 frames) */
static uint8_t  g_sig_buf[64];
static uint8_t  g_sig_bytes_received = 0;
static uint8_t  g_sig_block_seq = 0;
static uint8_t  g_sig_active = 0;

/** @brief  security access state */
static uint8_t  g_security_unlocked = 0;
static uint8_t  g_seed_generated = 0;
static uint8_t  g_seed[4];
static uint8_t  g_seed_sub = 0;
static uint8_t  g_security_fail_count = 0;
static uint32_t g_security_lockout_until_ms = 0;

/** @brief  firmware type selected by 0x2E 0x2010 (0=app, 1=bootloader) */
static uint8_t  g_firmware_type = 0;

/** @brief  SecurityAccess ECDSA signature buffer */
static uint8_t  g_sa_sig_buf[64];
static uint8_t  g_sa_sig_bytes_received = 0;
static uint8_t  g_sa_sig_block_seq = 0;

/** @brief  security access lockout: 60 seconds */
#define SECURITY_LOCKOUT_MS  60000U

/** @brief  maximum consecutive security access failures before lockout */
#define SECURITY_MAX_FAILURES 3U

/* private functions ---------------------------------------------------------*/

/**
 * @brief  generate a pseudo-random 32-bit seed using SysTick and LFSR
 * @retval 32-bit pseudo-random value
 */
static uint32_t generate_random_seed(void)
{
  static uint32_t lfsr = 0x12345678U;
  uint32_t tick = timer_get_tick();
  uint32_t bit;

  /* XOR tick into LFSR state for entropy */
  lfsr ^= tick;

  /* 32-bit maximal-length LFSR (taps at bits 31, 21, 1, 0) */
  bit = ((lfsr >> 0) ^ (lfsr >> 1) ^ (lfsr >> 21) ^ (lfsr >> 31)) & 1U;
  lfsr = (lfsr >> 1) | (bit << 31);

  /* additional mixing with current tick */
  lfsr ^= (tick << 7) ^ (tick >> 13);

  return lfsr;
}

/**
 * @brief  verify ECDSA P-256 signature over seed
 * @note   Verifies the 64-byte IEEE P1363 signature that the host computed
 *         over SHA256(seed) using the pre-provisioned public key.
 * @retval 1 if signature is valid, 0 if invalid
 */
static uint8_t verify_security_ecdsa_signature(void)
{
  const uint8_t *public_key;
  uint8_t seed_hash[32];
  int result;

  /* get the pre-provisioned public key */
  public_key = boot_verify_get_public_key();
  if (public_key == (const uint8_t *)0)
  {
    return 0U;
  }

  /* compute SHA-256 of the 4-byte seed */
  sha256_hash(g_seed, 4U, seed_hash);

  /* verify ECDSA P-256 signature */
  result = uECC_verify(public_key, seed_hash, g_sa_sig_buf);

  return (result == 1) ? 1U : 0U;
}

/**
 * @brief  erase all APP_A flash sectors
 * @retval 0 on success, non-zero on error
 */
static uint8_t erase_app_a_flash(void)
{
  uint32_t sector_addr;

  flash_unlock();
  for (sector_addr = APP_A_BASE_ADDR;
       sector_addr < (APP_A_BASE_ADDR + APP_A_SIZE);
       sector_addr += 0x800U)  /* 2KB sectors for AT32F426 */
  {
    if (flash_sector_erase(sector_addr) != FLASH_OPERATE_DONE)
    {
      flash_lock();
      return 1U;
    }
  }
  flash_lock();
  return 0U;
}

/**
 * @brief  update security lockout timer (call from safe mode main loop)
 * @retval none
 */
static void safe_mode_update_security_timer(void)
{
  /* lockout expires when system tick reaches the deadline */
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
    {
      uint8_t sub_func;

      if (len < 2U)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
        break;
      }

      sub_func = data[1];

      if (sub_func == 0x01U)
      {
        /* Sub-function 0x01: Request Seed */
        uint32_t now_ms;

        /* check security lockout */
        now_ms = timer_get_tick();
        if (g_security_fail_count >= SECURITY_MAX_FAILURES)
        {
          if ((int32_t)(now_ms - g_security_lockout_until_ms) < 0)
          {
            /* still in lockout period */
            safe_mode_send_nrc(service_id, UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED);
            break;
          }
          /* lockout expired, reset failure counter */
          g_security_fail_count = 0;
        }

        /* generate random 4-byte seed */
        {
          uint32_t seed_val = generate_random_seed();
          g_seed[0] = (uint8_t)((seed_val >> 24) & 0xFFU);
          g_seed[1] = (uint8_t)((seed_val >> 16) & 0xFFU);
          g_seed[2] = (uint8_t)((seed_val >> 8) & 0xFFU);
          g_seed[3] = (uint8_t)(seed_val & 0xFFU);
        }
        g_seed_generated = 1;
        g_seed_sub = sub_func;

        /* reset signature accumulation state */
        g_sa_sig_bytes_received = 0;
        g_sa_sig_block_seq = 0;

        /* positive response: SID + sub + seed */
        resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
        resp[1] = sub_func;
        resp[2] = g_seed[0];
        resp[3] = g_seed[1];
        resp[4] = g_seed[2];
        resp[5] = g_seed[3];
        safe_mode_send_response(resp, 6);
      }
      else if (sub_func == 0x03U)
      {
        /* Sub-function 0x03: Transfer Signature Chunk (for ECDSA P-256)
         * Host sends 64-byte signature in chunks: 6 bytes per frame.
         * Frame format: [0x27, 0x03, blockSeq, sig_byte0..sig_byte5]
         * First frame resets accumulation.  Total ~11 frames for 64 bytes. */
        uint8_t block_seq;
        uint8_t chunk_len;
        uint8_t i;

        if (!g_seed_generated || g_seed_sub != 0x01U)
        {
          safe_mode_send_nrc(service_id, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
          break;
        }

        if (len < 3U)
        {
          safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
          break;
        }

        block_seq = data[2];

        /* first frame: blockSeq == 1, reset buffer */
        if (block_seq == 0x01U)
        {
          g_sa_sig_block_seq = 0;
          g_sa_sig_bytes_received = 0;
          memset(g_sa_sig_buf, 0, 64);
        }

        g_sa_sig_block_seq++;
        if (block_seq != g_sa_sig_block_seq)
        {
          /* sequence error */
          g_sa_sig_bytes_received = 0;
          safe_mode_send_nrc(service_id, UDS_NRC_TRANSFER_DATA_ABORTED);
          break;
        }

        chunk_len = len - 3U; /* subtract SID, sub_func, blockSeq */
        if ((g_sa_sig_bytes_received + chunk_len) > 64U)
        {
          g_sa_sig_bytes_received = 0;
          safe_mode_send_nrc(service_id, UDS_NRC_RESPONSE_TOO_LONG);
          break;
        }

        /* accumulate signature data */
        for (i = 0; i < chunk_len; i++)
        {
          g_sa_sig_buf[g_sa_sig_bytes_received + i] = data[3U + i];
        }
        g_sa_sig_bytes_received += chunk_len;

        /* positive response */
        resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
        resp[1] = sub_func;
        resp[2] = block_seq;
        safe_mode_send_response(resp, 3);
      }
      else if (sub_func == 0x02U)
      {
        /* Sub-function 0x02: Verify Signature
         * Host has finished sending 64B signature via 0x03 chunks.
         * MCU now verifies SHA256(seed) + signature using ECDSA P-256. */
        if (!g_seed_generated || g_seed_sub != 0x01U)
        {
          /* no seed was requested first */
          safe_mode_send_nrc(service_id, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
          break;
        }

        if (g_sa_sig_bytes_received != 64U)
        {
          /* incomplete signature */
          g_seed_generated = 0;
          g_sa_sig_bytes_received = 0;
          safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
          break;
        }

        /* verify the ECDSA P-256 signature */
        if (verify_security_ecdsa_signature())
        {
          /* signature valid: unlock security */
          g_security_unlocked = 1;
          g_security_fail_count = 0;
          g_seed_generated = 0;
          g_sa_sig_bytes_received = 0;

          resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
          resp[1] = sub_func;
          safe_mode_send_response(resp, 2);
        }
        else
        {
          /* signature invalid: increment failure counter */
          g_security_fail_count++;
          g_seed_generated = 0;
          g_sa_sig_bytes_received = 0;

          if (g_security_fail_count >= SECURITY_MAX_FAILURES)
          {
            /* enter lockout period */
            g_security_lockout_until_ms = timer_get_tick() + SECURITY_LOCKOUT_MS;
            safe_mode_send_nrc(service_id, UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS);
          }
          else
          {
            safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
          }
        }
      }
      else
      {
        /* unsupported sub-function */
        safe_mode_send_nrc(service_id, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
      }
      break;
    }

    case UDS_READ_DATA_BY_ID:
    {
      uint16_t did;

      if (len < 3U)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
        break;
      }

      did = ((uint16_t)data[1] << 8) | (uint16_t)data[2];

      if (did == 0xF195U)
      {
        /* DID 0xF195: Software Version */
        const char *ver = SW_VERSION;
        uint8_t ver_len = (uint8_t)strlen(ver);
        uint8_t pos;

        if (ver_len > 5U) ver_len = 5U; /* fit in single frame: SID+DID+5 chars = 8 */

        resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
        resp[1] = data[1]; /* DID high byte */
        resp[2] = data[2]; /* DID low byte */
        for (pos = 0; pos < ver_len; pos++)
        {
          resp[3U + pos] = (uint8_t)ver[pos];
        }
        safe_mode_send_response(resp, 3U + ver_len);
      }
      else
      {
        safe_mode_send_nrc(service_id, UDS_NRC_REQUEST_OUT_OF_RANGE);
      }
      break;
    }

    case UDS_WRITE_DATA_BY_ID:
    {
      uint16_t did;

      /* security gate */
      if (!g_security_unlocked)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
        break;
      }

      if (len < 4U)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
        break;
      }

      did = ((uint16_t)data[1] << 8) | (uint16_t)data[2];

      if (did == 0x2010U)
      {
        /* DID 0x2010: Firmware Type Selection (0=app, 1=bootloader) */
        g_firmware_type = data[3];

        resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
        resp[1] = data[1];
        resp[2] = data[2];
        resp[3] = data[3];
        safe_mode_send_response(resp, 4);
      }
      else
      {
        safe_mode_send_nrc(service_id, UDS_NRC_REQUEST_OUT_OF_RANGE);
      }
      break;
    }

    case UDS_ROUTINE_CONTROL:
    {
      uint16_t routine_id;

      /* security gate */
      if (!g_security_unlocked)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
        break;
      }

      if (len < 4U)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
        break;
      }

      routine_id = ((uint16_t)data[2] << 8) | (uint16_t)data[3];

      if (data[1] == 0x01U && routine_id == 0xFF00U)
      {
        /* 0x31 0x01 0xFF00: Erase Memory (independent erase command) */
        if (erase_app_a_flash() != 0U)
        {
          safe_mode_send_nrc(service_id, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
          break;
        }

        /* initialize download state after erase */
        g_dl_write_addr    = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
        g_dl_bytes_written = 0;
        g_dl_block_seq     = 0;
        g_dl_active        = 1;

        resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
        resp[1] = data[1]; /* sub-function */
        resp[2] = data[2]; /* routine ID high */
        resp[3] = data[3]; /* routine ID low */
        safe_mode_send_response(resp, 4);
      }
      else
      {
        safe_mode_send_nrc(service_id, UDS_NRC_REQUEST_OUT_OF_RANGE);
      }
      break;
    }

    case UDS_REQUEST_DOWNLOAD:
    {
      /* security gate: require SecurityAccess (0x27) to be unlocked */
      if (!g_security_unlocked)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
        break;
      }

      /* per SRS: 0x34 only initializes download state, erase is done by 0x31 */
      g_dl_write_addr    = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
      g_dl_bytes_written = 0;
      g_dl_block_seq     = 0;
      g_dl_active        = 1;

      /* maxNumberOfBlockLength = maximum firmware size (not per-frame size).
       * actual per-frame payload is limited to 6 bytes (8-byte CAN - SID - blockSeq). */
      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = 0x20;
      resp[2] = 0x00;
      resp[3] = (uint8_t)((APP_A_SIZE >> 8) & 0xFFU);
      resp[4] = (uint8_t)(APP_A_SIZE & 0xFFU);
      safe_mode_send_response(resp, 5);
      break;
    }

    case UDS_TRANSFER_DATA:
    {
      uint8_t block_seq;
      uint8_t data_len;
      uint8_t i;
      flash_status_type flash_status;

      if (!g_dl_active)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_TRANSFER_DATA_ABORTED);
        break;
      }

      /* security gate */
      if (!g_security_unlocked)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
        break;
      }

      if (len < 3U)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
        break;
      }

      block_seq = data[1];
      g_dl_block_seq++;
      if (block_seq != (g_dl_block_seq & 0xFFU))
      {
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, 0x71U);
        break;
      }

      data_len = len - 2U;

      if ((g_dl_bytes_written + (uint32_t)data_len) > MAX_IMAGE_SIZE)
      {
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, UDS_NRC_RESPONSE_TOO_LONG);
        break;
      }

      /* check 4-byte alignment of write address */
      if ((g_dl_write_addr & 0x03U) != 0U)
      {
        g_dl_active = 0;
        safe_mode_send_nrc(service_id, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
        break;
      }

      /* write data to flash (4-byte aligned, unused bytes in last word remain 0xFF) */
      flash_unlock();
      for (i = 0; i < data_len; i += 4U)
      {
        uint32_t word_data = 0xFFFFFFFFU;
        uint8_t  remaining = data_len - i;
        uint8_t  copy_len  = (remaining > 4U) ? 4U : remaining;
        uint8_t  k;

        for (k = 0; k < copy_len; k++)
        {
          word_data &= ~((uint32_t)0xFFU << (k * 8U));
          word_data |= ((uint32_t)data[2U + i + k] << (k * 8U));
        }

        flash_status = flash_word_program(g_dl_write_addr + (uint32_t)i, word_data);
        if (flash_status != FLASH_OPERATE_DONE)
        {
          g_dl_active = 0;
          safe_mode_send_nrc(service_id, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
          break;
        }
      }
      flash_lock();

      if (!g_dl_active) break;

      g_dl_write_addr    += (uint32_t)data_len;
      g_dl_bytes_written += (uint32_t)data_len;

      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = block_seq;
      safe_mode_send_response(resp, 2);
      break;
    }

    case UDS_TRANSFER_SIGNATURE:
    {
      uint8_t block_seq;
      uint8_t sig_data_len;
      uint8_t i;

      /* security gate */
      if (!g_security_unlocked)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
        break;
      }

      if (len < 3U)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_INCORRECT_MSG_LENGTH);
        break;
      }

      block_seq = data[1];

      /* first frame: reset signature buffer */
      if (block_seq == 0x01U && g_sig_bytes_received == 0U)
      {
        g_sig_active = 1;
        g_sig_block_seq = 0;
        g_sig_bytes_received = 0;
        memset(g_sig_buf, 0xFF, 64);
      }

      if (!g_sig_active)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_TRANSFER_DATA_ABORTED);
        break;
      }

      g_sig_block_seq++;
      if (block_seq != (g_sig_block_seq & 0xFFU))
      {
        g_sig_active = 0;
        safe_mode_send_nrc(service_id, UDS_NRC_TRANSFER_DATA_ABORTED);
        break;
      }

      sig_data_len = len - 2U; /* subtract SID and blockSeq */

      if ((g_sig_bytes_received + sig_data_len) > 64U)
      {
        g_sig_active = 0;
        safe_mode_send_nrc(service_id, UDS_NRC_RESPONSE_TOO_LONG);
        break;
      }

      /* accumulate signature data */
      for (i = 0; i < sig_data_len; i++)
      {
        g_sig_buf[g_sig_bytes_received + i] = data[2U + i];
      }
      g_sig_bytes_received += sig_data_len;

      resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
      resp[1] = block_seq;
      safe_mode_send_response(resp, 2);
      break;
    }

    case UDS_REQUEST_TRANSFER_EXIT:
    {
      uint32_t computed_crc;
      uint32_t image_data_addr;
      image_header_t header;
      const uint32_t *hdr_words;
      uint32_t hdr_word_count;
      uint32_t w;

      if (!g_dl_active)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_TRANSFER_DATA_ABORTED);
        break;
      }

      /* security gate */
      if (!g_security_unlocked)
      {
        safe_mode_send_nrc(service_id, UDS_NRC_SECURITY_ACCESS_DENIED);
        break;
      }

      g_dl_active = 0;

      /* compute CRC32 of downloaded image */
      image_data_addr = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
      computed_crc = boot_crc32((const void *)image_data_addr, g_dl_bytes_written);

      /* prepare and write image header */
      memset((void *)&header, 0xFF, sizeof(image_header_t));
      header.magic        = IMAGE_MAGIC;
      header.image_length = g_dl_bytes_written;
      header.crc32        = computed_crc;

      /* fill signature from 0x38 TransferSignature data */
      if (g_sig_bytes_received == 64U)
      {
        memcpy(header.signature, g_sig_buf, 64U);
      }
      else
      {
        /* no signature received or incomplete -- use 0xFF (will fail verification) */
        memset(header.signature, 0xFF, 64U);
      }

      /* reset signature transfer state */
      g_sig_bytes_received = 0;
      g_sig_active = 0;
      g_sig_block_seq = 0;

      flash_unlock();
      hdr_words      = (const uint32_t *)&header;
      hdr_word_count = sizeof(image_header_t) / sizeof(uint32_t);
      for (w = 0; w < hdr_word_count; w++)
      {
        flash_word_program(APP_A_BASE_ADDR + (w * 4U), hdr_words[w]);
      }
      flash_lock();

      /* readback verification: ensure header was written correctly */
      for (w = 0; w < hdr_word_count; w++)
      {
        uint32_t readback = *(volatile uint32_t *)(APP_A_BASE_ADDR + (w * 4U));
        if (readback != hdr_words[w])
        {
          safe_mode_send_nrc(service_id, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
          return;
        }
      }

      /* update metadata to mark slot A as valid */
      g_meta.slot_a_valid  = 1;
      g_meta.slot_a_crc32  = computed_crc;
      g_meta.ota_state     = OTA_STATE_IDLE;

      /* switch active slot to the newly downloaded slot A */
      g_meta.active_slot   = SLOT_A;

      /* set trial boot so bootloader will roll back if APP fails to confirm */
      g_meta.trial_state   = TRIAL_STATE_PENDING;
      g_meta.trial_slot    = SLOT_A;

      boot_metadata_save(&g_meta);

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
      safe_mode_send_nrc(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED);
      break;
  }
}

/* exported functions --------------------------------------------------------*/

/**
 * @brief  enter safe mode: initialize CAN and wait for OTA download
 * @note   called when both application slots are invalid.
 *         runs a minimal event loop with CAN polling and watchdog refresh.
 * @param  none
 * @retval none (does not return unless watchdog resets)
 */
void enter_safe_mode(void)
{
  g_safe_mode = 1;

  /* initialize CAN for safe mode communication */
  can_driver_init();
  can_driver_register_rx_callback(safe_mode_can_rx_handler);

  /* safe mode event loop */
  while (1)
  {
    timer_poll();
    can_driver_poll();
    wdg_drv_refresh();
    safe_mode_update_security_timer();
  }
}
