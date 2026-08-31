/**
  **************************************************************************
  * @file     boot_jump.h
  * @brief    Application jump logic for bootloader
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
#ifndef __BOOT_JUMP_H
#define __BOOT_JUMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* exported functions -------------------------------------------------------*/

/**
 * @brief  check APP vector table before tearing down Boot
 * @param  app_addr: slot entry (header+256), [0]=MSP, [4]=Reset Handler
 * @retval 0 if MSP is in SRAM and Reset Handler is in APP flash
 */
int8_t boot_jump_vectors_ok(uint32_t app_addr);

/**
 * @brief  jump to application; validates vectors first, then disables IRQ/CAN
 * @note   does not return on success. On vector failure, returns so the
 *         caller can try the other slot or enter Safe Mode.
 * @param  app_addr: application vector table address
 */
void boot_jump_to_app(uint32_t app_addr);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_JUMP_H */
