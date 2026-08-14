/**
  **************************************************************************
  * @file     gpio_drv.c
  * @brief    GPIO driver for system peripherals (hall sensor, power control, LED)
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
#include "gpio_drv.h"
#include "timer_drv.h"

/* private variables ---------------------------------------------------------*/

/** @brief  hall sensor debounced state (persistent across calls) */
static hall_state_t hall_state;

/* exported functions --------------------------------------------------------*/

/**
 * @brief  initialize all GPIO peripherals
 * @note   configures hall sensor input, 12V buck enable, 5V wireless charge
 *         power control, and LED output.
 * @param  none
 * @retval none
 */
void gpio_drv_init(void)
{
  gpio_init_type gpio_init_struct;

  /* enable GPIOA and GPIOB peripheral clocks */
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* configure PA0: hall sensor input with pull-up (active low) */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = HALL_SENSOR_GPIO_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(HALL_SENSOR_GPIO_PORT, &gpio_init_struct);

  /* configure PB1: 12V buck enable, push-pull output, default off */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = BUCK_12V_EN_GPIO_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(BUCK_12V_EN_GPIO_PORT, &gpio_init_struct);
  gpio_bits_reset(BUCK_12V_EN_GPIO_PORT, BUCK_12V_EN_GPIO_PIN);  /* default off */

  /* configure PB2: 5V wireless charge power, push-pull output, default off */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = WLC_5V_EN_GPIO_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(WLC_5V_EN_GPIO_PORT, &gpio_init_struct);
  gpio_bits_reset(WLC_5V_EN_GPIO_PORT, WLC_5V_EN_GPIO_PIN);  /* default off */

  /* initialize hall sensor state */
  hall_state.raw_level       = 0;
  hall_state.stable_level    = 0;
  hall_state.hall_detected   = 0;
  hall_state.last_change_tick = 0;

  /* TODO: configure LED pin when assigned */
  /* TODO: configure arm motor GPIO pins when assigned */
}

/**
 * @brief  hall sensor debounce polling function
 * @note   must be called periodically from main loop (e.g. every 10ms)
 *         to update the debounced hall sensor state.
 * @param  none
 * @retval none
 */
void gpio_drv_hall_poll(void)
{
  uint8_t current_raw;
  uint32_t now;

  /* read current raw level */
  current_raw = (gpio_input_data_bit_read(HALL_SENSOR_GPIO_PORT,
                  HALL_SENSOR_GPIO_PIN) != RESET) ? 1U : 0U;

  /* check if level changed */
  if (current_raw != hall_state.raw_level)
  {
    hall_state.raw_level = current_raw;
    hall_state.last_change_tick = timer_get_tick();
  }

  /* check if level has been stable for debounce period */
  now = timer_get_tick();
  if ((now - hall_state.last_change_tick) >= HALL_DEBOUNCE_MS)
  {
    hall_state.stable_level = hall_state.raw_level;
  }

  /* determine hall detection status based on active-low configuration */
#if HALL_SENSOR_ACTIVE_LOW
  hall_state.hall_detected = (hall_state.stable_level == 0U) ? 1U : 0U;
#else
  hall_state.hall_detected = hall_state.stable_level;
#endif
}

/**
 * @brief  read hall sensor state with debounce filtering
 * @note   returns the debounced state. Must be called periodically.
 *         uses timer_get_tick() for debounce timing.
 * @retval 1 = hall sensor activated (phone present), 0 = not detected
 */
uint8_t gpio_drv_hall_read(void)
{
  return hall_state.hall_detected;
}

/**
 * @brief  enable or disable 12V buck converter
 * @param  enable: 1 = enable, 0 = disable
 * @retval none
 */
void gpio_drv_12v_enable(uint8_t enable)
{
  if (enable)
  {
    gpio_bits_set(BUCK_12V_EN_GPIO_PORT, BUCK_12V_EN_GPIO_PIN);
  }
  else
  {
    gpio_bits_reset(BUCK_12V_EN_GPIO_PORT, BUCK_12V_EN_GPIO_PIN);
  }
}

/**
 * @brief  enable or disable 5V wireless charging power supply
 * @param  enable: 1 = enable, 0 = disable
 * @retval none
 */
void gpio_drv_5v_enable(uint8_t enable)
{
  if (enable)
  {
    gpio_bits_set(WLC_5V_EN_GPIO_PORT, WLC_5V_EN_GPIO_PIN);
  }
  else
  {
    gpio_bits_reset(WLC_5V_EN_GPIO_PORT, WLC_5V_EN_GPIO_PIN);
  }
}

/**
 * @brief  control status LED
 * @param  on: 1 = turn on, 0 = turn off
 * @retval none
 */
void gpio_drv_set_led(uint8_t on)
{
  /* TODO: implement when LED pin is assigned */
  /* example:
   * if (on)
   *   gpio_bits_set(LED_GPIO_PORT, LED_GPIO_PIN);
   * else
   *   gpio_bits_reset(LED_GPIO_PORT, LED_GPIO_PIN);
   */
  (void)on;  /* suppress unused parameter warning */
}
