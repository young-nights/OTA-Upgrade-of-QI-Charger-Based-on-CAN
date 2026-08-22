# CAN-UDS OTA 代码审查报告

> **文档**: `docs/grok.md`
> **日期**: 2026-08-22
> **审查对象**: `qi_wireless_bootloader`（Bootloader）与 `qi_wireless_code`（APP）
> **审查目标**: 评估当前代码能否支撑 **CAN + ISO-TP + UDS** 的 MCU 自身 OTA
> **对照基准**: `docs/MCU-OTA详细实施方案.md`、`docs/flash.md`、`docs/通用CAN协议规范.md`、`docs/项目需求文档.md`，以及两端实际源码

---

## 1. 结论

两端已经具备 **CAN-UDS OTA 的主干**：Bootloader Safe Mode 能做 0x10 / 0x27 / 0x2E / 0x31 / 0x34 / 0x36 / 0x38 / 0x37 / 0x11，APP 能通过 Programming Session + ECUReset 跳进 Bootloader 下载。ISO-TP、ECDSA P-256 镜像验签、双槽擦写选择也已经写进代码。

**当前还不能称为可交付的双槽 CAN-UDS OTA。** 阻塞点不是“缺某个 SID 骨架”，而是下面几类会在实车/实总线直接失败或变砖的问题：

1. APP 只按 Slot A 链接（`0x08005100`），Slot B 无法作为独立可执行镜像启动，A/B 回滚名存实亡。
2. 元数据只写 Primary，Backup 区被 NVM 占用，掉电原子性被主动放弃。
3. 试启动 10s 超时在跳转 APP 后失效；APP 一启动就自动 CONFIRMED，HardFault 还持续喂狗，回滚几乎走不到。
4. 进入 Safe Mode 时立刻清掉 `OTA_STATE_DOWNLOADING`，下载过程中复位会回到旧 APP，会话无法恢复。
5. 上位机 / 测试工程未对齐现行 ISO-TP + ECDSA 流程；`0x38` 是私有 SID，标准 CCU 不会发。

建议：先把 **单槽可重复升级（始终写非运行槽，但只从固定执行地址启动）** 或 **真正的双镜像构建** 做通，再谈量产双槽回滚。

---

## 2. 审查范围

| 子项目 | 路径 | 角色 |
|--------|------|------|
| Bootloader | `qi_wireless_bootloader/` | 启动选择、验签、Safe Mode UDS 下载、试启动状态机 |
| APP | `qi_wireless_code/` | 运行时 UDS、Programming Session 触发复位进 Boot、试启动确认、生命周期广播 |

未把厂商库 `libraries/`、原理图和 Qi 芯片 UART IAP 当作本次主路径。Qi 芯片中继 OTA（Host → CAN → UART → Qi）**代码中不存在**。

当前构建体量（Keil 日志）：

| 镜像 | ROM | RAM (ZI) | 链接地址 |
|------|-----|----------|----------|
| Bootloader | Code=16058 + RO=786 ≈ 16.5 KB（20 KB 区内） | 6724 B | `0x08000000` 长度 `0x5000` |
| APP | Total ROM ≈ 3.25 KB | 很小 | `0x08005100` 长度 `0xB700`（仅 Slot A） |

APP 目前是 OTA/CAN 骨架，没有充电业务、没有 UART 协议解析、没有 GPIO 夹臂。这对“先打通 MCU OTA”是合理的，但不能用它验证试启动“核心初始化完成后再确认”。

---

## 3. 当前已经具备的能力

对照源码，下列能力是真实存在的，不是文档口头完成。

### 3.1 启动与下载主路径

```
APP: 0x10 0x02 → 0x11 0x01
        └─ ota_trigger_prepare() 写 ota_state=DOWNLOADING → NVIC_SystemReset
Boot: 读到 DOWNLOADING → 进 Safe Mode
      0x27 ECDSA → 0x2E 0x2010=0x01 → 0x31 0xFF00 擦非活跃槽
      → 0x34 初始化指针 → 0x36 写 Flash（跳过 256B 头）
      → 0x38 收 64B 签名 → 0x37 写 header + 验签 + trial=PENDING
      → 0x11 复位 → 试启动跳转 slot+0x100
```

### 3.2 已实现的 UDS（Bootloader Safe Mode）

| SID | 行为 | 备注 |
|-----|------|------|
| 0x10 | Default / Programming / Extended | 切 Default 会清安全与下载状态 |
| 0x11 | hardReset | 等 TX idle 后 `NVIC_SystemReset` |
| 0x22 | DID 0xF189 / 0xF18D | 缺 OTA DID |
| 0x27 | 0x01 seed / 0x03 分片 / 0x02 验签 | 已支持 ISO-TP 一次送 64B key |
| 0x2E | DID 0x2010，仅接受 `0x01`=APP | `g_firmware_type` 写入后未使用 |
| 0x31 | 0xFF00 擦槽；0xFF01 校验 | 擦的是 `select_inactive_slot()` |
| 0x34 | 复位写指针，返回 maxBlock=256 | **不解析** 地址/长度 |
| 0x36 | 按 blockSeq 写 Flash | 有 0xFF→0x01 回绕 |
| 0x38 | 私有：累加 64B 签名 | 非 ISO 14229 标准用法 |
| 0x3E | TesterPresent | Boot 侧无 S3 超时 |

### 3.3 APP 侧

- `0x10` 会话切换、`0x11` 在 Programming 下触发 OTA、`0x22` 若干 DID、`0x3E` 保活。
- `0x27 / 0x31 / 0x34 / 0x36 / 0x38` 返回 NRC 0x11，下载只放在 Boot。
- ISO-TP 收发与 Boot 同构；CAN 250 kbps、29-bit、ID `0x18DA0D03` / `0x18DA030D`。
- 上电设 `SCB->VTOR = 0x08005100`，与当前 scatter 一致。

这些足以在 **Slot A + 不断电 + 自定义上位机** 条件下做通一次升级演示。下面的问题决定它能不能变成产品功能。

---

## 4. P0 — 不修就无法做成可靠 OTA

### 4.1 双槽无法执行：APP 只链接在 Slot A

**位置**

- `qi_wireless_code/mdk_project/Objects/qi_wireless.sct`：`LR_IROM1 0x08005100`
- `qi_wireless_code/mdk_user/Src/main.c`：`SCB->VTOR = 0x08005000 + 256`
- Boot：`0x37` 把新镜像写到非活跃槽，再 `boot_jump_to_app(slot + 0x100)`

**问题**

Keil 生成的是绝对地址固件。向量表、函数指针、常量都指向 `0x08005100` 一带。同一份 BIN 写到 Slot B（`0x08010900`）后：

- 镜像 CRC / ECDSA 仍可能通过（校验的是数据字节，不看链接地址）。
- `boot_jump_to_app` 对 Reset Handler 的范围检查是 `[0x08005100, 0x0801C000)`，**Slot A 的入口地址在 Slot B 的向量表里也会被放行**。
- 实际跳进 Slot B 后，PC/VTOR 与代码链接地址错位，表现为 HardFault、跑旧 Slot A 代码、或中断向量错乱。

第二次 OTA（写 B）和回滚到 B 在现架构下都不可用。第一次写 A 看起来“成功”，会掩盖这个问题。

**建议（三选一，必须先定方案）**

1. **真双镜像**：同一 APP 工程打两份（Slot A scatter `0x08005100`，Slot B scatter `0x08010900`），上位机按目标槽发送对应 BIN。
2. **固定执行地址**：下载先写入非活跃槽，Boot 再 **拷贝到固定 XIP 地址** 再跳转（失去原地 A/B XIP，但一份 BIN 即可）。
3. **阶段收敛**：产品 v1 明确只维护 Slot A，B 仅作备份镜像，回滚用“把 B 拷回 A”而不是从 B 执行。

在方案落地前，不要把“双槽 A/B + 试启动回滚”标成已完成。

### 4.2 元数据没有备份写入，掉电安全被关掉

**位置**

- `qi_wireless_bootloader/mdk_app/Src/boot_metadata.c` `boot_metadata_save()`：只写 `0x0801C000`
- `qi_wireless_code/mdk_app/Src/ota_trigger.c` `ota_metadata_save()`：同样只写 Primary
- `qi_wireless_code/mdk_app/Inc/nvm_drv.h`：NVM 配置区 = `0x0801E000`，与 Backup 重叠

注释写得很清楚：为了不破坏 NVM，主动不写 Backup。

**后果**

`docs/flash.md` / 实施方案要求“先备后主 + CRC”。现在：

- Primary 擦除窗口掉电 → 元数据丢失。
- `boot_metadata_init()` 去读 Backup，读到的是 NVM 魔数 `"NVM1"` 或 `0xFF`，校验失败。
- 回退默认值：`active_slot=A`，`slot_*_valid=0`。若 APP 头还在，后续 `try_boot_slot` 仍可能靠镜像头救回；若 0x37 刚写完元数据就掉电，可能把一次成功升级当成“无有效槽”进 Safe Mode，或丢掉 trial/rollback 计数。

NVM 目前只有 `nvm_drv_init()`，没有业务读写，冲突是布局问题而不是运行期互相覆盖。但 **Backup 已经名存实亡**。

**建议**

把 NVM 从 `0x0801E000` 挪走，或把 8 KB Backup 再切成“元数据 2 KB + NVM 6 KB”，恢复双写。不要用“暂时不写 Backup”当作方案。

### 4.3 试启动超时与回滚基本走不通

**设计**（`通用CAN协议规范` §9.5 / 实施方案 §6）

- 新镜像试用，10 s 内完成核心初始化后确认。
- 超时或反复复位超过 3 次 → 回滚到旧槽。

**实现**

| 环节 | 代码实际行为 |
|------|----------------|
| Boot 里 `timer_create(1000, trial_timer_callback)` | 在 `try_boot_slot()` **之前**启动，一跳进 APP，SysTick/软件定时器全部被 APP 重建，**10 s 回调不会再跑** |
| APP `ota_trial_init()` + `ota_trial_poll()` | 100ms 健康延迟后自动确认 `trial_state` 为 CONFIRMED |
| APP `HardFault_Handler` | `while(1) { wdg_drv_refresh(); }` —— 死机也 **不复位**，Boot 的 retry/rollback 进不去 |
| `process_trial_state(ACTIVE)` | 只在 `last_boot_reason == WDG` 时加 retry；正常跑着的坏 APP 永远 ACTIVE 或已被自动确认 |

结果：只要新镜像能跑到 `main()` 后半段，就会被永久确认。真正该死机回滚的路径（HardFault、卡死但还在喂狗）都失效。再叠加 4.1，回滚目标槽本身也跑不起来。

**建议**

1. 试启动计时放在 **APP**：确认前不喂狗超过 WDG 窗口，或 APP 自己在 10 s 未确认时停狗复位。
2. 确认改为：核心健康检查通过 **且** 可选的 CCU DID 写入；不要 `main()` 里无条件 CONFIRMED。
3. Fault handler **禁止喂狗**，让 IWDG 复位回到 Boot。
4. Boot 的 `trial_timer_callback` 在跳转后无意义，应删除或改成“未跳转才计时”，避免误导。

### 4.4 下载会话在进 Safe Mode 时被清掉

**位置**: `qi_wireless_bootloader/mdk_user/Src/main.c`

```c
if (g_meta.ota_state == OTA_STATE_DOWNLOADING) {
    g_meta.ota_state = OTA_STATE_IDLE;   /* 注释: so we don't loop */
    boot_metadata_save(&g_meta);
    enter_safe_mode();
}
```

**问题**

- 擦 46 KB、ECDSA、0x36 写 Flash 期间若 WDG/掉电复位，下次启动 `ota_state` 已是 IDLE。
- 旧槽仍 valid → 直接进 APP，上位机还停在 Programming 流程中间。
- 若第一次升级（无有效 APP），仍会因镜像无效进 Safe Mode，表现不一致。

**建议**

`DOWNLOADING` 应保持到 `0x37` 成功（或明确 Abort）。Safe Mode 入口不要清这个标志。可用独立“下载中目标槽”字段，掉电后继续等 CCU 或超时再放弃。

### 4.5 标准 UDS 客户端对不上：0x38 + 镜像头由 MCU 生成

`通用CAN协议规范` §9.1 的流程是：

`0x10 → 0x27 0x01/0x02 → 0x2E 0x2010 → 0x31 FF00 → 0x34 → 0x36 → 0x37`（镜像自带 CRC/签名）

代码额外要求 **0x38 TransferSignature**，且 0x36 只发 **纯 APP 代码**（不含 256 B header）。0x37 在 MCU 上现场组 `image_header_t`（magic/length/crc/signature）。

ISO 14229 的 0x38 是 RequestFileTransfer。CCU / CANoe / 标准 UDS 上位机不会发这个 SID。没有 64 B 签名，0x37 直接 NRC 0x72。

**建议**

优先改为规范路径：0x36 的前 256 字节就是 `image_header_t`（host 预计算 length/CRC/签名），0x37 只做 flush + `boot_verify_image()`。0x38 仅作过渡可选。

### 4.6 量产/联调用的发送脚本仍是旧帧格式

`docs/ZCANPRO/list-cmd.list`：

- `is_canfd="1"`，MCU 是 Classical CAN 2.0B，CAN FD 帧对方收不到。
- 数据是 `11 01` / `10 02` / `27 01`，**没有 ISO-TP PCI**（应为 `02 11 01` 这种 SF）。现行 `isotp_rx_process()` 会把首字节当 PCI，请求被丢弃。
- 顺序是先 `0x11` 再 `0x10`。APP 在 Default 下 `0x11` 只复位、**不置 DOWNLOADING**。
- 只有 3 步，没有 0x27 验签、0x31、0x34、0x36、0x37。

没有对齐 ISO-TP + ECDSA 的主机脚本，板上代码无法在现有工具链上闭环。

---

## 5. P1 — 协议/健壮性，联调或异常流量会踩

### 5.1 CAN 硬件过滤器写错，功能寻址进不来

两端 `can_driver.c` 注释声称 `mask = 0x1FFC0000` 的 bit18=0，从而同时放行 PF=0xDA 与 0xDB。

`0x1FFC0000` 的 bit18 实际是 **1**。要 don't-care PF LSB，掩码应为 `0x1FF80000`。

后果：

- 物理请求 `0x18DA0D03` 仍能进（PF 必须匹配 0xDA）。
- 功能寻址 `0x18DB33D0` 在硬件层被丢掉。Boot 软件里对 `0x18DB33xx` 的判断不会执行。
- APP 的 `can_protocol_rx_handler` 本来也只收 `0x18DA0D03`，功能寻址两端都没有。

另外该过滤器对 `0x18DAxxxx` 几乎全放行，再在软件里精确过滤，总线诊断流量大时 IRQ 负担偏高。更干净的做法是 code/mask 锁 `TA=0x0D`。

### 5.2 ISO-TP 不完整，多帧响应和异常接收有洞

两端 `isotp.c` 同源，主要缺口：

| 项 | 现状 | 风险 |
|----|------|------|
| RX N_Cr 超时 | 无 | 丢 CF 后状态机停在 `RX_IN_PROGRESS`，后续 SF 还可能被处理，但 CF 流再也组不齐，直到下一次 FF |
| TX Block Size | 只等第一帧 CTS，然后按 1 ms 把剩余 CF 全部发完 | 测试仪 BS≠0 时违规 |
| TX 失败重试 | `can_driver_send` 忙则 -1，无 N_As 重试 | 0x36 正响应丢失，主机重传序号错乱 |
| `isotp_wait_cts` | 从 FIFO **弹出并丢弃** 非 FC 帧 | 多帧响应期间到达的 UDS 请求被吃掉 |
| 4 KB 重组缓冲 | `ISOTP_MAX_PAYLOAD=4095` | Boot ZI 已 6.7 KB，uECC 栈约 776 B，峰值栈 2.4 KB；20 KB SRAM 能放下，但余量不大 |

0x22 版本字符串、0x78+最终响应多数 ≤7 字节，走 SF，日常单帧不受影响。一旦 0x22 多 DID 或更长 NRC 流程走多帧，上述问题会暴露。

### 5.3 0x34 不解析 addressAndLengthFormatIdentifier

`boot_safe_mode.c` 的 0x34 忽略 CCU 下发的地址和 size，只 `dl_reset_state()`。若 0x31 没先擦，写未擦除 Flash 会 0x72。主机按规范填的 memoryAddress/memorySize 被无视，长度超槽只在 0x36 时才拦。

应按 ISO 14229 解析 ALFID，校验目标落在已绑定槽、size ≤ `slot_size - 256`。

### 5.4 DID / 会话与规范不一致

| 项目 | 规范 / 需求文档 | 代码 |
|------|-----------------|------|
| 软件版本 DID | 实施方案写 0xF195（已修正）；通用规范是 **0xF189** | 0xF189（与通用规范一致） |
| 0x2010 取值 | 部分文档写 0=app, 1=bootloader | 通用规范 0x01=APP, 0x03=Bootloader；Boot 只接受 0x01 |
| OTA DID 0x2112–0x2116 | APP/Boot 都应可读 | APP 有 2112/2113/2115/2116；**无 0x2114 pending**；Boot 0x22 **全部没有** |
| 0x22 多 DID | 规范 SHALL | 只解析第一个 DID |
| S3=5 s | 非默认会话无 TesterPresent 回 Default | APP 只在 **下一条 UDS 到来时** 才检查；Boot **完全没有** S3 |
| 0x11 安全等级 | PRD 写 Ext/Prog + L1；通用规范写无 | APP/Boot 均不要求 L1（偏通用规范） |
| Security seed | 通用规范 Ed25519 + **32 B seed**；SRS 更新为 ECDSA | 4 B seed + SHA-256，算法已换文档未全改 |

Boot 的 `g_firmware_type`、APP 的 `security_unlocked` 在 APP 里永远是 0（0x27 直接 NRC 0x11），因此 APP 的 0x2E 实际不可用。流程依赖“先复位进 Boot 再认证”，需要在联调文档里写死，避免 CCU 在 APP 里做 0x27/0x2E。

### 5.5 0x27 安全强度偏弱

- seed 来自 SysTick ⊕ 固定初值 LFSR `0x12345678`，上电后前几次可预测。
- 4 字节挑战空间不够（规范要 32 B 随机种子）。
- 0x27 0x03 在 ISO 14229 里是 Level 2 RequestSeed，被改成分片传签名，标准诊断仪会误解。ISO-TP 已能在 0x27 0x02 一次带 64 B，**应废弃 0x03 分片路径**。
- 连续 3 次失败锁 60 s；切 Default 会把 `g_security_fail_count` 清零，可被用来绕过锁定。

### 5.6 `pending_slot` 从未更新

`ota_metadata_t.pending_slot` 只在默认值里设为 `0xFE`。0x37 写的是 `active_slot` + `trial_slot`，不写 `pending_slot`。DID 0x2114 即使补上也会一直是 0xFE 或垃圾。掉电恢复文档里“看 pending_slot 判断半成品下载”没有对应代码。

### 5.7 请求下载默认槽与“必须先擦”不同步

`g_dl_slot_base` 上电就是 `APP_A_BASE_ADDR`（非 0），0x34 里 `if (g_dl_slot_base == 0)` 的绑定逻辑基本走不到。跳过 0x31 直接 0x34/0x36，会往 Slot A 未擦区域写。应要求“已擦标志”或在 0x34 检查目标扇区是否为 0xFF。

### 5.8 工厂镜像没有 header 时必进 Safe Mode

默认元数据 `slot_a_valid=0`。产线若只烧 APP 机器码到 `0x08005100`、不写 256 B `image_header_t`+ECDSA，Boot `boot_verify_image` 失败，两槽都失败 → 永久 Safe Mode。需要：

- 打包脚本（需求 D-13，未见实现）：插入 header、算 CRC、用 `docs/keys/private.pem` 签名；
- 产线烧录：Boot + 带 header 的 APP_A + 有效 metadata。

测试私钥在 `docs/keys/private.pem`，只适合实验室。量产必须换钥并避免把私钥放进仓库。

---

## 6. P2 — 质量、一致性、非 OTA 缺口

### 6.1 文档互相打架

| 主题 | 不一致 |
|------|--------|
| Flash 布局 | `flash.md` / 代码：Boot 20 KB + APP 46 KB；PRD §1.4 仍写 16 KB + 48 KB；子项目 `docs/can-ota-design.md` 还是旧 16 KB 布局 + CAN ID 0x100 私有协议 |
| 元数据大小 | 实施方案已修正为 272 B；代码 `padding[232]` → **272 B** |
| `image_header_t.reserved` | 文档 156 B + 额外 reserved u32；代码 `reserved[160]`，合计仍 256 B，偏移不同 |
| 擦除扇区数 | 文档“24×2 KB”；46 KB 槽实际 **23** 个扇区 |
| 波特率 | 子项目 design 写 500 kbps；代码 250 kbps（与整车规范一致） |
| 安全算法 | 通用规范仍写 Ed25519；代码与 SRS 更新是 ECDSA P-256 |
| `can-ota-modification-guide.md` | 描述的 `ota_config.h` / `can_ota_protocol.c` 文件在工程中 **不存在** |

以 **`docs/flash.md` + 两个工程的 header 宏** 为唯一内存地图，其余文档应改到与代码一致，或标明废弃。

### 6.2 Boot 跳转细节

`boot_jump.c`：关外设不完整（只 reset CAN1）、跳转前 `__enable_irq()`。向量和 MSP 校验合理。建议跳转保持 `__disable_irq()`，由 APP 的 `Reset_Handler`/`SystemInit` 再开中断。

Boot `HardFault` 死循环不喂狗，约 1 s 后 IWDG 复位，这是对的。APP 的 Fault 喂狗是错的（见 4.3）。

### 6.3 CAN 位时序

APB1=180 MHz，`div=10`，BTS1=54，BTS2=17 → 250 kbps，采样点 55/72 ≈ **76.4%**（规范建议 80%），SJW=1 偏小。车载长线或收发器延迟大时可能间歇 CRC/ACK 错误。建议调到约 80% 采样点、SJW≥2，并用示波器/CAN 分析仪确认。

### 6.4 生命周期广播不是 SRS 载荷

`lifecycle.c` 1 Hz 发 `0x18FF260D`，byte0=BOOTUP/OPERATIONAL，其余 0。SRS 要 100 ms 周期，并带充电状态/FOD/温度/功率。与 OTA 无直接冲突，但 CCU 不能靠广播判断充电健康，也就不能作为试启动“核心功能 OK”的判据。

### 6.5 Qi UART / 中继 OTA / GPIO

`qi_uart.c` 只有 9600 8N1 FIFO，`qi_uart_poll` 无 `0x55 0xAA` 解析，无 0xCC IAP。需求 6.2 的 Qi 芯片 OTA 整条链路未开始。与 MCU 自身 OTA 可并行，但不要写进“OTA 已完成”。

### 6.6 其它代码债

- Boot 工程编进了 `nvm_drv.c` 却不在 OTA 路径使用，占 Flash。
- `0x37` 部分失败分支用 `return` 而不是 `break`，行为正确但风格不一致。
- header 的 `version[]` / `build_timestamp` 在 0x37 保持 0xFF，DID 0xF189 是编译期字符串，与镜像头无关。
- ISO-TP / CAN 驱动在两个工程各一份，后续必会改一处漏一处。

---

## 7. 建议的闭环顺序

按依赖排序，而不是按模块大小。

### 阶段 A — 先打通“可重复升级”（实验室）

1. 冻结内存地图：以代码宏 + `flash.md` 为准，改掉 PRD / 子项目 design 的旧 16 KB 布局。
2. 明确双槽策略（4.1 三选一）。在真双链接落地前，主机只升级 Slot A，或 Boot 强制 `select_inactive_slot()` 但跳转前拷回 A。
3. 改 0x36/0x37：镜像自带 256 B 头，去掉对私有 0x38 的硬依赖（可暂时兼容）。
4. 写一套 **Classical CAN + ISO-TP** 的主机脚本（Python-can 或 ZCANPRO）：`10 02 → 11 01 →（Boot）27 01 → 27 02(64B) → 2E 2010 01 → 31 01 FF00 → 34 → 36×N → 37 → 11 → 22 F189`。
5. 修 ZCANPRO 列表：关 CAN FD、加 PCI、改正顺序。
6. 提供 `pack_image.py`：补 header、CRC32、ECDSA 签名。

### 阶段 B — 掉电与回滚

1. 拆开 Metadata Backup 与 NVM，恢复双写。
2. `DOWNLOADING` 保持到 0x37 成功。
3. APP 故障不喂狗；试启动由 APP 计时；禁止无条件 CONFIRMED。
4. 补 DID 0x2112–0x2116（含 pending），Boot Safe Mode 也要能读。

### 阶段 C — 协议对齐与加固

1. 过滤器改为放行 `0x18DA0D03` 与可选 `0x18DB33D0`。
2. ISO-TP：N_Cr 超时、BS 遵守、发送重试。
3. 0x34 解析地址/长度；S3 在 Boot 主循环检查。
4. 废弃 0x27 0x03；seed 加长并换更好的熵。
5. 位时序调到 80% 采样点。

### 阶段 D — 产品化（非本次阻塞，但清单上有）

- 量产密钥注入与 Boot 区写保护。
- Qi UART 协议 + CAN-UART 中继 OTA。
- 生命周期 100 ms 真实载荷。

---

## 8. 问题清单（便于跟踪）

| ID | 严重度 | 子项目 | 摘要 |
|----|--------|--------|------|
| OTA-01 | P0 | APP + Boot | 单链接地址，Slot B 不能作为可执行槽，回滚失败 |
| OTA-02 | P0 | 两端 metadata | 只写 Primary，Backup 与 NVM 冲突，掉电不原子 |
| OTA-03 | P0 | Boot trial + APP | 10 s 超时死代码；APP 自动确认；Fault 喂狗导致无法回滚 |
| OTA-04 | P0 | Boot main | 进 Safe Mode 立即清 DOWNLOADING，中途复位丢失会话 |
| OTA-05 | P0 | Boot UDS + 主机 | 私有 0x38 + MCU 生成 header，与标准 UDS 下载不兼容 |
| OTA-06 | P0 | 测试工程 | ZCANPRO 脚本未按 ISO-TP 与现行流程编写 |
| OTA-07 | P1 | 两端 CAN | 过滤器 mask 注释与数值不符，功能寻址收不到 |
| OTA-08 | P1 | 两端 ISO-TP | 无 N_Cr、不遵守 BS、wait_cts 丢帧、无发送重试 |
| OTA-09 | P1 | Boot 0x34 | 不解析地址/长度，跳过擦除会写脏 Flash |
| OTA-10 | P1 | 两端 DID/会话 | 缺 0x2114；Boot 无 OTA DID；无真正 S3；文档 DID 号混乱 |
| OTA-11 | P1 | Boot 0x27 | 4 B 伪随机 seed；0x03 占用标准子功能；Default 清失败计数 |
| OTA-12 | P1 | 元数据 | `pending_slot` 不更新 |
| OTA-13 | P1 | 产线 | 无打包/签名工具，工厂 APP 无 header 则无法启动 |
| OTA-14 | P2 | 文档 | 16/20 KB、500/250 kbps、Ed25519/ECDSA、元数据大小已修正为 272 B |
| OTA-15 | P2 | APP | HardFault 喂狗；广播 1 Hz 空载荷；UART IAP 未做 |

---

## 9. 一句话给实现的约束

**CAN-UDS OTA 的协议骨架已经在两个子项目里，不要再并行搞一套 0x100/0x101 私有 CAN-OTA。** 当前真正缺的是：可执行的双槽（或明确的单槽策略）、掉电安全的元数据、可恢复的下载会话、与 ISO 14229/ISO-TP 一致的主机流程，以及能在 Classical CAN 上跑通的测试脚本。先把 Slot A 的“擦 → 下 → 验签 → 复位 → 还能再升级”做成可重复实验，再打开 Slot B 和回滚。
