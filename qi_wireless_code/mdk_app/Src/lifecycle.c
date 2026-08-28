/**
  **************************************************************************
  * @file     lifecycle.c
  * @brief    Lifecycle broadcast module for peripheral status reporting
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
#include "lifecycle.h"
#include "can_protocol.h"
#include "can_driver.h"
#include "timer_drv.h"
#include <string.h>

/* ========================================================================== */
/*  Private constants                                                        */
/* ========================================================================== */

/** @brief  periodic broadcast interval in ms (1 Hz default) */
#define LIFECYCLE_PERIODIC_INTERVAL_MS   1000U

/* ========================================================================== */
/*  Private variables                                                        */
/* ========================================================================== */

/** @brief  current lifecycle state */
static uint8_t  g_lifecycle_state = LIFECYCLE_BOOTUP;

/** @brief  timestamp of last broadcast (for periodic sending) */
static uint32_t g_last_broadcast_tick = 0;

/* ========================================================================== */
/*  Private functions                                                        */
/* ========================================================================== */

/**
 * @brief  send a lifecycle broadcast frame
 * @param  state: lifecycle state value to send in byte 0
 * @retval none
 */
static void lifecycle_send(uint8_t state)
{
  uint8_t data[8];

  memset(data, 0, sizeof(data));
  data[0] = state;
  /* byte 1-7: reserved, all zeros */

  can_driver_send(CAN_ID_LIFECYCLE_BROADCAST, data, 8);
}

/**
 * @brief  check if current state supports periodic broadcast
 * @param  state: lifecycle state
 * @retval 1 = periodic broadcast enabled, 0 = disabled
 */
static uint8_t lifecycle_is_periodic(uint8_t state)
{
  return ((state == LIFECYCLE_OPERATIONAL) || (state == LIFECYCLE_DEGRADED)) ? 1U : 0U;
}

/* ========================================================================== */
/*  Bus-off recovery callback                                                */
/* ========================================================================== */

/**
 * @brief  bus-off recovery callback
 * @note   called from can_driver_poll() after bus-off recovery.
 *         sends BOOTUP broadcast to notify the bus of recovery.
 * @retval none
 */
static void lifecycle_on_busoff_recovery(void)
{
  /* send BOOTUP broadcast to indicate recovery (spec section 12) */
  g_lifecycle_state = LIFECYCLE_BOOTUP;
  lifecycle_send(LIFECYCLE_BOOTUP);
  g_last_broadcast_tick = timer_get_tick();
}

/* ========================================================================== */
/*  Exported functions                                                       */
/* ========================================================================== */

/**
 * @brief  initialize lifecycle module and send BOOTUP broadcast
 * @param  none
 * @retval none
 */
void lifecycle_init(void)
{
  g_lifecycle_state = LIFECYCLE_BOOTUP;
  g_last_broadcast_tick = timer_get_tick();

  /* register bus-off recovery callback with CAN driver */
  can_driver_register_busoff_recovery_callback(lifecycle_on_busoff_recovery);

  /* send BOOTUP immediately */
  lifecycle_send(LIFECYCLE_BOOTUP);
}

/**
 * @brief  set lifecycle state and send immediate broadcast
 * @param  state: new lifecycle state
 * @retval none
 */
void lifecycle_set_state(uint8_t state)
{
  if ((state < LIFECYCLE_BOOTUP) || (state > LIFECYCLE_SHUTDOWN))
  {
    return;  /* invalid state, ignore */
  }

  g_lifecycle_state = state;

  /* send immediate broadcast on state change */
  lifecycle_send(state);
  g_last_broadcast_tick = timer_get_tick();
}

/**
 * @brief  poll lifecycle for periodic broadcast
 * @note   must be called from the main loop.
 *         sends periodic broadcast every 1000ms when in OPERATIONAL or DEGRADED.
 * @param  none
 * @retval none
 */
void lifecycle_poll(void)
{
  uint32_t now;

  /* only send periodic broadcast in OPERATIONAL or DEGRADED */
  if (!lifecycle_is_periodic(g_lifecycle_state))
  {
    return;
  }

  now = timer_get_tick();
  if ((now - g_last_broadcast_tick) >= LIFECYCLE_PERIODIC_INTERVAL_MS)
  {
    g_last_broadcast_tick = now;
    lifecycle_send(g_lifecycle_state);
  }
}

/**
 * @brief  get current lifecycle state
 * @retval current lifecycle state value
 */
uint8_t lifecycle_get_state(void)
{
  return g_lifecycle_state;
}
