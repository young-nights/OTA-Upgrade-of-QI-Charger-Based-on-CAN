/**
  **************************************************************************
  * @file     boot_jump.c
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

/* includes ------------------------------------------------------------------*/
#include "boot_jump.h"
#include "boot_metadata.h"
#include "core_cm4.h"
#include "at32f422_426_conf.h"

/* private types -------------------------------------------------------------*/

/**
 * @brief  function pointer type for application reset handler
 */
typedef void (*app_reset_handler_t)(void);

/* exported functions --------------------------------------------------------*/

/**
 * @brief  jump to application at the given address
 * @note   performs the following sequence:
 *         1. disable all interrupts
 *         2. disable SysTick
 *         3. clear all pending interrupt flags (NVIC ICPR)
 *         4. set VTOR to application base address
 *         5. set MSP from application vector table entry [0]
 *         6. jump to application reset handler from vector table entry [1]
 *         this function does not return.
 * @param  app_addr: base address of the application (must contain a valid
 *                   vector table: [0]=initial MSP, [4]=reset handler)
 * @retval none (does not return on success)
 */
int8_t boot_jump_vectors_ok(uint32_t app_addr)
{
  uint32_t app_msp;
  uint32_t reset_fn;

  app_msp  = *(volatile uint32_t *)(app_addr);
  reset_fn = (*(volatile uint32_t *)(app_addr + 4U)) & 0xFFFFFFFEU;

  /* ARM initial SP may equal SRAM_BASE+SRAM_SIZE (one past last byte). */
  if ((app_msp < SRAM_BASE_ADDR) ||
      (app_msp > (SRAM_BASE_ADDR + SRAM_SIZE)))
  {
    return -1;
  }
  if ((reset_fn < (APP_A_BASE_ADDR + IMAGE_HEADER_SIZE)) ||
      (reset_fn >= META_PRIMARY_ADDR))
  {
    return -1;
  }
  return 0;
}

void boot_jump_to_app(uint32_t app_addr)
{
  uint32_t app_reset_addr;
  app_reset_handler_t app_reset_handler;
  volatile uint32_t delay;

  if (boot_jump_vectors_ok(app_addr) != 0)
  {
    return;
  }

  app_reset_addr = *(volatile uint32_t *)(app_addr + 4U);

  __disable_irq();

  can_reset(CAN1);
  crm_periph_clock_enable(CRM_CAN1_PERIPH_CLOCK, FALSE);

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  NVIC->ICER[0] = 0xFFFFFFFFU;
  NVIC->ICER[1] = 0xFFFFFFFFU;
  NVIC->ICPR[0] = 0xFFFFFFFFU;
  NVIC->ICPR[1] = 0xFFFFFFFFU;

  for (delay = 0; delay < 1000; delay++)
  {
    __NOP();
  }

  SCB->VTOR = app_addr;
  __set_MSP(*(volatile uint32_t *)(app_addr));

  app_reset_handler = (app_reset_handler_t)(app_reset_addr | 1U);
  app_reset_handler();
}
