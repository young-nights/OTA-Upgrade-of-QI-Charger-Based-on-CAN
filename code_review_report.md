# Qi 无线充电 Bootloader + APP 代码审查报告

> **审查日期**: 2026-08-19  
> **审查范围**: Bootloader (`qi_wireless_bootloader/`) + APP (`qi_wireless_code/`) 全部源码  
> **审查依据**: 项目需求文档、工作流文档、bootloader-flow 文档、IAP 协议文档  

---

## 审查问题汇总

| 等级 | 数量 | 说明 |
|------|------|------|
| 🔴 严重 | 6 | 可能导致安全漏洞、启动失败、数据损坏 |
| 🟡 中等 | 8 | 影响可靠性、可维护性或存在潜在风险 |
| 🟢 建议 | 6 | 代码质量改进、最佳实践 |

---

## 🔴 严重问题

### 🔴-1: SecurityAccess (0x27) 未做任何校验 — 安全后门

**位置**: `qi_wireless_bootloader/mdk_app/Src/boot_safe_mode.c:105-110`

**问题描述**: Bootloader Safe Mode 的 `0x27 SecurityAccess` 处理直接接受任意 seed/key，不做任何验证即返回正响应。任何知道 CAN ID 的人都可以通过发送 `0x27` 命令解锁安全访问，进而通过 `0x34/0x36/0x37` 写入任意固件。

```c
case UDS_SECURITY_ACCESS:
  /* SecurityAccess: accept any seed/key for bootloader (simplified) */
  resp[0] = service_id + UDS_POSITIVE_RESPONSE_OFFSET;
  resp[1] = data[1];
  safe_mode_send_response(resp, 2);
  break;
```

**修复建议**: 实现真正的 ECDSA P-256 安全访问流程：
1. 收到 seed 请求时生成随机 seed 并返回
2. 收到 key 请求时验证 ECDSA 签名（用预置公钥验证 seed 的签名）
3. 连续 3 次失败后锁定 60 秒
4. 或至少在量产版本中移除此服务，仅依赖镜像签名验证

---

### 🔴-2: uECC.c 模约减借用检测逻辑错误 — ECDSA 验签可能产生错误结果

**位置**: `qi_wireless_bootloader/mdk_app/Src/uECC.c` (`vli_mod_mult` 函数，约第 330-370 行)

**问题描述**: 模约减中的借用检测使用 `if (diff > 0xFFFFFFFFULL)` 来判断 64 位减法是否发生下溢，但 `diff` 是 `ecc_dword_t`（`uint64_t`）类型。当发生下溢时，值会回绕到一个**极大的正数**（如 `0xFFFFFFFFxxxxxxxx`），这个条件**永远为 true**（因为任何非零值都 > 0xFFFFFFFF）。然而，即使没有下溢，如果 `diff` 的高 32 位非零（合法的中间结果），该条件也会误判为借用。

```c
ecc_dword_t diff = (ecc_dword_t)accum[i + 6] - high[i] - borrow;
accum[i + 6] = (ecc_word_t)diff;
borrow = (diff > 0xFFFFFFFFULL) ? 1 : 0;  // 逻辑错误
```

**影响**: 模约减（P-256 快速约减）可能产生错误的中间结果，导致 ECDSA 签名验证产生**假阳性**（接受无效签名）或**假阴性**（拒绝有效签名）。这是密码学实现中的关键缺陷。

**修复建议**: 正确的借用检测应为：
```c
borrow = (diff >> 63) & 1;  // 检查最高位（符号位）
// 或者：
ecc_dword_t diff = (ecc_dword_t)accum[i + 6] - high[i] - borrow;
accum[i + 6] = (ecc_word_t)diff;
borrow = (accum[i + 6] > (ecc_dword_t)accum[i + 6]) ? 1 : 0; // 不对
// 最佳方案：使用更大的中间类型或重构减法逻辑
```

---

### 🔴-3: CAN RX FIFO 帧拷贝与 ISR 存在竞态条件

**位置**: `qi_wireless_bootloader/mdk_can/Src/can_driver.c:203-220` 及 `qi_wireless_code/mdk_can/Src/can_driver.c:203-220`（两处相同）

**问题描述**: `can_driver_poll()` 中从 FIFO 拷贝帧数据时，先读取 `rx_fifo[rx_fifo_tail]` 的 id/len/data，然后才禁用中断递减 `rx_fifo_count`。在读取数据和禁用中断之间，如果 CAN RX 中断触发且 FIFO 满，ISR 不会覆盖数据（因为 `rx_fifo_is_full()` 检查 count）。但如果 count=1 且 tail 位置的帧正在被读取，而 ISR 在 `rx_fifo_count--` 之前触发，ISR 会将新帧写入 `rx_fifo[rx_fifo_head]`（head 可能等于 tail+1），不会覆盖正在读取的帧。

**更严重的问题**: `rx_fifo_count` 的读取（`while (rx_fifo_count > 0)`）和帧数据拷贝之间没有原子保护。如果主循环读取 count=1 开始拷贝，但 ISR 在拷贝过程中将 count 增加到 2（新帧到达），拷贝完成后主循环递减 count 到 1（而不是 0），导致下次循环再次处理，但 tail 已经前进，可能跳过新帧。

**修复建议**: 在整个帧拷贝+tail推进+count递减操作期间禁用中断：
```c
__disable_irq();
frame.id = rx_fifo[rx_fifo_tail].id;
// ... copy data ...
rx_fifo_tail = (rx_fifo_tail + 1) % CAN_DRIVER_RX_FIFO_SIZE;
rx_fifo_count--;
__enable_irq();
```

---

### 🔴-4: Bootloader 未初始化 CAN 即尝试跳转 — 潜在外设状态污染

**位置**: `qi_wireless_bootloader/mdk_user/Src/main.c:45-85`

**问题描述**: Bootloader 的正常启动路径（非 Safe Mode）不调用 `can_driver_init()`，但也不调用任何外设反初始化。如果上一次运行（APP 或 Safe Mode）使能了 CAN 中断，Bootloader 跳转到 APP 时虽然在 `boot_jump_to_app()` 中清除了 NVIC，但 CAN 外设本身仍处于使能状态。这可能导致 APP 启动后立即收到 CAN 中断（如果 RX 缓冲区有残留数据），而 APP 的 CAN 驱动尚未初始化。

**修复建议**: 在 `boot_jump_to_app()` 中增加外设反初始化：
```c
// 在 __disable_irq() 之后，清除 CAN 外设
can_reset(CAN1);  // 复位 CAN 外设到默认状态
// 或者在跳转前调用各外设的 deinit
```

---

### 🔴-5: Metadata 备份区与 NVM 配置区地址冲突 — 备份机制失效

**位置**: `qi_wireless_bootloader/mdk_app/Inc/boot_metadata.h:36` 及 `qi_wireless_bootloader/mdk_app/Inc/nvm_drv.h:42`

**问题描述**: 
- `META_BACKUP_ADDR = 0x0801E000`（metadata 备份区）
- `NVM_CONFIG_BASE_ADDR = 0x0801E000`（NVM 配置区）

两个模块共享同一物理地址。`boot_metadata_save()` 只写主区（0x0801C000），不写备份区，以避免破坏 NVM 配置。但 `nvm_drv_write()` 在写入 NVM 配置时会擦除 0x0801E000 扇区，**彻底销毁 metadata 备份**。

如果此时主区 metadata 也损坏（如掉电），`boot_metadata_init()` 将无法从备份区恢复，只能使用默认值，导致 OTA 状态丢失。

**修复建议**: 
1. 方案 A：将 metadata 备份区移到其他地址（如 0x0801C000 之前的扇区，但空间不足）
2. 方案 B：在 NVM 驱动中增加 metadata 备份保护（写 NVM 前先读取 metadata 备份，写完后恢复）
3. 方案 C：接受单副本设计，但在文档中明确标注风险

---

### 🔴-6: uECC 标量乘法使用简单 double-and-add — 时序侧信道攻击

**位置**: `qi_wireless_bootloader/mdk_app/Src/uECC.c` (`point_mult` 函数，约第 680 行)

**问题描述**: 标量乘法 `point_mult()` 使用简单的 double-and-add 算法，从最高位到最低位遍历标量 k 的每一位。当 bit 为 1 时执行 `point_add()`，为 0 时只执行 `point_double()`。这两种操作的执行时间不同，攻击者可以通过测量 ECDSA 验签时间推断标量（签名的 r/s 值），从而恢复私钥。

```c
for (i = NUM_ECC_WORDS - 1; i >= 0; i--) {
    for (j = 31; j >= 0; j--) {
        point_double(&r, &r);
        if ((k[i] >> j) & 1) {
            point_add(&r, &r, p);  // 条件执行，泄露时序信息
        }
    }
}
```

**影响**: 在物理可接触的车载环境中，时序攻击可提取签名私钥，进而伪造固件签名。

**修复建议**: 使用 Montgomery ladder 或固定时间的 double-and-add-always 算法：
```c
// Double-and-add-always: 每次迭代都执行 add，只是 add 的目标不同
ecc_point_t r0, r1;
memset(&r0, 0, sizeof(r0));  // infinity
r1 = *p;
for (i = NUM_ECC_WORDS - 1; i >= 0; i--) {
    for (j = 31; j >= 0; j--) {
        int bit = (k[i] >> j) & 1;
        // 无条件执行两个操作
        point_add(&r0, &r0, &r1);  // 或 point_double
        point_double(&r1, &r1);    // 或 point_add
        // 通过条件交换选择正确的结果
        swap_points_if(&r0, &r1, bit);
    }
}
```

---

## 🟡 中等问题

### 🟡-1: Qi UART FIFO 读取与 ISR 竞态

**位置**: `qi_wireless_code/mdk_app/Src/qi_uart.c:155-165`

**问题描述**: `qi_uart_rx_read()` 中先读取 `rx_buf[rx_tail]`，再禁用中断递减 `rx_count`。如果在读取和禁用中断之间，USART2 RX 中断触发并写入新数据到 `rx_buf[rx_head]`（此时 head 可能等于 tail），可能读到不一致的数据。

**修复建议**: 在整个读取操作期间禁用中断。

---

### 🟡-2: Trial 超时计时器未消费 — 回滚仅依赖看门狗

**位置**: `qi_wireless_bootloader/mdk_app/Src/boot_trial.c:70-80`

**问题描述**: `trial_timer_callback()` 每秒递增 `g_trial_elapsed_sec` 并设置 `g_trial_timer_flag`，但代码中没有任何逻辑检查 `trial_timeout_sec (10)` 并执行超时回滚。Trial 回滚完全依赖看门狗复位 + `retry_count > max_retries` 路径。

如果新固件在 `trial_timeout_sec` 内没有崩溃但功能异常（如不确认 CONFIRMED），系统会一直以 ACTIVE 状态运行，不会自动回滚。

**修复建议**: 在 `main.c` 的主循环或 `trial_timer_callback` 中增加超时检查：
```c
if (g_trial_timer_flag) {
    g_trial_timer_flag = 0;
    if (g_trial_elapsed_sec >= g_meta.trial_timeout_sec) {
        // 超时未确认，触发回滚
        // ...
    }
}
```

---

### 🟡-3: Bootloader 0x36 Flash 写入地址可能非 4 字节对齐

**位置**: `qi_wireless_bootloader/mdk_app/Src/boot_safe_mode.c:155-185`

**问题描述**: `g_dl_write_addr` 初始值为 `APP_A_BASE_ADDR + IMAGE_HEADER_SIZE`（0x08004100，4 字节对齐），但每次写入后 `g_dl_write_addr += data_len`。如果主机每帧发送的 `data_len` 不是 4 的倍数（如 3 或 5 字节），后续帧的 `g_dl_write_addr` 将不再 4 字节对齐，`flash_word_program()` 可能写入错误地址或产生硬件错误。

**修复建议**: 
1. 在文档中强制要求主机每帧发送 4 字节倍数的载荷
2. 或在代码中增加对齐检查：`if (g_dl_write_addr % 4 != 0) return NRC;`

---

### 🟡-4: APP 端 0x34 响应 maxNumberOfBlockLength 值误导

**位置**: `qi_wireless_code/mdk_app/Src/can_protocol.c:95-100`

**问题描述**: APP 端对 `0x34 RequestDownload` 的正响应中 `maxNumberOfBlockLength = 0x0010`（16 字节），但实际下载由 Bootloader Safe Mode 接管，Bootloader 响应的值为 `0xC000`（48KB）。如果主机错误地使用 APP 的响应值作为传输约束，会导致传输失败。

**修复建议**: 
1. APP 端不响应 0x34（直接回 NRC 0x11 "service not supported"），因为 APP 本身不处理下载
2. 或在响应中使用与 Bootloader 一致的值

---

### 🟡-5: 0x37 处理中 Image Header 写入未校验

**位置**: `qi_wireless_bootloader/mdk_app/Src/boot_safe_mode.c:205-220`

**问题描述**: `0x37 RequestTransferExit` 处理中写入 Image Header（256 字节 = 64 word）后，未进行读回校验。如果 Flash 写入部分成功（某些 word 写入失败但 `flash_word_program` 未返回错误），Header 中的 magic/length/crc32 可能不一致，导致下次启动时镜像校验失败。

**修复建议**: 写入后增加读回校验循环：
```c
for (w = 0; w < hdr_word_count; w++) {
    uint32_t readback = *(volatile uint32_t *)(APP_A_BASE_ADDR + (w * 4U));
    if (readback != hdr_words[w]) {
        // 写入失败处理
    }
}
```

---

### 🟡-6: `vli_mod_mult` NIST P-256 约减实现过于复杂且可能不正确

**位置**: `qi_wireless_bootloader/mdk_app/Src/uECC.c` (`vli_mod_mult` 函数)

**问题描述**: P-256 快速约减的实现经历了多次迭代（代码注释中可见），最终使用 `T = low + high * K` 方法，但 borrow 传播逻辑存在缺陷（见 🔴-2）。整个函数约 200 行，包含大量注释掉的代码和尝试性实现，说明开发者对约减算法不够确定。

**修复建议**: 使用经过验证的 micro-ecc 库原版实现，或使用 NIST SP 800-186 标准的 P-256 约减公式，替换当前实现。

---

### 🟡-7: Bootloader 和 APP 的 `can_driver.c` 完全相同但独立维护

**位置**: `qi_wireless_bootloader/mdk_can/Src/can_driver.c` 及 `qi_wireless_code/mdk_can/Src/can_driver.c`

**问题描述**: 两个目录下的 `can_driver.c` 和 `can_driver.h` 内容完全相同，但独立维护。如果一个工程修改了 CAN 驱动而忘记同步另一个，会导致行为不一致。

**修复建议**: 将共享驱动代码提取到公共目录，两个工程通过 include path 引用同一份源码。

---

### 🟡-8: 异常处理函数（HardFault 等）死循环不喂狗

**位置**: `qi_wireless_bootloader/mdk_user/Src/at32f422_426_int.c:55-85` 及 APP 同名文件

**问题描述**: `HardFault_Handler`、`MemManage_Handler`、`BusFault_Handler`、`UsageFault_Handler` 均为 `while(1)` 死循环，不喂狗。IWDG 会在 ~1 秒后复位 MCU，但复位后如果异常仍然存在（如栈指针损坏），会进入无限复位循环。

**修复建议**: 
1. 在死循环中喂狗并设置一个标志（如写入特定寄存器或 SRAM 位置）记录故障信息
2. 或在死循环中尝试安全关机（禁用充电、关闭功率输出等）

---

## 🟢 建议

### 🟢-1: `select_boot_slot` 中 PENDING 分支实际不可达

**位置**: `qi_wireless_bootloader/mdk_app/Src/boot_trial.c:105-115`

**问题描述**: `select_boot_slot()` 检查 `trial_state == TRIAL_STATE_PENDING` 来选择 trial_slot，但在此之前 `process_trial_state()` 已经将 PENDING 转换为 ACTIVE。该分支永远不会被执行。

**建议**: 虽然不是 bug（OTA 写入时已设置 `active_slot = trial_slot`），但代码逻辑令人困惑。建议移除不可达分支或添加注释说明。

---

### 🟢-2: `timer_tick_inc` 在 ISR 中遍历所有定时器

**位置**: `qi_wireless_bootloader/mdk_app/Src/timer_drv.c:125-140`

**问题描述**: `timer_tick_inc()` 在 SysTick 中断中遍历全部 16 个定时器槽位。虽然单次遍历开销很小（~100 个时钟周期），但作为 ISR 应尽量精简。

**建议**: 如果定时器数量较多，可使用链表只跟踪活跃定时器。

---

### 🟢-3: SHA-256 使用栈上 256 字节 W 数组

**位置**: `qi_wireless_bootloader/mdk_app/Src/sha256.c:70`

**问题描述**: `sha256_transform()` 在栈上分配 `uint32_t W[64]`（256 字节）。AT32F426 只有 20KB SRAM，在 Bootloader 中（可能栈空间有限）需注意栈溢出风险。

**建议**: 确认链接脚本中分配的栈大小足够（建议 ≥ 2KB），或将 W 数组改为 static（但需注意重入性）。

---

### 🟢-4: `can_driver_send` 非线程安全

**位置**: `qi_wireless_bootloader/mdk_can/Src/can_driver.c:130-180` 及 APP 同名文件

**问题描述**: `can_driver_send()` 检查 TX 缓冲区状态、写入数据、触发发送，整个过程不是原子的。如果在主循环和中断上下文中同时调用（虽然当前代码中 ISR 不调用 send），可能导致 TX 缓冲区状态不一致。

**建议**: 当前架构下（只有主循环调用 send）问题不大，但建议在函数注释中标注"仅从主循环调用"。

---

### 🟢-5: `uECC_verify` 中 r/s 范围检查不完整

**位置**: `qi_wireless_bootloader/mdk_app/Src/uECC.c` (`uECC_verify` 函数)

**问题描述**: 范围检查 `if (!vli_cmp(curve_n, r) || !vli_cmp(curve_n, s))` 使用 `vli_cmp`（a >= b 返回 1），当 `r == curve_n` 时 `vli_cmp(curve_n, r)` 返回 1（相等），条件 `!vli_cmp(curve_n, r)` 为 0，不会拒绝。但 ECDSA 要求 r 和 s 在 `[1, n-1]` 范围内，`r == n` 应该被拒绝。

**建议**: 改为 `if (vli_cmp(r, curve_n) || vli_cmp(s, curve_n))`（r >= n 则拒绝）。

---

### 🟢-6: 元数据结构体中 `padding[488]` 占用大量空间

**位置**: `qi_wireless_bootloader/mdk_app/Inc/boot_metadata.h:85`

**问题描述**: `ota_metadata_t` 为 512 字节，其中有效字段仅 24 字节，`padding[488]` 占 95%。每次 `boot_metadata_save()` 都要擦除 8KB 扇区并写入 512 字节（128 word），Flash 写入量较大。

**建议**: 如果不需要 512 字节对齐（如为了匹配 Flash 页大小），可考虑减小结构体大小以减少 Flash 磨损。

---

## 代码质量评分

| 维度 | 得分 | 说明 |
|------|------|------|
| 正确性 | 18/25 | uECC 约减逻辑错误(🔴-2)、FIFO 竞态(🔴-3)影响核心功能 |
| 健壮性 | 14/20 | 错误处理整体良好，但安全访问缺失(🔴-1)、备份区冲突(🔴-5) |
| 可维护性 | 16/20 | 代码结构清晰、注释完整，但 uECC.c 代码混乱 |
| 性能 | 12/15 | 单帧 UDS 吞吐低（已知限制），SHA-256 栈使用合理 |
| 安全性 | 5/10 | SecurityAccess 后门(🔴-1)、时序侧信道(🔴-6)、ECDSA 实现缺陷(🔴-2) |
| 规范性 | 8/10 | 命名规范、风格统一，部分函数过长 |

**总分: 73/100**

---

## 关键风险点

1. **安全访问完全缺失** (🔴-1): 任何人均可通过 CAN 写入任意固件，车载环境下风险极高
2. **ECDSA 实现存在数学错误** (🔴-2): 模约减借用检测错误可能导致验签结果不可靠
3. **FIFO 竞态条件** (🔴-3): 高 CAN 负载下可能丢失帧或处理错误数据
4. **备份区地址冲突** (🔴-5): metadata 备份机制在 NVM 写入后实际失效
5. **时序侧信道** (🔴-6): 物理接触场景下可提取签名私钥

## 整体评价

代码架构设计合理，双槽 A/B + Trial Boot + Safe Mode 的 OTA 方案完整度较高。主要问题集中在：
- **安全实现不完整**: ECDSA 验签虽已集成但存在数学错误，SecurityAccess 为占位实现
- **并发安全不足**: CAN/UART 的 FIFO 读写存在竞态条件
- **Flash 布局冲突**: metadata 备份区与 NVM 配置区地址重叠

建议优先修复 🔴-1（安全访问）和 🔴-2（uECC 约减），这两个问题直接影响系统的安全性和正确性。
