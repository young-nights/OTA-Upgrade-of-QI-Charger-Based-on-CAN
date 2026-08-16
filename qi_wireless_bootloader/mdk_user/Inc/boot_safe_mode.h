/**
  **************************************************************************
  * @file     boot_safe_mode.h
  * @brief    Safe mode (UDS OTA download) interface
  **************************************************************************
  */

#ifndef __BOOT_SAFE_MODE_H
#define __BOOT_SAFE_MODE_H

#include <stdint.h>

/**
 * @brief  enter safe mode: initialize CAN and wait for OTA download
 * @note   called when both application slots are invalid.
 *         runs a minimal event loop with CAN polling and watchdog refresh.
 * @param  none
 * @retval none (does not return unless watchdog resets)
 */
void enter_safe_mode(void);

#endif /* __BOOT_SAFE_MODE_H */
