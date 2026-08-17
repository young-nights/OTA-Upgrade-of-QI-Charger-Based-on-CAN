# Qi 无线充电模块 CAN OTA 升级 — 详细工作流文档

> 本文档基于项目实际源码编写（`qi_wireless_bootloader/` 与 `qi_wireless_code/`），
> 所有函数名、变量名、常量与代码片段均与源码保持一致。
> 最后校核日期：2026-08-17（依据当前仓库源码状态）。

---

## 目录

1. [项目概述](#1-项目概述)
2. [Flash 布局](#2-flash-布局)
3. [Bootloader 工作流](#3-bootloader-工作流详细)
4. [APP 工作流](#4-app-工作流详细)
5. [OTA 升级完整流程（端到端）](#5-ota-升级完整流程端到端)
6. [CAN 通信协议](#6-can-通信协议)
7. [中断处理](#7-中断处理)
8. [关键常量和配置](#8-关键常量和配置)
9. [实现现状与注意事项](#9-实现现状与注意事项)

---

## 1. 项目概述

### 1.1 项目目标

在 **CAN 总线** 环境下，对 Qi 无线充电模块（AT32F426KBU7-4 主控）实现 **OTA 固件升级**：

- 通过 **UDS（Unified Diagnostic Services，统一诊断服务）** 协议在 CAN 上传输固件镜像；
- 采用 **双槽位（A/B Slot）** 设计，支持镜像校验失败时回退到旧版本；
- 引入 **Trial Boot（试运行）** 状态机，新固件需在限定时间内被 APP 确认，否则回滚；
- 提供 **Safe Mode（安全模式）**，当所有应用槽位无效时仍可通过 CAN 重新刷写。

### 1.2 硬件平台

| 项目 | 说明 |
|------|------|
| 主控芯片 | AT32F426KBU7-4（雅特力） |
| 内核 | Cortex-M4F（FPU，硬件除法） |
| 主频 | 180 MHz（HEXT + PLL，APB1 分频 = 1，即 APB1 = 180 MHz） |
| Flash | 128 KB（0x08000000 ~ 0x0801FFFF） |
| SRAM | 20 KB（0x20000000 ~ 0x20004FFF） |
| 外设库 | 标准外设库 SPL（`at32f422_426_*`），**非 HAL** |
| 通信外设 | CAN1（PA11=RX / PA12=TX，扩展帧，250 kbps）、USART2（Qi 芯片通信，9600 8N1） |
| 看门狗 | IWDG 独立看门狗（LSI ~40 kHz，~1 s 超时） |

### 1.3 软件架构

```
                    ┌────────────────────────────────────────────┐
                    │            CAN 总线 (250 kbps)             │
                    └───────▲──────────────────────▲────────────┘
                            │ UDS 请求/响应          │ 生命周期广播
              ┌─────────────┴───────────┐  ┌───────┴──────────┐
              │  Bootloader (16 KB)     │  │  APP 主应用(48KB) │
              │  0x08000000             │  │  0x08004000+      │
              │  · 启动引导/槽位选择     │  │  · 充电业务逻辑    │
              │  · Safe Mode + UDS OTA  │  │  · UDS 触发 OTA    │
              │  · 镜像校验/跳转        │  │  · Qi UART 驱动    │
              │  · Trial Boot 状态机    │  │  · Trial 确认      │
              └─────────────────────────┘  └───────────────────┘
                        │   共享 Flash 数据结构（metadata / image header）
                        ▼
              ┌─────────────────────────────────────────┐
              │   0x0801C000  OTA Metadata (512 B)      │
              │   0x0801E000  NVM 配置区(备份元数据占位) │
              └─────────────────────────────────────────┘
```

两个固件工程：

| 工程 | 目录 | 大小 | 职责 |
|------|------|------|------|
| Bootloader | `qi_wireless_bootloader/` | 16 KB | 上电引导、槽位选择、镜像校验、跳转 APP、Safe Mode（UDS OTA 下载） |
| APP | `qi_wireless_code/` | 48 KB | 充电业务、CAN UDS 响应、OTA 触发（写 metadata + 复位）、Qi UART 通信、Trial 确认 |

---

## 2. Flash 布局

### 2.1 地址分区表

常量定义于 `qi_wireless_bootloader/mdk_app/Inc/boot_metadata.h`（APP 侧在 `qi_wireless_code/mdk_app/Inc/ota_trigger.h` 中保持一致，前缀 `OTA_`）：

| 区域 | 起始地址 | 大小 | 用途 |
|------|---------|------|------|
| Bootloader | `0x08000000` | `0x4000` (16 KB) | 启动引导 + Safe Mode + UDS 下载 |
| APP_A | `0x08004000` | `0xC000` (48 KB) | 应用槽位 A（OTA 下载目标） |
| APP_B | `0x08010000` | `0xC000` (48 KB) | 应用槽位 B（回退槽位） |
| Metadata 主区 | `0x0801C000` | `0x2000` (8 KB) | OTA Metadata（512 B 结构体 + 冗余） |
| Metadata 备份区 | `0x0801E000` | `0x2000` (8 KB) | 元数据备份 / **NVM 配置区（共享，写保护约定）** |

```
0x08000000 ┌──────────────────────┐
           │  Bootloader (16KB)   │  0x08000000 ~ 0x08003FFF
0x08004000 ├──────────────────────┤
           │  APP_A (48KB)        │  Image Header(256B) + 应用代码
0x08010000 ├──────────────────────┤
           │  APP_B (48KB)        │  Image Header(256B) + 应用代码
0x0801C000 ├──────────────────────┤
           │  Metadata 主区 (8KB) │  ota_metadata_t (512B) @ 0x0801C000
0x0801E000 ├──────────────────────┤
           │  Metadata 备份/NVM   │  仅读取回退，不写入（避免破坏 NVM 配置）
0x08020000 └──────────────────────┘  (128KB Flash 末尾)
```

> **注意**：`boot_metadata_save()` 与 APP 的 `ota_metadata_save()` 均 **只写主区**
> （`META_PRIMARY_ADDR` / `OTA_META_PRIMARY_ADDR`），刻意跳过备份区
> （`0x0801E000`），因为该区域同时被 **NVM 配置（`nvm_drv`）** 占用。
> 备份区仅作为读取回退源。

### 2.2 OTA Metadata 结构体

```c
/* boot_metadata.h / ota_trigger.h —— 两处定义完全一致，共 512 字节 */
typedef struct
{
  uint32_t magic;               /*!< 0x4F54414D "MATO" */
  uint32_t version;             /*!< metadata format version = 1 */
  uint8_t  active_slot;         /*!< 0=A, 1=B */
  uint8_t  pending_slot;        /*!< 0=A, 1=B, 0xFE=none */
  uint8_t  slot_a_valid;        /*!< 1=slot A image valid */
  uint8_t  slot_b_valid;        /*!< 1=slot B image valid */
  uint32_t slot_a_crc32;        /*!< CRC32 of slot A image */
  uint32_t slot_b_crc32;        /*!< CRC32 of slot B image */
  uint8_t  trial_state;         /*!< 0=IDLE, 1=PENDING, 2=ACTIVE, 3=CONFIRMED */
  uint8_t  trial_slot;          /*!< slot under trial (0=A, 1=B) */
  uint8_t  trial_retry_count;   /*!< current retry count */
  uint8_t  trial_max_retries;   /*!< max retries (default 3) */
  uint16_t trial_timeout_sec;   /*!< trial timeout in seconds (default 10) */
  uint16_t reserved1;           /*!< reserved for alignment */
  uint32_t rollback_count;      /*!< number of rollbacks performed */
  uint8_t  last_boot_reason;    /*!< 0x00=power-on, 0x02=WDG, 0x03=OTA, 0x04=rollback */
  uint8_t  ota_state;           /*!< 0x00=idle, 0x01=downloading */
  uint8_t  reserved2[2];        /*!< reserved */
  uint8_t  padding[488];        /*!< padding to 512 bytes */
  uint32_t crc32;               /*!< CRC32 of all above fields */
} ota_metadata_t;
```

**CRC32 校验规则**：`crc32` 覆盖除自身外的全部字段。
偏移 `META_CRC32_OFFSET = sizeof(ota_metadata_t) - sizeof(uint32_t)`（即 508）。
算法为 IEEE 802.3（多项式 `0xEDB88320`，初值 `0xFFFFFFFF`，结果异或 `0xFFFFFFFF`），
实现为 `boot_crc32()`（Bootloader）/ `ota_crc32()`（APP），两处代码相同。

**常用常量**：

```c
#define META_MAGIC              0x4F54414DU   /*!< "MATO" */
#define META_VERSION            1U
#define SLOT_NONE               0xFEU
#define SLOT_A                  0U
#define SLOT_B                  1U
#define TRIAL_STATE_IDLE        0U
#define TRIAL_STATE_PENDING     1U
#define TRIAL_STATE_ACTIVE      2U
#define TRIAL_STATE_CONFIRMED   3U
#define TRIAL_MAX_RETRIES       3U
#define TRIAL_TIMEOUT_SEC       10U
#define BOOT_REASON_POWER_ON    0x00U
#define BOOT_REASON_WDG         0x02U
#define BOOT_REASON_OTA_ACT     0x03U
#define BOOT_REASON_ROLLBACK    0x04U
#define OTA_STATE_IDLE          0x00U
#define OTA_STATE_DOWNLOADING   0x01U
```

### 2.3 Image Header 结构体

定义于 `qi_wireless_bootloader/mdk_app/Inc/boot_verify.h`，**每个槽位的起始 256 字节**：

```c
#define IMAGE_MAGIC   0x4F544158U    /*!< "XATO" */

typedef struct
{
  uint32_t magic;              /*!< 0x4F544158 "XATO" */
  uint32_t image_length;       /*!< valid image size in bytes (excluding header) */
  uint32_t crc32;              /*!< CRC32 of image data (excluding header) */
  uint8_t  signature[64];      /*!< ECDSA P-256 R||S signature (placeholder) */
  char     version[16];        /*!< "MAJOR.MINOR.PATCH\0" */
  uint32_t build_timestamp;    /*!< Unix timestamp of build */
  uint8_t  reserved[160];      /*!< padding to 256 bytes */
} image_header_t;              /*!< 合计 256 字节 */
```

**布局关系**：

```
槽位基地址 (如 0x08004000)
  ├── [0x000 .. 0x0FF]  image_header_t（256B，OTA 下载时由 Bootloader 生成写入）
  └── [0x100 .. 槽位末尾]  应用镜像数据（向量表 + 代码，长度 = image_length）
```

> 关键点：**应用代码起始于 `槽位基地址 + IMAGE_HEADER_SIZE(256)`**。
> `try_boot_slot()` 跳转时使用 `boot_jump_to_app(slot_addr + IMAGE_HEADER_SIZE)`，
> 镜像 CRC32 也基于 `base_addr + IMAGE_HEADER_SIZE` 计算。

---

## 3. Bootloader 工作流（详细）

### 3.1 上电启动流程

入口 `qi_wireless_bootloader/mdk_user/Src/main.c`。流程图：

```
                    ┌─────────────────────┐
                    │  上电 / 复位 (Reset) │
                    └──────────┬──────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ① system_clock_config()          │  180MHz (HEXT+PLL)
              │    nvic_priority_group_config(4) │  4 位抢占优先级
              └──────────────────────────────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ② timer_drv_init()              │  SysTick 1ms tick
              │    wdg_drv_init()               │  IWDG ~1s 超时
              └──────────────────────────────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ③ boot_metadata_init(&g_meta)   │  主区→备份区→默认值
              └──────────────────────────────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ④ last_boot_reason=detect_boot_ │  检查 WDT 复位标志
              │    reason()                      │  → WDG / POWER_ON
              └──────────────────────────────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ⑤ process_trial_state(&g_meta)  │  Trial 状态机
              └──────────────────────────────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ⑤.5 ota_state==DOWNLOADING ?    │──是──▶ enter_safe_mode()
              └──────────────────────────────────┘        (不返回)
                               ▼ 否
              ┌──────────────────────────────────┐
              │ ⑥ select_boot_slot()            │  PENDING→trial_slot
              │                                  │  否则→active_slot
              └──────────────────────────────────┘
                               ▼
              ┌──────────────────────────────────┐
              │ ⑦ trial_state==ACTIVE ?         │──是──▶ 创建 1s 周期定时器
              └──────────────────────────────────┘       (trial_timer_callback)
                               ▼
              ┌──────────────────────────────────┐
              │ ⑧ try_boot_slot(选中槽位)        │  校验镜像→跳转
              └──────────────┬───────────────────┘
                   失败       │       成功
              ┌──────────────▼──────────┐   ┌──────────────┐
              │ ⑨ try_boot_slot(另一槽) │──▶│ 更新 active_ │
              │    成功→更新 active_slot │   │ slot 并保存  │
              └──────────────┬──────────┘   └──────────────┘
                   均失败     ▼
              ┌──────────────────────────────────────────────┐
              │ ⑩ enter_safe_mode() —— 双槽均无效，进入安全模式│
              └──────────────────────────────────────────────┘
```

**逐步说明**：

1. **时钟配置**：`system_clock_config()` 将系统时钟配置为 180 MHz；
   `nvic_priority_group_config(NVIC_PRIORITY_GROUP_4)` 设置 4 位抢占优先级分组。
2. **驱动初始化**：`timer_drv_init()`（SysTick 1 ms 时基 + 软件定时器池）；
   `wdg_drv_init()`（IWDG，约 998 ms 超时，一旦使能不可关闭）。
3. **元数据加载**：`boot_metadata_init(&g_meta)` 依次尝试：
   - 主区 `0x0801C000` 校验通过 → 拷贝到 `g_meta`；
   - 主区无效 → 尝试备份区 `0x0801E000`，有效则恢复写回主区；
   - 两区均无效 → `meta_fill_defaults()` 填充默认值并写主区（返回 -1）。
   - 校验规则（`meta_validate`）：magic == `META_MAGIC`、version == `META_VERSION`、
     CRC32 全字段校验通过。
4. **启动原因检测**：`detect_boot_reason()` 检查 RCC 复位状态寄存器
   `crm_flag_get(CRM_WDT_RESET_FLAG)`：
   - 看门狗复位 → 清除标志，返回 `BOOT_REASON_WDG (0x02)`；
   - 其余情况 → `BOOT_REASON_POWER_ON (0x00)`。
5. **Trial 状态机处理**：见 [3.2](#32-trial-boot-状态机)。
6. **OTA 下载请求检查**：若 `g_meta.ota_state == OTA_STATE_DOWNLOADING`（APP 触发 OTA 时
   写入），则立即清为 `OTA_STATE_IDLE` 并保存，然后 `enter_safe_mode()` 等待主机下载。
7. **槽位选择**：`select_boot_slot()`：
   - `trial_state == TRIAL_STATE_PENDING` → 选择 `trial_slot`；
   - 否则 → 选择 `active_slot`。
8. **Trial 超时定时器**：若 `trial_state == TRIAL_STATE_ACTIVE`，创建
   `TRIAL_TIMER_PERIOD_MS (1000)` ms 自动重载定时器，回调 `trial_timer_callback()`
   （每秒 `g_trial_elapsed_sec++` 并置 `g_trial_timer_flag = 1`）。
9. **尝试启动**：`try_boot_slot(boot_slot, &g_meta)`：
   - 校验镜像（见 [3.5](#35-image-验证流程)）；通过则标记槽位有效（首次置 1 时保存
     metadata）并 `boot_jump_to_app(slot_addr + IMAGE_HEADER_SIZE)`；
   - 失败返回 -1。
10. **回退到另一槽位**：首选槽失败 → 尝试 `other_slot`；若另一槽成功启动，则
    `g_meta.active_slot = other_slot` 并保存。
11. **双槽均失败** → `enter_safe_mode()`（不返回）。
12. 理论上不可达的兜底 `while(1) { wdg_drv_refresh(); }`。

### 3.2 Trial Boot 状态机

实现于 `qi_wireless_bootloader/mdk_app/Src/boot_trial.c`，由 `process_trial_state()`
在每次 Bootloader 启动时执行：

```
        ┌───────────────┐
        │     IDLE      │◀──────────────────────────────┐
        └───────┬───────┘                               │
                │ 请求试运行（PENDING 由外部置位）          │
                ▼                                        │
        ┌───────────────┐  retry_count++                 │
        │   PENDING     │  last_boot_reason=OTA_ACT      │
        └───────┬───────┘  保存 metadata                  │
                │ 状态机推进                               │
                ▼                                        │
        ┌───────────────┐  retry_count > max_retries(3)  │
        │    ACTIVE     │────────────────┐               │
        └───────┬───────┘                │               │
                │                        ▼               │
                │             ┌──────────────────┐       │
                │             │   ROLLBACK 处理   │       │
                │             │ rollback_count++  │       │
                │             │ → IDLE, retry=0   │       │
                │             │ active_slot=另一槽│       │
                │             │ (若该槽 valid)     │       │
                │             └──────────────────┘       │
                ▼                                        │
        ┌───────────────┐                                │
        │   CONFIRMED   │  APP 确认成功                   │
        └───────┬───────┘                                │
                │  → IDLE, retry_count=0, 保存            │
                └────────────────────────────────────────┘
```

| 状态 | 进入条件 | 处理动作 |
|------|---------|---------|
| `IDLE` | 默认 | 无操作 |
| `PENDING` | 外部将 `trial_state` 置 1 | → `ACTIVE`；`trial_retry_count++`；`last_boot_reason = BOOT_REASON_OTA_ACT`；保存 metadata |
| `ACTIVE` | PENDING 推进 | 若 `trial_retry_count > trial_max_retries`：`rollback_count++`、→ `IDLE`、`retry_count = 0`、`last_boot_reason = BOOT_REASON_ROLLBACK`，且若另一槽 `valid` 则切换 `active_slot` 到另一槽；保存 |
| `CONFIRMED` | APP 在试运行期确认（写 `CONFIRMED`） | → `IDLE`；`retry_count = 0`；保存 |
| 其他非法值 | - | 强制回 `IDLE` 并保存 |

**APP 侧确认**：APP 初始化时调用 `ota_confirm_if_needed()`，若读到
`trial_state == TRIAL_STATE_ACTIVE (2)` 则改为 `TRIAL_STATE_CONFIRMED (3)` 并保存
（见 [4.6](#46-trial-boot-确认流程)）。

> 当前源码现状：`TRIAL_STATE_PENDING` 尚无任何代码路径主动置位（详见
> [第 9 节](#9-实现现状与注意事项)），状态机框架与确认/回滚逻辑已实现。

### 3.3 Safe Mode 进入条件与 UDS 处理流程

**进入条件**（`enter_safe_mode()`，位于 `boot_safe_mode.c`）：

1. Bootloader 启动时发现 `ota_state == OTA_STATE_DOWNLOADING`（APP 触发的 OTA 请求）；
2. 两个槽位镜像均校验失败（`try_boot_slot` 双槽都返回 -1）。

**Safe Mode 事件循环**：

```c
void enter_safe_mode(void)
{
  g_safe_mode = 1;

  /* initialize CAN for safe mode communication */
  can_driver_init();
  can_driver_register_rx_callback(safe_mode_can_rx_handler);

  /* safe mode event loop */
  while (1)
  {
    timer_poll();
    can_driver_poll();
    wdg_drv_refresh();
  }
}
```

- CAN 使用与 APP 相同的 `can_driver_init()`（250 kbps 扩展帧，过滤全部扩展数据帧）；
- 循环内轮询软件定时器、轮询 CAN RX FIFO 并派发 `safe_mode_can_rx_handler`、喂狗；
- **不返回**（除非 IWDG 复位）。

**UDS 请求入口** `safe_mode_can_rx_handler(uint32_t id, uint8_t *data, uint8_t len)`：

- 忽略 `len == 0` 的帧；
- 取 `service_id = data[0]`，按服务分发（详见 3.4）；
- 响应统一通过 `safe_mode_send_response()` 发送（CAN ID `0x18DA030D`）；
- 不支持的服务回 NRC `0x7F + SID + 0x11`。

### 3.4 UDS 命令处理（Bootloader Safe Mode）

Safe Mode 下的 CAN ID：

```c
#define SAFE_MODE_CAN_ID_REQUEST    0x18DA0D03U  /*!< UDS request ID */
#define SAFE_MODE_CAN_ID_RESPONSE   0x18DA030DU  /*!< UDS response ID */
```

支持的服务（`boot_safe_mode.c`）：

```c
#define UDS_DIAG_SESSION_CTRL       0x10U
#define UDS_SECURITY_ACCESS         0x27U
#define UDS_REQUEST_DOWNLOAD        0x34U
#define UDS_TRANSFER_DATA           0x36U
#define UDS_REQUEST_TRANSFER_EXIT   0x37U
#define UDS_ECU_RESET               0x11U
#define UDS_NEGATIVE_RESPONSE       0x7FU
#define UDS_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U
```

#### 0x10 — DiagnosticSessionControl（诊断会话控制）

- 请求：`[0x10, sessionType]`
- 处理：直接返回正响应，回显会话类型
- 响应：`[0x50, sessionType]`（2 字节）

#### 0x27 — SecurityAccess（安全访问，简化版）

- 请求：`[0x27, subFunction, seed/key...]`
- 处理：**不做实际校验，接受任意 seed/key**（Bootloader 简化实现）
- 响应：`[0x67, subFunction]`（2 字节）

#### 0x34 — RequestDownload（请求下载）

- 请求：`[0x34, ...]`
- 处理流程：
  1. `flash_unlock()`；
  2. **擦除 APP_A 全部 24 个扇区**（每个 2 KB）：

     ```c
     for (sector_addr = APP_A_BASE_ADDR;
          sector_addr < (APP_A_BASE_ADDR + APP_A_SIZE);
          sector_addr += 0x800U)  /* 2KB sectors for AT32F426 */
     {
       if (flash_sector_erase(sector_addr) != FLASH_OPERATE_DONE)
       { erase_err = 1; break; }
     }
     flash_lock();
     ```
  3. 擦除失败 → NRC `0x72`（generalProgrammingFailure）；
  4. 初始化下载状态：

     ```c
     g_dl_write_addr    = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
     g_dl_bytes_written = 0;
     g_dl_block_seq     = 0;
     g_dl_active        = 1;
     ```
  5. 正响应（5 字节）：

     ```
     [0x74, 0x20, (APP_A_SIZE>>8)&0xFF, APP_A_SIZE&0xFF]
     = [0x74, 0x20, 0xC0, 0x00]
     ```
     - `0x20` = lengthFormatIdentifier（32 位长度）；
     - `0xC000` = maxNumberOfBlockLength（本实现为**最大固件总大小**，
       并非单帧长度；单帧有效载荷实际 ≤ 6 字节：8 字节 CAN - SID - blockSeq）。

#### 0x36 — TransferData（传输数据）

- 请求：`[0x36, blockSeq, data0..data5]`（最多 6 字节载荷）
- 处理流程：
  1. `g_dl_active == 0` → NRC `0x71`（transferDataAborted）；
  2. `len < 3` → NRC `0x13`（incorrectMessageLengthOrInvalidFormat）；
  3. 块序号校验：`g_dl_block_seq++` 后与请求的 `blockSeq` 比较，
     **不相等 → `g_dl_active = 0` 并回 NRC `0x71`**（要求主机从 blockSeq=1 顺序发送）；
  4. `data_len = len - 2`；若 `g_dl_bytes_written + data_len > MAX_IMAGE_SIZE`
     （`MAX_IMAGE_SIZE = APP_A_SIZE - IMAGE_HEADER_SIZE = 0xBF00`）→
     `g_dl_active = 0`，NRC `0x14`（responseTooLong）；
  5. **Flash 写入**（见 [3.6](#36-flash-写入流程)）；
  6. 写失败 → `g_dl_active = 0`，NRC `0x72`；
  7. 成功 → 更新 `g_dl_write_addr += data_len`、`g_dl_bytes_written += data_len`；
  8. 正响应：`[0x76, blockSeq]`（2 字节）。

#### 0x37 — RequestTransferExit（请求传输结束）

- 请求：`[0x37]`
- 处理流程：
  1. `g_dl_active == 0` → NRC `0x71`；
  2. `g_dl_active = 0`；
  3. **计算已下载镜像的 CRC32**：

     ```c
     image_data_addr = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE;
     computed_crc = boot_crc32((const void *)image_data_addr, g_dl_bytes_written);
     ```
  4. **生成并写入 Image Header**（写至 `APP_A_BASE_ADDR`，256 字节 = 64 word）：

     ```c
     memset((void *)&header, 0xFF, sizeof(image_header_t));
     header.magic        = IMAGE_MAGIC;
     header.image_length = g_dl_bytes_written;
     header.crc32        = computed_crc;
     flash_unlock();
     hdr_words = (const uint32_t *)&header;
     for (w = 0; w < hdr_word_count; w++)
       flash_word_program(APP_A_BASE_ADDR + (w * 4U), hdr_words[w]);
     flash_lock();
     ```
  5. **更新 metadata**：

     ```c
     g_meta.slot_a_valid = 1;
     g_meta.slot_a_crc32 = computed_crc;
     g_meta.ota_state    = OTA_STATE_IDLE;
     boot_metadata_save(&g_meta);
     ```
  6. 正响应：`[0x77]`（1 字节）。

#### 0x11 — ECUReset（ECU 复位）

- 请求：`[0x11, resetType]`
- 处理：先发送正响应 `[0x51]`，然后 `while(1) { /* wait for watchdog reset */ }`，
  **依靠 IWDG 在 ~1 s 后复位 MCU**，使 Bootloader 重新走引导流程启动新镜像。

#### 其他服务

- 一律回 NRC：`[0x7F, SID, 0x11]`（serviceNotSupported）。

### 3.5 Image 验证流程

实现于 `qi_wireless_bootloader/mdk_app/Src/boot_verify.c`，
`boot_verify_image(base_addr, slot_size)` 依次检查三项：

```
┌─────────────────────────────────────────────────────────┐
│ boot_verify_image(base_addr, slot_size)                 │
│                                                         │
│ ① magic:  header->magic == IMAGE_MAGIC (0x4F544158)?   │
│    否 → return -1                                       │
│ ② length: 0 < image_length <= slot_size - 256 ?         │
│    否 → return -1                                       │
│ ③ CRC32:  boot_crc32(base+256, image_length)            │
│           == header->crc32 ?                            │
│    否 → return -1                                       │
│ ④ (占位) ECDSA P-256 签名验证 —— 当前未实现（TODO）      │
│                                                         │
│ 全部通过 → return 0                                     │
└─────────────────────────────────────────────────────────┘
```

```c
int8_t boot_verify_image(uint32_t base_addr, uint32_t slot_size)
{
  const image_header_t *header = (const image_header_t *)base_addr;

  /* check 1: verify magic number */
  if (header->magic != IMAGE_MAGIC) return -1;

  /* check 2: verify image_length is within slot bounds */
  max_image_len = slot_size - IMAGE_HEADER_SIZE;
  if ((header->image_length == 0) || (header->image_length > max_image_len))
    return -1;

  /* check 3: verify CRC32 of image data (data follows the header) */
  image_data   = (const uint8_t *)(base_addr + IMAGE_HEADER_SIZE);
  computed_crc = boot_crc32((const void *)image_data, header->image_length);
  if (computed_crc != header->crc32) return -1;

  /* TODO: ECDSA P-256 signature verification (placeholder) */
  return 0;
}
```

### 3.6 Flash 写入流程（0x36 数据帧）

```c
/* 每帧最多 6 字节载荷，按 4 字节 word 编程 */
flash_unlock();
for (i = 0; i < data_len; i += 4U)
{
  uint32_t word_data = 0xFFFFFFFFU;      /* 初始化为全 1（擦除态） */
  uint8_t  remaining = data_len - i;
  uint8_t  copy_len  = (remaining > 4U) ? 4U : remaining;

  /* 不足 4 字节时，剩余位保持 0xFF 填充 */
  for (k = 0; k < copy_len; k++)
  {
    word_data &= ~((uint32_t)0xFFU << (k * 8U));
    word_data |= ((uint32_t)data[2U + i + k] << (k * 8U));
  }

  flash_status = flash_word_program(g_dl_write_addr + (uint32_t)i, word_data);
  if (flash_status != FLASH_OPERATE_DONE)
  {
    flash_lock();
    g_dl_active = 0;
    safe_mode_send_nrc(service_id, 0x72U);   /* generalProgrammingFailure */
    break;
  }
}
flash_lock();
```

要点：

- **4 字节对齐**：`g_dl_write_addr` 初值为 `0x08004000 + 0x100 = 0x08004100`
  （4 字节对齐）；`flash_word_program()` 要求 word 对齐；
- **边界检查**：写入总量不得超过 `MAX_IMAGE_SIZE (0xBF00)`，超限终止下载（NRC 0x14）；
- **部分 word 填充**：载荷不足 4 字节时高位补 `0xFF`（Flash 擦除态），
  前提是目标扇区已被 0x34 擦除；
- **失败处理**：任一 word 编程失败 → 立即锁 Flash、`g_dl_active = 0`、回 NRC 0x72。

### 3.7 跳转 APP 流程

实现于 `qi_wireless_bootloader/mdk_app/Src/boot_jump.c`：

```c
void boot_jump_to_app(uint32_t app_addr)   /* 由 try_boot_slot 传入 slot_addr+256 */
{
  /* step 1: disable all interrupts */
  __disable_irq();

  /* step 2: disable SysTick */
  SysTick->CTRL = 0;  SysTick->LOAD = 0;  SysTick->VAL = 0;

  /* step 3: clear all pending interrupts in NVIC */
  NVIC->ICER[0] = 0xFFFFFFFFU;  NVIC->ICER[1] = 0xFFFFFFFFU;
  NVIC->ICPR[0] = 0xFFFFFFFFU;  NVIC->ICPR[1] = 0xFFFFFFFFU;

  /* small delay to ensure all pending operations complete */
  for (delay = 0; delay < 1000; delay++) { __NOP(); }

  /* step 4: set vector table offset register to application base */
  SCB->VTOR = app_addr;

  /* step 5: read initial MSP from application vector table [0] */
  app_msp = *(volatile uint32_t *)(app_addr);

  /* step 6: read reset handler address from application vector table [1] */
  app_reset_addr = *(volatile uint32_t *)(app_addr + 4U);

  /* validate reset handler address (must be in valid flash range and thumb mode) */
  if ((app_reset_addr == 0xFFFFFFFFU) || (app_reset_addr == 0x00000000U))
    return;   /* invalid reset handler, cannot jump */

  /* set main stack pointer to application's initial MSP */
  __set_MSP(app_msp);

  /* cast to function pointer and jump (ensure thumb bit is set) */
  app_reset_handler = (app_reset_handler_t)(app_reset_addr | 1U);

  __enable_irq();
  app_reset_handler();   /* jump to application reset handler - does not return */
}
```

**跳转序列总结**：

```
关中断 → 关 SysTick → 清 NVIC 使能/挂起位 → 短延时 →
设置 VTOR = APP 基址 → 读向量表[0] 得 MSP → 读向量表[1] 得 Reset_Handler →
校验 Reset 地址合法 → __set_MSP → (Reset_Handler | 1) 跳转 → 开中断 → 进入 APP
```

> 注意：Bootloader 设置 `VTOR = slot_addr + 256` 后跳转；APP 自身在
> `main()` 开头会再次设置 `SCB->VTOR`（见 [4.1](#41-初始化流程)）。

---

## 4. APP 工作流（详细）

### 4.1 初始化流程

入口 `qi_wireless_code/mdk_user/Src/main.c`：

```
main()
  │
  ├─ SCB->VTOR = APP_BASE_ADDR (0x08004000)     ← 设置向量表偏移
  ├─ system_clock_config()                      ← 180 MHz
  ├─ nvic_priority_group_config(NVIC_PRIORITY_GROUP_4)
  ├─ timer_drv_init()                           ← 软件定时器 + SysTick 1ms
  ├─ wdg_drv_init()                             ← IWDG ~1s
  ├─ nvm_drv_init()                             ← NVM 配置驱动
  ├─ can_driver_init()                          ← CAN1 250kbps 扩展帧
  ├─ qi_uart_init()                             ← USART2 9600 8N1
  ├─ ota_confirm_if_needed()                    ← Trial 确认（须在 Flash 可用后）
  ├─ can_protocol_init()                        ← 注册 UDS 回调
  ├─ timer_create(100, broadcast_timer_callback, 1)  ← 100ms 周期广播定时器
  ├─ timer_start(...)
  ├─ send_broadcast()                           ← 上电即发 BOOTUP 广播
  │
  └─ while(1) 主循环
       ├─ timer_poll()        ← 软件定时器轮询
       ├─ can_driver_poll()   ← CAN 帧分发（触发 UDS 处理）
       ├─ qi_uart_poll()      ← Qi UART 数据轮询
       ├─ wdg_drv_refresh()   ← 喂狗
       ├─ 若 g_broadcast_flag → send_broadcast()   ← 100ms 周期广播
       └─ (TODO) 充电状态机 / Qi 芯片数据处理
```

```c
int main(void)
{
  /* set vector table to APP base address */
  SCB->VTOR = APP_BASE_ADDR;      /* 0x08004000U */

  /* configure system clock to 180MHz */
  system_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  /* initialize drivers */
  timer_drv_init();
  wdg_drv_init();
  nvm_drv_init();
  can_driver_init();
  qi_uart_init();

  /* confirm trial boot image if needed (must be after flash init) */
  ota_confirm_if_needed();

  /* initialize CAN protocol module (registers UDS handler) */
  can_protocol_init();

  /* create 100ms periodic broadcast timer */
  g_broadcast_timer_id = timer_create(100, broadcast_timer_callback, 1);
  timer_start(g_broadcast_timer_id);

  /* send BOOTUP broadcast */
  send_broadcast();

  /* main loop */
  while (1)
  {
    timer_poll();
    can_driver_poll();
    qi_uart_poll();
    wdg_drv_refresh();
    if (g_broadcast_flag) { g_broadcast_flag = 0; send_broadcast(); }
  }
}
```

### 4.2 生命周期广播（0x18FF260D）

`send_broadcast()`（`qi_wireless_code/mdk_user/Src/main.c`）：

```c
static void send_broadcast(void)
{
  uint8_t data[8];
  memset(data, 0, sizeof(data));

  /* byte 0: lifecycle = OPERATIONAL (0x03) */
  data[0] = 0x03U;
  /* byte 1-7: reserved, all zeros */

  can_driver_send(0x18FF260DU, data, 8);
}
```

- 上电初始化完成后立即发送一次（BOOTUP 广播）；
- 之后每 100 ms 由 `broadcast_timer_callback()` 置位 `g_broadcast_flag`，主循环发送；
- 帧格式：扩展帧 `0x18FF260D`，8 字节数据，byte0 = 生命周期状态
  （`0x03` = OPERATIONAL），其余字节保留为 0。

### 4.3 UDS 命令处理（APP 端）

实现于 `qi_wireless_code/mdk_app/Src/can_protocol.c`。CAN ID：

```c
#define CAN_PROTO_UDS_REQUEST       0x18DA0D03U  /*!< UDS request  (tester -> ECU) */
#define CAN_PROTO_UDS_RESPONSE      0x18DA030DU  /*!< UDS response (ECU -> tester) */
```

服务定义（`can_protocol.h`）：

```c
#define UDS_SID_DIAG_SESSION_CTRL   0x10U
#define UDS_SID_ECU_RESET           0x11U
#define UDS_SID_REQUEST_DOWNLOAD    0x34U
#define UDS_SID_TESTER_KEEPALIVE    0x3EU
#define UDS_NEGATIVE_RESPONSE       0x7FU
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U
#define UDS_NRC_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12U
```

入口 `can_protocol_rx_handler(uint32_t id, uint8_t *data, uint8_t len)`：

- 仅接受 `id == CAN_PROTO_UDS_REQUEST (0x18DA0D03)` 的帧，其余直接丢弃；
- `len == 0` 忽略；`service_id = data[0]` 分发。

| 服务 | 请求格式 | 处理 | 响应 |
|------|---------|------|------|
| `0x10` 诊断会话控制 | `[0x10, sub]` | 回显会话类型；无第二字节时默认 `0x01` | `[0x50, sub]` 或 `[0x50, 0x01]` |
| `0x11` ECU 复位 | `[0x11, sub]` | 先回正响应，**延时 ~2ms（36000 NOP @180MHz）确保响应发出**，然后 `ota_trigger_request()`（不复位不返回） | `[0x51, sub]` 或 `[0x51]` |
| `0x34` 请求下载 | `[0x34, ...]` | 回正响应，**延时 ~2ms**，然后 `ota_trigger_request()` | `[0x74, 0x20, 0x00, 0x00, 0x10]`（5 字节） |
| `0x3E` TesterPresent | `[0x3E, sub]` | 回显保活 | `[0x7E, sub]` 或 `[0x7E]` |
| 其他 | - | 不支持 | `[0x7F, SID, 0x11]` |

> APP 端 `0x34` 响应 `maxNumberOfBlockLength` 字段为 `0x0010`（示意值），
> 实际下载由 Bootloader Safe Mode 接管，主机应忽略该示意值、以
> Bootloader `0x34` 响应（`0xC000`）为准。

### 4.4 OTA 触发流程

实现于 `qi_wireless_code/mdk_app/Src/ota_trigger.c`：

```
主机发送 0x34 / 0x11
        │
        ▼
can_protocol_rx_handler() 发送正响应 + ~2ms 延时（保证 CAN 发送完成）
        │
        ▼
ota_trigger_request()
  ├─ ota_metadata_read(&meta)        读主区（回退备份区）
  ├─ meta.ota_state = OTA_STATE_DOWNLOADING
  ├─ ota_metadata_save(&meta)        写主区（擦 8KB + word 编程 + CRC）
  └─ NVIC_SystemReset()              ← 系统复位，进入 Bootloader
```

```c
void ota_trigger_request(void)
{
  ota_metadata_t meta;

  /* read current metadata */
  ota_metadata_read(&meta);

  /* set OTA state to downloading */
  meta.ota_state = OTA_STATE_DOWNLOADING;

  /* save metadata to primary flash */
  ota_metadata_save(&meta);

  /* perform system reset - bootloader will enter safe mode for download */
  NVIC_SystemReset();
}
```

`ota_metadata_save()` 细节（APP 侧）：

```c
/* 擦除主区 8KB = 4 × 2KB 扇区 */
flash_unlock();
for (i = 0; i < (OTA_META_PAGE_SIZE / OTA_FLASH_SECTOR_SIZE); i++)   /* 0x2000/0x800 = 4 */
{
  status = flash_sector_erase(OTA_META_PRIMARY_ADDR + (i * OTA_FLASH_SECTOR_SIZE));
  if (status != FLASH_OPERATE_DONE) { flash_lock(); return -1; }
}
/* 512 字节结构体按 word 编程 */
src   = (const uint32_t *)&meta_copy;      /* 先计算并填入 crc32 */
words = sizeof(ota_metadata_t) / sizeof(uint32_t);   /* 128 words */
for (i = 0; i < words; i++)
{
  status = flash_word_program(OTA_META_PRIMARY_ADDR + (i * 4U), src[i]);
  if (status != FLASH_OPERATE_DONE) { flash_lock(); return -1; }
}
flash_lock();
```

### 4.5 Trial Boot 确认流程

`ota_confirm_if_needed()`（`qi_wireless_code/mdk_user/Src/main.c`）：

```c
static void ota_confirm_if_needed(void)
{
  ota_metadata_t meta;

  /* read metadata from flash */
  if (ota_metadata_read(&meta) != 0)
    return;  /* invalid metadata, nothing to confirm */

  /* check if we are in active trial */
  if (meta.trial_state == TRIAL_STATE_ACTIVE)   /* 2U */
  {
    /* confirm the image */
    meta.trial_state = TRIAL_STATE_CONFIRMED;   /* 3U */

    /* save to primary flash */
    ota_metadata_save(&meta);
  }
}
```

- 在 CAN/UART 等驱动初始化完成后调用（确保 Flash 可用）；
- 若 Bootloader 以 Trial 方式启动了本镜像（`trial_state == ACTIVE`），
  则 APP 正常运行到此处即视为“试运行通过”，将状态改为 `CONFIRMED`；
- 下一次 Bootloader 启动时 `process_trial_state()` 将 `CONFIRMED` 收敛为 `IDLE`
  并清零重试计数。

### 4.6 Qi UART 驱动（USART2）

实现于 `qi_wireless_code/mdk_app/Src/qi_uart.c`，用于与 Qi 无线充电芯片通信。

**初始化**（`qi_uart_init()`）：

| 配置项 | 值 |
|--------|-----|
| 外设 | USART2（APB1） |
| TX 引脚 | PA2（AF MUX7，推挽） |
| RX 引脚 | PA3（AF MUX7，上拉输入） |
| 波特率 | 9600（`QI_UART_BAUDRATE`） |
| 帧格式 | 8 数据位 / 1 停止位 / 无校验（8N1） |
| 中断 | 接收数据寄存器非空 `USART_RDBF_INT`，NVIC 优先级 (3, 0) |
| RX 缓冲区 | 软件环形 FIFO，64 字节（`QI_UART_RX_BUF_SIZE`） |

**软件 FIFO**（ISR 写 / 主循环读）：

```c
static volatile uint8_t rx_buf[QI_UART_RX_BUF_SIZE];   /* 64 */
static volatile uint8_t rx_head = 0;   /*!< write index (ISR context) */
static volatile uint8_t rx_tail = 0;   /*!< read index  (main context) */
static volatile uint8_t rx_count = 0;  /*!< number of bytes in buffer */
```

**接收中断**（`qi_uart_rx_irq_handler()`，由 `USART2_IRQHandler` 调用）：

- `USART_RDBF_FLAG` 置位 → 读 `usart_data_receive(USART2)`；
- FIFO 未满则存入 `rx_buf[rx_head]`，`rx_head` 环形推进、`rx_count++`；
- 溢出标志 `USART_RORE_FLAG` 置位则清除。

**发送**（阻塞式）：

```c
void qi_uart_send(const uint8_t *data, uint8_t len)
{
  for (i = 0; i < len; i++)
  {
    while (usart_flag_get(USART2, USART_TDBE_FLAG) == RESET) { }  /* 等发送寄存器空 */
    usart_data_transmit(USART2, data[i]);
  }
  while (usart_flag_get(USART2, USART_TDC_FLAG) == RESET) { }      /* 等发送完成 */
}
```

**主循环轮询**（`qi_uart_poll()`）：

- 若 `rx_count > 0` 且已注册回调，则每次调用从 FIFO 读取 **1 字节** 并派发给回调
  （`qi_uart_register_rx_callback()` 注册），保证延迟有界；
- 当前源码中 **Qi 协议解析层尚未实现**（文件末尾 TODO：命令/响应协议、
  充电状态、功率控制、FOD 异物检测、超时重试等）。

---

## 5. OTA 升级完整流程（端到端）

### 5.1 时序总览

```
 主机/Tester                     APP                    Bootloader
 (CAN 0x18DA0D03)            (0x08004000+)            (0x08000000)
     │                            │                        │
     │ ① 0x34 RequestDownload     │                        │
     ├───────────────────────────▶│                        │
     │                            │ 写 metadata            │
     │                            │ (ota_state=DOWNLOADING)│
     │                            │ NVIC_SystemReset()     │
     │ ② 0x74 正响应               │                        │
     │◀───────────────────────────┤                        │
     │                            │─────── 系统复位 ───────▶│
     │                            │                        │ ③ 检测 ota_state
     │                            │                        │   ==DOWNLOADING
     │                            │                        │   → 进入 Safe Mode
     │ ④ 0x10/0x27 (可选)          │                        │
     ├────────────────────────────────────────────────────▶│ ⑤ 0x50/0x67 响应
     │◀────────────────────────────────────────────────────┤
     │ ⑥ 0x34 RequestDownload     │                        │
     ├────────────────────────────────────────────────────▶│ ⑦ 擦除 APP_A 24 扇区
     │◀────────────────────────────────────────────────────┤ ⑧ 0x74,0x20,0xC0,0x00
     │ ⑨ 0x36 TransferData(seq=1)│                        │
     ├────────────────────────────────────────────────────▶│ ⑩ word 编程
     │◀────────────────────────────────────────────────────┤ 0x76,1
     │ ⑪ 0x36 ... (seq=2..N)      │                        │
     ├────────────────────────────────────────────────────▶│ ...
     │◀────────────────────────────────────────────────────┤
     │ ⑫ 0x37 RequestTransferExit │                        │ ⑬ 计算 CRC32、写 Header
     ├────────────────────────────────────────────────────▶│  更新 metadata
     │◀────────────────────────────────────────────────────┤ ⑭ 0x77
     │ ⑮ 0x11 ECUReset            │                        │
     ├────────────────────────────────────────────────────▶│ ⑯ 0x51，等待 IWDG
     │                            │                        │
     │                            │──── 看门狗复位(~1s) ───▶│ ⑰ 重新引导：校验 APP_A
     │                            │                        │  → 通过 → 跳转
     │                            │◀──── 跳转至 APP_A+256 ─┤
     │                            │ ⑱ 上电广播 0x18FF260D  │
     │◀───────────────────────────┤                        │
     │                            │ ⑲ 若 Trial ACTIVE      │
     │                            │   → CONFIRMED 并保存   │
```

### 5.2 分阶段说明

**阶段 1 — APP 触发**（主机 → APP）：
1. 主机向 `0x18DA0D03` 发送 `0x34`（或 `0x11`）；
2. APP 回正响应并延时 ~2 ms 保证 CAN 发送完成；
3. APP 写 metadata（`ota_state = 0x01`）到 `0x0801C000`，随后 `NVIC_SystemReset()`。

**阶段 2 — Bootloader 进入 Safe Mode**：
4. 复位后 Bootloader 加载 metadata，发现 `ota_state == OTA_STATE_DOWNLOADING`；
5. 清除该状态并保存，进入 `enter_safe_mode()`（CAN 初始化 + 事件循环）。

**阶段 3 — UDS 下载**（主机 → Bootloader）：
6. （可选）`0x10` 会话控制 / `0x27` 安全访问（Bootloader 简化接受）；
7. `0x34` → Bootloader 擦除 APP_A 全部 24 个 2KB 扇区，初始化下载状态，
   回复 `[0x74, 0x20, 0xC0, 0x00]`；
8. `0x36` 数据帧从 **blockSeq=1** 起顺序发送（建议每帧 4~6 字节载荷，
   见 3.4/3.6 的序号与对齐约束）；每帧正响应 `[0x76, seq]`；
9. 传输约 `0xBF00` 字节后，主机发 `0x37`；
10. Bootloader 计算 CRC32、写 256 字节 Image Header、更新 metadata
    （`slot_a_valid=1`、`slot_a_crc32`、`ota_state=IDLE`），回复 `[0x77]`。

**阶段 4 — 复位与启动新镜像**：
11. 主机发 `0x11` → Bootloader 回 `[0x51]` 后空转等待 IWDG（~1 s）复位；
12. 复位后 Bootloader 重新引导：`select_boot_slot` → `try_boot_slot(active_slot)`
    → `boot_verify_image` 通过 → 跳转 `0x08004100`；
13. APP 启动，发 BOOTUP 广播，进入 100 ms 周期广播；若为试运行则确认。

### 5.3 错误处理与回滚策略

| 错误场景 | 检测点 | 处理 |
|---------|--------|------|
| 下载帧序号错乱 | 0x36 中 `block_seq != ++g_dl_block_seq` | 终止下载（`g_dl_active=0`），回 NRC `0x71`，需重新 0x34 开始 |
| 数据超限 | `bytes_written + data_len > MAX_IMAGE_SIZE` | 终止下载，回 NRC `0x14` |
| 帧长非法 | `len < 3` | 回 NRC `0x13` |
| Flash 擦除/编程失败 | `flash_sector_erase` / `flash_word_program` | 回 NRC `0x72`；下载状态复位 |
| 未开始下载就发 0x36/0x37 | `g_dl_active == 0` | 回 NRC `0x71` |
| 镜像校验失败（magic/length/CRC） | `boot_verify_image` | 尝试另一槽位；双槽失败 → Safe Mode |
| 新镜像启动即崩溃/超时 | IWDG 复位 → `detect_boot_reason()` = WDG | Trial retry 计数，超过 `trial_max_retries (3)` → 回滚到另一槽位 |
| metadata 损坏 | `meta_validate`（magic/version/CRC） | 主区→备份区→默认值三级回退 |
| 0x34 擦除中断/掉电 | 下次上电镜像校验失败 | 走槽位回退逻辑，不会启动半成品镜像 |

**回滚原则**：
- **双槽位 + 校验**：只有通过 magic + length + CRC32 的镜像才会被跳转；
- **Trial Boot**：新固件须在重试上限内被 APP 确认（`CONFIRMED`），否则切换
  `active_slot` 到另一有效槽位；
- **Safe Mode 兜底**：任何情况下双槽无效都能进入 Safe Mode 重新刷写，固件
  “刷不死”。

---

## 6. CAN 通信协议

### 6.1 CAN ID 分配

定义于 `qi_wireless_bootloader/mdk_can/Inc/can_driver.h`：

| CAN ID | 方向 | 用途 |
|--------|------|------|
| `0x18DA0D03` | CCU/Tester → Qi | UDS 请求（`CAN_ID_UDS_REQUEST` / `SAFE_MODE_CAN_ID_REQUEST` / `CAN_PROTO_UDS_REQUEST`） |
| `0x18DA030D` | Qi → CCU/Tester | UDS 响应（`CAN_ID_UDS_RESPONSE` / `SAFE_MODE_CAN_ID_RESPONSE` / `CAN_PROTO_UDS_RESPONSE`） |
| `0x18DB33D0` | 广播 | UDS 功能寻址广播（`CAN_ID_FUNCTIONAL_REQUEST`，预留） |
| `0x18FF260D` | Qi → CCU | 生命周期广播（100 ms，`CAN_ID_LIFECYCLE_BROADCAST`） |
| `0x18FF270D` | CCU → Qi | CCU 控制命令（`CAN_ID_CCU_CONTROL`，预留） |

ID 格式遵循 SAE J1939 风格（PGN + 源地址/目标地址）：

```
0x18 DA 0D 03  →  0x18 = 扩展帧(优先级6) | PGN 0xDA00 | 目标 0x0D | 源 0x03
0x18 DA 03 0D  →  0x18 = 扩展帧 | PGN 0xDA00 | 目标 0x03 | 源 0x0D
```

### 6.2 UDS 帧格式

当前实现为 **单帧 UDS**（ISO-TP 多帧尚未实现），每帧 8 字节 CAN 数据：

```
请求:  [ SID | SubFunc/参数 | 数据... ]        (0~8 字节)
响应:  [ SID+0x40 | 参数... ]                  正响应
NRC:   [ 0x7F | SID | NRC码 ]                  负响应
```

负响应码（NRC）汇总：

| NRC | 含义 | 使用方 |
|-----|------|--------|
| `0x11` | serviceNotSupported | Bootloader / APP |
| `0x12` | subFunctionNotSupported | APP（已定义，未使用） |
| `0x13` | incorrectMessageLengthOrInvalidFormat | Bootloader 0x36 |
| `0x14` | responseTooLong（数据超限） | Bootloader 0x36 |
| `0x71` | transferDataAborted | Bootloader 0x36/0x37 |
| `0x72` | generalProgrammingFailure | Bootloader 0x34/0x36 |

### 6.3 各命令/响应格式

**0x10 DiagnosticSessionControl**

```
请求  [0x10, sessionType]
正响应 [0x50, sessionType]
```

**0x27 SecurityAccess（Bootloader 简化）**

```
请求  [0x27, subFunction, ...]
正响应 [0x67, subFunction]
```

**0x34 RequestDownload（Bootloader）**

```
请求  [0x34, ...]
正响应 [0x74, 0x20, 0xC0, 0x00]      ← lengthFormatIdentifier=0x20, maxBlockLen=0xC000
NRC   [0x7F, 0x34, 0x72]            ← 擦除失败
```

**0x36 TransferData（Bootloader）**

```
请求  [0x36, blockSeq, data0..data5]   ← 载荷 ≤ 6 字节, blockSeq 从 1 顺序递增
正响应 [0x76, blockSeq]
NRC   [0x7F, 0x36, 0x71|0x13|0x14|0x72]
```

**0x37 RequestTransferExit（Bootloader）**

```
请求  [0x37]
正响应 [0x77]
NRC   [0x7F, 0x37, 0x71]
```

**0x11 ECUReset**

```
请求  [0x11, resetType]
正响应 [0x51, resetType]   (APP: 随后触发 OTA；Bootloader: 随后等待 IWDG 复位)
```

**0x3E TesterPresent（APP）**

```
请求  [0x3E, subFunction]  或 [0x3E]
正响应 [0x7E, subFunction] 或 [0x7E]
```

**0x18FF260D 生命周期广播（APP，100ms）**

```
数据: [0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
      byte0 = 0x03 (OPERATIONAL)，byte1-7 保留
```

---

## 7. 中断处理

中断处理集中于 `qi_wireless_code/mdk_user/Src/at32f422_426_int.c`
（Bootloader 与 APP 共用同一套 SPL 中断文件结构）。

| 中断 | 优先级 (抢占,子) | 处理函数 | 说明 |
|------|-----------------|---------|------|
| `SysTick_Handler` | 内核 | `timer_tick_inc()` | 1 ms 时基累加（软件定时器驱动） |
| `CAN1_RX_IRQHandler` | (1, 0) | `can_driver_rx_irq_handler()` | 读硬件 RX 缓冲 → 软件 FIFO |
| `CAN1_ERR_IRQHandler` | (2, 0) | `can_driver_err_irq_handler()` | 总线错误/离线恢复 |
| `USART2_IRQHandler` | (3, 0) | `qi_uart_rx_irq_handler()` | Qi UART 接收字节入 FIFO |
| `NMI_Handler` / `SVC_Handler` / `DebugMon_Handler` / `PendSV_Handler` | - | 空实现 | - |
| `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler` | - | `while(1)` | 异常死循环（可配合调试器/看门狗恢复） |

### 7.1 CAN1_RX_IRQHandler

```c
void can_driver_rx_irq_handler(void)
{
  if (can_flag_get(CAN1, CAN_RIF_FLAG) != RESET)
  {
    if (can_rxbuf_read(CAN1, &rx_buf) == SUCCESS)
    {
      if (!rx_fifo_is_full())          /* 软件 FIFO 深度 16 */
      {
        rx_fifo[rx_fifo_head].id  = rx_buf.id;
        rx_fifo[rx_fifo_head].len = (uint8_t)(rx_buf.data_length & 0x0F);
        /* 拷贝数据 */
        rx_fifo_head = (rx_fifo_head + 1) % CAN_DRIVER_RX_FIFO_SIZE;
        rx_fifo_count++;
      }
    }
    can_rxbuf_release(CAN1);           /* 释放硬件接收缓冲 */
    can_flag_clear(CAN1, CAN_RIF_FLAG);
  }
  /* RX 溢出标志处理 */
  if (can_flag_get(CAN1, CAN_ROIF_FLAG) != RESET)
    can_flag_clear(CAN1, CAN_ROIF_FLAG);
}
```

- ISR 只负责“收帧入软件 FIFO”，**协议解析推迟到主循环**
  `can_driver_poll()`（避免 ISR 内做耗时处理）；
- FIFO 满时丢弃新帧（不覆盖旧帧）。

### 7.2 CAN1_ERR_IRQHandler

```c
void can_driver_err_irq_handler(void)
{
  if (can_flag_get(CAN1, CAN_EIF_FLAG) != RESET)
  {
    if (can_busoff_get(CAN1) != RESET)
      can_busoff_reset(CAN1);          /* 总线关闭 → 请求恢复 */
    can_flag_clear(CAN1, CAN_EIF_FLAG);
  }
  /* 分别清除总线错误、仲裁丢失、错误被动标志 */
  if (can_flag_get(CAN1, CAN_BEIF_FLAG) != RESET) can_flag_clear(CAN1, CAN_BEIF_FLAG);
  if (can_flag_get(CAN1, CAN_ALIF_FLAG) != RESET) can_flag_clear(CAN1, CAN_ALIF_FLAG);
  if (can_flag_get(CAN1, CAN_EPIF_FLAG) != RESET) can_flag_clear(CAN1, CAN_EPIF_FLAG);
}
```

### 7.3 USART2_IRQHandler

```c
void qi_uart_rx_irq_handler(void)
{
  if (usart_flag_get(USART2, USART_RDBF_FLAG) != RESET)
  {
    received_byte = (uint8_t)usart_data_receive(USART2);
    if (rx_count < QI_UART_RX_BUF_SIZE)      /* 64 字节环形缓冲 */
    {
      rx_buf[rx_head] = received_byte;
      rx_head = (rx_head + 1) % QI_UART_RX_BUF_SIZE;
      rx_count++;
    }
    if (usart_flag_get(USART2, USART_RORE_FLAG) != RESET)   /* 溢出标志清除 */
      usart_flag_clear(USART2, USART_RORE_FLAG);
  }
}
```

### 7.4 SysTick_Handler

```c
void SysTick_Handler(void)
{
  timer_tick_inc();    /* 1ms 系统 tick，驱动软件定时器 */
}
```

**中断优先级小结**（NVIC_PRIORITY_GROUP_4，即 4 位抢占）：

```
CAN1_RX  (1,0)  >  CAN1_ERR (2,0)  >  USART2 (3,0)
```

---

## 8. 关键常量和配置

### 8.1 Flash 地址表（boot_metadata.h / ota_trigger.h）

```c
#define BOOT_BASE_ADDR          0x08000000U   /*!< bootloader start address */
#define BOOT_SIZE               0x4000U       /*!< 16KB */
#define APP_A_BASE_ADDR         0x08004000U   /*!< 48KB */
#define APP_A_SIZE              0xC000U
#define APP_B_BASE_ADDR         0x08010000U   /*!< 48KB */
#define APP_B_SIZE              0xC000U
#define META_PRIMARY_ADDR       0x0801C000U   /*!< primary metadata */
#define META_BACKUP_ADDR        0x0801E000U   /*!< backup metadata / NVM 区 */
#define META_PAGE_SIZE          0x2000U       /*!< 8KB */
#define IMAGE_HEADER_SIZE       256U
#define OTA_FLASH_SECTOR_SIZE   0x800U        /*!< 2KB per sector (APP 侧) */
```

### 8.2 CAN 波特率配置（can_driver.c）

```c
/* APB1 = 180 MHz（clock 配置 apb1div=1） */
#define CAN_BITTIME_DIV                 10U   /* CAN 时钟 = 180/10 = 18 MHz */
#define CAN_BITTIME_SJW                 1U
#define CAN_BITTIME_BTS1                54U
#define CAN_BITTIME_BTS2                17U
/* bit_time = 1 + 54 + 17 = 72 Tq → bitrate = 18 MHz / 72 = 250 kbps */
```

| 参数 | 值 |
|------|-----|
| 帧格式 | CAN 2.0B 扩展帧（29-bit ID）+ 数据帧 |
| 位率 | 250 kbps |
| 采样点 | (1+54)/72 ≈ 76.4% |
| 引脚 | PA11 = CAN_RX（MUX4，上拉），PA12 = CAN_TX（MUX4，推挽） |
| 过滤器 | Filter0：mask=0，接受所有扩展数据帧 |
| 中断 | `CAN_RIE_INT`（RX）+ `CAN_EIE_INT`（错误） |
| 发送策略 | 优先 PTB（主发送缓冲），满则 STB（次发送缓冲），双满返回 -1 |
| 软件 RX FIFO | 16 帧（`CAN_DRIVER_RX_FIFO_SIZE`） |

### 8.3 看门狗配置（wdg_drv.h）

```c
/* IWDG uses LSI clock (~40kHz). Once enabled, it cannot be disabled.
   LSI = 40kHz, DIV_128 -> 312.5 Hz -> 3.2ms/tick
   reload = 312 -> timeout ~ 998ms (~1000ms) */
void wdg_drv_init(void);
void wdg_drv_refresh(void);   /* 需在超时窗口内周期性调用 */
```

| 参数 | 值 |
|------|-----|
| 时钟源 | LSI（约 40 kHz） |
| 分频 | 128 → 312.5 Hz（3.2 ms/tick） |
| 重装值 | 312 |
| 超时 | ≈ 998 ms（约 1 s） |
| 特性 | 一旦使能不可关闭；复位后 `CRM_WDT_RESET_FLAG` 可检测 |

喂狗点：Bootloader Safe Mode 循环、Bootloader 兜底循环、APP 主循环。

### 8.4 定时器配置（timer_drv.h / timer_drv.c）

| 参数 | 值 |
|------|-----|
| 时基 | SysTick 1 ms 中断（`SysTick_Handler` → `timer_tick_inc()`） |
| 软件定时器数量 | 最多 16 个（`TIMER_DRV_MAX_TIMERS`） |
| 无效 ID | `0xFF`（`TIMER_INVALID_ID`） |
| API | `timer_create(period_ms, cb, auto_reload)` / `timer_start` / `timer_stop` / `timer_reset` / `timer_is_running` / `timer_get_tick` / `timer_poll` |

**工程内使用的定时器**：

| 定时器 | 周期 | 自动重载 | 用途 |
|--------|------|---------|------|
| Bootloader Trial 定时器 | 1000 ms（`TRIAL_TIMER_PERIOD_MS`） | 是 | `trial_timer_callback()`：试运行计时 |
| APP 广播定时器 | 100 ms | 是 | `broadcast_timer_callback()`：置位广播标志 |

### 8.5 NVIC 优先级配置

```c
nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);   /* 4 位抢占，0 位子优先级 */

nvic_irq_enable(CAN1_RX_IRQn,  1, 0);
nvic_irq_enable(CAN1_ERR_IRQn, 2, 0);
nvic_irq_enable(USART2_IRQn,   3, 0);
```

### 8.6 链接脚本（scatter）要点

- Bootloader：`LR_IROM1 0x08000000, 0x3FFF`；`RW_IRAM1 0x20000000, 0x5000`；
- APP：当前工程 `uvprojx` 中 IROM 配置为 `0x08000000, 0x20000`（整片 Flash），
  **与 OTA 槽位布局（APP 应链接于 0x08004000 + 0x100 之后）不一致**，交付时需按
  [第 9 节](#9-实现现状与注意事项) 调整链接地址。

---

## 9. 实现现状与注意事项

以下为基于当前仓库源码的事实性观察，供后续开发/评审参考：

1. **Trial Boot 触发链路未完全接通**：源码中没有任何路径主动将
   `trial_state` 置为 `TRIAL_STATE_PENDING`。状态机的 PENDING→ACTIVE→CONFIRMED/
   ROLLBACK 框架、APP 侧确认（`ota_confirm_if_needed`）均已实现，但“下载完成后
   置 PENDING 再复位”的触发步骤在 Safe Mode 的 `0x37` 处理中未写入
   （`0x37` 直接置 `slot_a_valid=1` 并保持 `ota_state=IDLE`）。

2. **Trial 超时计数未消费**：`trial_timer_callback()` 每秒递增
   `g_trial_elapsed_sec` 并置 `g_trial_timer_flag`，但当前可见代码中没有逻辑
   依据 `trial_timeout_sec (10)` 执行超时回滚；实际回滚依赖
   “看门狗复位 + `trial_retry_count > trial_max_retries (3)`” 路径
   （而 retry_count 仅在 PENDING→ACTIVE 时递增，若 PENDING 从未被置位，该路径
   同样不会触发）。**若需启用 Trial 回滚，需补充置 PENDING 及超时/重试消费逻辑。**

3. **APP 链接地址与 VTOR 需核对**：Bootloader 跳转目标为
   `slot_addr + IMAGE_HEADER_SIZE`（槽 A 为 `0x08004100`），并在跳转前设置
   `VTOR = 0x08004100`；而 APP `main()` 开头将 `SCB->VTOR` 设为
   `APP_BASE_ADDR (0x08004000)`。两者相差 256 字节，**必须保证 APP 工程实际
   链接地址与 Image Header 之后的偏移一致**（即向量表位于 `0x08004000+0x100`），
   否则中断向量取址错误。当前 `uvprojx` 的 IROM 仍为 `0x08000000`（整片），
   需按槽位布局修改 scatter/链接配置。

4. **单帧 UDS 限制**：Bootloader 仅支持单帧 UDS（每帧 8 字节，0x36 载荷
   ≤ 6 字节）。48 KB 固件约需 ~8192 帧；代码注释已说明后续可扩展
   ISO-TP 多帧提升吞吐。

5. **0x36 载荷与对齐建议**：`g_dl_write_addr` 按 `data_len`（帧载荷字节数）
   累加，而 Flash 按 4 字节 word 编程。若主机每帧载荷不是 4 的倍数，会造成
   后续帧写入地址非 4 字节对齐，`flash_word_program` 行为异常。**建议主机每帧
   固定发送 4 字节载荷**（8 字节 CAN 帧中 `[SID, seq, d0, d1, d2, d3]`），
   或将代码改为按 4 字节对齐地址推进。

6. **0x37 未写回 active_slot**：下载完成后仅置 `slot_a_valid=1`，未把
   `active_slot` 切换为 SLOT_A（也未写 pending/trial 字段）。若下载前
   `active_slot` 为 B，则复位后会继续启动 B 槽旧固件。当前单槽下载场景
   无影响，双槽切换场景需补充。

7. **ECDSA 签名验证为占位**：`image_header_t.signature[64]` 与
   `boot_verify_image()` 中的签名校验均未实现（TODO），当前仅
   magic + length + CRC32 防护。

8. **Qi 协议解析层未实现**：`qi_uart.c` 已完成 USART2 收发与 FIFO，但
   Qi 芯片命令/响应协议、充电状态机、FOD 等均为 TODO；当前广播字节 1-7 全 0。

9. **备份区写入约定**：`boot_metadata_save()` / `ota_metadata_save()` 均只写
   主区 `0x0801C000`，以保护 `0x0801E000` 处的 NVM 配置；备份区仅作为读取回退。
   任何新增写入该区域的代码需遵循此约定。

10. **0x11 复位依赖 IWDG**：Bootloader 的 ECUReset 通过空转等待看门狗复位
    （~1 s），期间不喂狗属预期行为；主机应在收到 `[0x51]` 后等待至少 1 s 再开始
    下一阶段操作。

---

*本文档由代码静态分析生成，建议在修改任一工程源码后同步更新对应章节。*
