# Flash 空间分配 — QI Wireless Bootloader

> **MCU**: AT32F426 (Cortex-M4F, 128KB Flash, 20KB SRAM)
> **Flash 基地址**: `0x08000000`
> **Flash 总大小**: 128KB (`0x08000000` ~ `0x0801FFFF`)
> **Sector 大小**: 2KB (AT32F426)

---

## 一、整体 Flash 布局总览

| 区域 | 起始地址 | 结束地址 | 大小 | 说明 |
|------|----------|----------|------|------|
| Bootloader | `0x08000000` | `0x08003FFF` | 16KB | 引导程序，含 OTA 逻辑 |
| Application Slot A | `0x08004000` | `0x0800FFFF` | 48KB | 应用固件 A |
| Application Slot B | `0x08010000` | `0x0801BFFF` | 48KB | 应用固件 B |
| Metadata Primary | `0x0801C000` | `0x0801DFFF` | 8KB | OTA 元数据主副本 |
| Metadata Backup | `0x0801E000` | `0x0801FFFF` | 8KB | OTA 元数据备份 / NVM 配置区 |

```
0x08000000 ┌─────────────────────────────┐
           │      Bootloader (16KB)      │
0x08004000 ├─────────────────────────────┤
           │   Application Slot A (48KB) │ ← image_header (256B) + app code
           │                             │
0x08010000 ├─────────────────────────────┤
           │   Application Slot B (48KB) │ ← image_header (256B) + app code
           │                             │
0x0801C000 ├─────────────────────────────┤
           │  Metadata Primary (8KB)     │ ← ota_metadata_t (512B)
0x0801E000 ├─────────────────────────────┤
           │  Metadata Backup (8KB)      │ ← ota_metadata_t 备份 / NVM 配置
0x0801FFFF └─────────────────────────────┘
```

---

## 二、ota_metadata_t 数据结构详解

`primary = (const ota_metadata_t *)META_PRIMARY_ADDR;` 这行代码将 Flash 地址 `0x0801C000` 强制类型转换为 `ota_metadata_t *` 指针，直接从 Flash 内存映射读取 OTA 元数据。

**结构体定义** (来源: `boot_metadata.h`):

```
Flash 地址 0x0801C000 开始:
┌──────────────────────────────────────────┐
│ magic (4字节)              │ ← 0x4F54414D "MATO"          偏移 0x00
│ version (4字节)            │ ← 1 (metadata format v1)     偏移 0x04
│ active_slot (1字节)        │ ← 0=A, 1=B                   偏移 0x08
│ pending_slot (1字节)       │ ← 0=A, 1=B, 0xFE=none        偏移 0x09
│ slot_a_valid (1字节)       │ ← 1=Slot A 镜像有效           偏移 0x0A
│ slot_b_valid (1字节)       │ ← 1=Slot B 镜像有效           偏移 0x0B
│ slot_a_crc32 (4字节)       │ ← Slot A 镜像 CRC32           偏移 0x0C
│ slot_b_crc32 (4字节)       │ ← Slot B 镜像 CRC32           偏移 0x10
│ trial_state (1字节)        │ ← 0=IDLE,1=PENDING,2=ACTIVE,3=CONFIRMED  偏移 0x14
│ trial_slot (1字节)         │ ← 正在试运行的 slot (0=A,1=B) 偏移 0x15
│ trial_retry_count (1字节)  │ ← 当前试运行重试次数           偏移 0x16
│ trial_max_retries (1字节)  │ ← 最大重试次数 (默认 3)        偏移 0x17
│ trial_timeout_sec (2字节)  │ ← 试运行超时秒数 (默认 10)     偏移 0x18
│ reserved1 (2字节)          │ ← 对齐保留                    偏移 0x1A
│ rollback_count (4字节)     │ ← 回滚次数计数器               偏移 0x1C
│ last_boot_reason (1字节)   │ ← 0x00=上电,0x02=WDG,0x03=OTA,0x04=回滚  偏移 0x20
│ ota_state (1字节)          │ ← 0x00=空闲,0x01=下载中        偏移 0x21
│ reserved2[2] (2字节)       │ ← 保留                        偏移 0x22
│ padding[488] (488字节)     │ ← 填充至 512 字节              偏移 0x24
│ crc32 (4字节)              │ ← 上述所有字段的 CRC32 校验    偏移 0x1FC
└──────────────────────────────────────────┘
共 512 字节 (0x200)
```

**字段说明**:

| 字段 | 类型 | 字节偏移 | 说明 |
|------|------|----------|------|
| `magic` | uint32_t | 0x00 | 魔数 `0x4F54414D` ("MATO")，用于识别有效元数据 |
| `version` | uint32_t | 0x04 | 元数据格式版本号，当前 = 1 |
| `active_slot` | uint8_t | 0x08 | 当前活跃 slot: 0=A, 1=B |
| `pending_slot` | uint8_t | 0x09 | 待切换 slot: 0=A, 1=B, 0xFE=无 |
| `slot_a_valid` | uint8_t | 0x0A | Slot A 镜像校验通过标志 |
| `slot_b_valid` | uint8_t | 0x0B | Slot B 镜像校验通过标志 |
| `slot_a_crc32` | uint32_t | 0x0C | Slot A 固件镜像的 CRC32 |
| `slot_b_crc32` | uint32_t | 0x10 | Slot B 固件镜像的 CRC32 |
| `trial_state` | uint8_t | 0x14 | 试运行状态机: IDLE(0)/PENDING(1)/ACTIVE(2)/CONFIRMED(3) |
| `trial_slot` | uint8_t | 0x15 | 当前试运行的 slot 编号 |
| `trial_retry_count` | uint8_t | 0x16 | 已重试次数 |
| `trial_max_retries` | uint8_t | 0x17 | 最大重试次数，默认 3 |
| `trial_timeout_sec` | uint16_t | 0x18 | 试运行超时，默认 10 秒 |
| `reserved1` | uint16_t | 0x1A | 保留对齐 |
| `rollback_count` | uint32_t | 0x1C | 累计回滚次数 |
| `last_boot_reason` | uint8_t | 0x20 | 最近一次启动原因码 |
| `ota_state` | uint8_t | 0x21 | OTA 状态: IDLE(0)/DOWNLOADING(1) |
| `reserved2[2]` | uint8_t[2] | 0x22 | 保留 |
| `padding[488]` | uint8_t[488] | 0x24 | 填充至 512 字节 |
| `crc32` | uint32_t | 0x1FC | CRC32 (覆盖除自身外的所有字段) |

**CRC32 计算范围**: 从 `magic` 到 `padding` 末尾，共 508 字节 (`META_CRC32_OFFSET = sizeof(ota_metadata_t) - 4`)。

**双副本冗余机制**:

- Primary: `0x0801C000` — 每次启动优先读取
- Backup: `0x0801E000` — Primary 校验失败时回退读取
- 保存顺序: 先写 Backup，再写 Primary (确保写 Primary 失败时 Backup 仍有效)

---

## 三、Application Slot 内部结构

每个 Application Slot 的起始位置包含一个 256 字节的 `image_header_t`，实际应用代码紧随其后。

### Slot A (`0x08004000` ~ `0x0800FFFF`, 48KB)

```
0x08004000 ┌─────────────────────────────┐
           │  image_header_t (256字节)    │ ← 镜像头
           │  ┌───────────────────────┐  │
           │  │ magic    (4字节)      │  │ ← 0x4F544158 "XATO"
           │  │ image_length (4字节)  │  │ ← 镜像数据长度 (不含头)
           │  │ crc32    (4字节)      │  │ ← 镜像数据 CRC32
           │  │ signature (64字节)    │  │ ← ECDSA P-256 签名 (预留)
           │  │ version  (16字节)     │  │ ← "MAJOR.MINOR.PATCH\0"
           │  │ build_timestamp(4字节)│  │ ← Unix 时间戳
           │  │ reserved (156字节)    │  │ ← 填充至 256 字节
           │  └───────────────────────┘  │
0x08004100 ├─────────────────────────────┤
           │  Application Code           │ ← boot_jump_to_app(0x08004100)
           │  (最大 48KB - 256B = 48896B) │
           │                             │
0x0800FFFF └─────────────────────────────┘
```

### Slot B (`0x08010000` ~ `0x0801BFFF`, 48KB)

```
0x08010000 ┌─────────────────────────────┐
           │  image_header_t (256字节)    │ ← 同 Slot A 结构
0x08010100 ├─────────────────────────────┤
           │  Application Code           │ ← boot_jump_to_app(0x08010100)
           │  (最大 48896 字节)           │
           │                             │
0x0801BFFF └─────────────────────────────┘
```

**image_header_t 结构体定义** (来源: `boot_verify.h`):

| 字段 | 类型 | 大小 | 说明 |
|------|------|------|------|
| `magic` | uint32_t | 4B | 魔数 `0x4F544158` ("XATO") |
| `image_length` | uint32_t | 4B | 镜像数据长度 (不含 header) |
| `crc32` | uint32_t | 4B | 镜像数据的 CRC32 校验 |
| `signature` | uint8_t[64] | 64B | ECDSA P-256 签名 (当前为预留) |
| `version` | char[16] | 16B | 版本字符串 "MAJOR.MINOR.PATCH\0" |
| `build_timestamp` | uint32_t | 4B | 构建时间 Unix 时间戳 |
| `reserved` | uint8_t[156] | 156B | 填充至 256 字节 |

**验证流程** (`boot_verify_image`):
1. 检查 `magic == 0x4F544158`
2. 检查 `image_length` 在合法范围内 (0 < length ≤ slot_size - 256)
3. 对 header 之后的 `image_length` 字节计算 CRC32，与 `header->crc32` 比对
4. 签名验证 (TODO，当前为占位)

---

## 四、Metadata 区域细节

### Metadata Primary (`0x0801C000` ~ `0x0801DFFF`, 8KB)

```
0x0801C000 ┌─────────────────────────────┐
           │  ota_metadata_t (512字节)    │ ← 元数据主副本
0x0801C200 ├─────────────────────────────┤
           │  未使用 (7680字节)           │ ← 一个 8KB page 剩余空间
           │                             │
0x0801DFFF └─────────────────────────────┘
```

- 擦写粒度: 整个 8KB page (`flash_sector_erase`)
- 写入方式: 按 uint32_t 逐字编程 (`flash_word_program`)

### Metadata Backup / NVM Config (`0x0801E000` ~ `0x0801FFFF`, 8KB)

```
0x0801E000 ┌─────────────────────────────┐
           │  ota_metadata_t 备份 (512B)  │ ← boot_metadata_save() 写入
           │  ── 或 ──                    │
           │  NVM Config Area (8KB)       │ ← nvm_drv 读写 (sector 粒度)
           │  ┌───────────────────────┐  │
           │  │ NVM Validity Magic    │  │ ← offset 0x00, "NVM1" (0x4E564D31)
           │  │ User Config Data ...  │  │
           │  └───────────────────────┘  │
0x0801FFFF └─────────────────────────────┘
```

**注意**: 此区域被两个模块共用 — `boot_metadata.c` 将其用作元数据备份，`nvm_drv.c` 将其用作 NVM 配置存储。两者不会同时使用（bootloader 阶段写元数据备份，应用阶段用 NVM 配置）。

NVM 驱动参数:
- Sector 大小: 2KB (`NVM_SECTOR_SIZE = 0x800`)
- Sector 数量: 4 (`0x2000 / 0x800`)
- 总容量: 8KB

---

## 五、Bootloader 区域 (`0x08000000` ~ `0x08003FFF`)

```
0x08000000 ┌─────────────────────────────┐
           │  Vector Table               │ ← 中断向量表
           │  Bootloader Code            │ ← 含以下模块:
           │   - boot_metadata.c         │   元数据读写
           │   - boot_verify.c           │   镜像校验 (CRC32)
           │   - boot_jump.c             │   跳转应用
           │   - can_driver              │   CAN 通信
           │   - UDS safe mode handler   │   安全模式 OTA
           │   - trial boot state machine│   试运行状态机
           │   - nvm_drv.c               │   NVM 驱动
0x08003FFF └─────────────────────────────┘
```

链接脚本配置: `LR_IROM1 0x08000000 0x00003FFF`

---

## 六、启动流程与 Flash 交互

```
上电 / 复位
    │
    ▼
[1] 读取 Primary Metadata (0x0801C000)
    │
    ├─ magic/version/CRC32 校验通过 → 使用 Primary
    │
    ├─ 校验失败 → 读取 Backup Metadata (0x0801E000)
    │   │
    │   ├─ Backup 有效 → 恢复 Primary，使用 Backup 数据
    │   │
    │   └─ Backup 也无效 → 填充默认值，写入双副本
    │
    ▼
[2] 检测启动原因 (WDG / Power-On)
    │
    ▼
[3] 处理试运行状态机 (trial_state)
    │
    ▼
[4] 选择 Boot Slot (active_slot 或 trial_slot)
    │
    ▼
[5] 校验 Slot 镜像 (image_header_t + CRC32)
    │
    ├─ 校验通过 → boot_jump_to_app(slot_base + 0x100)
    │
    └─ 校验失败 → 尝试另一个 Slot
        │
        ├─ 另一个 Slot 通过 → 更新 active_slot，跳转
        │
        └─ 两个 Slot 均失败 → 进入 Safe Mode (CAN OTA)
```

---

## 七、关键宏定义速查

| 宏名 | 值 | 含义 |
|------|-----|------|
| `BOOT_BASE_ADDR` | `0x08000000` | Bootloader 起始地址 |
| `BOOT_SIZE` | `0x4000` (16KB) | Bootloader 大小 |
| `APP_A_BASE_ADDR` | `0x08004000` | Slot A 起始地址 |
| `APP_A_SIZE` | `0xC000` (48KB) | Slot A 大小 |
| `APP_B_BASE_ADDR` | `0x08010000` | Slot B 起始地址 |
| `APP_B_SIZE` | `0xC000` (48KB) | Slot B 大小 |
| `META_PRIMARY_ADDR` | `0x0801C000` | 元数据主副本地址 |
| `META_BACKUP_ADDR` | `0x0801E000` | 元数据备份地址 |
| `META_PAGE_SIZE` | `0x2000` (8KB) | 元数据 page 大小 |
| `IMAGE_HEADER_SIZE` | `256` | 镜像头大小 |
| `META_MAGIC` | `0x4F54414D` | 元数据魔数 "MATO" |
| `IMAGE_MAGIC` | `0x4F544158` | 镜像头魔数 "XATO" |
| `SLOT_A` | `0` | Slot A 索引 |
| `SLOT_B` | `1` | Slot B 索引 |
| `SLOT_NONE` | `0xFE` | 无待切换 slot |

---

## 八、变更记录

| 版本 | 日期 | 改动说明 |
|------|------|----------|
| v1.0 | 2026-08-16 | 初始版本：基于源码梳理 Flash 空间分配、ota_metadata_t 结构、image_header_t 结构 |
