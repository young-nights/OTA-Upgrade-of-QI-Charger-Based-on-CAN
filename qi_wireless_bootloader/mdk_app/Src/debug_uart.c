/**
  **************************************************************************
  * @file     debug_uart.c
  * @brief    USART1 debug heartbeat on PB6 (TX) / PB7 (RX)
  **************************************************************************
  */

#include "debug_uart.h"
#include "timer_drv.h"

static const char *g_tag = "DBG";
static uint32_t g_last_ms = 0;
static uint32_t g_seq = 0;
static uint8_t g_ready = 0;

static void debug_uart_putc(uint8_t ch)
{
  uint32_t guard = 0x10000U;

  while ((usart_flag_get(USART1, USART_TDBE_FLAG) == RESET) && (guard > 0U))
  {
    guard--;
  }
  if (guard > 0U)
  {
    usart_data_transmit(USART1, ch);
  }
}

void debug_uart_puts(const char *s)
{
  if ((g_ready == 0U) || (s == (const char *)0))
  {
    return;
  }
  while (*s != '\0')
  {
    debug_uart_putc((uint8_t)*s);
    s++;
  }
}

void debug_uart_init(const char *tag)
{
  gpio_init_type gpio_init_struct;

  if (tag != (const char *)0)
  {
    g_tag = tag;
  }

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);

  /* PB6 = USART1_TX (mux0), PB7 = USART1_RX (mux0) */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_6;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE6, GPIO_MUX_0);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_7;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE7, GPIO_MUX_0);

  usart_init(USART1, DEBUG_UART_BAUDRATE, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_parity_selection_config(USART1, USART_PARITY_NONE);
  usart_transmitter_enable(USART1, TRUE);
  usart_receiver_enable(USART1, TRUE);
  usart_enable(USART1, TRUE);

  g_ready = 1;
  g_last_ms = timer_get_tick();
  g_seq = 0;

  debug_uart_puts(g_tag);
  debug_uart_puts(" start\r\n");
}

void debug_uart_poll(void)
{
  uint32_t now;
  uint32_t tick;
  char buf[12];
  uint8_t n;

  if (g_ready == 0U)
  {
    return;
  }

  now = timer_get_tick();
  if ((now - g_last_ms) < DEBUG_UART_PERIOD_MS)
  {
    return;
  }
  g_last_ms = now;
  g_seq++;

  /* "APP 000012345 00000001\r\n" — tag, SysTick ms, heartbeat count */
  debug_uart_puts(g_tag);
  debug_uart_putc((uint8_t)' ');

  tick = now;
  n = 8U;
  buf[8] = '\0';
  while (n > 0U)
  {
    n--;
    buf[n] = (char)('0' + (tick % 10U));
    tick /= 10U;
  }
  debug_uart_puts(buf);
  debug_uart_putc((uint8_t)' ');

  tick = g_seq;
  n = 8U;
  buf[8] = '\0';
  while (n > 0U)
  {
    n--;
    buf[n] = (char)('0' + (tick % 10U));
    tick /= 10U;
  }
  debug_uart_puts(buf);
  debug_uart_puts("\r\n");
}
