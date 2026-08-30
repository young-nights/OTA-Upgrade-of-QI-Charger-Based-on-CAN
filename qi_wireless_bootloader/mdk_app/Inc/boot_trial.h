/**
  **************************************************************************
  * @file     boot_trial.h
  * @brief    Trial boot state machine interface
  **************************************************************************
  */

#ifndef __BOOT_TRIAL_H
#define __BOOT_TRIAL_H

#include <stdint.h>
#include "boot_metadata.h"

/** @brief  trial boot timer period (1 second) */
#define TRIAL_TIMER_PERIOD_MS       1000U

/** @brief  OTA metadata instance (extern for main.c access) */
extern ota_metadata_t g_meta;

/**
 * @brief  detect reset source and return boot reason code
 * @param  none
 * @retval boot reason code
 */
uint8_t detect_boot_reason(void);

/**
 * @brief  select the slot to boot from based on metadata
 * @param  meta: pointer to metadata
 * @param  slot: output, selected slot index (0=A, 1=B)
 * @retval 0 on success (slot A or B), -1 if the chosen index is invalid
 */
int8_t select_boot_slot(const ota_metadata_t *meta, uint8_t *slot);

/**
 * @brief  perform trial boot state machine processing
 * @param  meta: pointer to metadata (mutable)
 * @retval none
 */
void process_trial_state(ota_metadata_t *meta);

/**
 * @brief  attempt to boot from a given slot
 * @param  slot: slot index (0=A, 1=B)
 * @param  meta: pointer to metadata
 * @retval 0 if the image verified (caller must jump), -1 on failure
 */
int8_t try_boot_slot(uint8_t slot, ota_metadata_t *meta);

/**
 * @brief  trial boot timer callback (called every 1 second)
 * @param  none
 * @retval none
 */
void trial_timer_callback(void);

#endif /* __BOOT_TRIAL_H */
