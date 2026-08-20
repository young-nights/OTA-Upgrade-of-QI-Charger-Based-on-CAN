# MCU OTA 详细实施方案

> **文档编号**: QI-MCU-OTA-001  
> **版本**: V1.0  
> **日期**: 2026-08-15  
> **主控芯片**: AT32F426KBU7-4 (Cortex-M4F, 180MHz, 128KB Flash, 20KB SRAM)  
> **参考规范**: SRS v1.1 (qi_charger_srs_cn)、REF-1 v1.1 (common_can_protocol_spec)、SRS update 0723

---

## 一、方案总览

### 1.1 设计目标

实现 CAN MCU (AT32F426) 通过 CAN/UDS 协议的 OTA 远程升级能力，满足以下 SRS 强制要求：

| 需求 ID | 要求 | 本方案响应 |
|---------|------|-----------|
| common §9.4 | 双槽固件布局 | ✅ APP_A + APP_B 双槽 |
| common §9.5 | 试启动与自动回滚 | ✅ 10s 超时 + 3 次重试 |
| common §9.6 | 掉电恢复 | ✅ 原子元数据 + 两阶段激活 |
| SRS §12.2 | Bootloader 独立区域 | ✅ 16KB 独立 Bootloader |
| SRS §12.4 | 安全启动 (P2) | ⏳ 预留接口，首版不实现 |
| common §7.4 | ECDSA P-256 安全访问 | ✅ micro-ecc 库集成 |
| SRS update 0723 | SecurityAccess ECDSA P-256 | ✅ SHA-256 + IEEE P1363 签名 |

### 1.2 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│                    CCU (UDS 客户端, 0x03)                     │
│              OTA 上位机 / 诊断工具 / 云端                     │
└───────────────────────┬──────────────────────────────────────┘
                        │ CAN 250 kbps, 29-bit 扩展帧
                        │ 0x18DA0D03 (请求) / 0x18DA030D (响应)
┌───────────────────────┴──────────────────────────────────────┐
│                CAN MCU (AT32F426, 0x0D)                      │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    Bootloader (16KB)                     │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────┐  │ │
│  │  │ 启动选择  │ │ 签名验证  │ │ 元数据管理│ │ 回滚控制  │  │ │
│  │  └──────────┘ └──────────┘ └──────────┘ └───────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────┐ ┌──────────────────────────┐ │
│  │      APP_A (48KB)          │ │     APP_B (48KB)         │ │
│  │  ┌──────────────────────┐  │ │  ┌────────────────────┐  │ │
│  │  │ UDS 服务栈            │  │ │  │ (待升级/备份槽)    │  │ │
│  │  │ CAN 驱动              │  │ │  │                    │  │ │
│  │  │ 串口通信              │  │ │  │                    │  │ │
│  │  │ GPIO 控制             │  │ │  │                    │  │ │
│  │  │ 充电状态管理          │  │ │  │                    │  │ │
│  │  └──────────────────────┘  │ │  └────────────────────┘  │ │
│  └────────────────────────────┘ └──────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              OTA 元数据区 (最后 16KB)                     │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────┐  │ │
│  │  │ 槽位状态  │ │ 镜像信息  │ │ 启动计数  │ │ CRC32     │  │ │
│  │  └──────────┘ └──────────┘ └──────────┘ └───────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

---

## 二、Flash 布局详细设计

### 2.1 内存映射

```
地址              大小    区域
─────────────────────────────────────
0x0800_0000       16KB    Bootloader
0x0800_4000       48KB    APP_A (槽位 0)
0x0801_0000       48KB    APP_B (槽位 1)
0x0801_C000       8KB     OTA 元数据 (主)
0x0801_E000       8KB     OTA 元数据 (备份)
0x0802_0000              Flash 结束 (128KB)
```

### 2.2 Bootloader 区域 (16KB)

| 属性 | 值 |
|------|-----|
| 起始地址 | 0x0800_0000 |
| 大小 | 16KB (0x4000) |
| 写保护 | 量产时启用 Flash 写保护，防止 APP 意外擦除 |
| 内容 | 启动选择逻辑、ECDSA 验签、元数据管理、CAN 基础驱动（仅用于 OTA） |

### 2.3 APP 槽位 (各 48KB)

| 属性 | APP_A | APP_B |
|------|-------|-------|
| 起始地址 | 0x0800_4000 | 0x0801_0000 |
| 大小 | 48KB (0xC000) | 48KB (0xC000) |
| 中断向量表 | 偏移至 0x0800_4000 | 偏移至 0x0801_0000 |
| 最大镜像大小 | 47KB (留 1KB 头部) | 47KB (留 1KB 头部) |

**镜像头部格式** (每个 APP 槽位前 256 字节):

```
偏移    长度    字段                说明
──────────────────────────────────────────────
0x00    4       魔数                0x4F544158 ("XATO")
0x04    4       镜像长度            有效镜像字节数 (不含头部)
0x08    4       CRC32               镜像数据 CRC32
0x0C    64      ECDSA 签名          R‖S, 64 字节 (IEEE P1363)
0x4C    16      版本字符串          "MAJOR.MINOR.PATCH\0"
0x5C    4       编译时间戳          Unix timestamp
0x60    4       保留                0x00000000
0x64    156     保留                0x00
0x100   ...     镜像数据            实际固件代码
```

### 2.4 OTA 元数据区 (主 + 备份, 各 8KB)

采用**主备双份 + CRC32 校验**实现原子更新：

| 属性 | 主元数据 | 备份元数据 |
|------|----------|-----------|
| 起始地址 | 0x0801_C000 | 0x0801_E000 |
| 大小 | 8KB | 8KB |
| 写入策略 | 先写主，成功后写备份 | 用于掉电恢复 |

**元数据结构体**:

```c
typedef struct {
    uint32_t magic;              /* 0x4F54414D ("MATO") */
    uint32_t version;            /* 元数据格式版本, 当前 = 1 */
    
    /* 槽位信息 */
    uint8_t  active_slot;        /* 0=A, 1=B */
    uint8_t  pending_slot;       /* 0=A, 1=B, 0xFE=无 */
    uint8_t  slot_a_valid;       /* 0=无效, 1=有效 */
    uint8_t  slot_b_valid;       /* 0=无效, 1=有效 */
    
    /* 镜像信息 */
    uint32_t slot_a_crc32;       /* APP_A 镜像 CRC32 */
    uint32_t slot_b_crc32;       /* APP_B 镜像 CRC32 */
    uint16_t slot_a_version_major;
    uint16_t slot_a_version_minor;
    uint16_t slot_a_version_patch;
    uint16_t slot_b_version_major;
    uint16_t slot_b_version_minor;
    uint16_t slot_b_version_patch;
    
    /* 试启动管理 */
    uint8_t  trial_state;        /* 0=无试启动, 1=试启动中, 2=已确认 */
    uint8_t  trial_slot;         /* 试启动的槽位 */
    uint8_t  trial_retry_count;  /* 已重试次数 */
    uint8_t  trial_max_retries;  /* 最大重试次数 (默认 3) */
    uint16_t trial_timeout_sec;  /* 试启动超时 (默认 10s) */
    uint16_t reserved;
    
    /* 回滚计数器 */
    uint32_t rollback_count;     /* 自制造以来的回滚次数 */
    
    /* 最近启动原因 */
    uint8_t  last_boot_reason;   /* 0x00=上电, 0x01=UDS, 0x02=WDG, 0x03=OTA, 0x04=回滚, 0x05=掉电 */
    uint8_t  reserved2[3];
    
    /* 下载状态 */
    uint8_t  ota_state;          /* 0x00=空闲, 0x01=下载中, 0x02=验证中, 0x03=待激活 */
    uint8_t  reserved3[3];
    
    /* 保留 */
    uint8_t  reserved4[488];     /* 填充至 512 字节 */
    
    /* CRC32 (覆盖上述所有字段) */
    uint32_t crc32;
} ota_metadata_t;  /* 总计 512 字节 */
```

**元数据更新流程 (原子性保证)**:

```
1. 擦除主元数据页 (0x0801_C000)
2. 写入新元数据到主区
3. 计算并写入 CRC32
4. 擦除备份元数据页 (0x0801_E000)
5. 写入相同元数据到备份区
6. 完成

掉电恢复:
- 主元数据 CRC 有效 → 使用主元数据
- 主元数据 CRC 无效, 备份 CRC 有效 → 用备份覆盖主元数据
- 两者均无效 → 使用默认值 (active_slot=A, 所有槽位无效)
```

---

## 三、Bootloader 详细设计

### 3.1 启动流程

```
                    ┌──────────────┐
                    │   上电/复位   │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │ 初始化硬件     │
                    │ (时钟, Flash)  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │ 读取元数据     │
                    │ (主+备份校验)  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │ 元数据 CRC 有效?│
                    └──┬────────┬──┘
                   否  │        │ 是
                       │        │
                ┌──────▼──┐  ┌──▼───────────┐
                │ 默认状态  │  │ 检查试启动状态 │
                │ A 有效   │  └──┬────────┬──┘
                └──────┬──┘     │        │
                       │   trial_state   │
                       │   == 1?         │
                       │        │        │
                       │   ┌────▼────┐   │
                       │   │ 试启动中  │   │
                       │   │ 检查重试  │   │
                       │   │ 次数     │   │
                       │   └────┬────┘   │
                       │        │        │
                       │   超限? │        │
                       │   ┌────▼────┐   │
                       │   │ 回滚到   │   │
                       │   │ 之前槽位  │   │
                       │   └────┬────┘   │
                       │        │        │
                    ┌──▼────────▼────────▼──┐
                    │   确定活跃槽位地址      │
                    └──────────┬────────────┘
                               │
                    ┌──────────▼────────────┐
                    │ 验证镜像签名           │
                    │ (ECDSA P-256)         │
                    └──┬────────────────┬───┘
                   失败│                │成功
                       │                │
                ┌──────▼──┐     ┌───────▼───────┐
                │ 尝试另一 │     │ 设置试启动状态  │
                │ 槽位     │     │ 跳转到 APP     │
                └──────┬──┘     └───────────────┘
                       │
                ┌──────▼──┐
                │ 两槽均   │
                │ 失败?    │
                └──┬────┬─┘
               是  │    │ 否
                   │    │
            ┌──────▼┐  ┌▼──────┐
            │ 安全   │  │ 跳转  │
            │ 模式   │  │ APP   │
            └───────┘  └───────┘
```

### 3.2 跳转到 APP 的实现

```c
/**
 * @brief  跳转到指定 APP 槽位
 * @param  app_addr: APP 起始地址 (0x0800_4000 或 0x0801_0000)
 */
static void boot_jump_to_app(uint32_t app_addr)
{
    uint32_t jump_addr;
    void (*app_entry)(void);
    
    /* 1. 禁用所有中断 */
    __disable_irq();
    
    /* 2. 关闭所有外设时钟 (避免中断残留) */
    crm_periph_clock_enable(CRM_TMR2_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_USART2_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_CAN1_PERIPH_CLOCK, FALSE);
    /* ... 关闭其他已使能的外设 ... */
    
    /* 3. 重置 SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    
    /* 4. 设置中断向量表偏移 */
    SCB->VTOR = app_addr;
    
    /* 5. 设置主堆栈指针 */
    __set_MSP(*(volatile uint32_t*)app_addr);
    
    /* 6. 获取复位向量并跳转 */
    jump_addr = *(volatile uint32_t*)(app_addr + 4);
    app_entry = (void (*)(void))jump_addr;
    app_entry();
}
```

### 3.3 APP 侧中断向量表配置

APP 启动时必须重新设置 VTOR 指向自身：

```c
/* APP main.c 开头 */
void system_init(void)
{
    /* 设置中断向量表偏移到 APP 起始地址 */
    SCB->VTOR = APP_BASE_ADDR;  /* 0x0800_4000 或 0x0801_0000 */
    
    /* 配置 NVIC 优先级组 */
    nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
    
    /* ... 其他初始化 ... */
}
```

---

## 四、UDS 服务实现

### 4.1 需要实现的 UDS 服务

| SID | 服务 | OTA 相关 | 会话要求 | 安全要求 |
|-----|------|----------|----------|----------|
| 0x10 | DiagnosticSessionControl | 是 | 无 | 无 |
| 0x11 | CCUReset | 是 | Programming | 无 |
| 0x22 | ReadDataByIdentifier | 是 | 无 | 无 |
| 0x27 | SecurityAccess | 是 | Extended/Programming | 无 |
| 0x2E | WriteDataByIdentifier | 是 | Extended/Programming | Level 1 |
| 0x31 | RoutineControl | 是 | Extended/Programming | Level 1 |
| 0x34 | RequestDownload | 是 | Programming | Level 1 |
| 0x36 | TransferData | 是 | Programming | Level 1 |
| 0x37 | RequestTransferExit | 是 | Programming | Level 1 |
| 0x3E | TesterPresent | 是 | 无 | 无 |

### 4.2 会话管理

> **说明**：Bootloader Safe Mode 下无显式会话概念，直接处理所有 UDS 请求，不区分 Default/Extended/Programming 会话。

```c
/* 会话管理在 APP 端实现 */
typedef enum {
    SESSION_DEFAULT     = 0x01,
    SESSION_EXTENDED    = 0x03,
    SESSION_PROGRAMMING = 0x02,
} uds_session_t;

typedef struct {
    uds_session_t current;
    uint32_t      s3_timer_ms;     /* S3 超时计时器 */
    bool          security_unlocked; /* SecurityAccess Level 1 解锁状态 */
    uint8_t       security_fail_count; /* 连续失败次数 */
    uint32_t      security_lock_timer; /* 锁定延迟计时器 */
} uds_session_mgr_t;

/* S3 超时 = 5000ms, 超时后返回 Default Session */
#define UDS_S3_TIMEOUT_MS  5000

/* SecurityAccess 锁定: 连续 3 次失败后锁定 10 秒 */
#define SA_MAX_ATTEMPTS    3
#define SA_LOCK_DELAY_MS   10000
```

**会话切换规则**:

| 当前 → 目标 | 安全状态 | 行为 |
|-------------|----------|------|
| Default → Extended | 保持 | 保持当前安全状态 |
| Default → Programming | 清除 | 清除安全解锁，需重新认证 |
| Extended → Programming | 清除 | 清除安全解锁，需重新认证 |
| Programming → Default | 清除 | 清除安全解锁，中止传输 |
| 任何 → Default | 清除 | 清除安全解锁，中止传输 |

### 4.3 SecurityAccess (ECDSA P-256 分帧验签)

**三步流程**:

```
CCU                          MCU (Bootloader Safe Mode)
 │                              │
 │── 0x27 0x01 (请求种子) ────→│
 │                              │ 生成 4 字节随机种子 (g_seed)
 │                              │ 重置签名接收缓冲区
 │←─ 0x67 0x01 + seed (4B) ────│
 │                              │
 │── 0x27 0x03 + seq + chunk ──→│ × N 帧（每帧 6B，共 ~11 帧）
 │   累积 64B ECDSA 签名        │ 校验 blockSeq 连续性
 │←─ 0x67 0x03 + seq ──────────│
 │                              │
 │── 0x27 0x02 (验签请求) ────→│
 │                              │ SHA256(g_seed) → hash
 │                              │ uECC_verify(pubkey, hash, sig)
 │←─ 0x67 0x02 (成功) ─────────│ 或 NRC 0x33/0x35
 │                              │
```

**实现要点**:

- 种子来源: 4 字节随机种子 (PRNG, 基于 SysTick + Flash 偏移)
- 哈希算法: SHA-256 (对 4 字节 seed 进行哈希)
- 签名格式: R‖S, 固定 64 字节 (IEEE P1363, 非 ASN.1 DER)
- 签名传输: 0x27 0x03 分帧接收，每帧 6B 签名数据 + blockSeq，共约 11 帧
- 公钥存储: 65 字节非压缩 SEC1 点 (0x04 ‖ X ‖ Y)，存放在 `.ecdsa_pubkey` section，附带 magic marker ("KEYP") 用于损坏检测
- 验签函数: `verify_security_ecdsa_signature()` — 使用 `sha256_hash(g_seed)` + `uECC_verify()`
- 超时处理: ECDSA 验签可能需要 200~800ms, 超过 P2 (50ms) 时发送 NRC 0x78 (ResponsePending), P2* = 5000ms

**NRC 处理**:

| 条件 | NRC |
|------|-----|
| 安全未解锁时访问受保护服务 | 0x33 (SecurityAccessDenied) |
| 签名验证失败 | 0x35 (InvalidKey) |
| 连续 3 次失败 → 60 秒锁定 | 0x35 (ExceededNumberOfAttempts) |
| 锁定期间再次请求 | 0x37 (RequiredTimeDelayNotExpired) |

### 4.4 固件下载流程

#### 4.4.1 RequestDownload (0x34)

```
请求: 0x34 + dataFormatIdentifier + addressAndLengthFormatIdentifier + 
      memoryAddress + memorySize

响应: 0x74 + lengthFormatIdentifier + maxNumberOfBlockLength
```

**MCU 处理逻辑**:

1. 检查 SecurityAccess 已解锁
2. 仅初始化下载状态（不执行擦除）
3. 返回 maxNumberOfBlockLength (建议 256 字节)

> **注意**: 按 SRS 规范，擦除操作由 0x31 0xFF00 独立执行，0x34 不再执行擦除。

#### 4.4.2 TransferData (0x36)

```
请求: 0x36 + blockSequenceCounter + transferData

响应: 0x76 + blockSequenceCounter
```

**MCU 处理逻辑**:

1. 检查 blockSequenceCounter 与期望值匹配
2. 将数据写入目标槽位 Flash
3. 更新内部 CRC32 计算
4. 递增期望的块序号 (0xFF → 0x00 → 0x01)
5. 返回正响应

**块序号规则**:

- 从 0x01 开始
- 递增至 0xFF 后回绕至 0x01
- 0x00 不用作正常序号
- 序号不匹配返回 NRC 0x73 (WrongBlockSequenceCounter)

#### 4.4.3 RequestTransferExit (0x37)

```
请求: 0x37
响应: 0x77
```

**MCU 处理逻辑**:

1. 计算最终 CRC32
2. 验证 CRC32 与镜像头中的值匹配
3. 验证 ECDSA 签名
4. 更新元数据 (标记槽位有效, 设置 pending_slot)
5. 设置 OTA 状态 = 待激活 (0x03)

### 4.5 RoutineControl (0x31)

#### 擦除例程 (0xFF00)

```
请求: 0x31 0x01 0xFF00
响应: 0x71 0x01 0xFF00 0x00
```

- 要求: Programming Session + SecurityAccess Level 1
- 行为: 擦除目标槽位 Flash 区域
- 超时: 可能需要数秒, 发送 NRC 0x78

#### 清除故障例程 (0x2100)

```
请求: 0x31 0x01 0x2100
响应: 0x71 0x01 0x2100 0x00
```

- 要求: Extended Session + SecurityAccess Level 1
- 行为: 清除锁存的故障码

### 4.6 DID 实现

| DID | 读/写 | 实现位置 | 说明 |
|-----|-------|----------|------|
| 0xF195 | 读 | Bootloader | 软件版本字符串 (SW_VERSION "1.0.0") |
| 0xF18D | 读 | Bootloader | Bootloader 版本 |
| 0x2010 | 读/写 | Bootloader | 固件类型选择 (0=app, 1=bootloader)，需安全解锁 |
| 0x2112 | 读 | APP/Bootloader | OTA 状态 |
| 0x2113 | 读 | APP/Bootloader | 活动槽位 |
| 0x2114 | 读 | APP/Bootloader | 待定槽位 |
| 0x2115 | 读 | Bootloader | 最近启动原因 |
| 0x2116 | 读 | APP/Bootloader | 回滚计数器 |

---

## 五、ECDSA P-256 集成

### 5.1 库选择

| 库 | Flash 占用 | RAM 占用 | 许可证 | 推荐 |
|----|-----------|----------|--------|------|
| micro-ecc | ~5KB | ~1KB | BSD | ✅ 推荐 |
| tinycrypt | ~8KB | ~2KB | BSD | 备选 |
| Mbed TLS (仅 ECDSA) | ~15KB | ~4KB | Apache 2.0 | 过大 |

### 5.2 密钥管理

**公钥存储方案**:

```
Bootloader 区域 (只读, 写保护)
┌─────────────────────────────────────┐
│ 0x0800_0000  启动代码               │
│ ...                                 │
│ .ecdsa_pubkey section:              │
│   65 字节非压缩 SEC1 公钥 (04‖X‖Y) │
│   4 字节 magic marker "KEYP"        │
│ ...                                 │
│ 0x0800_3FFF  Bootloader 结束        │
└─────────────────────────────────────┘
```

**密钥管理**:

- 测试密钥已生成，保存在 `docs/keys/` 目录：
  - `docs/keys/private.pem` — 测试私钥（Host 侧签名用）
  - `docs/keys/public.pem` — 测试公钥（编译进 Bootloader）
- 量产时需替换为正式密钥对，私钥由 CCU/上位机保管
- 公钥通过 `.ecdsa_pubkey` section 的 `__attribute__((section(...)))` 编译进 Bootloader，附带 magic marker 用于损坏检测

### 5.3 签名验证流程

```c
/**
 * @brief  验证 APP 镜像的 ECDSA P-256 签名
 * @param  slot: 槽位索引 (0=A, 1=B)
 * @retval true=验证通过, false=验证失败
 */
bool ota_verify_image_signature(uint8_t slot)
{
    uint32_t base_addr = (slot == 0) ? APP_A_BASE : APP_B_BASE;
    image_header_t *header = (image_header_t*)base_addr;
    
    /* 1. 检查魔数 */
    if (header->magic != IMAGE_MAGIC) {
        return false;
    }
    
    /* 2. 检查镜像长度 */
    if (header->image_length == 0 || header->image_length > slot_size - 256) {
        return false;
    }
    
    /* 3. CRC32 校验 */
    if (crc32_compute(image_data, header->image_length) != header->crc32) {
        return false;
    }
    
    /* 4. ECDSA P-256 签名验证 */
    uint8_t hash[32];
    sha256_hash(image_data, header->image_length, hash);
    
    const uint8_t *public_key = boot_verify_get_public_key();
    if (public_key == NULL) return false;
    
    return uECC_verify(public_key, hash, header->signature, uECC_secp256r1()) == 1;
}
```

### 5.4 性能预估

| 操作 | 预估耗时 | 备注 |
|------|----------|------|
| SHA-256 (48KB 镜像) | ~50ms | Cortex-M4F 硬件加速 |
| ECDSA P-256 验签 | ~200-800ms | micro-ecc, 软件实现 |
| **总计** | ~250-850ms | 超过 P2 (50ms), 需 NRC 0x78 |

---

## 六、试启动与回滚机制

### 6.1 状态流转

```
┌──────────┐    OTA 完成     ┌──────────┐   CCUReset    ┌──────────┐
│  空闲     │ ──────────────→│  待激活   │ ────────────→│  试启动   │
│ (idle)    │                │ (pending) │              │ (trial)   │
└──────────┘                └──────────┘              └─────┬────┘
                                                            │
                              ┌──────────────────────────────┤
                              │                              │
                        超时/失败                        确认成功
                              │                              │
                        ┌─────▼────┐                  ┌─────▼────┐
                        │  回滚     │                  │  已确认   │
                        │ (rollback)│                  │(confirmed)│
                        └──────────┘                  └──────────┘
```

### 6.2 试启动流程

```
1. Bootloader 检测到 trial_state == TRIAL_PENDING
2. 设置 trial_state = TRIAL_ACTIVE
3. 递增 trial_retry_count
4. 如果 trial_retry_count > trial_max_retries:
   - 执行回滚 (切换到之前的有效槽位)
   - 递增 rollback_count
   - 设置 last_boot_reason = OTA_ROLLBACK (0x04)
5. 跳转到试启动槽位的 APP
6. APP 启动后:
   - 初始化 CAN 通信
   - 初始化安全子系统
   - 初始化 NVM 访问
   - 发送 BOOTUP 广播
   - 在 10s 内通过 UDS 0x2E 写入确认标志
7. 如果 10s 内未收到确认:
   - 看门狗复位 → Bootloader 重新进入试启动流程
```

### 6.3 APP 侧确认机制

```c
/**
 * @brief  APP 启动后确认新镜像
 *         在核心初始化完成后、进入主循环前调用
 */
void ota_confirm_new_image(void)
{
    ota_metadata_t meta;
    ota_metadata_read(&meta);
    
    if (meta.trial_state == TRIAL_ACTIVE) {
        /* 确认新镜像 */
        meta.trial_state = TRIAL_CONFIRMED;
        meta.slot_a_valid = (meta.active_slot == 0) ? 1 : meta.slot_a_valid;
        meta.slot_b_valid = (meta.active_slot == 1) ? 1 : meta.slot_b_valid;
        meta.pending_slot = 0xFE;  /* 无待定 */
        meta.ota_state = 0x05;     /* 已确认 */
        ota_metadata_write(&meta);
    }
}
```

### 6.4 回滚流程

```c
/**
 * @brief  Bootloader 执行回滚
 */
static void boot_rollback(void)
{
    ota_metadata_t meta;
    ota_metadata_read(&meta);
    
    /* 确定回滚目标: 之前的有效槽位 */
    uint8_t rollback_slot;
    if (meta.trial_slot == 0 && meta.slot_b_valid) {
        rollback_slot = 1;  /* 回滚到 B */
    } else if (meta.trial_slot == 1 && meta.slot_a_valid) {
        rollback_slot = 0;  /* 回滚到 A */
    } else {
        /* 无有效回滚目标, 进入安全模式 */
        boot_enter_safe_mode();
        return;
    }
    
    /* 更新元数据 */
    meta.active_slot = rollback_slot;
    meta.pending_slot = 0xFE;
    meta.trial_state = TRIAL_IDLE;
    meta.trial_retry_count = 0;
    meta.rollback_count++;
    meta.last_boot_reason = 0x04;  /* OTA 回滚 */
    meta.ota_state = 0x06;         /* 已回滚 */
    ota_metadata_write(&meta);
    
    /* 跳转到回滚槽位 */
    uint32_t addr = (rollback_slot == 0) ? APP_A_BASE : APP_B_BASE;
    boot_jump_to_app(addr);
}
```

---

## 七、掉电恢复策略

### 7.1 各阶段掉电分析

| 阶段 | 掉电后果 | 恢复策略 |
|------|----------|----------|
| **RequestDownload 之前** | 无影响 | Bootloader 正常启动之前的镜像 |
| **Flash 擦除中** | 目标槽位可能部分擦除 | Bootloader 检测到 pending_slot + trial_state=IDLE → 标记目标槽位无效, 启动之前的有效槽位 |
| **TransferData 期间** | 目标槽位数据不完整 | Bootloader 检测到 trial_state=PENDING 但镜像 CRC 无效 → 标记槽位无效, 启动之前的有效槽位 |
| **元数据写入中** | 主元数据可能损坏 | Bootloader 读取备份元数据恢复 |
| **CCUReset 之后、APP 启动前** | 新镜像未执行 | Bootloader 正常进入试启动流程 |
| **试启动期间** | 新镜像部分执行 | 看门狗复位 → Bootloader 递增重试计数 → 超限则回滚 |
| **确认写入中** | 确认标志可能未写入 | 试启动超时 → 看门狗复位 → 重试 → 最终回滚 |

### 7.2 元数据损坏恢复

```c
/**
 * @brief  读取并验证 OTA 元数据 (含主备切换)
 */
bool ota_metadata_read(ota_metadata_t *meta)
{
    ota_metadata_t primary, backup;
    bool primary_valid, backup_valid;
    
    /* 读取主元数据 */
    flash_read(META_PRIMARY_BASE, &primary, sizeof(primary));
    primary_valid = (primary.magic == META_MAGIC) && 
                    (crc32_calc(&primary, sizeof(primary) - 4) == primary.crc32);
    
    /* 读取备份元数据 */
    flash_read(META_BACKUP_BASE, &backup, sizeof(backup));
    backup_valid = (backup.magic == META_MAGIC) && 
                   (crc32_calc(&backup, sizeof(backup) - 4) == backup.crc32);
    
    if (primary_valid) {
        memcpy(meta, &primary, sizeof(primary));
        if (!backup_valid) {
            /* 备份损坏, 修复 */
            ota_metadata_write_to_backup(&primary);
        }
        return true;
    }
    
    if (backup_valid) {
        /* 主元数据损坏, 从备份恢复 */
        memcpy(meta, &backup, sizeof(backup));
        ota_metadata_write_to_primary(&backup);
        return true;
    }
    
    /* 两者均损坏, 使用默认值 */
    memset(meta, 0, sizeof(ota_metadata_t));
    meta->magic = META_MAGIC;
    meta->version = 1;
    meta->active_slot = 0;
    meta->pending_slot = 0xFE;
    meta->slot_a_valid = 1;  /* 假设 A 是出厂固件 */
    meta->slot_b_valid = 0;
    meta->trial_state = TRIAL_IDLE;
    meta->last_boot_reason = 0xFF;  /* 未知 */
    return false;
}
```

---

## 八、CAN 驱动与 ISO-TP

### 8.1 CAN ID 配置

| CAN ID | 方向 | 用途 |
|--------|------|------|
| 0x18DA0D03 | CCU → MCU | UDS 请求 |
| 0x18DA030D | MCU → CCU | UDS 响应 |
| 0x18FF260D | MCU → 广播 | 生命周期状态 |

### 8.2 ISO-TP 参数

| 参数 | 值 |
|------|-----|
| 单帧载荷 | 0-7 字节 |
| 多帧载荷 | 8-4095 字节 |
| 块大小 (Block Size) | 0x00 (无限制) |
| STmin | 1ms |
| N_As / N_Ar / N_Bs / N_Cr | 1000ms |

### 8.3 Bootloader 中的 CAN 驱动

Bootloader 需要一个精简的 CAN + ISO-TP 驱动, 仅用于 OTA 下载:

```c
/* Bootloader CAN 驱动 (精简版) */
typedef struct {
    /* CAN 基础收发 */
    bool (*can_init)(uint32_t baudrate);
    bool (*can_send)(uint32_t id, uint8_t *data, uint8_t len);
    bool (*can_receive)(uint32_t *id, uint8_t *data, uint8_t *len, uint32_t timeout_ms);
    
    /* ISO-TP 传输 */
    bool (*isotp_send)(uint8_t *data, uint32_t len);
    bool (*isotp_receive)(uint8_t *buffer, uint32_t buffer_size, uint32_t *actual_len, uint32_t timeout_ms);
} boot_can_driver_t;
```

---

## 九、实现阶段划分

### 阶段 1: Bootloader 基础框架 (第 7 周, 20h)

| 任务 | 工时 | 产出 |
|------|------|------|
| Flash 布局定义与链接脚本 | 4h | linker.ld |
| Bootloader 启动代码 + 跳转逻辑 | 6h | boot_main.c |
| 元数据管理 (读/写/校验) | 6h | ota_metadata.c |
| Flash 读写驱动 (Bootloader 精简版) | 4h | boot_flash.c |

### 阶段 2: CAN/ISO-TP + UDS 框架 (第 7 周, 20h)

| 任务 | 工时 | 产出 |
|------|------|------|
| Bootloader 精简 CAN 驱动 | 4h | boot_can.c |
| ISO-TP 传输层 (单帧+多帧) | 6h | boot_isotp.c |
| UDS 服务框架 (0x10/0x11/0x22/0x27/0x3E) | 6h | boot_uds.c |
| 会话管理 + S3 超时 | 4h | boot_session.c |

### 阶段 3: ECDSA P-256 + 安全访问 (第 8 周, 16h) ✅ 已完成

| 任务 | 工时 | 产出 | 状态 |
|------|------|------|------|
| micro-ecc 库集成 | 4h | uECC.c/h | ✅ |
| SHA-256 实现 | 4h | sha256.c | ✅ |
| SecurityAccess (0x27) 分帧验签流程 | 6h | boot_safe_mode.c | ✅ |
| 公钥存储与管理 (.ecdsa_pubkey section) | 2h | boot_verify.c | ✅ |

### 阶段 4: OTA 下载流程 (第 8 周, 16h) ✅ 已完成

| 任务 | 工时 | 产出 | 状态 |
|------|------|------|------|
| RequestDownload (0x34) — 仅初始化下载状态 | 4h | boot_safe_mode.c | ✅ |
| TransferData (0x36) + 块序号管理 (& 0xFF 回绕) | 4h | boot_safe_mode.c | ✅ |
| RequestTransferExit (0x37) + 验证 | 4h | boot_safe_mode.c | ✅ |
| 擦除例程 (0x31 0xFF00) | 2h | boot_safe_mode.c | ✅ |
| DID 读写 (0xF195, 0x2010, 0x22/0x2E) | 2h | boot_safe_mode.c | ✅ |

### 阶段 5: 试启动与回滚 (第 9 周, 12h)

| 任务 | 工时 | 产出 |
|------|------|------|
| 试启动状态机 | 4h | boot_trial.c |
| 回滚逻辑 | 4h | boot_rollback.c |
| APP 侧确认接口 | 2h | ota_confirm.c |
| 掉电恢复测试 | 2h | 测试报告 |

### 阶段 6: APP 侧 UDS OTA 服务 (第 9 周, 12h)

| 任务 | 工时 | 产出 |
|------|------|------|
| APP 侧 UDS 服务集成 (0x34/0x36/0x37) | 6h | app_ota.c |
| APP 侧 DID 实现 (0x2112-0x2116) | 2h | app_did.c |
| APP 侧试启动确认 | 2h | app_trial.c |
| Programming Session 切换逻辑 | 2h | app_session.c |

### 阶段 7: 集成测试 (第 10 周, 12h)

| 任务 | 工时 | 产出 |
|------|------|------|
| OTA 上位机模拟器联调 (seed+0x5555) | 4h | 测试日志 |
| ECDSA P-256 完整流程验证 | 4h | 测试日志 |
| 掉电恢复全场景测试 | 2h | 测试报告 |
| 回滚机制验证 | 2h | 测试报告 |

**总计**: ~108h, 约 2.5 周 (可与阶段四/五并行推进)

---

## 十、风险与应对

| 风险 | 概率 | 影响 | 应对措施 |
|------|------|------|----------|
| 48KB APP 空间不足 | 中 | 高 | 优化代码体积 (-Os); 评估是否可压缩 Bootloader 至 8KB 释放空间 |
| ECDSA 验签超时 (200-800ms) | 高 | 中 | NRC 0x78 延迟响应, P2*=5000ms 足够 |
| 掉电导致元数据损坏 | 低 | 高 | 主备双份 + CRC32 校验, 两步写入 |
| Flash 写保护影响 APP 升级 | 低 | 中 | Bootloader 中临时解除写保护, 升级完成后重新启用 |
| micro-ecc 库兼容性 | 低 | 中 | 提前验证 AT32F426 编译兼容性 |

---

## 十一、交付物清单

| 编号 | 交付物 | 格式 | 说明 |
|------|--------|------|------|
| D-06 | Bootloader 源代码 | C/H | 含 Makefile/链接脚本 |
| D-07 | Flash 布局文档 | MD | 本文档 §2 |
| D-13 | OTA 升级包生成工具 | Python | 签名 + 打包脚本 |
| D-14 | 量产烧录指南 | MD | 公钥注入 + 写保护配置 |
| D-15 | OTA 测试报告 | MD | 含掉电/回滚测试结果 |

---

## 十二、变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| V1.0 | 2026-08-15 | 初稿 | Mr.Hu |
| V1.1 | 2026-08-20 | 对齐 SRS v1.1：SecurityAccess 改为 ECDSA P-256 分帧验签；0x34 不再执行擦除（由 0x31 独立完成）；新增 0x22/0x2E 服务；DID 0xF189→0xF195；公钥存储改用 .ecdsa_pubkey section + magic marker；阶段 3/4 标记为已完成 | Mr.Hu |
