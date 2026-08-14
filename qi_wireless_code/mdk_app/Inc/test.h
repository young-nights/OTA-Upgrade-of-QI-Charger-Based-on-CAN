/**
  **************************************************************************
  * @file     test.h
  * @brief    Test framework header for timer and watchdog driver verification
  **************************************************************************
  *
  * Test results are stored in global variables and observed via debugger.
  * GPIO PA0 is toggled for oscilloscope verification of timing accuracy.
  *
  **************************************************************************
  */

/* define to prevent recursive inclusion -----------------------------------*/
#ifndef __TEST_H
#define __TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes ----------------------------------------------------------------*/
#include "at32f422_426.h"
#include "at32f422_426_gpio.h"

/* exported constants ------------------------------------------------------*/

/** @brief GPIO pin used for oscilloscope verification (PA0) */
#define TEST_GPIO_PORT        GPIOA
#define TEST_GPIO_PIN         GPIO_PINS_0
#define TEST_GPIO_CLK         CRM_GPIOA_PERIPH_CLOCK

/** @brief Total number of test cases */
#define TEST_CASE_COUNT       19

/** @brief Uncomment to enable IWDG tests (will cause MCU reset!) */
/* #define TEST_ENABLE_WDG */

/* exported types ----------------------------------------------------------*/

/**
 * @brief  Test result enumeration
 */
typedef enum {
  TEST_PASS = 0,   /*!< Test passed */
  TEST_FAIL = 1,   /*!< Test failed */
  TEST_SKIP = 2    /*!< Test skipped */
} test_result_t;

/**
 * @brief  Test case descriptor
 */
typedef struct {
  const char    *id;        /*!< Test case ID, e.g. "TC-TMR-010" */
  const char    *name;      /*!< Test case name */
  test_result_t result;     /*!< Actual test result */
  uint32_t      expected;   /*!< Expected value */
  uint32_t      actual;     /*!< Actual value */
} test_case_t;

/* exported variables ------------------------------------------------------*/

/** @brief Global test result array */
extern test_case_t test_results[TEST_CASE_COUNT];

/** @brief Global pass/fail counters for debugger inspection */
extern volatile uint32_t test_pass_count;
extern volatile uint32_t test_fail_count;

/* exported functions ------------------------------------------------------*/

/**
 * @brief  Initialize test GPIO (PA0) for oscilloscope verification
 */
void test_gpio_init(void);

/**
 * @brief  Run all test cases
 */
void test_run_all(void);

/**
 * @brief  Output test summary via GPIO toggle pattern
 * @note   Fast toggle (3x @ 100ms) = all pass
 *         Slow toggle (3x @ 500ms) = some failures
 */
void test_print_summary(void);

/* helper macros -----------------------------------------------------------*/

/**
 * @brief  Assert equality and record result in test case struct
 * @param  tc:   pointer to test_case_t
 * @param  exp:  expected value (uint32_t)
 * @param  act:  actual value (uint32_t)
 */
#define TEST_ASSERT_EQ(tc, exp, act) do { \
  (tc)->expected = (uint32_t)(exp);       \
  (tc)->actual   = (uint32_t)(act);       \
  (tc)->result   = ((uint32_t)(exp) == (uint32_t)(act)) ? TEST_PASS : TEST_FAIL; \
  if ((tc)->result == TEST_PASS) test_pass_count++; else test_fail_count++; \
} while(0)

/**
 * @brief  Assert that value is within tolerance range
 * @param  tc:   pointer to test_case_t
 * @param  exp:  expected value
 * @param  act:  actual value
 * @param  tol:  tolerance (act must be in [exp-tol, exp+tol])
 */
#define TEST_ASSERT_NEAR(tc, exp, act, tol) do { \
  (tc)->expected = (uint32_t)(exp);              \
  (tc)->actual   = (uint32_t)(act);              \
  uint32_t _e = (uint32_t)(exp);                 \
  uint32_t _a = (uint32_t)(act);                 \
  uint32_t _t = (uint32_t)(tol);                 \
  (tc)->result = (_a >= (_e - _t) && _a <= (_e + _t)) ? TEST_PASS : TEST_FAIL; \
  if ((tc)->result == TEST_PASS) test_pass_count++; else test_fail_count++; \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* __TEST_H */
