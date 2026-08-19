# Qi 无线充电 MCU OTA 功能专项审查报告

**审查日期**: 2026-08-19  
**审查范围**: MCU (AT32F426) OTA 升级完整流程  
**审查目标**: 确保 OTA 升级流程能够完整走通  

---

## 一、OTA 流程逐环节审查

### 环节 1: OTA 触发入口 (APP → Bootloader)

**文件**: `can_protocol.c`, `ota_trigger.c`, `ota_trigger.h`

**流程**: Host 发送 ECUReset(0x11) → APP 响应 → `ota_trigger_request()` → 写 metadata → `NVIC_SystemReset()`

**审查结论**: ✅ 流程正确

- `can_protocol.c` 正确处理 0x11 SID，先发正响应再调用 `ota_trigger_request()`
- `ota_trigger_request()` 读取当前 metadata，设置 `ota_state = OTA_STATE_DOWNLOADING`，写入 Flash 后调用 `NVIC_SystemReset()`
- 有 ~2ms 延迟确保 CAN 响应发出后再复位

🟢 **优化建议**: ECUReset 可以携带 bootMode 参数区分"重启到 Bootloader"和"普通重启"，当前代码对任何 ECUReset 都触发 OTA，可能误触发。

---

### 环节 2: Bootloader 启动与 Safe Mode 判断

**文件**: `bootloader/mdk_user/Src/main.c`

**流程**: `boot_metadata_init()` → `detect_boot_reason()` → `process_trial_state()` → 检查 `ota_state` → `enter_safe_mode()`

**审查结论**: ✅ 流程正确

- Step 5.5 检查 `g_meta.ota_state == OTA_STATE_DOWNLOADING`，清除状态后进入 Safe Mode
- 先清除 ota_state 再保存，避免重启循环
- Safe Mode 不返回（无限循环 CAN 事件循环）

🟡 **可靠性问题**: `boot_metadata_init()` 在 primary 和 backup 都无效时，写默认值到 primary。但 APP 侧的 `ota_trigger_request()` 调用 `ota_metadata_read()` 在双区都无效时只填充默认值不写 Flash。如果首次 OTA 前 primary 区被意外擦除，Bootloader 会写默认值，但 APP 侧读到的默认值可能与 Bootloader 写入的不同步（中间有一次复位）。实际影响较小，因为 Bootloader 先运行并写入了默认值。

---

### 环节 3: Safe Mode UDS 命令处理

**文件**: `boot_safe_mode.c`

#### 3.1 SecurityAccess (0x27)

**审查结论**: 🟡 功能可用但安全性弱

- Seed 生成使用 LFSR + SysTick，熵源有限
- Key 验证使用确定性混合函数：`expected = s * 0x45D9F3BU; expected ^= expected >> 16; expected *= 0x45D9F3BU;`
- 代码注释承认这是占位实现："For full ECDSA-based security, this should verify an ECDSA signature"
- 有 3 次失败锁定 + 60 秒锁定期，基本防护到位

🟡 **安全问题**: 混合函数是确定性的，攻击者逆向固件后可从 seed 计算出 expected key。生产环境应替换为 ECDSA 签名验证。

#### 3.2 RequestDownload (0x34)

**审查结论**: 🔴 发现阻断问题

- 安全门控正确：要求 `g_security_unlocked` 才允许下载
- Flash 擦除正确：擦除整个 APP_A 区域 (0x08004000 ~ 0x0800FFFF, 48KB, 24 个 2KB 扇区)
- 写入起始地址正确：`g_dl_write_addr = APP_A_BASE_ADDR + IMAGE_HEADER_SIZE = 0x08004100`
- 响应中返回 `maxNumberOfBlockLength = APP_A_SIZE (0xC000 = 48KB)`

🔴 **阻断问题 #1: 下载目标固定为 APP_A**

```c
/* erase APP_A flash area */
for (sector_addr = APP_A_BASE_ADDR; ...)
```

**TransferData 也固定写入 APP_A**。这意味着：
- 如果当前 active_slot = A（正在运行 APP_A），OTA 会覆盖当前运行的固件
- 无法下载到 APP_B 实现真正的 A/B 无缝切换
- 设计文档描述的"双槽 A/B 设计"在当前实现中只有单槽写入能力

**影响**: OTA 可以走通（写入 APP_A 后切换 active_slot=A），但无法实现 A/B 无缝升级。如果当前运行的就是 APP_A，OTA 过程中如果断电，APP_A 被擦除但新固件未写完，设备将进入 Safe Mode（Bootloader 发现两个槽都无效），无法回滚到旧固件。

#### 3.3 TransferData (0x36)

**审查结论**: ✅ 流程基本正确，有小问题

- Block sequence 验证正确：`g_dl_block_seq` 从 0 开始，每帧 +1
- 安全门控正确
- Flash 写入逻辑正确：4 字节对齐写入，不足 4 字节的尾部填充 0xFF
- 数据长度检查正确：`g_dl_bytes_written + data_len > MAX_IMAGE_SIZE` 时拒绝
- 每帧有效载荷 = CAN 帧长度 - 2 (SID + blockSeq) = 最多 6 字节

🟡 **可靠性问题 #2: 无写入后校验**

每帧写入 Flash 后没有 readback 验证。虽然 AT32F426 的 Flash 编程一般可靠，但在恶劣环境下（EMI、电源波动）可能出现静默写入错误。建议至少对关键帧（最后一帧）做 readback。

🟡 **可靠性问题 #3: 48KB 固件需 ~8192 帧**

以 250kbps CAN、每帧 6 字节有效载荷计算：
- 48KB / 6 = 8192 帧
- 每帧 CAN 传输时间 ≈ 0.5ms（含帧间隔）
- 总传输时间 ≈ 4 秒
- 看门狗超时 ~1 秒，Safe Mode 主循环中有 `wdg_drv_refresh()`，不会触发 WDG 复位 ✅

#### 3.4 TransferExit (0x37)

**审查结论**: 🔴 发现阻断问题

- CRC32 计算正确：`boot_crc32(image_data_addr, g_dl_bytes_written)`
- Image Header 写入正确：magic = IMAGE_MAGIC (0x4F544158), image_length, crc32
- Readback 验证正确：逐字比较写入的 header
- Metadata 更新正确：slot_a_valid=1, trial_state=PENDING, trial_slot=A

🔴 **阻断问题 #3 (核心): Image Header 缺少 ECDSA 签名**

```c
/* prepare and write image header */
memset((void *)&header, 0xFF, sizeof(image_header_t));
header.magic        = IMAGE_MAGIC;
header.image_length = g_dl_bytes_written;
header.crc32        = computed_crc;
// ← 没有写入 header.signature[64]
```

`image_header_t` 结构体定义（256 字节）：
```c
typedef struct {
  uint32_t magic;           // offset 0,  4 bytes
  uint32_t image_length;    // offset 4,  4 bytes
  uint32_t crc32;           // offset 8,  4 bytes
  uint8_t  signature[64];   // offset 12, 64 bytes  ← 全为 0xFF
  char     version[16];     // offset 76, 16 bytes
  uint32_t build_timestamp; // offset 92, 4 bytes
  uint8_t  reserved[160];   // offset 96, 160 bytes
} image_header_t;
```

TransferExit 只写了前 12 字节 (magic + length + crc32)，signature 字段全为 0xFF（Flash 擦除后的值）。

**后果链**：
1. TransferExit 完成，metadata 写入 `trial_state=PENDING`
2. MCU 复位，Bootloader 启动
3. `process_trial_state()` 将 trial_state 从 PENDING → ACTIVE
4. `try_boot_slot()` 调用 `boot_verify_image()`
5. `boot_verify_image()` 检查 magic ✅，检查 length ✅，检查 CRC32 ✅
6. **ECDSA 验签：`uECC_verify()` 用全 0xFF 的签名验证 SHA-256 哈希 → 必然失败**
7. 返回 -1，`try_boot_slot()` 失败
8. 尝试另一个槽（如果也失败）→ 进入 Safe Mode
9. **OTA 升级永远无法成功启动新固件**

---

### 环节 4: CAN 通信层

**文件**: `bootloader/mdk_can/can_driver.c`, `code/mdk_can/can_driver.c`

**审查结论**: ✅ 实现正确

- Bootloader 和 APP 的 CAN 驱动代码完全一致
- 250kbps，Extended Frame (29-bit ID)
- 软件 RX FIFO (深度 16) + ISR 写入 + 主循环读取
- 接受过滤器 mask=0（接受所有 Extended 帧），由上层协议按 ID 过滤
- Bus-off 自动恢复

🟡 **可靠性问题 #4**: Bootloader `can_driver_poll()` 使用 `__disable_irq()` 保护整个 FIFO 读取+移动序列；APP 版本只在 `rx_fifo_count--` 时禁中断。两版本不一致，Bootloader 版本更安全但禁中断时间更长。建议统一。

---

### 环节 5: Flash 操作

**文件**: `nvm_drv.c`（Bootloader 和 APP 版本一致）

**审查结论**: ✅ 实现正确

- `flash_word_program()` 逐字写入，4 字节对齐
- 擦除前 flash_unlock()，完成后 flash_lock()
- NVM 驱动的 read-modify-write 逻辑正确

🟡 **可靠性问题 #5: NVM 与 Metadata 地址冲突**

```
NVM_CONFIG_BASE_ADDR   = 0x0801E000  (nvm_drv.h)
OTA_META_BACKUP_ADDR   = 0x0801E000  (ota_trigger.h / boot_metadata.h)
```

NVM 配置区和备份 Metadata 共享同一 Flash 区域。代码注释已标注此冲突，且 `boot_metadata_save()` 和 `ota_metadata_save()` 都只写 primary 区 (0x0801C000)。但 `boot_metadata_init()` 会尝试读取 backup 区作为恢复源，如果 NVM 驱动覆盖了 backup 区的内容，恢复功能失效。

---

### 环节 6: 重启与 Trial Boot

**文件**: `boot_trial.c`, `boot_trial.h`

**审查结论**: ✅ 流程正确

**Trial Boot 状态机**：
```
PENDING → ACTIVE → CONFIRMED
   ↓         ↓
  (首次启动)  (超时/重试失败 → 回滚)
```

- `process_trial_state()`: PENDING → ACTIVE（增加 retry_count）
- `select_boot_slot()`: PENDING 时选择 trial_slot，其他状态选择 active_slot
- `try_boot_slot()`: 验证镜像 → 跳转
- APP 侧 `ota_confirm_if_needed()`: 检测 trial_state=ACTIVE → 设置 CONFIRMED
- 下次启动 `process_trial_state()`: CONFIRMED → IDLE

🟡 **可靠性问题 #6: Trial 超时回滚逻辑**

`trial_timer_callback()` 中：
```c
if (g_meta.trial_state == TRIAL_STATE_ACTIVE &&
    g_trial_elapsed_sec >= (uint32_t)g_meta.trial_timeout_sec)
{
    g_meta.trial_retry_count++;
    g_meta.trial_state = TRIAL_STATE_IDLE;
    g_meta.active_slot = (g_meta.trial_slot == SLOT_A) ? SLOT_B : SLOT_A;
    boot_metadata_save(&g_meta);
    while (1) { /* 等待 WDG 复位 */ }
}
```

问题：
1. 回滚到"另一个槽"，但如果另一个槽的镜像也无效（slot_x_valid=0），回滚后 `try_boot_slot()` 会失败，最终进入 Safe Mode。这是安全的降级行为，但不是"回滚"。
2. `process_trial_state()` 中 ACTIVE 状态检查 `trial_retry_count > trial_max_retries` 才回滚，但 `trial_timer_callback()` 中每次超时都回滚。两个回滚路径可能冲突：timer 回滚设置 IDLE + 切换槽，但 `process_trial_state()` 不会再触发回滚（因为已经是 IDLE）。逻辑上可行但不够清晰。

---

### 环节 7: ECDSA 验签

**文件**: `boot_verify.c`, `sha256.c`, `uECC.c`

**审查结论**: ✅ 验签代码实现正确，但无法通过（因为签名未写入）

验签流程：
1. 读取 pre-provisioned 公钥从 `BOOT_ECDSA_PUBLIC_KEY_ADDR = 0x08003C00`（Bootloader 区域内）
2. SHA-256 哈希镜像数据（不含 header）
3. `uECC_verify()` 验证 ECDSA P-256 签名

`boot_verify_image()` 的 4 项检查：
1. ✅ magic == IMAGE_MAGIC
2. ✅ image_length 在 slot 范围内
3. ✅ CRC32 匹配
4. ❌ **ECDSA 签名验证必然失败**（签名字段为 0xFF）

---

### 环节 8: APP 跳转

**文件**: `boot_jump.c`

**审查结论**: ✅ 实现正确且完善

- 禁中断 → 重置 CAN1 → 禁 SysTick → 清 NVIC → 设 VTOR → 设 MSP → 跳转
- 检查 reset handler 地址有效性（非 0x00000000 / 0xFFFFFFFF）
- 设置 Thumb 位 (|= 1U)
- 跳转前重新使能中断（让 APP 的中断正常工作）

---

### 环节 9: 看门狗

**文件**: `wdg_drv.c`

**审查结论**: ✅ 实现正确

- IWDG 超时 ~1 秒（LSI 40kHz / 128 = 312.5Hz, reload=312 → 998.4ms）
- Safe Mode 主循环中定期喂狗
- Trial Boot 定时器 1000ms 周期 ≈ WDG 超时，边界情况可能触发意外复位

🟡 **可靠性问题 #7**: Trial Timer (1000ms) 和 WDG (~998ms) 几乎同步。如果 timer_poll() + wdg_drv_refresh() 的执行时间导致 WDG 未及时喂狗，可能在 Trial 确认前触发 WDG 复位。建议将 trial timer 周期设为 2000ms 或增大 WDG 超时。

---

## 二、问题汇总

| # | 严重性 | 环节 | 文件 | 问题描述 | 修复建议 |
|---|--------|------|------|----------|----------|
| 1 | 🔴 阻断 | TransferExit | `boot_safe_mode.c` | Image Header 未写入 ECDSA 签名字段，boot_verify 验签必然失败，OTA 无法成功 | 方案A: TransferExit 接收签名数据写入 header; 方案B: boot_verify 跳过验签（仅 CRC32）; 方案C: 签名预烧录到固定 Flash 地址 |
| 2 | 🔴 阻断 | 设计 | 整体 | 无签名数据来源：CAN 单帧 8 字节无法传输 64 字节签名，也无预烧录机制 | 需要设计签名传输方案（ISO-TP 多帧 / 预烧录 / 分段传输） |
| 3 | 🟡 可靠性 | RequestDownload | `boot_safe_mode.c` | 下载固定写入 APP_A，无法实现 A/B 无缝切换 | 根据 pending_slot 选择写入目标 |
| 4 | 🟡 安全 | SecurityAccess | `boot_safe_mode.c` | Key 验证使用确定性混合函数，非密码学安全 | 生产环境替换为 ECDSA 或 HMAC |
| 5 | 🟡 可靠性 | Trial Boot | `boot_trial.c` | Trial Timer (1000ms) ≈ WDG (998ms)，边界情况可能误触发 WDG 复位 | 增大 WDG 超时或减小 trial timer 频率 |
| 6 | 🟡 可靠性 | Metadata | `boot_metadata.c` | backup 区与 NVM 配置区地址冲突，备份恢复功能可能失效 | 分离 backup 区和 NVM 区，或移除 backup 依赖 |
| 7 | 🟡 可靠性 | TransferData | `boot_safe_mode.c` | Flash 写入无 readback 校验 | 至少对最后一帧做 readback |
| 8 | 🟢 优化 | ECUReset | `can_protocol.c` | 任何 ECUReset 都触发 OTA，无法区分普通重启 | 增加 bootMode 参数判断 |
| 9 | 🟢 优化 | CAN 驱动 | 两版本 `can_driver.c` | FIFO 读取的临界区保护策略不一致 | 统一为 Bootloader 版本（全程禁中断） |

---

## 三、OTA 可行性评估

### 结论: 🔴 当前无法走通完整 OTA 流程

**阻断原因**:

OTA 流程在 **TransferExit → 重启 → Trial Boot → 验签** 这一步断裂：

1. TransferExit 正确计算了 CRC32 并写入 Image Header
2. 但 **ECDSA P-256 签名字段 (64 字节) 全为 0xFF**
3. Bootloader 的 `boot_verify_image()` 在 CRC32 校验通过后执行 ECDSA 验签
4. `uECC_verify()` 用全 0xFF 的签名验证 SHA-256 哈希 → **必然返回 0 (失败)**
5. `try_boot_slot()` 返回 -1 → 尝试另一个槽 → 也失败 → **进入 Safe Mode**
6. **设备永远卡在 Safe Mode，无法启动新固件**

### 修复路径（按优先级）

**方案 A（推荐，最小改动）**:
在 `boot_safe_mode.c` 的 TransferExit 处理中，将签名数据写入 header：
- 需要 ISO-TP 多帧支持传输 64 字节签名
- 或：在 TransferData 的最后一帧附加签名（需要修改协议）

**方案 B（快速验证）**:
临时跳过 `boot_verify_image()` 中的 ECDSA 验签：
```c
// boot_verify.c - 临时注释掉验签
// if (verify_result != 1) { return -1; }
```
- 仅保留 magic + length + CRC32 验证
- 适合开发调试阶段，不适合生产

**方案 C（预烧录方案）**:
- 签名在编译时嵌入固件（作为 const 数组）
- TransferExit 从下载的固件数据中提取签名写入 header
- 需要修改固件格式，将签名放在固定偏移

---

## 四、风险点列表

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| 签名缺失导致 OTA 失败 | 100% | 阻断 | 实施方案 A/B/C |
| SecurityAccess 被逆向 | 中 | 设备被非法升级 | 替换为 ECDSA 验签 |
| OTA 中途断电（单槽写入） | 低 | 设备进入 Safe Mode | 实现 A/B 双槽写入 |
| Flash 写入静默错误 | 极低 | 固件损坏 | 增加 readback 校验 |
| WDG 误触发 Trial 回滚 | 低 | 误回滚 | 调整 WDG/Timer 参数 |
| NVM 覆盖 Backup Metadata | 低 | 备份恢复失效 | 分离地址空间 |

---

**审查人**: Evaluator Agent  
**审查文件数**: 20+ 源文件  
**审查置信度**: 95%（基于代码静态分析，未运行实际测试）
