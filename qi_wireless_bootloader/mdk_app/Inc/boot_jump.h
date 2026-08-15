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
 * @brief  jump to application at the given address
 * @note   performs the following sequence:
 *         1. disable all interrupts
 *         2. disable SysTick
 *         3. clear pending interrupt flags
 *         4. set VTOR to application base address
 *         5. set MSP from application vector table
 *         6. jump to application reset handler
 *         this function does not return.
 * @param  app_addr: base address of the application (must contain a valid
 *                   vector table: [0]=initial MSP, [4]=reset handler)
 * @retval none (does not return on success)
 */
void boot_jump_to_app(uint32_t app_addr);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_JUMP_H */
