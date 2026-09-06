/**
  **************************************************************************
  * @file     sit1145.h
  * @brief    SIT1145 CAN FD 收发器驱动（AT32F426）
  * @details  本头文件定义 SIT1145 收发器的全部寄存器地址、位掩码、
  *           模式常量及对外接口。硬件通过 SPI1 与 MCU 通信。
  *
  *           工作模式一览：
  *           ┌─────────┬────────┬────────┬──────────────┐
  *           │ 模式    │ 功耗   │ SPI    │ CAN 收发     │
  *           ├─────────┼────────┼────────┼──────────────┤
  *           │ Normal  │ 高     │ ✅ 可用 │ ✅ 收发      │
  *           │ Standby │ 低     │ ✅ 可用 │ ❌ 仅监听唤醒 │
  *           │ Sleep   │ 最低   │ ❌ 不可用│ ❌ 完全关闭  │
  *           └─────────┴────────┴────────┴──────────────┘
  **************************************************************************
  */

#ifndef __SIT1145_H
#define __SIT1145_H

#ifdef __cplusplus
extern "C" {
#endif

/* 头文件 ------------------------------------------------------------------*/
#include "at32f422_426.h"

/* ==========================================================================
 *  引脚定义
 * ========================================================================== */

/** @brief SIT1145 SPI 片选引脚（PA4，低电平有效，软件控制） */
#define SIT1145_CS_GPIO_PORT        GPIOA
#define SIT1145_CS_GPIO_PIN         GPIO_PINS_4

/** @brief SIT1145 悬空引脚（PB3，硬件未使用，输出低防止悬空） */
#define SIT1145_FLOAT_GPIO_PORT     GPIOB
#define SIT1145_FLOAT_GPIO_PIN      GPIO_PINS_3

/* ==========================================================================
 *  SPI 读写协议
 *  SIT1145 SPI 帧格式：
 *    写：CS↓ → [addr<<1 | 0] → [data] → CS↑
 *    读：CS↓ → [addr<<1 | 1] → [dummy, 收 data] → CS↑
 * ========================================================================== */

/** @brief SPI 写标志（地址字节最低位=0） */
#define SIT1145_WRITE               0x00

/** @brief SPI 读标志（地址字节最低位=1） */
#define SIT1145_READ                0x01

/* ==========================================================================
 *  寄存器地址
 *  SIT1145 共 8 个可访问寄存器，地址宽度 7 位
 * ========================================================================== */

#define SIT1145_REG_MODE_CONTROL          0x01  /**< 模式控制（读写）：选择 Sleep/Standby/Normal */
#define SIT1145_REG_MAIN_STATUS           0x03  /**< 主状态（只读）：FSMS/OTWS/NMS */
#define SIT1145_REG_CAN_CONTROL           0x20  /**< CAN 控制（读写）：CMC/CFDC/PNCOK/CPNC 配置 */
#define SIT1145_REG_TRANSCEIVER_STATUS    0x22  /**< 收发器状态（只读）：CTS 等状态位 */
#define SIT1145_REG_TRANSCEIVER_EVENT_EN  0x23  /**< 事件使能（读写）：CWE/WUE 等唤醒使能位 */
#define SIT1145_REG_TRANSCEIVER_EVENT     0x24  /**< 事件标志（读写，W1C）：CW/WUF 等唤醒标志 */
#define SIT1145_REG_DATA_RATE             0x26  /**< 数据速率（读写）：CAN 波特率配置 */
#define SIT1145_REG_IDENTIFICATION        0x7E  /**< 芯片识别码（只读）：0x70=AQ，0x74=AQ-FD */

/* ==========================================================================
 *  芯片识别码（数据手册表28）
 *  上电后读 0x7E 寄存器验证 SPI 通信是否正常
 * ========================================================================== */

#define SIT1145_ID_AQ               0x70  /**< SIT1145AQ 识别码 */
#define SIT1145_ID_AQ_FD            0x74  /**< SIT1145AQ/FD 识别码（支持 CAN FD） */

/* ==========================================================================
 *  收发器状态寄存器位定义（0x22，只读）— 数据手册表5
 * ========================================================================== */

/**
 * @brief CTS 位（bit7）：Clear To Send
 * @note  1 = 收发器已进入激活模式（Normal），可驱动 CAN 总线发送帧
 *        0 = 收发器未处于激活模式（Standby/Sleep/离线）
 */
#define SIT1145_TRAN_STA_CTS        (1U << 7)

/**
 * @brief CPNERR 位（bit6）：CAN Partial Networking Error
 * @note  局部网络错误标志（只读）：
 *        0 = 未检测到 CAN 局部网络错误（PNFDE=0 且 PNCOK=1 时）
 *        1 = 检测到局部网络错误
 *        本项目未使用局部网络功能，该位通常为 0
 */
#define SIT1145_TRAN_STA_CPNERR     (1U << 6)

/* ==========================================================================
 *  主状态寄存器位定义（0x03，只读）— 数据手册表3
 *  上电后默认 0x00
 * ========================================================================== */

/**
 * @brief FSMS 位（bit7）：Fail-Safe Mode Status
 * @note  指示进入 Sleep 模式的原因：
 *        0 = 由 SPI 指令（写 MODE_CONTROL=0x01）触发
 *        1 = 由 VCC 欠压（undervoltage）自动触发
 */
#define SIT1145_MAIN_STA_FSMS       (1U << 7)

/**
 * @brief OTWS 位（bit6）：Over-Temperature Warning Status
 * @note  过温警告状态：
 *        0 = 温度正常
 *        1 = 芯片温度超过过温警告阈值（典型 150°C）
 *        在 Normal 模式下持续监测，过温时收发器可能自动保护
 */
#define SIT1145_MAIN_STA_OTWS       (1U << 6)

/**
 * @brief NMS 位（bit5）：Normal Mode Status
 * @note  指示上电后是否已进入过 Normal 模式：
 *        0 = 上电后从未进入 Normal 模式
 *        1 = 自上电以来至少进入过一次 Normal 模式
 *        该标志上电后清零，首次进入 Normal 后置位，之后不会清零
 */
#define SIT1145_MAIN_STA_NMS        (1U << 5)

/* ==========================================================================
 *  收发器事件使能（0x23）与事件标志（0x24）
 *  两寄存器位定义相同，兼容 TJA1145
 *  事件标志为 W1C（Write-1-to-Clear）：写1清除对应标志
 * ========================================================================== */

/**
 * @brief CWE 位（bit0）：CAN Wake-up Enable
 * @note  写入 TRANSCEIVER_EVENT_EN 寄存器
 *        1 = 使能标准 CAN 唤醒检测（ISO 11898-2 WUP 模式）
 *        0 = 关闭 CAN 唤醒检测
 */
#define SIT1145_CWE                 (1U << 0)

/**
 * @brief CW 位（bit0）：CAN Wake-up event
 * @note  读 TRANSCEIVER_EVENT 寄存器
 *        1 = 检测到 CAN 总线唤醒模式（WUP）
 *        0 = 无唤醒事件
 *        写1清零（W1C）
 */
#define SIT1145_CW                  (1U << 0)

/* ==========================================================================
 *  模式控制寄存器值（0x01）
 *  写入 MODE_CONTROL 寄存器切换工作模式
 *  [2:0] = 模式选择位，其他位保留
 * ========================================================================== */

#define SIT1145_MC_SLEEP_MODE       0x01  /**< Sleep 模式：最低功耗，SPI 不可用 */
#define SIT1145_MC_STANDBY_MODE     0x04  /**< Standby 模式：低功耗监听，SPI 可用 */
#define SIT1145_MC_NORMAL_MODE      0x07  /**< Normal 模式：全功能，CAN 收发正常 */
#define SIT1145_MC_MODE_MASK        0x07  /**< 模式位掩码 [2:0] */

/* ==========================================================================
 *  CAN 控制寄存器位定义（0x20）
 *  配置 CAN 收发器的工作模式和功能使能
 * ========================================================================== */

#define SIT1145_CAN_CTRL_CFDC_POS   6U   /**< bit6：CAN FD 容忍使能（1=容忍 FD 帧） */
#define SIT1145_CAN_CTRL_PNCOK_POS  5U   /**< bit5：局部网络配置有效（0=无效） */
#define SIT1145_CAN_CTRL_CPNC_POS   4U   /**< bit4：选择性唤醒使能（0=关闭） */
#define SIT1145_CAN_CTRL_CMC_POS    0U   /**< bit[1:0]：CAN 收发器模式 */
#define SIT1145_CAN_CTRL_CMC_MASK   0x03 /**< CMC 位掩码 */

/* CAN 控制寄存器配置值（本项目使用的组合） */

#define SIT1145_CAN_FD_TOLERANCE_EN (1U << SIT1145_CAN_CTRL_CFDC_POS)   /**< bit6=1：容忍 CAN FD 帧 */
#define SIT1145_CAN_NETW_INVALID    (0U << SIT1145_CAN_CTRL_PNCOK_POS)  /**< bit5=0：局部网络配置无效 */
#define SIT1145_SEL_WAKEUP_DIS      (0U << SIT1145_CAN_CTRL_CPNC_POS)   /**< bit4=0：关闭选择性唤醒 */

/* CMC 模式值（bit[1:0]）— 数据手册表4 */
#define SIT1145_CMC_OFFLINE         0x00  /**< 离线模式：CAN 收发器完全关闭，不上总线 */
#define SIT1145_CMC_ACTIVE_UVLO     0x01  /**< 主动模式（带欠压检测）：正常收发，VCC 欠压自动恢复 */
#define SIT1145_CMC_ACTIVE_NO_UVLO  0x02  /**< 主动模式（不带欠压检测）：正常收发，VCC 欠压不自动恢复 */
#define SIT1145_CMC_LISTEN_ONLY     0x03  /**< 只听模式：仅接收不发送，用于总线监听/诊断 */

/** @brief 本项目使用的 CMC 模式：主动模式（带欠压检测） */
#define SIT1145_CAN_MODE_NORMAL     SIT1145_CMC_ACTIVE_UVLO

/* ==========================================================================
 *  数据速率寄存器值（0x26）
 *  配置 CAN 总线波特率
 * ========================================================================== */

#define SIT1145_DRATE_50K           0x00  /**< 50 kbps */
#define SIT1145_DRATE_100K          0x01  /**< 100 kbps */
#define SIT1145_DRATE_125K          0x02  /**< 125 kbps */
#define SIT1145_DRATE_250K          0x03  /**< 250 kbps */
#define SIT1145_DRATE_500K          0x05  /**< 500 kbps */
#define SIT1145_DRATE_1000K         0x07  /**< 1000 kbps */

/** @brief 本项目默认 CAN 速率：250kbps */
#define SIT1145_DEFAULT_DRATE       SIT1145_DRATE_250K

/* ==========================================================================
 *  导出函数
 * ========================================================================== */

/**
 * @brief  初始化 SIT1145 CAN 收发器
 * @note   完整初始化流程：
 *         1. 使能 GPIO/SPI 时钟
 *         2. 配置 SPI1 引脚（PA5=SCK, PA6=MISO, PA7=MOSI）
 *         3. 配置 CS 引脚（PA4，默认高电平=未选中）
 *         4. 配置悬空引脚 PB3（输出低）
 *         5. 初始化 SPI1（Mode 1, 8bit, MSB, 软件 CS）
 *         6. 读识别码 0x7E 验证 SPI 通信
 *         7. 配置 CAN Control（CFDC=1, CMC=01）
 *         8. 配置 Data Rate（250kbps）
 *         9. 使能标准 CAN 唤醒（CWE=1）
 *        10. 进入 Standby 模式（APP 低功耗默认状态）
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_init(void);

/**
 * @brief  通过 SPI 写 SIT1145 寄存器
 * @note   SPI 帧：CS↓ → [addr<<1|0] → [data] → CS↑
 * @param  addr: 寄存器地址（7 位）
 * @param  data: 写入值（8 位）
 */
void sit1145_write_reg(uint8_t addr, uint8_t data);

/**
 * @brief  通过 SPI 读 SIT1145 寄存器
 * @note   SPI 帧：CS↓ → [addr<<1|1] → [0xFF, 收 data] → CS↑
 * @param  addr: 寄存器地址（7 位）
 * @retval 寄存器值（8 位），超时返回 0xFF
 */
uint8_t sit1145_read_reg(uint8_t addr);

/**
 * @brief  切换到 Normal 模式并等待 CTS 就绪
 * @note   Normal 模式下 CAN 收发器激活，可收发帧。
 *         切换后轮询 TRANSCEIVER_STATUS 的 CTS 位（最多 20ms），
 *         CTS=1 表示收发器已就绪可驱动总线。
 *         如果第一次切换失败会重试一次。
 * @retval 1 成功（CTS=1），0 失败（超时或 SPI 错误）
 */
uint8_t sit1145_normal_mode_set(void);

/**
 * @brief  切换到 Standby 模式
 * @note   Standby 模式：低功耗监听状态。
 *         - CAN 总线被动（不发送帧）
 *         - SPI 接口仍可访问（可读写寄存器）
 *         - 收发器监听 CAN 总线，检测到 WUP 模式后置位 CW 标志
 *         - RXD 引脚在唤醒事件期间被强制拉低
 * @retval 1 成功，0 失败
 */
uint8_t sit1145_standby_mode_set(void);

/**
 * @brief  切换到 Sleep 模式
 * @note   Sleep 模式：最低功耗。
 *         ⚠️ SPI 接口在 Sleep 模式下完全不可用！
 *         调用后任何 SPI 读写都会超时返回 0xFF。
 *         唤醒方式：INH 引脚电平变化，或重新上电。
 *         调用前须确保 CAN 控制器已停止。
 * @retval 1 始终返回 1（无法回读验证，SPI 已断开）
 */
uint8_t sit1145_sleep_mode_set(void);

/**
 * @brief  获取 SIT1145 当前工作模式
 * @note   读取 MODE_CONTROL 寄存器 [2:0] 位
 * @retval 模式值：
 *         SIT1145_MC_SLEEP_MODE   (0x01) — Sleep
 *         SIT1145_MC_STANDBY_MODE (0x04) — Standby
 *         SIT1145_MC_NORMAL_MODE  (0x07) — Normal
 *         0xFF — SPI 读取失败（可能处于 Sleep 模式）
 */
uint8_t sit1145_get_mode(void);

/**
 * @brief  读取 SIT1145 主状态寄存器（0x03，只读）
 * @note   反映芯片全局状态：
 *         - FSMS（bit7）：Sleep 触发原因（0=SPI 指令，1=VCC 欠压）
 *         - OTWS（bit6）：过温警告（0=正常，1=温度过高）
 *         - NMS（bit5）：是否进入过 Normal（0=从未，1=至少一次）
 * @retval 主状态寄存器原始值，SPI 失败返回 0xFF
 */
uint8_t sit1145_get_main_status(void);

/**
 * @brief  判断 Sleep 模式是否由 VCC 欠压触发
 * @note   读取主状态寄存器 FSMS 位（bit7）
 * @retval 1=由欠压触发，0=由 SPI 指令触发，0xFF=SPI 读取失败
 */
uint8_t sit1145_is_sleep_by_undervoltage(void);

/**
 * @brief  查询过温警告状态
 * @note   读取主状态寄存器 OTWS 位（bit6），温度超阈值时置位
 * @retval 1=过温警告，0=温度正常，0xFF=SPI 读取失败
 */
uint8_t sit1145_is_overtemp_warning(void);

/**
 * @brief  查询是否已进入过 Normal 模式
 * @note   读取主状态寄存器 NMS 位（bit5）
 *         上电后首次进 Normal 时硬件置位，之后不清零
 * @retval 1=已进入过 Normal，0=从未进入，0xFF=SPI 读取失败
 */
uint8_t sit1145_has_entered_normal(void);

/**
 * @brief  读取 CAN 控制寄存器（0x20，读写）
 * @note   返回 CAN 控制寄存器原始值，包含 CFDC/PNCOK/CPNC/CMC 等配置位
 * @retval 寄存器原始值，SPI 失败返回 0xFF
 */
uint8_t sit1145_get_can_control(void);

/**
 * @brief  获取当前 CMC 模式值（bit[1:0]）
 * @note   读取 CAN 控制寄存器并提取 CMC 位：
 *         - SIT1145_CMC_OFFLINE    (0x00) 离线模式
 *         - SIT1145_CMC_ACTIVE_UVLO(0x01) 主动模式（带欠压检测）
 *         - SIT1145_CMC_ACTIVE_NO_UVLO(0x02) 主动模式（不带欠压检测）
 *         - SIT1145_CMC_LISTEN_ONLY(0x03) 只听模式
 * @retval CMC 模式值，SPI 失败返回 0xFF
 */
uint8_t sit1145_get_cmc_mode(void);

/**
 * @brief  读取收发器状态寄存器（0x22，只读）
 * @note   返回收发器状态原始值，包含 CTS/CPNERR 等状态位
 * @retval 寄存器原始值，SPI 失败返回 0xFF
 */
uint8_t sit1145_get_transceiver_status(void);

/**
 * @brief  查询 CTS 状态（收发器是否就绪）
 * @note   读取收发器状态寄存器 CTS 位（bit7）
 *         CTS=1 表示收发器已进入激活模式，可驱动 CAN 总线
 * @retval 1=就绪可发送，0=未就绪，0xFF=SPI 读取失败
 */
uint8_t sit1145_is_cts_ready(void);

/**
 * @brief  查询局部网络错误状态
 * @note   读取收发器状态寄存器 CPNERR 位（bit6）
 *         本项目未使用局部网络功能，该位通常为 0
 * @retval 1=有错误，0=无错误，0xFF=SPI 读取失败
 */
uint8_t sit1145_is_cpn_error(void);

/**
 * @brief  使能标准 CAN 唤醒（写 CWE=1 到 EVENT_EN 寄存器）
 * @note   使能后 SIT1145 在 Standby 模式下监听 CAN 总线，
 *         检测到 ISO 11898-2 WUP 模式后置位 CW 标志。
 *         同时关闭选择性唤醒（WUF），只用标准唤醒。
 */
void sit1145_wake_enable(void);

/**
 * @brief  检测是否有 CAN 唤醒事件挂起
 * @note   双重检测机制：
 *         1. 先查 PA11 引脚电平 — Standby 时 SIT1145 在整个唤醒事件期间
 *            强制 RXD(PA11) 拉低，MCU 可通过 GPIO 读到低电平
 *         2. 再查 SPI 寄存器 — 读 TRANSCEIVER_EVENT 的 CW 位
 *            （CW=1 表示检测到 WUP 模式，W1C 标志）
 *         PA11 引脚检测更快（无 SPI 开销），CW 寄存器更可靠
 * @retval 1=有唤醒事件，0=无
 */
uint8_t sit1145_wakeup_pending(void);

/**
 * @brief  清除 CAN 唤醒事件标志（写1清零 CW 位）
 * @note   向 TRANSCEIVER_EVENT 寄存器的 CW 位写1，
 *         硬件自动清零该标志（W1C 机制）。
 *         进入 Standby 前和唤醒后都应调用此函数。
 */
void sit1145_wakeup_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __SIT1145_H */
