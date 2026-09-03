# Qi 无线充电模块 — CAN-UDS OTA 固件升级系统

> 基于 AT32F426 的车载 Qi 无线充电模块，通过 CAN 总线实现 UDS 诊断与双槽 OTA 空中固件升级。

---

## 目录

- [1. 项目概述](#1-项目概述)
- [2. 系统拓扑](#2-系统拓扑)
- [3. 核心特性](#3-核心特性)
- [4. 仓库目录结构](#4-仓库目录结构)
- [5. Flash 空间布局](#5-flash-空间布局)
- [6. 协议栈架构](#6-协议栈架构)
- [7. OTA 升级流程](#7-ota-升级流程)
- [8. Bootloader 启动流程](#8-bootloader-启动流程)
- [9. Python 工具集](#9-python-工具集)
- [10. 构建与烧录](#10-构建与烧录)
- [11. 文档索引](#11-文档索引)

---

## 1. 项目概述

### 1.1 项目目标

为车载 Qi 无线充电模块开发完整的 CAN-UDS OTA 固件升级方案，满足整车级 ECU 远程刷写需求。

### 1.2 硬件平台

| 项目 | 规格 |
|------|------|
| 主控 MCU | AT32F426KBU7-4 (Cortex-M4F, QFN32) |
| 主频 | 180 MHz (HEXT 8MHz + PLL) |
| Flash / SRAM | 128 KB / 20 KB |
| CAN 收发器 | SIT1145 (SPI 配置 Normal Mode) |
| CAN 总线 | CAN 2.0B, 29-bit 扩展帧, 250 kbps |
| 无线充接口 | USART2 (PA2/PA3, 9600 8N1) ↔ Qi 芯片 |
| 霍尔传感器 | PA0 — 磁场检测 (有磁=低/无磁=高) |
| 供电控制 | PB1 (12V Buck, 低有效), PB2 (5V Qi, 高有效) |
| 调试接口 | SWD (PA13/PA14), USART1 预留 (PB6/PB7) |

### 1.3 工程组成

项目包含 **两个独立 Keil 工程** + **一套 Python 工具链**：

| 工程 | 目录 | 职责 |
|------|------|------|
| Bootloader | `qi_wireless_bootloader/` | 上电引导、镜像验签、双槽选择、Safe Mode UDS 下载、Trial Boot 管理 |
| APP Slot A | `qi_wireless_code_slotA/` | Qi 充电业务、CAN 生命周期广播、OTA 触发 (进入 Boot) |
| APP Slot B | `qi_wireless_code_slotB/` | 同 Slot A，IROM 基址不同 (0x08011900)，用于 A/B 交替升级 |
| 工具集 | `python_tools/` | 镜像打包、签名、合并、验证、一键 OTA 脚本 |

---

## 2. 系统拓扑

### 2.1 整车网络拓扑

```
┌─────────────────────────────────────────────────────────────────────┐
│                        整车 CAN 总线 (250 kbps)                     │
│                     29-bit 扩展帧, 120Ω 终端                        │
│                                                                     │
│  ┌──────────┐          ┌──────────┐          ┌──────────────┐      │
│  │   CCU    │  CAN Bus │  Qi 模块  │  USART2  │  Qi 充电芯片  │      │
│  │ (主机)   │◄────────►│ (本项目)  │◄────────►│  (无线充)    │      │
│  │ 0x03     │          │ 0x0D     │  9600bps  │              │      │
│  └────┬─────┘          └────┬─────┘          └──────────────┘      │
│       │                     │                                       │
│       │ UDS 诊断            │ 霍尔 PA0                              │
│       │ ISO-TP              │ (磁场检测)                            │
│       │                     │                                       │
│  ┌────┴─────┐          ┌────┴─────┐                                 │
│  │ PC 工具   │          │ SIT1145  │                                 │
│  │ ZCANPRO  │          │ CAN 收发器│                                 │
│  │ CAN 分析仪│          │ SPI 配置  │                                 │
│  └──────────┘          └──────────┘                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 CAN 寻址

| 方向 | CAN ID | 说明 |
|------|--------|------|
| 请求 (CCU → Qi) | `0x18DA0D03` | 物理寻址, 目标=0x0D, 源=0x03 |
| 响应 (Qi → CCU) | `0x18DA030D` | 物理寻址, 目标=0x03, 源=0x0D |

### 2.3 MCU 引脚分配

```
                        AT32F426KBU7-4 (QFN32)
                       ┌─────────────────────┐
              VDD 3.3V │1                32│ PB8 (NC, 输出低)
          HEXT_IN 8MHz │2                31│ BOOT0
         HEXT_OUT 8MHz │3                30│ PB7 (Debug RX)
               M_NRST  │4                29│ PB6 (Debug TX)
              VDDA 3.3V│5                28│ PB5 (NC, 输出低)
        PA0 (霍尔检测) │6                27│ PB4 (NC, 输出低)
          PA1 (NC,低)  │7                26│ PB3 (SIT1145, 输出低)
     PA2 (USART2_TX→Qi)│8                25│ PA15 (NC, 输出低)
     PA3 (USART2_RX←Qi)│9                24│ PA14 (SWCLK)
      PA4 (SPI1_CS)    │10               23│ PA13 (SWDIO)
      PA5 (SPI1_SCK)   │11               22│ PA12 (CAN_TX)
      PA6 (SPI1_MISO)  │12               21│ PA11 (CAN_RX)
      PA7 (SPI1_MOSI)  │13               20│ PA10 (NC, 输出低)
          PB0 (NC,低)  │14               19│ PA9  (NC, 输出低)
  PB1 (12V_Buck,低有效)│15               18│ PA8  (NC, 输出低)
  PB2 (5V_Qi,高有效)   │16               17│ PB10 (NC, 输出低)
                       └─────────────────────┘
```

### 2.4 系统模块关系

```
┌─────────────────────────────────────────────────────────────┐
│                    Qi 无线充电模块                            │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              Bootloader (28KB @ 0x08000000)            │  │
│  │                                                       │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐  │  │
│  │  │boot_jump │ │boot_safe │ │boot_trial│ │boot_meta│  │  │
│  │  │  跳转控制 │ │  Safe    │ │  Trial   │ │ Metadata│  │  │
│  │  │          │ │  Mode    │ │  Boot    │ │ 管理    │  │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────────┘  │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐  │  │
│  │  │boot_verify│ │  isotp   │ │sit1145   │ │can_drv  │  │  │
│  │  │ CRC/ECDSA│ │ ISO-TP   │ │CAN 收发器│ │CAN 驱动 │  │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────────┘  │  │
│  │  ┌──────────┐ ┌──────────┐                           │  │
│  │  │  sha256  │ │  uECC    │                           │  │
│  │  │  哈希    │ │ ECDSA签名│                           │  │
│  │  └──────────┘ └──────────┘                           │  │
│  └───────────────────────────────────────────────────────┘  │
│                           │ 跳转                             │
│  ┌────────────────────────▼──────────────────────────────┐  │
│  │           Application (42KB/Slot @ +0x100)             │  │
│  │                                                       │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐  │  │
│  │  │can_proto │ │ qi_uart  │ │board_gpio│ │lifecycle│  │  │
│  │  │CAN 协议栈│ │Qi 串口   │ │  GPIO    │ │生命周期 │  │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────────┘  │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐  │  │
│  │  │ota_trigger│ │device_inf│ │ nvm_drv  │ │  isotp  │  │  │
│  │  │OTA 触发  │ │设备信息  │ │NVM 驱动  │ │ ISO-TP  │  │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                共享 Flash 区域                          │  │
│  │  Metadata (主+备) │ Device Info (SN) │ NVM Config      │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 核心特性

### 3.1 双槽 A/B Ping-Pong 升级

```
出厂状态:                    第一次 OTA:                 第二次 OTA:
┌──────────┐               ┌──────────┐               ┌──────────┐
│ Slot A   │ ← 活跃        │ Slot A   │ ← 旧版        │ Slot A   │ ← 新版(活跃)
│ (v1.0)   │               │ (v1.0)   │               │ (v1.1)   │
├──────────┤               ├──────────┤               ├──────────┤
│ Slot B   │ ← 空          │ Slot B   │ ← 新版(活跃)  │ Slot B   │ ← 旧版
│ (空)     │               │ (v1.1)   │               │ (v1.0)   │
└──────────┘               └──────────┘               └──────────┘
```

- 只擦写 **非活跃槽**，活跃槽始终保留可回退
- 掉电安全：先写 Metadata 备份，再写主副本

### 3.2 Trial Boot 试运行机制

```
OTA 下载完成 → 复位 → Bootloader 选择新槽
                              │
                    ┌─────────▼──────────┐
                    │  Trial Boot (10s)   │
                    │  新槽 APP 运行      │
                    │                     │
                    │  APP 健康检查通过?   │
                    │  ├─ 是 → 确认       │
                    │  │       active=新槽 │
                    │  │       (永久生效)  │
                    │  └─ 否 → 超时复位   │
                    │         回滚旧槽    │
                    └─────────────────────┘
```

### 3.3 安全机制

| 安全层 | 实现 |
|--------|------|
| 镜像签名 | ECDSA P-256, 64字节 R‖S (IEEE P1363) |
| 完整性校验 | CRC32 (IEEE 802.3) + SHA-256 哈希 |
| 安全访问 | UDS 0x27 SecurityAccess, Seed-Key + ECDSA |
| 防回滚 | 版本号校验 (XATO 头 version 字段) |
| 掉电保护 | Metadata 双副本 (先写备后写主) |
| Device Info 保护 | OTA 擦写跳过 0x0801D000~0x0801DFFF, SN 永久保留 |

### 3.4 Safe Mode 救砖

当双槽镜像均无效或 metadata 标记为 `DOWNLOADING` 时，Bootloader 自动进入 Safe Mode：
- 不跳转 APP，停留在 Bootloader
- 开放完整 UDS 下载路径 (0x34/0x36/0x37)
- 支持从空片状态恢复

---

## 4. 仓库目录结构

```
ota-upgrade-of-qi-charger-based-on-can/
│
├── README.md                           ← 本文件
│
├── qi_wireless_bootloader/             ← Bootloader 工程
│   ├── mdk_project/                    ← Keil 工程文件 (.uvprojx)
│   ├── mdk_user/
│   │   ├── Inc/                        ← 时钟、中断配置头文件
│   │   └── Src/
│   │       └── main.c                  ← 入口: bootloader_main()
│   ├── mdk_can/
│   │   ├── Inc/can_driver.h
│   │   └── Src/can_driver.c            ← CAN 底层驱动
│   ├── mdk_app/
│   │   ├── Inc/
│   │   │   ├── boot_jump.h             ← 跳转控制
│   │   │   ├── boot_metadata.h         ← Metadata 读写
│   │   │   ├── boot_safe_mode.h        ← Safe Mode UDS 下载
│   │   │   ├── boot_trial.h            ← Trial Boot 管理
│   │   │   ├── boot_verify.h           ← CRC/ECDSA 校验
│   │   │   ├── isotp.h                 ← ISO-TP 传输层
│   │   │   ├── sha256.h                ← SHA-256 哈希
│   │   │   ├── sit1145.h               ← SIT1145 CAN 收发器驱动
│   │   │   ├── timer_drv.h             ← 定时器驱动
│   │   │   └── uECC.h                  ← ECDSA P-256 签名库
│   │   └── Src/                        ← 对应 .c 实现
│   └── libraries/
│       ├── cmsis/                      ← ARM CMSIS 核心文件
│       └── drivers/                    ← AT32 SPL 外设驱动库
│
├── qi_wireless_code_slotA/             ← APP 工程 (Slot A, IROM=0x08007100)
│   ├── mdk_project/
│   ├── mdk_user/
│   │   ├── Inc/
│   │   │   ├── at32f422_426_conf.h     ← 外设模块配置
│   │   │   └── ...
│   │   └── Src/
│   │       └── main.c                  ← APP 入口
│   ├── mdk_can/
│   │   ├── Inc/can_driver.h
│   │   └── Src/can_driver.c
│   └── mdk_app/
│       ├── Inc/
│       │   ├── board_gpio.h            ← GPIO 配置 (霍尔/供电)
│       │   ├── can_protocol.h          ← CAN-UDS 协议处理
│       │   ├── device_info.h           ← 设备信息 (SN 等)
│       │   ├── lifecycle.h             ← 生命周期状态广播
│       │   ├── nvm_drv.h               ← NVM 配置存储
│       │   ├── ota_trigger.h           ← OTA 触发 (进入 Boot)
│       │   ├── qi_uart.h               ← Qi 芯片串口通信
│       │   └── ...
│       └── Src/                        ← 对应 .c 实现
│
├── qi_wireless_code_slotB/             ← APP 工程 (Slot B, IROM=0x08011900)
│   └── (结构同 Slot A)
│
├── python_tools/                       ← Python 工具集
│   ├── pack_image.py                   ← 裸 bin → XATO 头 .ota.bin
│   ├── merge_prod_bin.py               ← Boot + APP 合并产线镜像
│   ├── verify_image.py                 ← 镜像完整性 + 签名校验
│   ├── sign_seed.py                    ← SecurityAccess seed 签名
│   ├── zcanpro_ext_ota.py              ← ZCANPRO 一键 OTA 脚本
│   └── 脚本使用说明.md
│
└── docs/                               ← 项目文档
    ├── 1. AT32F426KBU7-4_引脚定义.md
    ├── 2. Flash 分配方案.md
    ├── 3. 通用CAN协议规范.md
    ├── 4. IAP数据通信协议规范.md        ← MCU ↔ Qi 芯片 UART 协议
    ├── 5. CAN-UDS通信协议结构.md
    ├── 6. qi_charger_srs_zh.md         ← Qi 充电模块 SRS
    ├── 7. workflow-detail.md            ← 完整工作流详解
    ├── 8. Bootloader Safe Mode 下载顺序.md
    ├── 9. APP镜像打包与产线烧录.md
    ├── 10. CAN-UDS OTA 测试用例表.md    ← 62 条测试用例
    ├── 11. 签名校验与脚本使用.md
    ├── 12. 签名原理与Seed机制.md
    ├── 宏定义切换方式.md
    ├── 官方IAP例程vs自定义Bootloader对比.md
    ├── 计划安排表.md
    └── keys/
        ├── private.pem                 ← ECDSA P-256 私钥
        └── public.pem                  ← ECDSA P-256 公钥 (烧入 Bootloader)
```

---

## 5. Flash 空间布局

> **MCU**: AT32F426 — 128KB Flash, 2KB Sector

```
地址             大小      区域                    说明
──────────────────────────────────────────────────────────────────
0x08000000 ┌─────────────────────────────┐
           │     Bootloader (28KB)       │  引导 + Safe Mode + 验签
           │     0x7000 bytes            │
0x08007000 ├─────────────────────────────┤
           │  Slot A: XATO Header (256B) │  magic / length / CRC32
0x08007100 │  Slot A: APP Code (41.75KB) │  ← Keil IROM 入口
           │  0xA700 bytes               │
0x08011800 ├─────────────────────────────┤
           │  Slot B: XATO Header (256B) │  同 Slot A 结构
0x08011900 │  Slot B: APP Code (41.75KB) │  ← OTA 写入目标
           │  0xA700 bytes               │
0x0801C000 ├─────────────────────────────┤
           │  Metadata Primary (2KB)     │  ota_metadata_t (272B)
0x0801C800 ├─────────────────────────────┤
           │  Metadata Backup (2KB)      │  掉电安全副本
0x0801D000 ├─────────────────────────────┤
           │  Device Info (4KB)          │  SN 序列号, OTA 不擦除此区
0x0801E000 ├─────────────────────────────┤
           │  NVM Config (8KB)           │  APP 运行时配置
0x0801FFFF └─────────────────────────────┘
```

### Metadata 结构 (`ota_metadata_t`, 272 字节)

| 偏移 | 字段 | 说明 |
|------|------|------|
| 0x00 | magic | `0x4F54414D` ("MATO") |
| 0x08 | active_slot | 当前活跃槽 (0=A, 1=B) |
| 0x09 | pending_slot | 待试运行槽 (0xFE=无) |
| 0x0A | slot_a_valid | Slot A 镜像有效标志 |
| 0x0B | slot_b_valid | Slot B 镜像有效标志 |
| 0x14 | trial_state | 0=IDLE, 1=PENDING, 2=ACTIVE, 3=CONFIRMED |
| 0x21 | ota_state | 0=IDLE, 1=DOWNLOADING (触发 Safe Mode) |
| 0x10C | crc32 | 上述字段 CRC32 校验 |

### XATO 镜像头 (256 字节)

| 偏移 | 字段 | 说明 |
|------|------|------|
| 0x00 | magic | `0x4F544158` ("XATO") |
| 0x04 | image_length | 固件长度 (不含头) |
| 0x08 | crc32 | 固件 CRC32 校验 |
| 0x0C | signature[64] | ECDSA P-256 签名 (R‖S) |
| 0x4C | version[16] | 版本字符串 |
| 0x5C | timestamp | 编译时间戳 |

---

## 6. 协议栈架构

```
┌─────────────────────────────────────────────┐
│        应用层 (Application Layer)            │
│   ISO 14229 — UDS 诊断服务                   │
│   0x10 会话 │ 0x11 复位 │ 0x22 读DID        │
│   0x27 安全 │ 0x2E 写DID │ 0x31 例程        │
│   0x34/36/37/38 下载 │ 0x3E 保活            │
├─────────────────────────────────────────────┤
│        传输层 (Transport Layer)              │
│   ISO 15765-2 — ISO-TP                       │
│   单帧(SF) / 首帧(FF) / 连续帧(CF) / 流控(FC) │
├─────────────────────────────────────────────┤
│        数据链路层 (Data Link Layer)          │
│   CAN 2.0B — 29-bit 扩展帧                   │
│   仲裁 / CRC / 错误检测 / Bus-Off 恢复       │
├─────────────────────────────────────────────┤
│        物理层 (Physical Layer)               │
│   SIT1145 CAN 收发器 (SPI 配置)              │
│   250 kbps / 120Ω 终端 / 差分信号            │
└─────────────────────────────────────────────┘
```

### 关键 UDS 服务

| SID | 名称 | Bootloader | APP |
|-----|------|:----------:|:---:|
| 0x10 | DiagnosticSessionControl | ✅ | ✅ |
| 0x11 | ECUReset | ✅ | ✅ |
| 0x22 | ReadDataByIdentifier | ✅ | ✅ |
| 0x27 | SecurityAccess (ECDSA) | ✅ | ✅ |
| 0x2E | WriteDataByIdentifier | ✅ | ✅ |
| 0x31 | RoutineControl (擦槽) | ✅ | ❌ (NRC 0x11) |
| 0x34 | RequestDownload | ✅ | ❌ (NRC 0x11) |
| 0x36 | TransferData | ✅ | ❌ |
| 0x37 | RequestTransferExit | ✅ | ❌ |
| 0x3E | TesterPresent | ✅ | ✅ |

> APP 不实现 0x34/0x36/0x37，下载只在 Bootloader Safe Mode 下进行。

---

## 7. OTA 升级流程

### 7.1 端到端 OTA 时序

```
  CCU (主机)                          Qi 模块 (Bootloader)
     │                                      │
     │  ── 10 02 (Programming Session) ──►  │
     │  ◄── 50 02 ──────────────────────────│
     │                                      │
     │  ── 27 01 (RequestSeed) ──────────►  │
     │  ◄── 67 01 [seed 4B] ────────────────│
     │                                      │
     │  ── 27 03 [签名分片 ×16] ─────────►  │  SHA256(seed) → ECDSA
     │  ◄── 67 03 ──────────────────────────│
     │                                      │
     │  ── 27 02 (SendKey/验签) ─────────►  │
     │  ◄── 67 02 (OK) ─────────────────────│
     │                                      │
     │  ── 2E 2010 01 (选 APP) ──────────►  │
     │  ◄── 6E 2010 ────────────────────────│
     │                                      │
     │  ── 31 01 FF00 (擦非活跃槽) ──────►  │  ◄── 7F 31 78 (ResponsePending)
     │  ◄── 71 01 FF00 ─────────────────────│      擦除中...
     │                                      │
     │  ── 34 (RequestDownload) ─────────►  │
     │  ◄── 74 20 01 00 (maxBlock=256) ─────│
     │                                      │
     │  ── 36 [blockSeq + 256B 数据] ────►  │  重复 N 次
     │  ◄── 76 [blockSeq] ──────────────────│
     │                                      │
     │  ── 37 (TransferExit) ────────────►  │  ◄── 7F 37 78 (验签中...)
     │  ◄── 77 ─────────────────────────────│
     │                                      │
     │  ── 11 01 (HardReset) ────────────►  │
     │  ◄── 51 01 ──────────────────────────│
     │                                      │
     │         MCU 复位 → Trial Boot        │
```

### 7.2 APP 触发 OTA

```
APP 收到 OTA 指令 (CAN / 霍尔 / 内部条件)
    │
    ├─ 写 metadata: ota_state = DOWNLOADING
    ├─ NVIC_SystemReset()
    │
    └─ Bootloader 检测到 ota_state == DOWNLOADING
       └─ enter_safe_mode() → 开放 UDS 下载
```

---

## 8. Bootloader 启动流程

```
上电 / 复位
  │
  ├─ system_clock_config()          ← 180 MHz
  ├─ nvic_priority_group_config()
  ├─ timer_drv_init()
  ├─ boot_metadata_init()           ← 读主区 → 备份 → 默认值
  ├─ detect_boot_reason()           ← 上电/软复位/WDG/OTA/回滚
  │
  ├─ ota_state == DOWNLOADING? ──是──▶ enter_safe_mode() (不返回)
  │
  ├─ process_trial_state()          ← PENDING→ACTIVE, 超限回滚
  ├─ select_boot_slot()             ← PENDING/ACTIVE→trial_slot, 否则→active_slot
  │
  ├─ try_boot_slot(选中槽)           ← 验签 XATO 头 + ECDSA
  │     ├─ 通过 → jump_to_app()
  │     └─ 失败 → try_boot_slot(另一槽)
  │           ├─ 旧槽成功 → 写回滚 metadata → jump
  │           └─ 双槽失败 → enter_safe_mode()
  │
  └─ enter_safe_mode()              ← Safe Mode: 完整 UDS 下载循环
```

---

## 9. Python 工具集

| 脚本 | 功能 | 依赖 |
|------|------|------|
| `pack_image.py` | 裸 bin → XATO 头 .ota.bin (CRC32 + ECDSA P-256) | 标准库 |
| `merge_prod_bin.py` | Bootloader + Slot A 合并为单文件产线镜像 | 标准库 |
| `verify_image.py` | 校验 XATO 镜像完整性 + 签名 | 标准库 |
| `sign_seed.py` | SecurityAccess seed 签名生成 + CAN 帧输出 | `cryptography` |
| `zcanpro_ext_ota.py` | ZCANPRO 扩展脚本, 一键 CAN-UDS OTA | `python-can`, `zcanpro` |

### 快速使用

```bash
# 打包镜像 (仓库根目录执行)
python python_tools/pack_image.py \
  --bin qi_wireless_code_slotA/mdk_project/Objects/qi_wireless.bin \
  --key docs/keys/private.pem \
  --out qi_wireless_code_slotA/mdk_project/Objects/qi_wireless.ota.bin

# 合并产线镜像
python python_tools/merge_prod_bin.py \
  --boot qi_wireless_bootloader/mdk_project/Objects/qi_wireless.bin \
  --app  qi_wireless_code_slotA/mdk_project/Objects/qi_wireless.ota.bin \
  --out  prod_image.bin

# 校验镜像
python python_tools/verify_image.py \
  --bin qi_wireless_code_slotA/mdk_project/Objects/qi_wireless.ota.bin \
  --pub docs/keys/public.pem

# 一键 OTA (需 ZCANPRO + python-can)
python python_tools/zcanpro_ext_ota.py
```

---

## 10. 构建与烧录

### 10.1 编译环境

| 工具 | 版本要求 |
|------|----------|
| Keil MDK | v5.38+ |
| AT32 IDE Pack | AT32F426 支持包 |
| Python | 3.8+ (工具脚本) |

### 10.2 Bootloader 编译

1. 打开 `qi_wireless_bootloader/mdk_project/qi_wireless.uvprojx`
2. Target → IROM1: `0x08000000` / `0x7000`
3. Build → 输出 `qi_wireless.bin`

### 10.3 APP 编译 (Slot A)

1. 打开 `qi_wireless_code_slotA/mdk_project/qi_wireless.uvprojx`
2. Target → IROM1: `0x08007100` / `0xA700`
3. Linker → 勾选 "Use Memory Layout from Target Dialog"
4. Build → 输出 `qi_wireless.bin` (裸 bin，不含 XATO 头)

### 10.4 产线烧录

```bash
# 1. 打包 APP 镜像
python python_tools/pack_image.py --bin qi_wireless_code_slotA/mdk_project/Objects/qi_wireless.bin

# 2. 合并 Boot + APP
python python_tools/merge_prod_bin.py

# 3. 烧录合并后的 prod_image.bin 到 0x08000000
# (使用 J-Link / AT-Link / SWD)
```

> **注意**: 产线只烧 Bootloader + Slot A。Slot B 出厂为空，留给首次 CAN OTA 写入。

---

## 11. 文档索引

| 编号 | 文档 | 内容 |
|------|------|------|
| 1 | [引脚定义](docs/1.%20AT32F426KBU7-4_引脚定义.md) | AT32F426KBU7-4 QFN32 全引脚功能定义 |
| 2 | [Flash 分配方案](docs/2.%20Flash%20分配方案.md) | 128KB Flash 分区、Metadata 结构、XATO 头 |
| 3 | [通用 CAN 协议规范](docs/3.%20通用CAN协议规范.md) | 整车外设通用 CAN 协议架构 (CCU↔外设) |
| 4 | [IAP 数据通信协议](docs/4.%20IAP数据通信协议规范.md) | MCU ↔ Qi 芯片 UART 通信协议 |
| 5 | [CAN-UDS 通信协议](docs/5.%20CAN-UDS通信协议结构.md) | ISO-TP + UDS 协议栈详解 |
| 6 | [Qi 充电模块 SRS](docs/6.%20qi_charger_srs_zh.md) | 软件需求规格说明书 |
| 7 | [工作流详解](docs/7.%20workflow-detail.md) | Bootloader/APP 完整工作流、职责划分 |
| 8 | [Safe Mode 下载顺序](docs/8.%20Bootloader%20Safe%20Mode%20下载顺序.md) | 逐步 CAN 帧数据、UDS 下载序列 |
| 9 | [镜像打包与产线烧录](docs/9.%20APP镜像打包与产线烧录.md) | pack_image.py 用法、Keil IROM 配置 |
| 10 | [OTA 测试用例表](docs/10.%20CAN-UDS%20OTA%20测试用例表.md) | 62 条测试用例 (P0/P1/P2) |
| 11 | [签名校验与脚本](docs/11.%20签名校验与脚本使用.md) | 签名工具使用说明 |
| 12 | [签名原理与 Seed 机制](docs/12.%20签名原理与Seed机制.md) | ECDSA P-256 + SHA-256 原理 |

---

## 许可证

本项目为内部开发项目，未经授权不得外传。

---

> **Lime 固件团队** — 2026
