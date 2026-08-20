# QI Wireless Bootloader — 细节流程文档

> **项目**: OTA-Upgrade-of-QI-Charger-Based-on-CAN / qi_wireless_bootloader
> **MCU**: AT32F426 (Cortex-M4F, 128KB Flash)
> **源码入口**: `mdk_user/Src/main.c`

---

## 一、总览：Bootloader 做了什么

Bootloader 在每次上电/复位时运行，**不直接运行应用固件**，而是先判断"该跑哪个固件、固件有没有问题、需不需要回滚"，然后才跳转。

整个生命周期只有两个阶段：
1. **决策阶段** — 在 bootloader 中执行（毫秒级）
2. **运行阶段** — 跳转到应用固件，bootloader 不再参与

```
上电 → 读元数据 → 判断状态 → 选slot → 校验镜像 → 跳转
                                                    ↓
                                              应用固件运行
                                                    ↓
                                              应用固件主动上报"我OK" → 元数据更新为 CONFIRMED
```

---

## 二、详细启动流程（按执行顺序）

### Step 1: 硬件初始化

```c
system_clock_config();    // 配置系统时钟 180MHz
nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
timer_drv_init();         // 软件定时器
wdg_drv_init();           // 看门狗 ~1000ms 超时
```

看门狗一旦启动就**无法关闭**，后续必须定期喂狗，否则 MCU 复位。

### Step 2: 读取 OTA 元数据 (`boot_metadata_init`)

```c
boot_metadata_init(&g_meta);
```

**读取顺序**:
```
① 读 Primary (0x0801C000)
   → 校验 magic == "MATO" && version == 1 && CRC32 正确
   → 通过 → 直接使用

② Primary 校验失败 → 读 Backup (0x0801E000)
   → 通过 → 用 Backup 数据恢复 Primary，然后使用

③ 两个都失败 → 填充默认值
   → active_slot = A, trial_state = IDLE
   → 写入双副本
```

**关键点**：这是"强一致"设计 — 只要元数据没被彻底破坏（两个副本全毁），就能正常启动。

### Step 3: 检测启动原因

```c
g_meta.last_boot_reason = detect_boot_reason();
```

| 原因 | 代码 | 触发条件 |
|------|------|----------|
| 上电复位 | `0x00` | 正常上电/外部复位 |
| 看门狗复位 | `0x02` | RCC 寄存器中有 WDT_RESET_FLAG |
| OTA 激活 | `0x03` | 新固件首次试运行 |
| 回滚 | `0x04` | 试运行失败，回退到旧固件 |

### Step 4: 处理试运行状态机 (`process_trial_state`)

**这是整个 OTA 的核心决策逻辑。** 每次上电都会执行一次。

```
                    ┌──────────────────────────────────┐
                    │       process_trial_state()       │
                    │     每次上电只执行一次              │
                    └──────────────────────────────────┘
                                  │
                    ┌─────────────┼─────────────┐
                    ▼             ▼             ▼
              trial_state    trial_state    trial_state
               == IDLE       == PENDING     == ACTIVE
                    │             │             │
                    ▼             ▼             ▼
                 什么都不做    PENDING→ACTIVE   检查重试次数
                              retry_count++        │
                              保存元数据      ┌────┴────┐
                                              ▼         ▼
                                        count > max   count ≤ max
                                              │         │
                                              ▼         ▼
                                         执行回滚     什么都不做
                                         切回旧slot   (等应用确认)
                                         保存元数据
```

#### 4a: IDLE → 什么都不做

正常状态，没有 OTA 在进行。

#### 4b: PENDING → 变成 ACTIVE（真正开始试运行）

**触发条件**：上一次 OTA 写入新固件后，将 `trial_state` 设为 `PENDING`，`trial_slot` 设为新固件的 slot。

**执行动作**:
```c
meta->trial_state       = TRIAL_STATE_ACTIVE;   // PENDING → ACTIVE
meta->trial_retry_count++;                       // 重试计数 +1
meta->last_boot_reason  = BOOT_REASON_OTA_ACT;   // 标记为 OTA 激活
boot_metadata_save(meta);                        // 写入 Flash
```

**为什么这里就变成 ACTIVE？** 因为 PENDING 只是一个"请求"，真正开始试运行是在 bootloader 启动时。一旦进入 ACTIVE，就意味着"这次启动就是试运行"。

#### 4c: ACTIVE → 检查重试次数，超限就回滚

**这是关键的安全机制。**

```c
case TRIAL_STATE_ACTIVE:
  if (meta->trial_retry_count > meta->trial_max_retries)
  {
    // 超限 → 回滚
    meta->rollback_count++;
    meta->trial_state       = TRIAL_STATE_IDLE;   // 清除试运行状态
    meta->trial_retry_count = 0;                   // 重置计数
    meta->last_boot_reason  = BOOT_REASON_ROLLBACK;

    // 切到另一个 slot
    other_slot = (meta->trial_slot == SLOT_A) ? SLOT_B : SLOT_A;
    if (另一个 slot 镜像有效)
      meta->active_slot = other_slot;

    boot_metadata_save(meta);
  }
```

**回滚触发条件**：`trial_retry_count > trial_max_retries`（默认 > 3，即第 4 次启动时触发）

**回滚做了什么**：
1. `rollback_count++` — 记录回滚次数
2. `trial_state = IDLE` — 结束试运行
3. `trial_retry_count = 0` — 重置计数
4. `active_slot = 另一个 slot` — 切回旧固件
5. `last_boot_reason = ROLLBACK` — 标记回滚原因

**问题：新固件为什么会反复重启？**
- 新固件有 bug，运行中触发了看门狗 → MCU 复位 → bootloader 再次启动 → retry_count++
- 新固件卡死 → 看门狗超时 → 复位 → retry_count++
- 新固件主动调用 `NVIC_SystemReset()` → 复位 → retry_count++

**每次复位都会重新进入 `process_trial_state`**，所以 retry_count 会持续累积，直到超过阈值触发回滚。

#### 4d: CONFIRMED → 清理状态，新固件正式上线

**触发条件**：应用固件运行成功后，通过 CAN 上报确认，bootloader 将 `trial_state` 设为 `CONFIRMED`。下次启动时执行清理。

```c
case TRIAL_STATE_CONFIRMED:
  meta->trial_state       = TRIAL_STATE_IDLE;   // 清除试运行状态
  meta->trial_retry_count = 0;                   // 重置计数
  boot_metadata_save(meta);
```

**CONFIRMED 之后发生了什么**：
- `trial_state` 回到 IDLE → 后续启动不再进入试运行逻辑
- `trial_retry_count` 清零 → 重置重试计数
- `active_slot` 保持不变 → 新固件成为正式活跃固件
- `slot_x_valid` 已经在 `try_boot_slot` 中被设为 1

**这就是"A/B 双 slot 升级"的完整闭环**：写新固件 → 试运行 → 确认 → 正式切换。

### Step 5: 选择启动 Slot (`select_boot_slot`)

```c
static int8_t select_boot_slot(const ota_metadata_t *meta, uint8_t *slot)
{
  if (meta->trial_state == TRIAL_STATE_PENDING)
  {
    *slot = meta->trial_slot;    // 试运行 → 用试运行 slot
    return 0;
  }
  *slot = meta->active_slot;     // 正常 → 用活跃 slot
  return 0;
}
```

**注意**：此时 `trial_state` 已经被 Step 4 从 PENDING 改成了 ACTIVE，所以这里 `trial_state == PENDING` 的分支**永远不会被命中**。

这意味着 `select_boot_slot` 的实际行为是：**永远用 `active_slot`**。

**那试运行 slot 怎么被选中？** 在 Step 4b 中，PENDING 变成 ACTIVE 时，`trial_slot` 并没有被写入 `active_slot`。所以如果 `trial_slot != active_slot`，新固件**不会被启动**。

**这看起来是个 bug**，但实际上不是：OTA 写入新固件时会同时设置 `active_slot = trial_slot`，所以两者是相等的。

### Step 6: 启动试运行定时器（仅 ACTIVE 状态）

```c
if (g_meta.trial_state == TRIAL_STATE_ACTIVE)
{
  trial_tmr_id = timer_create(1000, trial_timer_callback, 1);
  timer_start(trial_tmr_id);
}
```

**定时器做什么**：每秒触发一次，递增 `g_trial_elapsed_sec`，设置 `g_trial_timer_flag`。

**但代码中没有使用这两个变量**。`g_trial_elapsed_sec` 和 `g_trial_timer_flag` 被设置但从未被读取。这是一个**预留接口**，用于未来实现"试运行超时"逻辑 — 如果应用固件在 N 秒内没有确认，自动回滚。

当前实现中，试运行的"超时"完全依赖**看门狗复位**：如果新固件卡死，看门狗会复位 MCU，retry_count++，最终触发回滚。

### Step 7: 校验镜像并跳转 (`try_boot_slot`)

```c
boot_result = try_boot_slot(boot_slot, &g_meta);
```

**校验流程** (`boot_verify_image`):
```
① 检查 image_header.magic == "XATO" (0x4F544158)
   → 失败 → 镜像无效

② 检查 image_length 在合法范围 (0 < length ≤ slot_size - 256)
   → 失败 → 镜像无效

③ 对 header 之后的 image_length 字节计算 CRC32
   → 与 header->crc32 比对
   → 不一致 → 镜像无效

④ ECDSA P-256 签名验证
   → 从 `.ecdsa_pubkey` section 读取 65 字节非压缩公钥
   → 计算 SHA256(image_data)
   → 调用 uECC_verify() 验证签名
   → 验签失败 → 镜像无效

⑤ 全部通过 → 镜像有效
```

**跳转流程** (`boot_jump_to_app`):
```
① 关中断 (__disable_irq)
② 关 SysTick
③ 清除所有 NVIC 挂起中断
④ 设置 VTOR = slot_base + 0x100 (跳过 image_header)
⑤ 设置 MSP = 应用固件向量表[0]
⑥ 跳转到 应用固件向量表[1] (reset handler)
⑦ 重新开中断 (__enable_irq)
```

**跳转前会更新 slot 有效性标志**：
```c
if (*valid_flag == 0)
{
  *valid_flag = 1;              // 标记 slot 有效
  boot_metadata_save(meta);     // 写入 Flash
}
boot_jump_to_app(slot_addr + IMAGE_HEADER_SIZE);  // 跳转，不返回
```

### Step 8: 主 Slot 失败 → 尝试备用 Slot

```c
if (boot_result != 0)
{
  other_slot = (boot_slot == SLOT_A) ? SLOT_B : SLOT_A;
  boot_result = try_boot_slot(other_slot, &g_meta);

  if (boot_result == 0)
  {
    g_meta.active_slot = other_slot;    // 更新活跃 slot
    boot_metadata_save(&g_meta);
  }
}
```

**这是"最后一道防线"**：如果选中的 slot 镜像校验失败（比如写入过程中断电），自动尝试另一个 slot。

### Step 9: 两个 Slot 都失败 → 进入安全模式

```c
if (boot_result != 0)
{
  enter_safe_mode();    // 不返回
}
```

**安全模式做什么**：
1. 初始化 CAN 驱动（250kbps）
2. 注册 UDS 服务回调
3. 进入死循环：`timer_poll() → can_driver_poll() → wdg_drv_refresh()`

**安全模式支持的 UDS 服务**：

| 服务 | SID | 功能 |
|------|-----|------|
| DiagnosticSessionCtrl | 0x10 | 切换诊断会话 |
| SecurityAccess | 0x27 | 安全访问（ECDSA P-256，子功能 0x01/0x03/0x02） |
| ReadDataByIdentifier | 0x22 | 读取 DID（0xF195 软件版本） |
| WriteDataByIdentifier | 0x2E | 写入 DID（0x2010 固件类型选择） |
| RoutineControl | 0x31 | 例程控制（0xFF00 擦除内存） |
| RequestDownload | 0x34 | 请求下载（初始化下载状态） |
| TransferData | 0x36 | 传输数据块 |
| TransferSignature | 0x38 | 传输固件签名 |
| RequestTransferExit | 0x37 | 传输结束（CRC32 + 写 header） |
| ECUReset | 0x11 | ECU 复位 |

**安全模式是"砖机恢复"机制**：即使两个 slot 的固件都损坏，也能通过 CAN 重新烧录。

---

## 三、OTA 升级完整流程（从写入到确认）

```
┌─────────────────────────────────────────────────────────────────────┐
│                        OTA 升级完整流程                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  [当前状态] active_slot=A, trial_state=IDLE                         │
│  [正在运行] Slot A 的固件                                            │
│                                                                     │
│  ① 通过 CAN (UDS) 下载新固件到 Slot B                               │
│     → 写入 image_header + 固件数据到 0x08010000                      │
│     → 设置 metadata:                                                │
│        active_slot    = B    ← 切换活跃 slot                         │
│        trial_slot     = B    ← 标记试运行 slot                       │
│        trial_state    = PENDING  ← 请求试运行                        │
│        slot_b_valid   = 0    ← 新固件还没验证过                       │
│     → 保存 metadata → 触发复位 (ECU Reset)                          │
│                                                                     │
│  ② 复位后 bootloader 启动                                           │
│     → boot_metadata_init: 读取 metadata                             │
│     → process_trial_state: PENDING → ACTIVE, retry_count=1          │
│     → select_boot_slot: active_slot=B → 选中 Slot B                 │
│     → try_boot_slot: 校验 Slot B 镜像                               │
│        → 校验通过 → slot_b_valid=1, 保存, 跳转到 Slot B              │
│        → 校验失败 → 尝试 Slot A → 跳转到 Slot A                     │
│                                                                     │
│  ③ Slot B 的固件开始运行                                            │
│     → 固件执行自检 (通信/外设/业务逻辑)                              │
│     → 自检通过 → 通过 CAN 上报确认                                  │
│        → bootloader 将 trial_state = CONFIRMED                      │
│        → 下次启动时清理状态: IDLE, retry_count=0                     │
│                                                                     │
│  ④ 如果 Slot B 固件有问题 (看门狗复位)                               │
│     → 复位后 retry_count++                                          │
│     → 重复 ② 的流程，每次 retry_count+1                              │
│     → 第 4 次启动时 retry_count(4) > max_retries(3)                  │
│     → 触发回滚: active_slot=A, trial_state=IDLE                      │
│     → Slot A 的旧固件恢复运行                                        │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 四、关键时序图

### 4.1 正常 OTA 升级成功

```
时间轴 ─────────────────────────────────────────────────────→

Bootloader          CAN总线             应用固件(Slot B)
    │                    │                    │
    │  ① 读 metadata     │                    │
    │  PENDING → ACTIVE  │                    │
    │  retry_count = 1   │                    │
    │                    │                    │
    │  ② 校验 Slot B     │                    │
    │  通过 → 跳转 ─────────────────────────→ │
    │                    │                    │ ③ 运行
    │                    │                    │  自检通过
    │                    │  ← 上报确认 ─────── │
    │  ④ 写 CONFIRMED    │                    │
    │                    │                    │
    │  [下次启动]         │                    │
    │  CONFIRMED → IDLE  │                    │
    │  retry_count = 0   │                    │
    │  → 正常启动 Slot B  │                    │
```

### 4.2 OTA 升级失败，自动回滚

```
时间轴 ─────────────────────────────────────────────────────→

Bootloader          看门狗              应用固件(Slot B)
    │                    │                    │
    │  第1次启动          │                    │
    │  PENDING→ACTIVE    │                    │
    │  retry_count = 1   │                    │
    │  校验通过 → 跳转 ─────────────────────→ │
    │                    │                    │ 卡死/崩溃
    │                    │  超时复位 ←─────────│
    │                    │                    │
    │  第2次启动          │                    │
    │  ACTIVE            │                    │
    │  retry_count=2 ≤ 3 │                    │
    │  校验通过 → 跳转 ─────────────────────→ │
    │                    │                    │ 又卡死
    │                    │  超时复位 ←─────────│
    │                    │                    │
    │  第3次启动          │                    │
    │  retry_count=3 ≤ 3 │                    │
    │  → 跳转 ────────────────────────────→  │
    │                    │                    │ 又卡死
    │                    │  超时复位 ←─────────│
    │                    │                    │
    │  第4次启动          │                    │
    │  retry_count=4 > 3 │                    │
    │  → 触发回滚!        │                    │
    │  active_slot = A   │                    │
    │  trial_state = IDLE│                    │
    │  → 跳转 Slot A ─────────────────────→  │ (旧固件)
```

### 4.3 两个 Slot 都损坏，进入安全模式

```
时间轴 ─────────────────────────────────────────────────────→

Bootloader
    │
    │  读 metadata → 有效
    │  选中 Slot A
    │  校验 Slot A → 失败 (magic 错误/CRC 不匹配)
    │  尝试 Slot B
    │  校验 Slot B → 也失败
    │  → 进入安全模式
    │
    │  初始化 CAN (250kbps)
    │  进入 UDS 服务循环
    │  等待外部工具通过 CAN 烧录新固件...
```

---

## 五、metadata 字段在流程中的变化

以下表格展示一次**成功 OTA** 过程中 metadata 关键字段的变化：

| 阶段 | active_slot | trial_state | trial_slot | retry_count | slot_a_valid | slot_b_valid | last_boot_reason |
|------|-------------|-------------|------------|-------------|--------------|--------------|------------------|
| OTA 前 (Slot A 运行) | A(0) | IDLE(0) | A(0) | 0 | 1 | 0 | POWER_ON(0x00) |
| 写入新固件到 B，触发 OTA | B(1) | PENDING(1) | B(1) | 0 | 1 | 0 | POWER_ON(0x00) |
| 第 1 次启动 (PENDING→ACTIVE) | B(1) | ACTIVE(2) | B(1) | 1 | 1 | 0→1 | OTA_ACT(0x03) |
| Slot B 运行成功，上报确认 | B(1) | CONFIRMED(3) | B(1) | 1 | 1 | 1 | OTA_ACT(0x03) |
| 下次启动 (CONFIRMED→IDLE) | B(1) | IDLE(0) | B(1) | 0 | 1 | 1 | OTA_ACT(0x03) |

以下表格展示一次**失败 OTA（回滚）** 过程中 metadata 关键字段的变化：

| 阶段 | active_slot | trial_state | retry_count | last_boot_reason |
|------|-------------|-------------|-------------|------------------|
| OTA 前 | A(0) | IDLE(0) | 0 | POWER_ON(0x00) |
| 写入新固件到 B | B(1) | PENDING(1) | 0 | POWER_ON(0x00) |
| 第 1 次启动 | B(1) | ACTIVE(2) | 1 | OTA_ACT(0x03) |
| Slot B 崩溃，看门狗复位 | | | | |
| 第 2 次启动 | B(1) | ACTIVE(2) | 2 | OTA_ACT(0x03) |
| Slot B 崩溃，看门狗复位 | | | | |
| 第 3 次启动 | B(1) | ACTIVE(2) | 3 | OTA_ACT(0x03) |
| Slot B 崩溃，看门狗复位 | | | | |
| 第 4 次启动 (retry>max) | A(0) | IDLE(0) | 0 | ROLLBACK(0x04) |
| → 回滚到 Slot A 运行 | A(0) | IDLE(0) | 0 | ROLLBACK(0x04) |

---

## 六、已知限制与注意事项

1. **试运行超时未实现**：`g_trial_elapsed_sec` 和 `g_trial_timer_flag` 被设置但未被使用，超时机制完全依赖看门狗复位。

2. **CONFIRMED 的触发方不在 bootloader 中**：bootloader 本身不会主动设置 `CONFIRMED`，需要应用固件通过 CAN 通信告知 bootloader（或 bootloader 的安全模式 UDS 服务）来设置。

3. **retry_count 的累加时机**：每次 bootloader 启动时在 `process_trial_state` 中累加，而不是在应用固件崩溃时。这意味着如果应用固件正常运行很久后崩溃，retry_count 从上次的值继续累加。

4. **SecurityAccess 采用 ECDSA P-256 分帧验签**：`SecurityAccess`（0x27）通过子功能 0x01/0x03/0x02 三步完成验签。Host 先请求 seed（4B），然后通过 0x03 分帧传输 64B ECDSA 签名（每帧 6B，共约 11 帧），最后 0x02 触发 MCU 验签（SHA256(seed) + uECC_verify）。3 次失败后锁定 60 秒。

---

## 七、变更记录

| 版本 | 日期 | 改动说明 |
|------|------|----------|
| v1.0 | 2026-08-17 | 初始版本：基于源码梳理 bootloader 细节流程、试运行状态机、OTA 完整流程 |
| v1.1 | 2026-08-20 | 对齐 SRS v1.1：ECDSA P-256 签名验证已实现（非占位）；SecurityAccess 改为分帧验签；新增 0x22/0x2E/0x31 服务；OTA 流程顺序更新 |
