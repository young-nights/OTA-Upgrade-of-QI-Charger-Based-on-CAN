# Qi 无线充电 CAN-UDS OTA 代码审查文档

> **初审日期**: 2026-08-22  
> **复审日期**: 2026-08-22（第二轮，对照当前源码 + `bootloader.map` / `bootloader.htm`）  
> **审查对象**: `qi_wireless_bootloader/`（Bootloader）+ `qi_wireless_code/`（APP）  
> **审查范围**: CAN-UDS OTA 升级路径（ISO 14229 + ISO 15765-2 ISO-TP）  
> **审查依据**: 当前源码、scatter/map、Keil 栈深度报告、`docs/flash.md`、`docs/MCU-OTA详细实施方案.md`、`docs/通用CAN协议规范.md`  
> **说明**: 仓库内 `code_review_report.md`（2026-08-19）已过时。当时的 SecurityAccess 后门、CAN FIFO 竞态、0x37 无回读等问题，当前代码中大多已修复。本文以 **2026-08-22 源码** 为准。

---

## 一、复审结论

**代码相对初审没有改动。初审五项阻塞全部成立，且 C1 / C5 比原先更严重。**

复审用 `bootloader.map` 和 Keil `bootloader.htm` 把「擦 Boot 尾部」「栈可能不够」从推断落成地址和数字：

| 项 | 初审判断 | 复审证据 |
|----|----------|----------|
| C1 擦到 Bootloader | 16.46KB 跨过 `0x08004000` | `0x31` 擦 `0x08004000` 会毁掉 SHA256 `K`、曲线 `N/P`、`Region$$Table`、`.data` LMA。**第一次擦除后再复位，Boot 自身初始化/验签路径损坏，后续无法再解锁 OTA。** |
| C5 栈溢出 | 估算 ECDSA > 1KB | Keil：**`uECC_verify` 调用链 Max Depth = 2192～2424 字节**，栈只有 **1024**。启动验签和 `0x27 0x02` 都会 HardFault。 |
| 其余 C2/C3/C4、H1–H11 | 仍成立 | 源码未改 |

复审新增：

- **H12** CAN 发送只在 `TSTAT==IDLE` 时用 PTB，后续帧挤 STB，满则静默丢应答
- **H13** `0x36` 的 `uint8_t i` 在 `data_len` 为 253–255 时死循环
- **M10** `0x38` 不检查编程会话
- **M11** Boot `0x22` 版本无 `'\0'`（刚好塞进 8 字节）；APP 带 `'\0'` 变成 9 字节发不出去

**按当前源码，CAN-UDS OTA 不能在真机上可靠跑通。不修 C1–C5，上板联调没有意义。**

必须先修的阻塞项：

1. Flash 布局三套互相打架（头文件 / scatter / 文档）——第一次 `0x31` 可能变砖 Boot
2. APP 的 UDS 应答没有走 ISO-TP
3. 下载永远覆盖 APP_A
4. Bootloader 写 metadata 未 `flash_unlock()`
5. ECDSA 验签栈深 2KB+，栈只有 1KB

---

## 二、两个子项目架构

| 工程 | 角色 |
|------|------|
| `qi_wireless_code` | 运行中的 APP：收 UDS，用 `0x11` 写 metadata 标志后复位进 Boot |
| `qi_wireless_bootloader` | 启动选择 + Safe Mode：真正做 `0x27 / 0x31 / 0x34 / 0x36 / 0x38 / 0x37` 下载 |

### 2.1 设计上的 OTA 流程

```
Host                              APP                         Bootloader Safe Mode
 |                                 |                                |
 | 1. 0x10 0x02 / 0x11 0x01        |                                |
 |-------------------------------->| ota_state = DOWNLOADING        |
 |<------------ 0x51 0x01 ---------| NVIC_SystemReset()             |
 |                                 |------ reset ------------------>|
 |                                 |                                | 进入 Safe Mode
 | 2. 0x10 0x02 Programming        |                                |
 |----------------------------------------------------------------->|
 | 3. 0x27 0x01 RequestSeed        |                                |
 | 4. 0x27 0x03 分片签名 / 0x27 0x02 验签                           |
 | 5. 0x2E 0x2010 固件类型         |                                |
 | 6. 0x31 0x01 0xFF00 擦除        |                                |
 | 7. 0x34 RequestDownload         |                                |
 | 8. 0x36 TransferData × N        |                                |
 | 9. 0x38 TransferSignature       |                                |
 |10. 0x37 RequestTransferExit     |                                |
 |11. 0x11 0x01 ECUReset           |                                |
 |                                 |<----- trial jump (slot+0x100) -|
 |12. APP 确认 trial               |                                |
```

### 2.2 关键常量（代码现状）

| 项 | 值 |
|----|-----|
| CAN 波特率 | 250 kbps，29 位扩展帧 |
| UDS 请求 / 响应 | `0x18DA0D03` / `0x18DA030D` |
| 生命周期广播 | `0x18FF260D` |
| CCU 地址 / MCU 地址 | `0x03` / `0x0D` |
| 元数据主区 / 备份区 | `0x0801C000` / `0x0801E000` |
| 镜像头 | 256B，魔数 `"XATO"` (`0x4F544158`) |
| 元数据魔数 | `"MATO"` (`0x4F54414D`)，结构体实际 272 字节 |
| 加密 | ECDSA P-256 + SHA-256，IEEE P1363 R\|\|S 64B |
| IWDG | 约 1 秒 |
| 栈 | 两边都是 **1KB** (`Stack_Size = 0x400`) |

---

## 三、问题汇总

| 等级 | 编号 | 问题 | 位置 |
|------|------|------|------|
| 严重 | C1 | Flash 布局三套不一致，擦除会破坏 Bootloader | `boot_metadata.h` / `ota_trigger.h` / scatter |
| 严重 | C2 | APP UDS 应答未走 ISO-TP | `can_protocol.c` |
| 严重 | C3 | 双槽未实现，永远擦写 APP_A | `boot_safe_mode.c` |
| 严重 | C4 | Bootloader 写 metadata 未 `flash_unlock()` | `boot_metadata.c` |
| 严重 | C5 | ECDSA 撑爆 1KB 栈 | `startup_*.s` / `uECC.c` |
| 高 | H1 | 试启动回滚实际不起作用 | `boot_trial.c` / APP `main.c` |
| 高 | H2 | APP `0x11` 无条件进 OTA | `can_protocol.c` |
| 高 | H3 | SecurityAccess 非标准，标准 CCU 无法解锁 | `boot_safe_mode.c` |
| 高 | H4 | `0x34` LFI / `0x36` `uint8_t` 截断 | `boot_safe_mode.c` |
| 高 | H5 | `0x36` 非 4 字节对齐会中止下载 | `boot_safe_mode.c` |
| 高 | H6 | ISO-TP TX 不等 Flow Control | `isotp.c` |
| 高 | H7 | 擦 Flash / 验签期间不喂 IWDG | `boot_safe_mode.c` / `wdg_drv.c` |
| 高 | H8 | `0x37` 不验签就标槽位有效 | `boot_safe_mode.c` |
| 高 | H9 | `0x38` 正响应 SID `0x78` 与 NRC `0x78` 撞车 | `boot_safe_mode.c` |
| 高 | H10 | `try_boot_slot` 把跳转失败当成成功 | `boot_trial.c` |
| 高 | H11 | `0x11` 复位不等 CAN TX 完成 | APP / Boot `0x11` 处理 |
| 高 | H12 | CAN TX 仅 `TSTAT==IDLE` 才用 PTB，后续帧挤 STB 并可能静默失败 | `can_driver.c` |
| 高 | H13 | `0x36` 循环变量 `uint8_t i`，`data_len` 为 253–255 时死循环 | `boot_safe_mode.c` |
| 中 | M1 | `0x34` 不解析请求、不要求先擦除 | `boot_safe_mode.c` |
| 中 | M2 | 缺 `0x31 0x01 0xFF01` 依赖检查 | Safe Mode |
| 中 | M3 | Bootloader 无 S3 会话超时 | `boot_safe_mode.c` |
| 中 | M4 | seed 非密码学随机数 | `generate_random_seed()` |
| 中 | M5 | 元数据备份区与 NVM 重叠且从不写备份 | `nvm_drv.h` |
| 中 | M6 | DID `0x2010` / `0xF189` 与文档、测试仪不一致 | 多处 |
| 中 | M7 | 仓库内有私钥 `docs/keys/private.pem` | `docs/keys/` |
| 中 | M8 | Qi 芯片 UART IAP 未实现 | `qi_uart.c` |
| 中 | M9 | 测试工程 / ZCANPRO 脚本无法完成 OTA | `can_uds_ota_test/` |
| 中 | M10 | `0x38` 只检查安全解锁，不检查编程会话 | `boot_safe_mode.c` |
| 中 | M11 | Boot `0x22` 版本无空终止；APP 带空终止却 DLC>8 | `boot_safe_mode.c` / `can_protocol.c` |
| 低 | L1 | 错误 NRC（过长用 `0x14`、序号用 `0x71` 而非 `0x73`） | `boot_safe_mode.c` |
| 低 | L2 | 旧设计文档（0x100/0x101、500kbps）与代码矛盾 | `*/docs/can-ota-design.md` |
| 低 | L3 | HardFault 空转，靠 IWDG 复位 | `at32f422_426_int.c` |

---

## 四、严重问题详述

### C1. Flash 布局三套不一致 — 会擦掉 Bootloader / 跳错地址

文档、链接脚本、头文件是三套地图：

| 来源 | Bootloader | APP_A | 入口 |
|------|------------|-------|------|
| `docs/flash.md`、`.sct`、APP `main.c` VTOR | 20KB `@0x08000000` | `@0x08005000` / 46KB | `@0x08005100` |
| `boot_metadata.h`、`ota_trigger.h` | **16KB** | **`@0x08004000` / 48KB** | **`@0x08004100`** |
| APP `.uvprojx` 的 Cpu IROM | — | 仍写着 `0x08004100` | scatter 实际是 `0x08005100` |

已编译结果（`bootloader.map`）：

- Bootloader `ER_IROM1` 实际占用 **`0x41CC`（16.46KB）**，已经跨过 `0x08004000`
- APP 链接在 **`0x08005100`**

`0x31` 擦除从 `0x08004000` 起的 2KB 扇区（`0x08004000`–`0x080047FF`），会当场毁掉仍在运行的 Boot 镜像尾部：

| 符号 | 地址 | 后果 |
|------|------|------|
| `G_bytes`（P-256 生成元） | `0x08003fd8`，65B，跨到 `0x08004019` | 后 25 字节被擦掉 |
| SHA256 常量 `K` | `0x0800401c`，256B | **整表擦掉** |
| 曲线 `N` / `P` / `Rn` / `Rp` | `0x0800411c` 起 | 整表擦掉 |
| `Region$$Table`（scatter 加载表） | `0x080041ac` | 下次启动无法正确拷贝 `.data` |
| `.data` LMA | `0x080041d0` | `system_core_clock`、LFSR 初值等初始化源被毁 |

因此：

1. 工厂烧录的 APP（无 `XATO` 头、代码在 `0x08005100`）会被 `boot_verify_image()` 判无效，**永远进 Safe Mode**。
2. 第一次 `0x31` 擦除发生在 **当前会话仍在运行** 时。本会话里 `0x36/0x37` 可能还能跑（RAM 里已解锁、CRC 不依赖被擦常量）。**复位之后**：scatter 从被擦 Flash 拷 `.data`，SHA256/ECDSA 常量已是 `0xFF` 或 APP 垃圾。下一次 `0x27 0x02` 无法解锁，**后续 OTA 全部失败**。这不是「进不了 APP」，是 **Boot 自毁**。
3. 下载写到 `0x08004100`，复位后跳到错误地址。

**修复建议**：两套头文件改成与 scatter / `flash.md` 一致：

```c
#define BOOT_SIZE        0x5000U
#define APP_A_BASE_ADDR  0x08005000U
#define APP_A_SIZE       0xB800U
#define APP_B_BASE_ADDR  0x08010800U
#define APP_B_SIZE       0xB800U
```

同时：

- APP 工程 Cpu IROM 改为 `0x08005100`
- 增加 post-build：在 APP `.bin` 前加 256B `image_header_t`（含魔数、长度、CRC32、ECDSA 签名），否则即使地址对了，工厂 APP 也过不了验签

---

### C2. APP 的 UDS 应答没有 ISO-TP

Bootloader 用 `isotp_tx_send()`，APP 却直接发裸 UDS：

```c
/* qi_wireless_code/mdk_app/Src/can_protocol.c */
static void proto_send_response(uint8_t *data, uint8_t len)
{
  can_driver_send(CAN_PROTO_UDS_RESPONSE, data, len);
}
```

标准测试仪发 `02 10 02`（ISO-TP 单帧），APP 回 `50 02`（无 PCI）。ISO-TP 栈会丢掉这帧。  
`0x10 02 → 0x11` 这条「从 APP 进 Boot」的路径，用 ZCANPRO / CANoe / 任何 ISO-TP 客户端都走不通。

连带：`0x22` 读版本（SID+DID+`"1.0.0\0"` = 9 字节）会被 `can_driver_send()` 因 `len > 8` 直接失败，应答发不出去。

**修复建议**：APP 所有正/负响应都走 `isotp_tx_send(CAN_ID_UDS_RESPONSE, ...)`。工程里这份函数已经有了，只是没接上。

---

### C3. 双槽是文档，代码只写 APP_A

规范要求：下载不得覆盖正在运行的槽，失败要保留旧镜像。

实际：

- `erase_app_a_flash()` / `0x36` / `0x37` **永远擦写 APP_A**
- APP_B 从未被编程
- `g_firmware_type`（DID `0x2010`）写了但下载路径不用
- 掉电或验签失败后没有可启动的旧槽 → 只能 Safe Mode 再刷

这不是风格问题，是 **掉电/坏包会变砖**（至少变「只能进 Boot 下载」）。

**修复建议**：下载到 **非 active 槽**，`boot_verify_image()` 通过后再切 `pending/trial`。Bootloader 类型在未实现 BL 自升级前应回 NRC。

---

### C4. Bootloader 写 metadata 没有 `flash_unlock()`

APP 的 `ota_metadata_save()` 会 `flash_unlock()`。Boot 的 `boot_metadata_save()` / `meta_write_to_flash()` **全程不加解锁**。复位后 Flash 是锁住的。AT32 在 `OPLK` 置位时写 `FLASH_CTRL` 无效：擦写要么失败，要么忙标志根本不置位、函数把 `FLASH_OPERATE_DONE` 当成成功（空操作）。`boot_metadata_save()` 可能 **返回 0 但 Flash 没变**。

后果：

1. APP 把 `ota_state = DOWNLOADING` 写进去（有 unlock）→ 复位进 Boot
2. Boot 想清成 `IDLE` 再进 Safe Mode → **写失败**，Flash 里仍是 DOWNLOADING
3. `0x37` 想写 `trial PENDING` + `slot_a_valid` → **再失败**，但仍然回正响应
4. 再 `0x11` 复位 → Boot 又看到 DOWNLOADING → **再次进 Safe Mode**，新镜像不会被启动

`0x37` 的 image header 本身能写（那段有 unlock），所以 Flash 里可能有镜像，但启动元数据永远更不上。

**修复建议**：`boot_metadata_save()` 与 APP 一样，在擦写前后 `flash_unlock()` / `flash_lock()`，检查返回值；失败时 `0x37` 回 NRC `0x72`，不要发正响应。

---

### C5. ECDSA 验签会撑爆 1KB 栈（Keil 已量化）

两边启动文件：

```
Stack_Size      EQU     0x00000400   /* 1KB */
```

`bootloader.map`：`Stack_Mem` 在 `0x20001650`，大小 1024。

Keil `bootloader.htm` 调用链栈深（不含 ISR）：

| 路径 | Max Depth |
|------|-----------|
| `main` → `try_boot_slot` → `boot_verify_image` → `uECC_verify` → `point_mul` → `jp_add` → `jp_double` → `modmul` | **2192 B** |
| `isotp_message_received` → `uECC_verify` → …（`0x27 0x02`） | **2424 B** |
| `uECC_verify` 自身 | 776 B 帧 + 深度 2120 B |

`CAN1_RX_IRQHandler` 另需 144 B。验签期间来一帧 CAN，栈需求再加一截。

结论不是「可能不够」，是 **编译器已经算出深度是栈的 2 倍以上**。启动验签和安全解锁都会 HardFault，然后 IWDG 复位。现象像「镜像永远无效 / 无法解锁」。SRAM 有 20KB。

**修复建议**：栈至少改到 **4–8KB**（建议 8KB），并用水位标记在 `uECC_verify` 期间实测。`point_mul` 里 `r0/r1/sum` 三个 `jpoint_t`（各 96B）占大量栈，也可改为 `static`（非重入）。

---

## 五、高优先级问题详述

### H1. 试启动回滚实际不起作用

设计是 10s 超时 + 3 次重试。实现上：

1. Bootloader 在 `try_boot_slot()` **之前** 建了 1s 定时器，跳转 APP 时 `boot_jump_to_app()` 关掉 SysTick，定时器不再跑。
2. `trial_retry_count` **只在 PENDING→ACTIVE 时 +1**。APP 看门狗复位后状态仍是 ACTIVE，计数不再增加，永远到不了 `max_retries`。
3. APP 一启动就 `ota_confirm_if_needed()` 把 trial 标成 CONFIRMED。只要 APP 能跑到 `main()`，坏固件也会被「确认」，不会回滚。

结果：真正会挂的新固件（HardFault / 看门狗）会 **无限重启同一槽**，不会切到 B。B 槽本身也从未被写入。

**修复建议**：

- 确认应在 CAN + Qi 初始化成功、并对外发出 OPERATIONAL 之后
- `ACTIVE` 期间每次 WDG / HardFault 复位都要 `retry_count++`
- 超时判断要在 Boot 侧用「复位次数 / 持久化时间」，不能靠跳转前那个软件定时器
- 真正实现 A/B 双槽后，回滚才有目标

---

### H2. APP 的 `0x11` 无条件进 OTA

```c
/* qi_wireless_code/mdk_app/Src/can_protocol.c  handle_ecu_reset() */
ota_trigger_request();  /* 任何会话、任何子功能都会进 OTA */
```

不检查会话、不检查子功能、不检查安全访问。默认会话里任意 `0x11`（包括普通 hardReset）都会把 `ota_state` 写成 DOWNLOADING 并进 Safe Mode。

规范里 `0x11` 在 APP 侧应是复位；进下载应至少限制为 **编程会话 + `0x11 01`**。普通诊断复位不该拆掉正在充电的 APP。

另外：

- `ota_metadata_save()` 失败仍 `NVIC_SystemReset()`；进 Boot 后看不到 DOWNLOADING，OTA 静默失败
- 36000 次 `__NOP()` 在 180MHz 大约 **0.2ms**，注释写 2ms，CAN 应答可能还没发出去就开始擦 metadata
- 未停止 Qi 充电 / 未发 SHUTDOWN 生命周期

**修复建议**：仅在 `SESSION_PROGRAMMING` 且子功能 `0x01` 时进入 OTA；默认会话 `0x11 01` 只做 `NVIC_SystemReset()`；保存失败回 NRC `0x72` 且不复位；等待 CAN TX 完成后再擦 Flash。

---

### H3. SecurityAccess 非标准

| 规范 (`通用CAN协议规范` §7.4) | 代码 |
|-------------------------------|------|
| Ed25519，32 字节 seed | ECDSA P-256，**4 字节** LFSR seed |
| `0x27 0x02` 一次带 64B 签名（ISO-TP 多帧） | 自造 `0x27 0x03` 分片，再用 `0x02` 验签 |

标准 CCU 发 `27 02 + 64B key` 会因 `g_sa_sig_bytes_received != 64` 得到 NRC `0x13`。

seed 是 SysTick + LFSR，不是密码学随机数。规范要求硬件 RNG。

**修复建议**：`0x27 0x02` 直接接受 ISO-TP 整包 64B 签名；seed 至少 32 字节或与规范对齐；量产用硬件 RNG。保留 `0x03` 仅作兼容可选。

---

### H4. `0x34` LFI 编码错误 + `0x36` 长度截断

```c
resp[1] = 0x20;  /* LFI = 2 字节 */
resp[2] = 0x00;
resp[3] = (MAX_IMAGE_SIZE >> 8) & 0xFF;  /* 0xBF */
resp[4] = MAX_IMAGE_SIZE & 0xFF;         /* 0x00 */
```

ISO 14229：`lengthFormatIdentifier` 高半字节表示 `maxNumberOfBlockLength` 的字节数。`0x20` 表示后面只有 **2** 字节。主机读到的是 `0x00BF`（191），不是整镜像大小 `0xBF00`。多出来的字节是非法的。

`0x36` 使用：

```c
uint8_t data_len = (uint8_t)(len - 2U);
uint8_t i;
for (i = 0; i < data_len; i += 4U) { ... }
```

ISO-TP 最大 4095。载荷 >257 时静默截断却回正响应。`i` 也是 `uint8_t`：`data_len` 为 253–255 时 **确定死循环**（见 H13）。

块序号错误回的是 NRC `0x71`，规范是 **`0x73`**。过长用了 `0x14`（ResponseTooLong），也不对。

**修复建议**：

- LFI 与 maxBlockLength 按 ISO 编码，广告的是 **单次 0x36 最大长度**（如 128/256，含 SID+BSC），不是整个槽大小
- 拷贝长度用 `uint16_t`
- 序号错误用 NRC `0x73`

---

### H5. `0x36` 非 4 字节对齐会中止下载

写地址初值 4 字节对齐，但每次 `g_dl_write_addr += data_len`。最后一词会用 `0xFF` 填充，若 `data_len % 4 != 0`，**下一帧**会命中：

```c
if ((g_dl_write_addr & 0x03U) != 0U) {
  g_dl_active = 0;
  /* NRC 0x72 */
}
```

主机若按错误的 maxBlockLength=191 发（189 字节数据，`189 % 4 = 1`），第 2 块就会失败。ZCANPRO 示例每帧 6 字节，同样对不齐。

**修复建议**：内部缓冲到 4 字节再写；或对非 4 倍数回 NRC `0x13`，且不要把地址推进到未对齐状态。

---

### H6. ISO-TP TX 不等 Flow Control、不实现 STmin

`isotp_tx_send()` 发完 FF 立刻把所有 CF 打出去。注释里承认这一点。

DID `0xF189` / `0xF18D` 响应是 8 字节（`62` + DID + `"1.0.0"`）→ 必须多帧。15765-2 测试仪会等 FC 再发 CF，MCU 已经把 CF 打完，测试仪可能丢帧超时。

RX 侧：超长 FF 不回 FC Overflow；没有 N_Cr / N_Bs 超时；SN 错误静默丢弃。

**修复建议**：FF 之后等 FC（N_Bs）；遵守 BS / STmin；RX 溢出发 FC status 2；N_Cr 超时后中止。

---

### H7. 擦 Flash / 验签期间不喂 IWDG

`wdg_drv.c`：超时约 998ms。`erase_app_a_flash()` 连续擦 23～24 个 2KB sector，循环里没有 `wdg_drv_refresh()`。Flash 操作会卡住 CPU，IWDG（LSI）仍在走。AT32 扇区擦除常见 20–50ms，最坏超过 1s。

ECDSA 验签（文档估计 200–800ms）同样不在循环里喂狗。擦到一半被复位 → APP_A 半残，又没有 B 槽备份。结合 C1，还可能擦掉 Bootloader 尾部。

**修复建议**：每个 sector 以及验签前后刷新 IWDG；或把编程期 IWDG 超时加长；超长擦除期间重复发 NRC `0x78`。

---

### H8. `0x37` 不验签就标槽位有效

```c
computed_crc = boot_crc32(..., g_dl_bytes_written);
header.crc32 = computed_crc;
/* 有 64B 签名就拷贝，否则填 0xFF */
g_meta.slot_a_valid = 1;   /* 此时尚未 boot_verify_image() */
```

CRC 是对刚写入的数据再算一遍再存回去，**永远匹配**，不是独立校验。缺 `0x38` 或签名错误仍回正响应。真正验签要等到下次启动 `try_boot_slot()`，那时 A 已被覆盖。

**修复建议**：写完 header 后立刻 `boot_verify_image()`（或实现 `0x31 0x01 0xFF01`），失败回 NRC `0x72`，不要置 `slot_*_valid`。

---

### H9. 自定义 `0x38` 正响应 SID 是 `0x78`

`0x38 + 0x40 = 0x78`，与 NRC `0x78` ResponsePending 撞值。很多测试仪看到 payload 首字节 `0x78` 会当成「还在等」。

ISO 14229 的 `0x38` 本是 RequestFileTransfer。签名应通过 ISO-TP 多帧放进 `0x27 0x02` 或 `0x37` / `0x31` 例程，不要用自定义 `0x38`。

---

### H10. `try_boot_slot()` 把跳转失败当成成功

```c
boot_jump_to_app(slot_addr + IMAGE_HEADER_SIZE);
return 0;  /* 若 jump 返回（向量非法），main 以为启动成功 */
```

`boot_jump_to_app()` 在复位向量为 `0` / `0xFFFFFFFF` 时会 `return`。`main` 随后 `while(1) wdg_drv_refresh()` —— 既不进 APP，也不进 Safe Mode。

向量校验过弱：不检查 Thumb 位、不检查 MSP 是否在 SRAM、不检查复位地址是否落在该槽。

**修复建议**：jump 返回则 `return -1`；校验 MSP ∈ `0x20000000` 范围、复位地址 ∈ 槽范围且带 Thumb 位。

---

### H11. `0x11` 复位不等 CAN TX 完成

`can_driver_send()` 只是把帧丢进 PTB/STB，然后约 36000 次 NOP（约 0.2ms）就 `NVIC_SystemReset()`。250 kbps 下一帧约 0.5–1ms。主机经常看不到 `51 01`。

**修复建议**：轮询 CAN 发送完成（带超时 + 喂狗），总线空闲一小段后再复位。

---

### H12. CAN TX：只有 `TSTAT==IDLE` 才用 PTB，后续帧会挤爆 STB

```c
/* can_driver.c */
if (tx_status.current_tstat == 0)  /* CAN_TSTAT_IDLE */
    txbuf_sel = CAN_TXBUF_PTB;
else
    /* TSTAT==TRANSMITTED(3) 也会走到这里 */
    txbuf_sel = CAN_TXBUF_STB;
```

第一帧发出后状态是 `CAN_TSTAT_TRANSMITTED = 3`，不是 0。之后所有发送都走 STB。STB 满则 `can_driver_send()` 返回 -1。`isotp_tx_send()` / `safe_mode_send_response()` **忽略返回值**。

后果：ISO-TP 多帧（DID 版本 8 字节、NRC 0x78 后的正响应）可能只发出 FF、CF 被丢，主机超时。APP 生命周期 1Hz 广播与 UDS 应答也会抢 STB。

**修复建议**：PTB 空闲条件改为 `IDLE` 或 `TRANSMITTED`（且 PTB 可写）；发送失败要重试或上报；`isotp_tx_send` 检查返回值。

---

### H13. `0x36` 在 `data_len` 为 253–255 时死循环

```c
uint8_t data_len = (uint8_t)(len - 2U);
uint8_t i;
for (i = 0; i < data_len; i += 4U) { ... }
```

`i` 是 `uint8_t`。`data_len` 为 253/254/255 时，`i` 走到 252 仍满足 `i < data_len`，`i += 4` 回绕成 0，循环永不结束，IWDG 约 1s 后复位，Flash 停在半写状态。

若主机按错误的 LFI 把整槽当一块、或 ISO-TP 打到接近 256 字节，就会踩中。即使按「191 字节块」碰巧避开死循环，H4 的 `uint8_t` 截断仍然在。

**修复建议**：`data_len` 和循环变量都用 `uint16_t`/`uint32_t`。

---

## 六、中低优先级

### M1. `0x34` 忽略请求内容

不解析 ALFID / 地址 / 大小，不检查是否已在传输，不要求先执行 `0x31` 擦除。未擦除时 `0x36` 会因 Flash 编程失败得到 `0x72`。重复 `0x34` 会把已开始的传输状态清零。

### M2. 缺少 `0x31 0x01 0xFF01`

规范流程是 `37` 之后做依赖检查再 `11`。当前验签推迟到复位后，主机无法在复位前知道镜像是否可用。

### M3. Bootloader 没有 S3 会话超时

规范：非默认会话 5s 无 TesterPresent 应回到默认并锁安全。Safe Mode 里编程会话和解锁会一直保持到复位。作为专用 Boot 可接受，但不完整。

APP 的 S3 只在「下一条 UDS 到来时」检查，不会主动回落。

### M4 / M7. 安全

- seed 为 LFSR ⊕ SysTick，可预测
- 已解锁后再失败的 `27 02` 不会重新上锁
- 私钥 `docs/keys/private.pem` 在仓库里，任何人都能签固件
- 量产必须换密钥，私钥不得入库

### M5. 元数据备份失效

`META_BACKUP_ADDR = 0x0801E000` 与 `NVM_CONFIG_BASE_ADDR` 重叠。现在 APP/Boot 都只写主区，文档中的双副本掉电恢复是空的。主区擦写中掉电会丢 OTA 状态。

### M6. DID / 固件类型不一致

| 项 | 代码 | 部分文档 / ZCANPRO |
|----|------|---------------------|
| 软件版本 DID | `0xF189` | `0xF195` |
| 固件类型 APP | 规范 / APP 头文件 `0x01` | Boot 注释 `0=app`，ZCANPRO 发 `0x00` |

### M8. Qi 芯片 UART IAP 未实现

`docs/数据通信协议-IAP.md` 描述 Host → CAN → MCU → UART `0xCC` → Qi 芯片。APP 只有 UART 驱动和 `TODO` 协议解析，没有组帧/转发。当前 CAN-UDS **只能升 MCU，不能升 Qi 芯片**。

OTA 前也未停止线圈驱动 / 未发 `LIFECYCLE_SHUTDOWN`。

### M9. 测试工程无法完成 OTA

`can_uds_ota_test/`：`seqList` 空、波特率 500k、CAN-FD。不能用。

`docs/ZCANPRO` 示例脚本问题：

- 裸 UDS，无 ISO-TP PCI
- 用**固件签名**当 SecurityAccess 的 seed 签名
- `blockSeq` `0xFF → 0x00`（MCU 要 `0xFF → 0x01`）
- 每帧 6 字节，对不齐 4 字节 Flash
- DID `22 F1 95`（MCU 是 `F189`）
- 无 NRC `0x78` 重试、无 TesterPresent

`docs/测试用例表.md` 只有 Timer/IWDG，没有 UDS/OTA 用例。

### M10. `0x38` 不检查编程会话

`0x36` / `0x37` / `0x31` 都要求 `SESSION_PROGRAMMING`。`0x38` 只检查 `g_security_unlocked`。`0x27` 本身也不要求编程会话，因此可在默认会话解锁后直接传签名。应与下载路径同一套会话门闩。

### M11. 版本 DID 空终止不一致

规范：版本字符串含 `'\0'`，总长 ≤16。Boot `0x22 F189` 拷贝 `"1.0.0"` 共 5 字符、应答 8 字节，刚好单帧，但无空终止。APP `str_copy_to_resp` 带 `'\0'`，变成 9 字节，再叠加 C2（无 ISO-TP）被 `can_driver_send` 拒绝。同一 DID 两边行为不同。

### L2. 不要用旧设计文档当协议

`qi_wireless_*/docs/can-ota-design.md`、`can-ota-modification-guide.md` 仍是 `0x100/0x101/0x102`、500kbps、4 字节载荷。与当前固件无关。

---

## 七、已经做对的部分

- CAN ID `0x18DA0D03` / `0x18DA030D`、250kbps、29 位扩展帧，APP/Boot 一致
- APP 不实现 `0x34/0x36/0x37`（回 NRC `0x11`），下载只在 Safe Mode
- `ota_state = DOWNLOADING` 作为 APP→Boot 握手，Boot 会清掉再进 Safe Mode
- SecurityAccess 已有 ECDSA 验签、3 次失败锁定、NRC `0x78` 覆盖擦除/验签
- ISO-TP RX 的 SF/FF/CF/SN 回绕基本正确
- CAN RX FIFO 拷贝已在关中断下完成（旧审查 🔴-3 已修）
- `boot_jump_to_app()` 会复位 CAN、清 NVIC、设 VTOR/MSP
- `0x37` 写 header 后有读回（旧审查 🟡-5 已修）
- metadata 结构体、CRC32 多项式在 APP/Boot 之间对齐（272 字节，不是文档里的 512）
- `uECC` 借用检测已改为 `diff >> 63`（旧审查 🔴-2 的旧实现已替换）
- `10` 进默认会话会清安全和解锁状态；`3E` 子功能 `00`；suppressPosRsp 已处理
- 字编程小端打包对 Cortex-M 正确
- Safe Mode 空闲循环会刷新 IWDG

---

## 八、与 2026-08-19 旧审查对照

| 旧编号 | 旧结论 | 当前状态 |
|--------|--------|----------|
| 🔴-1 | SecurityAccess 接受任意 key | **已修**：ECDSA seed 路径存在 |
| 🔴-2 | uECC `vli_mod_mult` 借用检测错误 | **已替换实现**（新风险是栈溢出 C5） |
| 🔴-3 | CAN RX FIFO 竞态 | **已修**：整段关中断 |
| 🔴-4 | 跳转 APP 未关 CAN | **已修**：`can_reset(CAN1)` |
| 🔴-5 | 元数据备份与 NVM 冲突 | **仍在**：现改为不写备份，双副本名存实亡 |
| 🔴-6 | uECC 时序侧信道 | 仍在；仅验签，风险低于签名 |
| 🟡-3 | 0x36 地址非 4 对齐 | **部分缓解**：有对齐检查，但会直接中止（H5） |
| 🟡-4 | APP 0x34 maxBlockLength 误导 | **已修**：APP 对 0x34 回 NRC 0x11 |
| 🟡-5 | 0x37 header 无回读 | **已修** |

本次相对 8-19 旧审查的**新增**阻塞项：C1 布局分裂（复审证实会擦掉 `K`/`N`/`P`/`Region$$Table`）、C2 APP 无 ISO-TP TX、C4 metadata 未 unlock、C5 栈深 2.1KB+ vs 1KB、H1 trial 失效、H4/H5/H13 块长度与对齐、H6 TX 不等 FC、H12 PTB 判断错误。

---

## 九、协议对照（规范 vs 实现）

| 项 | ISO 14229 / 通用 CAN 规范 | 当前实现 |
|----|---------------------------|----------|
| 寻址 | 29 位 `0x18DA TA SA` | 符合（`0D` / `03`） |
| 波特率 | 250 kbps | 250 kbps（旧文档 500kbps 勿用） |
| ISO-TP | FF 后等 FC，STmin，N_* 超时 | RX 发 CTS；**TX 忽略 FC** |
| `10 02` | 编程会话 | 有 |
| `27` | Seed + SendKey（规范 Ed25519 / 32B seed） | 分片 ECDSA，4B seed，自造 `27 03` |
| `31 FF00` | 擦除 | 有，仅 APP_A |
| `31 FF01` | 检查编程依赖 | **缺失** |
| `34` | ALFID + 地址 + 大小；maxBlockLength = 一帧 0x36 | 忽略请求；LFI 编错 |
| `36` | BSCI 1..FF，跳过 0 | 回绕正确；长度/`uint8_t`/对齐有 bug |
| `37` | 仅结束传输 | 写 header + metadata + 自算 CRC |
| `38` | RequestFileTransfer | 被改成 TransferSignature |
| `11 01` | 正响应发出后再复位 | 复位过早 |
| 双槽 | 写非活跃槽 | 永远写 A |

规范内部也有矛盾：`通用CAN协议规范` 写 Ed25519，`MCU-OTA详细实施方案` 写 ECDSA P-256。实现走的是后者。上位机必须以**代码**为准，或先冻结协议再改代码。

---

## 十、建议修复顺序

1. **统一 Flash 地图**（头文件 + scatter + uvprojx + 文档），并加 APP 镜像头打包脚本。确认 Boot 镜像结束地址 **严格 < `0x08005000`**，且 `.data` LMA / `Region$$Table` 不落在 APP 擦除区。  
2. **栈改为 8KB**（Keil 测得验签链 2.1–2.4KB，加上 ISR 余量）。  
3. **Bootloader `boot_metadata_save()` 加 `flash_unlock()`**，检查返回值；必要时读回校验。  
4. **APP 应答改走 `isotp_tx_send`**  
5. **下载改非 active 槽（真正的 A/B）**；擦写循环喂狗  
6. **修好 trial：复位计数 + 推迟 CONFIRMED**  
7. **`0x11` 加会话/子功能门闩；`0x27 0x02` 接受 ISO-TP 整包 64B 签名**  
8. **修 `0x34` LFI、`0x36` 用 `uint16_t` 长度与循环变量、NRC `0x73`、去掉自定义 `0x38`**  
9. ISO-TP TX 等待 FC / STmin；CAN `can_driver_send` 正确判断 PTB 空闲；`0x37` 先验签再标有效  
10. 最后再对接上位机（ISO-TP、seed 签名 ≠ 镜像签名、块序号 `0xFF→0x01`、4 字节对齐）

---

## 十一、未在硬件上验证的点

- AT32 扇区擦除最长时间 vs 1s IWDG（需对照 `DS_AT32F422_426`）
- Flash 上锁时写 `FLASH_CTRL` 是报错还是空操作成功（C4；两种都会导致 metadata 不落盘）
- 量产公钥是否等于 `g_ecdsa_public_key` / `docs/keys/public.pem`
- `uECC` 对已知 ECDSA 测试向量是否通过（栈先修，否则测不到算法）
- AT32 CAN STB 深度；H12 在连续多帧下是否必现
- Keil 实际烧录用的是 `.uvprojx` Cpu IROM（`0x08004100`）还是 scatter（`0x08005100`）

---

## 十二、底线

UDS 状态机已经搭起来了，但复审用 map/htm 确认：

1. **`0x31` 按错误基址擦除会毁掉 Boot 的 SHA256/ECDSA 常量和 C 运行时加载表**，不是只「写错 APP」
2. **验签调用链栈深 2192–2424B，栈只有 1024B**，启动和 `0x27` 都会先 HardFault
3. 再加上 metadata 未 unlock、APP 无 ISO-TP TX、只写 APP_A、trial 回滚是死代码

真实 CAN-UDS OTA 几乎不可能启动新镜像；第一次擦除后再复位，Boot 可能再也无法完成安全解锁。

先修布局、栈、unlock、ISO-TP TX、喂狗和「验签后再激活」，再谈联调。
