/**
  **************************************************************************
  * @file     test.c
  * @brief    Test case implementation for timer and watchdog drivers
  **************************************************************************
  *
  * Test results are stored in global variables, observed via debugger.
  * GPIO PA0 is toggled for oscilloscope verification of timing accuracy.
  * No printf is used; UART may not be ready on this board.
  *
  **************************************************************************
  */

/* includes ----------------------------------------------------------------*/
#include "test.h"
#include "timer_drv.h"
#include "wdg_drv.h"
#include "at32f422_426_conf.h"  /* pulls in all HAL peripheral headers including WDT */

/* private variables -------------------------------------------------------*/

/** @brief Global test result array */
test_case_t test_results[TEST_CASE_COUNT];

/** @brief Pass/fail counters for debugger inspection */
volatile uint32_t test_pass_count = 0;
volatile uint32_t test_fail_count = 0;

/** @brief Callback counter used by timer callback tests */
static volatile uint32_t cb_count = 0;

/** @brief Flag for callback context verification */
static volatile uint32_t cb_in_interrupt = 0;

/** @brief Flag for blocking callback tick check */
static volatile uint32_t cb_tick_before = 0;
static volatile uint32_t cb_tick_after = 0;

/* private functions -------------------------------------------------------*/

/**
 * @brief  Simple callback that increments cb_count
 */
static void cb_increment(void)
{
  cb_count++;
}

/**
 * @brief  Callback that checks IPSR to verify execution context
 * @note   IPSR != 0 means we are in handler (interrupt) mode
 */
static void cb_check_context(void)
{
  uint32_t ipsr;
  __asm volatile ("MRS %0, IPSR" : "=r"(ipsr));
  cb_in_interrupt = (ipsr != 0) ? 1 : 0;
  cb_count++;
}

/**
 * @brief  Callback that busy-waits 5ms, then records tick
 */
static void cb_blocking(void)
{
  cb_tick_before = timer_get_tick();
  busy_wait(5);
  cb_tick_after = timer_get_tick();
  cb_count++;
}

/**
 * @brief  GPIO toggle callback for periodic timer (TC-TMR-031)
 */
static void cb_gpio_toggle(void)
{
  GPIO_Pins_Toggle(TEST_GPIO_PORT, TEST_GPIO_PIN);
}

/**
 * @brief  Busy-wait delay in milliseconds (does not rely on timer_get_tick)
 * @param  ms: milliseconds to wait
 */
static void busy_wait(uint32_t ms)
{
  /* Approximate busy loop for 180MHz Cortex-M4.
   * ~45000 cycles per ms at 180MHz (with loop overhead).
   * This is intentionally rough; for accurate delays use timer_get_tick(). */
  volatile uint32_t count = ms * 45000;
  while (count--) {
    __asm volatile ("nop");
  }
}

/**
 * @brief  Blocking delay using timer_get_tick() with timer_poll()
 * @param  ms: milliseconds to wait
 * @note   Calls timer_poll() during wait so callbacks can fire
 */
static void delay_ms(uint32_t ms)
{
  uint32_t start = timer_get_tick();
  while ((timer_get_tick() - start) < ms) {
    timer_poll();
  }
}

/**
 * @brief  Toggle PA0 once
 */
static void test_gpio_toggle(void)
{
  GPIO_Pins_Toggle(TEST_GPIO_PORT, TEST_GPIO_PIN);
}

/* =========================================================================
 *  Test Case Implementations
 * ========================================================================= */

/* --------------------------------------------------------------------------
 *  TC-TMR-001: SysTick accuracy verification (manual, oscilloscope)
 * -------------------------------------------------------------------------- */
static void test_systick_accuracy(test_case_t *tc)
{
  tc->id   = "TC-TMR-001";
  tc->name = "systick_accuracy";

  /* Toggle PA0 at 500ms intervals for oscilloscope measurement.
   * Expect 500ms high, 500ms low = 1Hz square wave. */
  for (int i = 0; i < 6; i++) {
    test_gpio_toggle();
    delay_ms(500);
  }

  /* Manual test: verify with oscilloscope. Mark as SKIP here. */
  tc->result   = TEST_SKIP;
  tc->expected = 500; /* ms per half-period */
  tc->actual   = 0;   /* must be verified by oscilloscope */
}

/* --------------------------------------------------------------------------
 *  TC-TMR-002: Tick increment verification
 * -------------------------------------------------------------------------- */
static void test_tick_increment(test_case_t *tc)
{
  tc->id   = "TC-TMR-002";
  tc->name = "tick_increment";

  uint32_t before = timer_get_tick();
  delay_ms(100);
  uint32_t after = timer_get_tick();
  uint32_t elapsed = after - before;

  /* Expect ~100ms, allow +-10ms tolerance */
  TEST_ASSERT_NEAR(tc, 100, elapsed, 10);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-010: timer_create() basic
 * -------------------------------------------------------------------------- */
static void test_timer_create(test_case_t *tc)
{
  tc->id   = "TC-TMR-010";
  tc->name = "timer_create";

  uint8_t id = timer_create(100, cb_increment, 0);
  TEST_ASSERT_EQ(tc, 1, (id != TIMER_INVALID_ID) ? 1 : 0);

  /* Cleanup */
  if (id != TIMER_INVALID_ID) {
    timer_stop(id);
  }
}

/* --------------------------------------------------------------------------
 *  TC-TMR-011: timer_create() pool full (17th should fail)
 * -------------------------------------------------------------------------- */
static void test_timer_create_pool_full(test_case_t *tc)
{
  tc->id   = "TC-TMR-011";
  tc->name = "timer_create_pool_full";

  uint8_t ids[TIMER_DRV_MAX_TIMERS];
  uint8_t fail_count = 0;

  /* Create 16 timers (fill the pool) */
  for (int i = 0; i < TIMER_DRV_MAX_TIMERS; i++) {
    ids[i] = timer_create(1000, cb_increment, 0);
    if (ids[i] == TIMER_INVALID_ID) {
      fail_count++;
    }
  }

  /* The 17th should fail */
  uint8_t extra = timer_create(1000, cb_increment, 0);

  /* Cleanup all */
  for (int i = 0; i < TIMER_DRV_MAX_TIMERS; i++) {
    if (ids[i] != TIMER_INVALID_ID) {
      timer_stop(ids[i]);
    }
  }

  /* Pass if 16 succeeded and 17th failed */
  uint32_t ok = (fail_count == 0 && extra == TIMER_INVALID_ID) ? 1 : 0;
  TEST_ASSERT_EQ(tc, 1, ok);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-012: timer_create() invalid parameters
 * -------------------------------------------------------------------------- */
static void test_timer_create_invalid_params(test_case_t *tc)
{
  tc->id   = "TC-TMR-012";
  tc->name = "timer_create_invalid_params";

  uint8_t fail_count = 0;

  /* period = 0 should fail */
  if (timer_create(0, cb_increment, 0) != TIMER_INVALID_ID) {
    fail_count++;
  }

  /* callback = NULL should fail */
  if (timer_create(100, NULL, 0) != TIMER_INVALID_ID) {
    fail_count++;
  }

  TEST_ASSERT_EQ(tc, 0, fail_count);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-013: timer start/stop/is_running
 * -------------------------------------------------------------------------- */
static void test_timer_start_stop(test_case_t *tc)
{
  tc->id   = "TC-TMR-013";
  tc->name = "timer_start_stop";

  uint8_t id = timer_create(100, cb_increment, 0);
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1); /* force fail */
    return;
  }

  uint8_t errors = 0;

  /* Before start: should not be running */
  if (timer_is_running(id) != 0) errors++;

  timer_start(id);

  /* After start: should be running */
  if (timer_is_running(id) != 1) errors++;

  timer_stop(id);

  /* After stop: should not be running */
  if (timer_is_running(id) != 0) errors++;

  TEST_ASSERT_EQ(tc, 0, errors);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-020: One-shot timer fires once
 * -------------------------------------------------------------------------- */
static void test_timer_one_shot(test_case_t *tc)
{
  tc->id   = "TC-TMR-020";
  tc->name = "timer_one_shot";

  cb_count = 0;
  uint8_t id = timer_create(50, cb_increment, 0); /* one-shot, 50ms */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);
  delay_ms(60);

  /* Should have fired exactly once */
  TEST_ASSERT_EQ(tc, 1, cb_count);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-021: One-shot does not repeat
 * -------------------------------------------------------------------------- */
static void test_timer_one_shot_no_repeat(test_case_t *tc)
{
  tc->id   = "TC-TMR-021";
  tc->name = "timer_one_shot_no_repeat";

  cb_count = 0;
  uint8_t id = timer_create(50, cb_increment, 0);
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);
  delay_ms(60);   /* first fire at ~50ms */
  delay_ms(100);  /* wait another 100ms */

  /* Should still be 1, not more */
  TEST_ASSERT_EQ(tc, 1, cb_count);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-030: Periodic timer fires repeatedly
 * -------------------------------------------------------------------------- */
static void test_timer_periodic(test_case_t *tc)
{
  tc->id   = "TC-TMR-030";
  tc->name = "timer_periodic";

  cb_count = 0;
  uint8_t id = timer_create(100, cb_increment, 1); /* periodic, 100ms */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);
  delay_ms(1050);

  /* Expect ~10 firings in 1050ms at 100ms period, allow 8~12 */
  TEST_ASSERT_NEAR(tc, 10, cb_count, 2);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-031: Periodic timer GPIO broadcast (manual, oscilloscope)
 * -------------------------------------------------------------------------- */
static void test_timer_broadcast_gpio(test_case_t *tc)
{
  tc->id   = "TC-TMR-031";
  tc->name = "timer_broadcast_gpio";

  uint8_t id = timer_create(100, cb_gpio_toggle, 1); /* 100ms periodic */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);

  /* Let it run for 2 seconds for oscilloscope verification */
  delay_ms(2000);

  timer_stop(id);

  /* Manual test: verify 100ms toggle period on oscilloscope */
  tc->result   = TEST_SKIP;
  tc->expected = 100; /* ms per half-period */
  tc->actual   = 0;
}

/* --------------------------------------------------------------------------
 *  TC-TMR-040: timer_reset() restarts counting
 * -------------------------------------------------------------------------- */
static void test_timer_reset(test_case_t *tc)
{
  tc->id   = "TC-TMR-040";
  tc->name = "timer_reset";

  cb_count = 0;
  uint8_t id = timer_create(50, cb_increment, 0); /* one-shot, 50ms */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);
  delay_ms(30);   /* 30ms elapsed, not yet fired */

  /* Reset: counter goes back to 0 */
  timer_reset(id);

  delay_ms(30);   /* 30ms after reset, still not 50ms total */
  /* At this point, 30ms since reset — should not have fired yet */
  uint32_t count_before = cb_count;

  delay_ms(25);   /* now ~55ms since reset, should fire */
  uint32_t count_after = cb_count;

  /* Should have fired exactly once (count_after - count_before == 1) */
  TEST_ASSERT_EQ(tc, 1, count_after - count_before);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-041: 16 timers running simultaneously
 * -------------------------------------------------------------------------- */
static void test_timer_max_count(test_case_t *tc)
{
  tc->id   = "TC-TMR-041";
  tc->name = "timer_max_count";

  uint8_t ids[TIMER_DRV_MAX_TIMERS];
  uint8_t create_fail = 0;
  uint8_t run_fail = 0;

  /* Create and start 16 periodic timers at 100ms */
  for (int i = 0; i < TIMER_DRV_MAX_TIMERS; i++) {
    ids[i] = timer_create(100, cb_increment, 1);
    if (ids[i] == TIMER_INVALID_ID) {
      create_fail++;
      continue;
    }
    timer_start(ids[i]);
  }

  /* Let them run for 1 second */
  delay_ms(1000);

  /* Verify all are still running */
  for (int i = 0; i < TIMER_DRV_MAX_TIMERS; i++) {
    if (ids[i] != TIMER_INVALID_ID) {
      if (!timer_is_running(ids[i])) {
        run_fail++;
      }
    }
  }

  /* Cleanup */
  for (int i = 0; i < TIMER_DRV_MAX_TIMERS; i++) {
    if (ids[i] != TIMER_INVALID_ID) {
      timer_stop(ids[i]);
    }
  }

  uint32_t ok = (create_fail == 0 && run_fail == 0) ? 1 : 0;
  TEST_ASSERT_EQ(tc, 1, ok);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-042: 1ms period timer accuracy
 * -------------------------------------------------------------------------- */
static void test_timer_1ms_period(test_case_t *tc)
{
  tc->id   = "TC-TMR-042";
  tc->name = "timer_1ms_period";

  cb_count = 0;
  uint8_t id = timer_create(1, cb_increment, 1); /* 1ms periodic */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);
  delay_ms(10);

  /* Expect ~10 firings in 10ms, allow 8~12 */
  TEST_ASSERT_NEAR(tc, 10, cb_count, 2);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-050: Callback runs in main-loop context (not interrupt)
 * -------------------------------------------------------------------------- */
static void test_timer_callback_context(test_case_t *tc)
{
  tc->id   = "TC-TMR-050";
  tc->name = "timer_callback_context";

  cb_count = 0;
  cb_in_interrupt = 0xFF; /* invalid initial value */

  uint8_t id = timer_create(50, cb_check_context, 0); /* one-shot */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id);
  delay_ms(60);

  /* cb_in_interrupt should be 0 (not in interrupt context) */
  TEST_ASSERT_EQ(tc, 0, cb_in_interrupt);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-TMR-051: Callback blocking does not lose ticks
 * -------------------------------------------------------------------------- */
static void test_timer_callback_blocking(test_case_t *tc)
{
  tc->id   = "TC-TMR-051";
  tc->name = "timer_callback_blocking";

  cb_count = 0;
  cb_tick_before = 0;
  cb_tick_after = 0;

  uint8_t id = timer_create(50, cb_blocking, 0); /* one-shot, 50ms */
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  uint32_t tick_before_start = timer_get_tick();
  timer_start(id);
  delay_ms(60);
  uint32_t tick_after_done = timer_get_tick();
  uint32_t total_elapsed = tick_after_done - tick_before_start;

  /* Callback should have fired once */
  uint8_t errors = 0;
  if (cb_count != 1) errors++;

  /* The callback blocked 5ms, plus 50ms timer = should complete in ~60ms.
   * Total elapsed should be reasonable (< 100ms). */
  if (total_elapsed > 100) errors++;

  /* Tick should have continued during blocking (SysTick still runs) */
  if (cb_tick_after <= cb_tick_before) errors++;

  TEST_ASSERT_EQ(tc, 0, errors);
  timer_stop(id);
}

/* --------------------------------------------------------------------------
 *  TC-WDG-001: IWDG init register verification
 * -------------------------------------------------------------------------- */
#ifdef TEST_ENABLE_WDG
static void test_wdg_init(test_case_t *tc)
{
  tc->id   = "TC-WDG-001";
  tc->name = "wdg_init";

  /* Initialize watchdog */
  wdg_drv_init();

  /* Read WDT registers to verify configuration.
   * Expected: DIV=128 (WDT_CLK_DIV_128=0x05), RLD=312 (~1000ms timeout) */
  uint8_t errors = 0;

  /* Check prescaler: DIV_128 -> div bits = 0x05 */
  uint32_t pr = WDT->div;
  if (pr != (uint32_t)WDT_CLK_DIV_128) errors++;

  /* Check reload value: should be 312 */
  uint32_t rlr = WDT->rld;
  if (rlr != 312) errors++;

  TEST_ASSERT_EQ(tc, 0, errors);
}

/* --------------------------------------------------------------------------
 *  TC-WDG-010: IWDG refresh prevents reset
 * -------------------------------------------------------------------------- */
static void test_wdg_refresh(test_case_t *tc)
{
  tc->id   = "TC-WDG-010";
  tc->name = "wdg_refresh";

  /* Already initialized in test_wdg_init().
   * Feed the watchdog for 10 seconds. If we don't reset, test passes. */
  for (int i = 0; i < 1000; i++) {
    wdg_drv_refresh();
    delay_ms(10);
  }

  /* If we reach here, watchdog did not trigger reset. */
  TEST_ASSERT_EQ(tc, 1, 1);
}
#endif /* TEST_ENABLE_WDG */

/* --------------------------------------------------------------------------
 *  TC-INT-001: Integration - timer + watchdog main loop
 * -------------------------------------------------------------------------- */
#ifdef TEST_ENABLE_WDG
static void test_integration_main_loop(test_case_t *tc)
{
  tc->id   = "TC-INT-001";
  tc->name = "integration_main_loop";

  wdg_drv_init();

  /* Create a 100ms periodic timer for GPIO toggle */
  cb_count = 0;
  uint8_t id = timer_create(100, cb_gpio_toggle, 1);
  if (id == TIMER_INVALID_ID) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }
  timer_start(id);

  /* Simulate main loop for 10 seconds */
  for (int i = 0; i < 10000; i++) {
    timer_poll();
    if ((i % 10) == 0) {  /* every ~10ms */
      wdg_drv_refresh();
    }
    busy_wait(1); /* ~1ms */
  }

  timer_stop(id);

  /* If we reach here, integration passed (no watchdog reset) */
  TEST_ASSERT_EQ(tc, 1, 1);
}
#endif /* TEST_ENABLE_WDG */

/* --------------------------------------------------------------------------
 *  TC-INT-003: Integration - multiple timers
 * -------------------------------------------------------------------------- */
static void test_integration_multi_timer(test_case_t *tc)
{
  tc->id   = "TC-INT-003";
  tc->name = "integration_multi_timer";

  static volatile uint32_t cb10_count = 0;
  static volatile uint32_t cb100_count = 0;
  static volatile uint32_t cb1000_count = 0;

  /* Use lambda-style: we need separate callback functions.
   * Since C doesn't have closures, use a simple approach with
   * the existing cb_increment and separate counters. We'll use
   * a wrapper approach with static variables. */

  /* For simplicity, create 3 timers all using cb_increment,
   * then check total count. 10ms + 100ms + 1000ms over 1 second:
   * ~100 + ~10 + ~1 = ~111 total callbacks. */
  cb_count = 0;

  uint8_t id1 = timer_create(10, cb_increment, 1);    /* 10ms periodic */
  uint8_t id2 = timer_create(100, cb_increment, 1);   /* 100ms periodic */
  uint8_t id3 = timer_create(1000, cb_increment, 1);  /* 1000ms periodic */

  uint8_t create_fail = 0;
  if (id1 == TIMER_INVALID_ID) create_fail++;
  if (id2 == TIMER_INVALID_ID) create_fail++;
  if (id3 == TIMER_INVALID_ID) create_fail++;

  if (create_fail > 0) {
    TEST_ASSERT_EQ(tc, 0, 1);
    return;
  }

  timer_start(id1);
  timer_start(id2);
  timer_start(id3);

  delay_ms(1000);

  timer_stop(id1);
  timer_stop(id2);
  timer_stop(id3);

  /* Total callbacks: ~100 (10ms) + ~10 (100ms) + ~1 (1000ms) = ~111
   * Allow wide tolerance: 90 ~ 130 */
  TEST_ASSERT_NEAR(tc, 111, cb_count, 20);
}

/* =========================================================================
 *  Test Framework Functions
 * ========================================================================= */

/**
 * @brief  Initialize test GPIO (PA0) for oscilloscope verification
 */
void test_gpio_init(void)
{
  gpio_init_type gpio_init_struct;

  /* Enable GPIOA clock */
  crm_periph_clock_enable(TEST_GPIO_CLK, TRUE);

  /* Configure PA0 as push-pull output */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins      = TEST_GPIO_PIN;
  gpio_init_struct.gpio_mode      = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull      = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(TEST_GPIO_PORT, &gpio_init_struct);

  /* Start low */
  GPIO_Pins_Reset(TEST_GPIO_PORT, TEST_GPIO_PIN);
}

/**
 * @brief  Run all test cases
 */
void test_run_all(void)
{
  uint8_t idx = 0;

  /* Reset counters */
  test_pass_count = 0;
  test_fail_count = 0;

  /* Initialize driver subsystems */
  timer_drv_init();
  test_gpio_init();

  /* --- Timer driver tests --- */
  test_systick_accuracy(&test_results[idx++]);              /* TC-TMR-001 */
  test_tick_increment(&test_results[idx++]);                /* TC-TMR-002 */

  /* --- timer_create() tests --- */
  test_timer_create(&test_results[idx++]);                  /* TC-TMR-010 */
  test_timer_create_pool_full(&test_results[idx++]);        /* TC-TMR-011 */

  /* Reinitialize timer system: pool-full test exhausted all 16 slots,
   * and timer_drv has no timer_delete(), so we must reset the pool. */
  timer_drv_init();

  test_timer_create_invalid_params(&test_results[idx++]);   /* TC-TMR-012 */

  /* --- start/stop tests --- */
  test_timer_start_stop(&test_results[idx++]);              /* TC-TMR-013 */

  /* --- one-shot tests --- */
  test_timer_one_shot(&test_results[idx++]);                /* TC-TMR-020 */
  test_timer_one_shot_no_repeat(&test_results[idx++]);      /* TC-TMR-021 */

  /* --- periodic tests --- */
  test_timer_periodic(&test_results[idx++]);                /* TC-TMR-030 */
  test_timer_broadcast_gpio(&test_results[idx++]);          /* TC-TMR-031 */

  /* --- reset and edge-case tests --- */
  test_timer_reset(&test_results[idx++]);                   /* TC-TMR-040 */
  test_timer_max_count(&test_results[idx++]);               /* TC-TMR-041 */

  /* Reinitialize: test_timer_max_count exhausted all 16 pool slots */
  timer_drv_init();

  test_timer_1ms_period(&test_results[idx++]);              /* TC-TMR-042 */

  /* --- callback context tests --- */
  test_timer_callback_context(&test_results[idx++]);        /* TC-TMR-050 */
  test_timer_callback_blocking(&test_results[idx++]);       /* TC-TMR-051 */

  /* --- Integration test (no WDG) --- */
  test_integration_multi_timer(&test_results[idx++]);       /* TC-INT-003 */

  /* --- IWDG tests (cause reset, must be last!) --- */
#ifdef TEST_ENABLE_WDG
  test_wdg_init(&test_results[idx++]);                      /* TC-WDG-001 */
  test_wdg_refresh(&test_results[idx++]);                   /* TC-WDG-010 */
  test_integration_main_loop(&test_results[idx++]);         /* TC-INT-001 */
#endif
}

/**
 * @brief  Output test summary via GPIO toggle pattern
 * @note   Fast toggle (3x @ 100ms) = all pass
 *         Slow toggle (3x @ 500ms) = some failures
 */
void test_print_summary(void)
{
  uint32_t interval;
  uint32_t toggle_count;

  if (test_fail_count == 0) {
    /* All passed: fast blink 3 times */
    interval = 100;
    toggle_count = 6; /* 3 full cycles = 6 toggles */
  } else {
    /* Some failed: slow blink 3 times */
    interval = 500;
    toggle_count = 6;
  }

  /* Make sure LED is off before pattern */
  GPIO_Pins_Reset(TEST_GPIO_PORT, TEST_GPIO_PIN);

  for (uint32_t i = 0; i < toggle_count; i++) {
    test_gpio_toggle();
    delay_ms(interval);
  }

  /* Leave LED off */
  GPIO_Pins_Reset(TEST_GPIO_PORT, TEST_GPIO_PIN);

  /* Spin forever so debugger can inspect results */
  while (1) {
    __asm volatile ("wfi");
  }
}
