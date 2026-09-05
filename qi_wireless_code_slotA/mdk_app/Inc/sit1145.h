/**
  **************************************************************************
  * @file     sit1145.h
  * @brief    SIT1145 CAN FD 收发器驱动（AT32F426）
  **************************************************************************
  */

#ifndef __SIT1145_H
#define __SIT1145_H

#ifdef __cplusplus
extern "C" {
#endif

/* 头文件 ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* 导出常量 ---------------------------------------------------------------*/

/* SIT1145 SPI 片选引脚定义（PA4，低电平有效） */
#define SIT1145_CS_GPIO_PORT        GPIOA
#define SIT1145_CS_GPIO_PIN         GPIO_PINS_4

/* SIT1145 悬空引脚 PB3（输出低） */
#define SIT1145_FLOAT_GPIO_PORT     GPIOB
#define SIT1145_FLOAT_GPIO_PIN      GPIO_PINS_3

/* SPI 读写协议位 */
#define SIT1145_WRITE               0x00
#define SIT1145_READ                0x01

/* SIT1145 寄存器地址 */
#define SIT1145_REG_MODE_CONTROL          0x01  /* 模式控制 */
#define SIT1145_REG_CAN_CONTROL           0x20  /* CAN 控制 */
#define SIT1145_REG_TRANSCEIVER_STATUS    0x22  /* 收发器状态 */
#define SIT1145_REG_TRANSCEIVER_EVENT_EN  0x23  /* 收发器事件使能 */
#define SIT1145_REG_TRANSCEIVER_EVENT     0x24  /* 收发器事件标志 */
#define SIT1145_REG_DATA_RATE             0x26  /* 数据速率 */
#define SIT1145_REG_IDENTIFICATION        0x7E  /* 芯片识别码 */

/* 芯片识别码寄存器值（数据手册表28） */
#define SIT1145_ID_AQ               0x70  /* SIT1145AQ */
#define SIT1145_ID_AQ_FD            0x74  /* SIT1145AQ/FD */

/* 收发器状态寄存器位（0x22） */
#define SIT1145_TRAN_STA_CTS        (1U << 7)  /* 1=Normal 模式，可发送 */

/* 收发器事件使能（0x23）/ 事件标志（0x24），兼容 TJA1145 */
#define SIT1145_CWE                 (1U << 0)  /* CAN 唤醒使能 */
#define SIT1145_CW                  (1U << 0)  /* CAN 唤醒事件标志（写1清零） */

/* 模式控制寄存器值 */
#define SIT1145_MC_SLEEP_MODE       0x01  /* 睡眠模式 */
#define SIT1145_MC_STANDBY_MODE     0x04  /* 待机模式 */
#define SIT1145_MC_NORMAL_MODE      0x07  /* 正常模式 */
#define SIT1145_MC_MODE_MASK        0x07  /* 模式位掩码 */

/* CAN 控制寄存器位定义 */
#define SIT1145_CAN_CTRL_CFDC_POS   6U   /* CAN FD 容忍使能位 */
#define SIT1145_CAN_CTRL_PNCOK_POS  5U   /* 局部网络配置有效位 */
#define SIT1145_CAN_CTRL_CPNC_POS   4U   /* 选择性唤醒使能位 */
#define SIT1145_CAN_CTRL_CMC_POS    0U   /* CAN 收发器模式位 */
#define SIT1145_CAN_CTRL_CMC_MASK   0x03

/* CAN 控制配置值 */
#define SIT1145_CAN_FD_TOLERANCE_EN (1U << SIT1145_CAN_CTRL_CFDC_POS)
#define SIT1145_CAN_NETW_INVALID    (0U << SIT1145_CAN_CTRL_PNCOK_POS)
#define SIT1145_SEL_WAKEUP_DIS      (0U << SIT1145_CAN_CTRL_CPNC_POS)
#define SIT1145_CAN_MODE_NORMAL     0x01  /* 正常模式，VCC 欠压自动恢复 */

/* 数据速率寄存器值 */
#define SIT1145_DRATE_50K           0x00
#define SIT1145_DRATE_100K          0x01
#define SIT1145_DRATE_125K          0x02
#define SIT1145_DRATE_250K          0x03
#define SIT1145_DRATE_500K          0x05
#define SIT1145_DRATE_1000K         0x07

/* 本项目默认 CAN 速率：250kbps */
#define SIT1145_DEFAULT_DRATE       SIT1145_DRATE_250K

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief  初始化 SIT1145 CAN 收发器
 * @note   SPI Mode 1，配置 CAN 控制寄存器、数据速率 250kbps、使能 CWE，
 *         然后进入 Standby 模式。标准 ISO 11898-2 唤醒模式已使能，
 *         任意 250kbps CAN 帧均可唤醒。APP 启动后保持 Standby 等待唤醒。
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_init(void);

/**
 * @brief  通过 SPI 写 SIT1145 寄存器
 * @param  addr: 寄存器地址
 * @param  data: 写入值
 */
void sit1145_write_reg(uint8_t addr, uint8_t data);

/**
 * @brief  通过 SPI 读 SIT1145 寄存器
 * @param  addr: 寄存器地址
 * @retval 寄存器值
 */
uint8_t sit1145_read_reg(uint8_t addr);

/**
 * @brief  切换 SIT1145 到 Normal 模式
 * @note   Normal 模式：CAN 收发器激活，可收发帧。
 *         切换完成后 CTS 位置位。
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_normal_mode_set(void);

/**
 * @brief  切换 SIT1145 到 Standby 模式
 * @note   Standby 模式：低功耗监听状态，CAN 总线被动（不发送）。
 *         收发器可被 CAN 总线活动唤醒。SPI 接口仍可访问。
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_standby_mode_set(void);

/**
 * @brief  切换 SIT1145 到 Sleep 模式
 * @note   Sleep 模式：最低功耗。
 *         ⚠️ SPI 接口在 Sleep 模式下不可用，任何 SPI 读写都会失败。
 *         唤醒方式：INH 引脚电平变化，或重新上电。
 *         调用前须确保 CAN 控制器已停止。
 * @retval 1 始终返回1（无法验证，SPI 已断开）
 */
uint8_t sit1145_sleep_mode_set(void);

/**
 * @brief  获取 SIT1145 当前工作模式
 * @retval 模式控制寄存器值 & 0x07：
 *         SIT1145_MC_SLEEP_MODE   (0x01) — Sleep
 *         SIT1145_MC_STANDBY_MODE (0x04) — Standby
 *         SIT1145_MC_NORMAL_MODE  (0x07) — Normal
 *         0xFF — SPI 读取失败（可能处于 Sleep 模式）
 */
uint8_t sit1145_get_mode(void);

/**
 * @brief  使能标准 CAN 远程唤醒（CWE=1，关闭选择性唤醒 WUF）
 */
void sit1145_wake_enable(void);

/**
 * @brief  检测是否有 CAN 唤醒事件挂起
 * @retval 1=有唤醒事件，0=无
 */
uint8_t sit1145_wakeup_pending(void);

/**
 * @brief  清除 CAN 唤醒事件标志（写1清零 CW 位）
 */
void sit1145_wakeup_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __SIT1145_H */
