/**
  **************************************************************************
  * @file     sit1145.c
  * @brief    SIT1145 CAN FD 收发器驱动（AT32F426）
  *
  * @note     硬件接线：
  *           PA4  - SPI1_CS  （GPIO 软件控制，低电平有效）
  *           PA5  - SPI1_SCK （AF MUX0）
  *           PA6  - SPI1_MISO（AF MUX0）
  *           PA7  - SPI1_MOSI（AF MUX0）
  *           PA11 - CAN_RX  （由 can_driver 管理）
  *           PA12 - CAN_TX  （由 can_driver 管理）
  *           PB3  - 悬空引脚（输出低）
  *
  *           SIT1145 SPI 协议：
  *           写：CS 拉低 → 发 (addr << 1) → 发 data → CS 拉高
  *           读：CS 拉低 → 发 (addr << 1) | 0x01 → 读 data → CS 拉高
  **************************************************************************
  */

/* 头文件 ------------------------------------------------------------------*/
#include "sit1145.h"

/* 私有宏定义 --------------------------------------------------------------*/

/**
 * @brief  SPI1 时钟分频配置
 * @note   APB2 时钟假设为 180 MHz（系统时钟）
 *         DIV_128 分频后 SPI 时钟约 1.4 MHz，SIT1145 可靠工作
 */
#define SIT1145_SPI_DIV             SPI_MCLK_DIV_128

/* CS 建立/保持延迟（180MHz 下约 1us） */
#define SIT1145_CS_DELAY_CYCLES     180U

/* 私有函数 ---------------------------------------------------------------*/

/**
 * @brief  软件延时约 1 微秒
 * @note   简单忙等，针对 ~180 MHz 系统时钟校准
 */
static void sit1145_delay_us(void)
{
  volatile uint32_t i = SIT1145_CS_DELAY_CYCLES;
  while (i-- > 0U)
  {
    __asm("nop");
  }
}

/**
 * @brief  毫秒级忙等延时（仅初始化路径使用）
 */
static void sit1145_delay_ms(uint32_t ms)
{
  uint32_t n;
  while (ms-- > 0U)
  {
    for (n = 0U; n < 1000U; n++)
    {
      sit1145_delay_us();
    }
  }
}

/**
 * @brief  等待收发器 CTS=1（Normal 模式，可驱动总线）
 * @retval 1=CTS 已置位，0=超时
 */
static uint8_t sit1145_wait_cts(uint32_t timeout_ms)
{
  uint32_t elapsed;
  uint8_t sta;

  for (elapsed = 0U; elapsed < timeout_ms; elapsed++)
  {
    sta = sit1145_read_reg(SIT1145_REG_TRANSCEIVER_STATUS);
    if ((sta & SIT1145_TRAN_STA_CTS) != 0U)
    {
      return 1U;
    }
    sit1145_delay_ms(1U);
  }
  return 0U;
}

/**
 * @brief  拉低 CS（选中 SIT1145）
 */
static void sit1145_cs_low(void)
{
  gpio_bits_reset(SIT1145_CS_GPIO_PORT, SIT1145_CS_GPIO_PIN);
  sit1145_delay_us();
}

/**
 * @brief  拉高 CS（释放 SIT1145）
 */
static void sit1145_cs_high(void)
{
  sit1145_delay_us();
  gpio_bits_set(SIT1145_CS_GPIO_PORT, SIT1145_CS_GPIO_PIN);
}

/**
 * @brief  SPI1 收发一个字节（阻塞）
 * @param  tx_data: 要发送的字节
 * @retval 接收到的字节
 */
static uint8_t sit1145_spi_xfer(uint8_t tx_data)
{
  uint32_t guard = 100000U;

  while ((spi_i2s_flag_get(SPI1, SPI_I2S_TDBE_FLAG) == RESET) && (guard != 0U))
  {
    guard--;
  }
  if (guard == 0U)
  {
    return 0xFFU;
  }

  spi_i2s_data_transmit(SPI1, (uint16_t)tx_data);

  guard = 100000U;
  while ((spi_i2s_flag_get(SPI1, SPI_I2S_RDBF_FLAG) == RESET) && (guard != 0U))
  {
    guard--;
  }
  if (guard == 0U)
  {
    return 0xFFU;
  }

  return (uint8_t)spi_i2s_data_receive(SPI1);
}

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief  通过 SPI 写 SIT1145 寄存器
 * @param  addr: 寄存器地址
 * @param  data: 写入值
 */
void sit1145_write_reg(uint8_t addr, uint8_t data)
{
  sit1145_cs_low();
  sit1145_spi_xfer((uint8_t)(addr << 1) | SIT1145_WRITE);
  sit1145_spi_xfer(data);
  sit1145_cs_high();
}

/**
 * @brief  通过 SPI 读 SIT1145 寄存器
 * @param  addr: 寄存器地址
 * @retval 寄存器值
 */
uint8_t sit1145_read_reg(uint8_t addr)
{
  uint8_t val;

  sit1145_cs_low();
  sit1145_spi_xfer((uint8_t)(addr << 1) | SIT1145_READ);
  val = sit1145_spi_xfer(0xFF);
  sit1145_cs_high();

  return val;
}

/**
 * @brief  通用模式切换（带验证）
 * @param  target_mode: SIT1145_MC_SLEEP_MODE / STANDBY / NORMAL
 * @retval 1 成功，0 失败
 */
static uint8_t sit1145_set_mode(uint8_t target_mode)
{
  uint8_t mode_val;

  sit1145_write_reg(SIT1145_REG_MODE_CONTROL, target_mode);
  sit1145_delay_us();
  sit1145_delay_us();

  /* Sleep 模式会关闭 SPI，无法回读验证，直接信任写入 */
  if (target_mode == SIT1145_MC_SLEEP_MODE)
  {
    return 1U;
  }

  mode_val = sit1145_read_reg(SIT1145_REG_MODE_CONTROL);
  if ((mode_val & SIT1145_MC_MODE_MASK) != target_mode)
  {
    return 0;
  }

  return 1U;
}

/**
 * @brief  切换 SIT1145 到 Normal 模式
 * @note   切换前须确保 CAN Control 和 Data Rate 寄存器已配置
 *         （部分收发器进入 Normal 后会忽略寄存器写入）。
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_normal_mode_set(void)
{
  if (sit1145_set_mode(SIT1145_MC_NORMAL_MODE) == 0U)
  {
    sit1145_delay_ms(1U);
    if (sit1145_set_mode(SIT1145_MC_NORMAL_MODE) == 0U)
    {
      return 0U;
    }
  }
  return sit1145_wait_cts(20U);
}

/**
 * @brief  切换 SIT1145 到 Standby 模式
 * @note   Standby 模式：低功耗监听状态，CAN 总线被动（不发送）。
 *         SPI 仍可访问，收发器可被 CAN 总线活动唤醒。
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_standby_mode_set(void)
{
  return sit1145_set_mode(SIT1145_MC_STANDBY_MODE);
}

/**
 * @brief  切换 SIT1145 到 Sleep 模式
 * @note   Sleep 模式：最低功耗。
 *         ⚠️ SPI 接口在 Sleep 模式下不可用，调用后任何 SPI 读写都会失败。
 *         唤醒方式：INH 引脚电平变化，或重新上电。
 *         调用前须确保 CAN 控制器已停止。
 * @retval 1 始终返回1（无法验证，SPI 已断开）
 */
uint8_t sit1145_sleep_mode_set(void)
{
  return sit1145_set_mode(SIT1145_MC_SLEEP_MODE);
}

/**
 * @brief  获取 SIT1145 当前工作模式
 * @retval 模式控制寄存器值 & 0x07：
 *         SIT1145_MC_SLEEP_MODE   (0x01) — Sleep
 *         SIT1145_MC_STANDBY_MODE (0x04) — Standby
 *         SIT1145_MC_NORMAL_MODE  (0x07) — Normal
 *         0xFF — SPI 读取失败（可能处于 Sleep 模式）
 */
uint8_t sit1145_get_mode(void)
{
  uint8_t val = sit1145_read_reg(SIT1145_REG_MODE_CONTROL);
  /* SPI 失败时（芯片处于 Sleep），spi_xfer 超时返回 0xFF */
  return val & SIT1145_MC_MODE_MASK;
}

/**
 * @brief  使能标准 CAN 远程唤醒（CWE=1，关闭选择性唤醒 WUF）
 */
void sit1145_wake_enable(void)
{
  sit1145_write_reg(SIT1145_REG_TRANSCEIVER_EVENT_EN, SIT1145_CWE);
}

/**
 * @brief  检测是否有 CAN 唤醒事件挂起
 * @note   双重检测：
 *         1. PA11 引脚电平（Standby 时 SIT1145 强制 RXD 拉低）
 *         2. SPI 读取 TRANSCEIVER_EVENT 寄存器 CW 位
 * @retval 1=有唤醒事件，0=无
 */
uint8_t sit1145_wakeup_pending(void)
{
  uint8_t ev;

  /* Standby 模式下 SIT1145 在整个唤醒事件期间强制 RXD(PA11) 拉低 */
  if (gpio_input_data_bit_read(GPIOA, GPIO_PINS_11) == RESET)
  {
    return 1U;
  }

  ev = sit1145_read_reg(SIT1145_REG_TRANSCEIVER_EVENT);
  if (ev == 0xFFU)
  {
    return 0U;
  }
  return ((ev & SIT1145_CW) != 0U) ? 1U : 0U;
}

/**
 * @brief  清除 CAN 唤醒事件标志（写1清零 CW 位）
 */
void sit1145_wakeup_clear(void)
{
  sit1145_write_reg(SIT1145_REG_TRANSCEIVER_EVENT, SIT1145_CW);
}

/**
 * @brief  初始化 SIT1145 CAN 收发器
 * @note   执行以下步骤：
 *         1. 使能 GPIOA、GPIOB、SPI1 外设时钟
 *         2. 配置 PA5/PA6/PA7 为 SPI1 AF 引脚（MUX0）
 *         3. 配置 PA4 为 GPIO 输出（CS，默认高电平）
 *         4. 配置 PB3 为 GPIO 输出低（悬空引脚）
 *         5. 初始化 SPI1 Mode 1（CPOL=0, CPHA=1）— 数据手册下降沿采样
 *         6. 读识别码 0x7E（0x70=AQ / 0x74=AQ-FD）
 *         7. 配置 CAN Control 寄存器（CMC=01, CFDC=1）
 *         8. 配置 Data Rate 寄存器（250kbps）
 *         9. 使能标准 CAN 唤醒（CWE）
 *        10. 切换到 Standby（APP 低功耗默认状态）
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_init(void)
{
  gpio_init_type gpio_init_struct;
  spi_init_type spi_init_struct;
  uint8_t can_ctrl_val;
  uint8_t chip_id;

  /* ---- 步骤1：使能外设时钟 ---- */
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);

  /* ---- 步骤2：配置 SPI1 数据引脚（PA5=SCK, PA6=MISO, PA7=MOSI）---- */
  /* SCK 和 MOSI：无上下拉 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_5 | GPIO_PINS_7;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);

  /* MISO（PA6）：上拉，防止 SIT1145 未驱动时引脚悬空 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = GPIO_PINS_6;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);

  /* PA5 -> SPI1_SCK（AF MUX0） */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE5, GPIO_MUX_0);
  /* PA6 -> SPI1_MISO（AF MUX0） */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_0);
  /* PA7 -> SPI1_MOSI（AF MUX0） */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_0);

  /* ---- 步骤3：配置 PA4 为 CS（GPIO 输出，默认高电平）---- */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = SIT1145_CS_GPIO_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(SIT1145_CS_GPIO_PORT, &gpio_init_struct);
  /* CS 拉高（未选中） */
  gpio_bits_set(SIT1145_CS_GPIO_PORT, SIT1145_CS_GPIO_PIN);

  /* ---- 步骤4：配置 PB3 为输出低（悬空引脚）---- */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins           = SIT1145_FLOAT_GPIO_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(SIT1145_FLOAT_GPIO_PORT, &gpio_init_struct);
  gpio_bits_reset(SIT1145_FLOAT_GPIO_PORT, SIT1145_FLOAT_GPIO_PIN);

  /* ---- 步骤5：初始化 SPI1 ---- */
  spi_default_para_init(&spi_init_struct);
  spi_init_struct.transmission_mode     = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode     = SPI_MODE_MASTER;
  spi_init_struct.mclk_freq_division    = SIT1145_SPI_DIV;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.frame_bit_num         = SPI_FRAME_8BIT;
  spi_init_struct.clock_polarity        = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase           = SPI_CLOCK_PHASE_2EDGE;
  spi_init_struct.cs_mode_selection     = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI1, &spi_init_struct);

  /* 使能 SPI1 */
  spi_enable(SPI1, TRUE);
  sit1145_delay_ms(1U);

  /* ---- 步骤6：通过识别码 0x7E 做 SPI 通信检查 ---- */
  chip_id = sit1145_read_reg(SIT1145_REG_IDENTIFICATION);
  if ((chip_id != SIT1145_ID_AQ) && (chip_id != SIT1145_ID_AQ_FD))
  {
    return 0;
  }

  /* ---- 步骤7：配置 CAN Control 寄存器 ----
   *   CFDC=1（CAN FD 容忍使能）
   *   PNCOK=0（局部网络配置无效）
   *   CPNC=0（选择性唤醒关闭）
   *   CMC=01（正常模式，VCC 欠压自动恢复）
   *   必须在切换到 Normal 模式之前完成配置 */
  can_ctrl_val = SIT1145_CAN_FD_TOLERANCE_EN
               | SIT1145_CAN_NETW_INVALID
               | SIT1145_SEL_WAKEUP_DIS
               | SIT1145_CAN_MODE_NORMAL;
  sit1145_write_reg(SIT1145_REG_CAN_CONTROL, can_ctrl_val);
  if (sit1145_read_reg(SIT1145_REG_CAN_CONTROL) != can_ctrl_val)
  {
    return 0;
  }

  /* ---- 步骤8：配置 Data Rate（250kbps）---- */
  sit1145_write_reg(SIT1145_REG_DATA_RATE, SIT1145_DEFAULT_DRATE);
  if (sit1145_read_reg(SIT1145_REG_DATA_RATE) != SIT1145_DEFAULT_DRATE)
  {
    return 0;
  }

  /* ---- 步骤9：使能标准 CAN 唤醒（ISO 11898-2 WUP），关闭选择性唤醒 ----
   *     CCU 发送任意 250kbps 帧即可唤醒（通常是 UDS 0x18DA0D03）。
   *     选择性唤醒（CPNC=PNCOK=1）保持关闭。 */
  sit1145_wake_enable();
  sit1145_wakeup_clear();

  /* ---- 步骤10：上电默认进入 Standby（SPI 仍可访问）---- */
  if (sit1145_standby_mode_set() == 0U)
  {
    sit1145_delay_ms(1U);
    if (sit1145_standby_mode_set() == 0U)
    {
      return 0;
    }
  }

  return 1;
}
