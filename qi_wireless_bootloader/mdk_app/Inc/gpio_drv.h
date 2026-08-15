/**
  **************************************************************************
  * @file     gpio_drv.h
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

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __GPIO_DRV_H
#define __GPIO_DRV_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* exported constants --------------------------------------------------------*/

/**
 * @brief  GPIO pin assignments
 */
#define HALL_SENSOR_GPIO_PORT           GPIOA        /*!< hall sensor GPIO port */
#define HALL_SENSOR_GPIO_PIN            GPIO_PINS_0  /*!< hall sensor GPIO pin (PA0) */
#define HALL_SENSOR_ACTIVE_LOW          1            /*!< 1 = active low (low = detected) */

#define BUCK_12V_EN_GPIO_PORT           GPIOB        /*!< 12V buck enable GPIO port */
#define BUCK_12V_EN_GPIO_PIN            GPIO_PINS_1  /*!< 12V buck enable GPIO pin (PB1) */

#define WLC_5V_EN_GPIO_PORT             GPIOB        /*!< 5V wireless charge power GPIO port */
#define WLC_5V_EN_GPIO_PIN              GPIO_PINS_2  /*!< 5V wireless charge power GPIO pin (PB2) */

/**
 * @brief  Hall sensor debounce configuration
 */
#define HALL_DEBOUNCE_MS                50U          /*!< debounce time in milliseconds */

/* exported types ------------------------------------------------------------*/

/**
 * @brief  Hall sensor debounced state
 */
typedef struct
{
  uint8_t  raw_level;       /*!< raw GPIO level (0 or 1) */
  uint8_t  stable_level;    /*!< debounced stable level (0 or 1) */
  uint8_t  hall_detected;   /*!< 1 = hall sensor activated (phone present) */
  uint32_t last_change_tick;/*!< timestamp of last level change in ms */
} hall_state_t;

/* exported functions -------------------------------------------------------*/

/**
 * @brief  initialize all GPIO peripherals
 * @note   configures hall sensor input, 12V buck enable, 5V wireless charge
 *         power control, and LED output.
 * @param  none
 * @retval none
 */
void gpio_drv_init(void);

/**
 * @brief  read hall sensor state with debounce filtering
 * @note   returns the debounced state. Must be called periodically.
 *         uses timer_get_tick() for debounce timing.
 * @retval 1 = hall sensor activated (phone present), 0 = not detected
 */
uint8_t gpio_drv_hall_read(void);

/**
 * @brief  enable or disable 12V buck converter
 * @param  enable: 1 = enable, 0 = disable
 * @retval none
 */
void gpio_drv_12v_enable(uint8_t enable);

/**
 * @brief  enable or disable 5V wireless charging power supply
 * @param  enable: 1 = enable, 0 = disable
 * @retval none
 */
void gpio_drv_5v_enable(uint8_t enable);

/**
 * @brief  control status LED
 * @param  on: 1 = turn on, 0 = turn off
 * @retval none
 */
void gpio_drv_set_led(uint8_t on);

/**
 * @brief  hall sensor debounce polling function
 * @note   must be called periodically from main loop (e.g. every 10ms)
 *         to update the debounced hall sensor state.
 * @param  none
 * @retval none
 */
void gpio_drv_hall_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __GPIO_DRV_H */
