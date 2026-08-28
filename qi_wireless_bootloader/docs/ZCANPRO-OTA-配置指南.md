# ZCANPRO OTA 配置指南

> **适用项目**：Qi 无线充电器 Bootloader OTA  
> **芯片平台**：AT32F426 (Cortex-M4F, 128KB Flash, 20KB SRAM)  
> **文档版本**：V1.0  
> **创建日期**：2026-08-27

---

## 一、Safe Mode 进入原因分析

`main.c` 启动流程中，以下 4 个条件会触发 `enter_safe_mode()`：

| # | 条件 | 场景 |
|---|------|------|
| 1 | `g_meta.ota_state == OTA_STATE_DOWNLOADING` | 上次 OTA 下载中途断电/复位，metadata 未清除 |
| 2 | `select_boot_slot()` 返回 -1 | metadata 无效（全新芯片/Flash 被擦） |
| 3 | `try_boot_slot(boot_slot)` 与 `try_boot_slot(other_slot)` 均失败 | 两个 slot 都没有合法镜像 |
| 4 | `boot_metadata_init()` 使用默认值 → `slot_a_valid=0, slot_b_valid=0` | 空白芯片首次上电 |

**常见原因**：全新芯片或 metadata 区被擦过，两个 slot 均无合法镜像，`boot_verify_image()` 的 magic 检查失败，最终进入 safe_mode。这是正常行为，safe_mode 就是为此设计的——等待上位机通过 CAN 进行 OTA 下载。

---

## 二、Flash 布局

```
0x08000000 ┌──────────────────┐ 20KB
           │   Bootloader     │
0x08005000 ├──────────────────┤ 46KB (0xB800)
           │   APP_A Slot     │ ← image_header_t(256B) + firmware
0x08010800 ├──────────────────┤ 46KB (0xB800)
           │   APP_B Slot     │ ← image_header_t(256B) + firmware
0x0801C000 ├──────────────────┤ 2KB
           │   Metadata 主区   │
0x0801C800 ├──────────────────┤ 2KB
           │   Metadata 备份   │
0x0801D000 ├──────────────────┤
           │   NVM / 其他      │
0x08020000 └──────────────────┘ 128KB Flash 末尾
```

### 镜像头结构 `image_header_t`（256 字节）

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0x00 | 4 | magic | `0x4F544158` ("XATO") |
| 0x04 | 4 | image_length | 固件数据长度（不含 header） |
| 0x08 | 4 | crc32 | 固件数据的 CRC32 |
| 0x0C | 64 | signature | ECDSA P-256 R‖S 签名 |
| 0x4C | 16 | version | "MAJOR.MINOR.PATCH\0" |
| 0x5C | 4 | build_timestamp | Unix 时间戳 |
| 0x60 | 160 | reserved | 填充到 256 字节 |

---

## 三、ZCANPRO 基础通道配置

| 参数 | 值 |
|------|-----|
| CAN 类型 | **CAN 2.0B（扩展帧，29-bit ID）** |
| 波特率 | **250 kbps** |
| 发送 ID（ZCANPRO → MCU） | **0x18DA0D03** |
| 接收 ID（MCU → ZCANPRO） | **0x18DA030D** |

---

## 四、ISO-TP 帧格式速查

ZCANPRO 需要手动组 ISO-TP 帧（不自动做 ISO-TP 分段），格式如下：

### 单帧 SF（UDS 数据 ≤ 7 字节）

```
Byte0: [0x0|len]    ← PCI类型=0x00, 低4位=数据长度
Byte1..ByteN: UDS 数据
ByteN+1..Byte7: 0x00 填充
```

### 首帧 FF（UDS 数据 > 7 字节）

```
Byte0: [0x1|len_hi] ← PCI类型=0x10, 低4位=长度高4位
Byte1: len_lo       ← 长度低8位
Byte2..Byte7: UDS 数据前 6 字节
```

### 连续帧 CF

```
Byte0: [0x2|SN]     ← PCI类型=0x20, 低4位=序列号
Byte1..Byte7: UDS 数据续传
```

SN 从 1 开始，每帧 +1，0x0F 后回到 0x01。

### 流控帧 FC（MCU → ZCANPRO）

```
Byte0: [0x30|FS]    ← FS=0x00(CTS), 0x01(Wait), 0x02(Overflow)
Byte1: BS           ← Block Size（0x00=无限制）
Byte2: STmin        ← 连续帧间隔（0x01=1ms）
Byte3..Byte7: 0xCC 填充
```

---

## 五、UDS 指令序列（逐条配置）

### Step 1: 切换到编程会话

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `02 10 02 00 00 00 00 00` |
| 解析 | `02`=SF长度2, `10`=DiagnosticSessionControl, `02`=ProgrammingSession |
| 预期正响应 | `02 50 02 00 00 00 00 00` |
| 发送后等待 | **100ms** |

### Step 2: SecurityAccess - Request Seed (0x27 0x01)

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `02 27 01 00 00 00 00 00` |
| 解析 | `02`=SF长度2, `27`=SecurityAccess, `01`=RequestSeed |
| 预期正响应 | `06 67 01 [seed0] [seed1] [seed2] [seed3]` |
| 发送后等待 | **50ms** |

### Step 3: SecurityAccess - Transfer Signature (0x27 0x03)

ECDSA P-256 签名 64 字节，每帧最多传 6 字节签名数据，共 11 帧。

**帧格式**：`[0x07, 0x27, 0x03, blockSeq, sig0..sig5]`

| 帧号 | blockSeq | 数据 |
|------|----------|------|
| 1 | 0x01 | `07 27 03 01 [sig0..sig5]` |
| 2 | 0x02 | `07 27 03 02 [sig6..sig11]` |
| 3 | 0x03 | `07 27 03 03 [sig12..sig17]` |
| 4 | 0x04 | `07 27 03 04 [sig18..sig23]` |
| 5 | 0x05 | `07 27 03 05 [sig24..sig29]` |
| 6 | 0x06 | `07 27 03 06 [sig30..sig35]` |
| 7 | 0x07 | `07 27 03 07 [sig36..sig41]` |
| 8 | 0x08 | `07 27 03 08 [sig42..sig47]` |
| 9 | 0x09 | `07 27 03 09 [sig48..sig53]` |
| 10 | 0x0A | `07 27 03 0A [sig54..sig59]` |
| 11 | 0x0B | `07 27 03 0B [sig60..sig63] 00 00` |

**每帧间隔**：收到正响应 `03 67 03 [blockSeq]` 后等待 **10~20ms** 再发下一帧。

### Step 4: SecurityAccess - Verify Signature (0x27 0x02)

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `02 27 02 00 00 00 00 00` |
| 解析 | `02`=SF长度2, `27`=SecurityAccess, `02`=SendKey/Verify |
| 预期 NRC 0x78 | `03 7F 27 78 00 00 00 00`（ResponsePending，非错误） |
| 预期正响应 | `02 67 02 00 00 00 00 00` |
| 等待时间 | **200~800ms**（ECDSA 验签耗时） |

### Step 5: 选择固件类型 (0x2E 0x2010)

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `04 2E 20 10 01 00 00 00` |
| 解析 | `04`=SF长度4, `2E`=WriteDataByIdentifier, `2010`=DID, `01`=APP |
| 预期正响应 | `03 6E 20 10 00 00 00 00` |
| 发送后等待 | **100ms** |

### Step 6: 擦除 Flash (0x31 0x01 0xFF00)

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `04 31 01 FF 00 00 00 00` |
| 解析 | `04`=SF长度4, `31`=RoutineControl, `01`=startRoutine, `FF00`=erase |
| 预期 NRC 0x78 | `03 7F 31 78 00 00 00 00`（正在擦除，耐心等待） |
| 预期正响应 | `04 71 01 FF 00 00 00 00` |
| 等待时间 | **1~2 秒**（Flash 擦除耗时） |

### Step 7: 请求下载 (0x34)

UDS 数据 13 字节，需 ISO-TP 首帧 + 连续帧。

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| FF 数据 | `10 0D 34 00 44 08 00 50` |
| CF_1 数据 | `21 00 00 00 [size0] [size1] [size2] [size3] CC` |
| 解析 | `34`=RequestDownload, `00`=dataFormatIdentifier, `44`=ALFID(4字节地址+4字节长度), `08005000`=APP_A基址 |
| 预期正响应 | `04 74 20 01 00 00 00 00`（maxBlockLen=256） |
| 发送后等待 | **100ms** |

**size 计算**：打包后文件总长（**含** 256 字节 header），以大端序填入，且 ≤ 槽大小 `0xB800`。示例：40KB 固件 + 256B 头 = `0x0000A100`。

### Step 8: TransferData (0x36) — 分块传输

每个 TransferData 块的 UDS 数据格式：`[0x36, blockSeq, data...]`

**UDS 数据最大 256 字节** → ISO-TP 需要 FF + 多个 CF。

| 参数 | 值 |
|------|-----|
| blockSeq 起始 | 0x01 |
| blockSeq 递增 | +1/块，0x00 跳过（0xFF 下一个是 0x01） |
| ISO-TP CF 间隔 | **1ms**（MCU FC 的 STmin=0x01） |
| 块间间隔 | 收到正响应 `02 76 [BS]` 后 **10ms** |

**示例：256 字节数据块的 ISO-TP 帧**

```
FF:  10 [len_hi] [len_lo] 36 [BS] [data0..data4]
FC:  30 00 01 CC CC CC CC CC   ← MCU 回复的流控
CF:  21 [data5..data11]
CF:  22 [data12..data18]
CF:  23 [data19..data25]
...
CF:  2x [剩余数据]
```

### Step 9: RequestTransferExit (0x37)

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `01 37 00 00 00 00 00 00` |
| 解析 | `01`=SF长度1, `37`=RequestTransferExit |
| 预期 NRC 0x78 | `03 7F 37 78 00 00 00 00`（正在验证镜像） |
| 预期正响应 | `01 77 00 00 00 00 00 00` |
| 等待时间 | **100~500ms**（镜像验证 + ECDSA 签名校验） |

### Step 10: ECUReset (0x11)

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `02 11 01 00 00 00 00 00` |
| 解析 | `02`=SF长度2, `11`=ECUReset, `01`=hardReset |
| 预期正响应 | `02 51 01 00 00 00 00 00` |
| 等待时间 | **2000ms**（等 MCU 复位完成） |

### Step 11: 验证版本（复位后重新连接）

| 项目 | 值 |
|------|-----|
| 发送 ID | `0x18DA0D03` |
| 数据 | `03 22 F1 89 00 00 00 00` |
| 解析 | `03`=SF长度3, `22`=ReadDataByIdentifier, `F189`=Software Version（APP 不实现 `F195`） |
| 预期正响应 | `xx 62 F1 89 [版本字符串...]` |

---

## 六、发送间隔汇总

| 步骤 | 指令 | 发送后等待时间 | 特殊说明 |
|------|------|---------------|---------|
| 1 | 0x10 0x02 | **100ms** | — |
| 2 | 0x27 0x01 | **50ms** | 收到 seed |
| 3 | 0x27 0x03 ×11 | **10~20ms/帧** | 每帧等正响应后再发 |
| 4 | 0x27 0x02 | **等 NRC 0x78 结束** | 验签 200~800ms |
| 5 | 0x2E 0x2010 | **100ms** | — |
| 6 | 0x31 0x01 0xFF00 | **等 NRC 0x78 结束** | 擦除 1~2 秒 |
| 7 | 0x34 | **100ms** | — |
| 8 | 0x36 × N | **10ms/块** | ISO-TP CF 间隔 1ms |
| 9 | 0x37 | **等 NRC 0x78 结束** | 验证 100~500ms |
| 10 | 0x11 0x01 | **2000ms** | 等复位完成 |
| 11 | 0x22 0xF189 | — | 确认版本 |

---

## 七、S3 超时机制

| 参数 | 值 |
|------|-----|
| S3 超时时间 | **5000ms** |
| 超时行为 | 回退到 Default Session，清除 SecurityAccess 解锁状态 |
| NRC 0x78 影响 | **不触发 S3 超时**（NRC 0x78 会刷新计时器） |

**对策**：步骤间手动等待时，确保总间隔不超过 5 秒。等待 NRC 0x78 期间无需担心。

### 手动操作时序约束（ZCANPRO 手动点击场景）

ZCANPRO 手动逐条发送时，每次收到正响应后 **3 秒内点下一条**，留 2 秒余量。

各阶段的时序要求：

| 阶段 | 能否慢 | 原因 |
|------|--------|------|
| Step 1→2→3→4（会话+安全） | ⚠️ 每步间隔 < 5s | S3 超时会清除解锁 |
| Step 4 验签 | ✅ 可以等 | NRC 0x78 会持续刷新 S3 计时器 |
| Step 4→5→6（解锁后操作） | ⚠️ 每步间隔 < 5s | S3 超时会回退会话 |
| Step 6 擦除 | ✅ 可以等 | NRC 0x78 刷新 S3 |
| Step 7→8→9（下载传输） | ⚠️ 每步间隔 < 5s | S3 超时 |
| Step 9 transferExit | ✅ 可以等 | NRC 0x78 刷新 S3 |
| Step 9→10（退出→复位） | ⚠️ < 5s | 复位前需要在当前会话内 |

### TesterPresent 续命方案

如果手速跟不上 5 秒间隔，可在 ZCANPRO 中配置一个 **自动定时发送 TesterPresent** 的任务，每 **2 秒** 发送一次：

```
发送 ID: 0x18DA0D03
数据: 02 3E 00 00 00 00 00 00
```

- `02` = SF 长度 2
- `3E` = TesterPresent
- `00` = sub-function（不需要正响应）

此帧会持续刷新 S3 计时器，确保中间步骤有充足时间手动操作。收到 NRC 0x78 的步骤（Step 4/6/9）可以放心等待，MCU 处理完会发正响应，收到后同样 3 秒内发下一条。

---

## 八、SecurityAccess 锁定机制

| 参数 | 值 |
|------|-----|
| 最大连续失败次数 | **3 次** |
| 锁定时间 | **60 秒** |
| 锁定期间 NRC | `0x36` (ExceededNumberOfAttempts) |
| 失败 NRC | `0x35` (InvalidKey) |

验签失败 3 次后需等待 60 秒才能重试。使用正确的 ECDSA 签名可避免锁定。

---

## 九、快速验证步骤

先验证 CAN 通信是否正常，再执行完整 OTA 流程：

1. 发 `0x10 0x02` → 收 `0x50 0x02` ✅ 会话切换成功
2. 发 `0x27 0x01` → 收 `0x67 01 [seed]` ✅ Seed 获取成功

两步均通过后，按 Step 3~11 顺序执行完整 OTA 流程。

---

## 十、NRC 错误码速查

| NRC | 含义 | 常见原因 |
|-----|------|---------|
| 0x11 | ServiceNotSupported | 会话不对或 SID 错误 |
| 0x12 | SubfunctionNotSupported | 子功能值错误 |
| 0x13 | IncorrectMessageLength | 数据长度不匹配 |
| 0x22 | ConditionsNotCorrect | 未进 Programming Session |
| 0x24 | RequestSequenceError | 步骤顺序错误（如未擦除就下载） |
| 0x31 | RequestOutOfRange | DID/参数值越界 |
| 0x33 | SecurityAccessDenied | 未解锁 SecurityAccess |
| 0x35 | InvalidKey | ECDSA 签名验证失败 |
| 0x36 | ExceededNumberOfAttempts | 连续失败 3 次，已锁定 |
| 0x37 | RequiredTimeDelayNotExpired | 锁定 60 秒未到 |
| 0x70 | UploadDownloadNotAccepted | 下载请求被拒 |
| 0x71 | TransferDataAborted | 传输中止（序列号错误或超限） |
| 0x72 | GeneralProgrammingFailure | Flash 写入/擦除失败 |
| 0x73 | WrongBlockSequence | blockSeq 不连续 |
| 0x78 | ResponsePending | **非错误**，MCU 正在处理长耗时操作 |

---

## 变更记录

| 版本 | 日期 | 主要改动 |
|------|------|---------|
| V1.0 | 2026-08-27 | 初版，完整 ZCANPRO OTA 配置指南 |
| V1.1 | 2026-08-27 | 新增手动操作时序约束、TesterPresent 续命方案 |
