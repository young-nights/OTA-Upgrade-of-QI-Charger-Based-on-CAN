# Qi 无线充 CAN-UDS OTA 软件进度计划表

> 更新日期：2026-09-01

---

## 〇、里程碑时间线

| 里程碑 | 计划日期 | 实际日期 | 状态 | 备注 |
|--------|---------|---------|------|------|
| M1 需求与方案设计 | 2026-08-25 | 2026-08-25 | ✅ 完成 | Flash 分区、UDS 协议、签名方案确定 |
| M2 Bootloader 基础功能 | 2026-08-28 | 2026-08-28 | ✅ 完成 | Safe Mode UDS 栈、Flash 读写、Metadata 管理 |
| M3 安全验签链路 | 2026-08-30 | 2026-08-31 | ✅ 完成 | ECDSA P-256 签名/验签、SecurityAccess、公钥嵌入 |
| M4 Python 工具链 | 2026-08-31 | 2026-09-01 | ✅ 完成 | sign_seed / verify_image / zcanpro_ext_ota 修复验证 |
| M5 链接地址修正 | 2026-09-01 | 2026-09-01 | ✅ 完成 | Keil IROM 与 .sct 统一，APP/BL 地址对齐 |
| M6 OTA 数据传输链路 | 2026-09-01 | 2026-09-01 | ✅ 完成 | 27 01/03/02 验签 + 0x34 下载 + 0x36 传输 + 0x37 退出，全流程通过 |
| M6.1 复位后 APP 启动 | 2026-09-03 | — | ⏳ 待验证 | Reset 后 APP CAN 栈初始化，22 F1 95 响应 |
| M7 APP Trial Boot 确认 | 2026-09-05 | — | 📋 待开发 | APP 侧 trial_confirm + ota_trial_poll |
| M8 产线批量烧录 | 2026-09-10 | — | 📋 待开发 | 批量校验 + 烧录工具 |
| M9 安全加固 | 2026-09-15 | — | 📋 待开发 | 防回滚版本号、安全启动链 |

---

## 一、已完成项 ✅

### 1. Bootloader 固件

| 模块 | 状态 | 说明 |
|------|------|------|
| Flash 分区方案 | ✅ 完成 | Boot 28KB + APP_A 42KB + APP_B 42KB + Metadata 2KB + Device Info 4KB |
| Safe Mode UDS 栈 | ✅ 完成 | 0x10/0x11/0x22/0x27/0x2E/0x31/0x34/0x36/0x37 全部实现 |
| SecurityAccess (0x27) | ✅ 完成 | 27 01 seed 生成 + 27 03 分片接收 + 27 02 ECDSA 验签 |
| TransferExit (0x37) | ✅ 完成 | Flash 写入 + CRC32 校验 + ECDSA P-256 验签 + NRC 0x78 保活 |
| OTA Metadata 管理 | ✅ 完成 | 主备双份 + CRC32 校验 + power-loss safe 写入 |
| Trial Boot 状态机 | ✅ 完成 | PENDING → ACTIVE → CONFIRMED / ROLLBACK，最多 3 次重试 |
| Image 验证 (boot_verify) | ✅ 完成 | 6 项检查：Magic + Length + CRC32 + Reset Handler + Pubkey + ECDSA |
| 公钥嵌入 | ✅ 完成 | SEC1 65 字节硬编码在 `.ecdsa_pubkey` section，带 KEYP magic 防篡改 |
| Keil IROM 配置 | ✅ 完成 | `IROM(0x08000000,0x7000)` 与 `.sct` 一致 |

### 2. APP 固件

| 模块 | 状态 | 说明 |
|------|------|------|
| UDS 栈 | ✅ 完成 | 0x10/0x11/0x22/0x27/0x2E/0x31 基本实现 |
| SecurityAccess (APP 侧) | ✅ 完成 | 27 01/03/02 完整流程，ECDSA 验签 |
| CAN 驱动 | ✅ 完成 | 扩展帧收发，ISO-TP 多帧 |
| Keil IROM 配置 | ✅ 完成 | `IROM(0x08007100,0xA700)` 与 `.sct` 一致 |

### 3. Python 工具链

| 脚本 | 状态 | 说明 |
|------|------|------|
| `sign_seed.py` | ✅ 完成 | OpenSSL 双重 hash 修复 + DER→P1363 截断修复 |
| `verify_image.py` | ✅ 完成 | 新增 Reset Handler + 公钥一致性检查 + `--slot A/B` 参数 |
| `zcanpro_ext_ota.py` | ✅ 完成 | NRC 0x78 处理 + 线程硬超时 + 超时重发机制 |
| ECDSA 签名 | ✅ 完成 | Python ecdsa 库 + OpenSSL 双路径，P1363 格式输出 |
| 镜像打包 | ✅ 完成 | 现场打包（裸 bin + XATO 头）或读取已打包 ota.bin |

### 4. 文档

| 文档 | 状态 |
|------|------|
| Flash 分配方案 | ✅ |
| CAN-UDS 通信协议结构 | ✅ |
| Bootloader Safe Mode 下载顺序 | ✅ |
| APP 镜像打包与产线烧录 | ✅ |
| 签名校验与脚本使用 | ✅ |
| 签名原理与 Seed 机制 | ✅ |
| CAN-UDS OTA 测试用例表 | ✅ |

---

## 二、待验证项 ⏳

| # | 项目 | 前置条件 | 验证方法 | 预期结果 |
|---|------|---------|---------|---------|
| 1 | APP 复位后启动 | Keil 重新编译 APP (IROM=0x08007100) | OTA 后抓 CAN 原始帧，看 APP 有无发出任何帧 | APP 启动后 CAN 有帧 |
| 2 | 复位后 `22 F1 95` 响应 | APP CAN 驱动正常初始化 | OTA 完成后脚本自动发 `22 F1 95` | 收到正响应 |
| 3 | Trial Boot 回滚机制 | 人为烧入一个坏镜像 | 复位后 bootloader 自动回滚到上一个好镜像 | 回滚成功，设备正常 |

---

## 三、待开发项 📋

| # | 项目 | 优先级 | 说明 |
|---|------|--------|------|
| 1 | APP 确认 Trial Boot | P0 | APP 启动后需调用 `trial_confirm()` 设置 `TRIAL_STATE_CONFIRMED`，否则 bootloader 会一直重试回滚 |
| 2 | APP `ota_trial_poll()` | P0 | 10s 窗口内确认 trial，超时则 bootloader 回滚 |
| 3 | SIT1145 CAN 收发器管理 | P1 | Boot/APP 切换时 SIT1145 Normal/Standby 模式控制 |
| 4 | 产线批量烧录工具 | P2 | 基于 `verify_image.py` 扩展，支持批量校验 + 烧录 |
| 5 | OTA 断点续传 | P2 | 当前中断后需重新下载，可增加偏移量记录 |
| 6 | 安全加固 | P3 | 防回滚版本号、安全启动链、密钥轮换机制 |

---

## 四、关键地址速查

| 名称 | 地址 | 大小 |
|------|------|------|
| Bootloader | `0x08000000` | 28KB (0x7000) |
| APP Slot A 入口 | `0x08007100` | 42KB (0xA700) |
| APP Slot B 入口 | `0x08011900` | 42KB (0xA700) |
| Metadata 主区 | `0x0801C000` | 2KB (0x800) |
| Metadata 备份 | `0x0801C800` | 2KB (0x800) |
| Device Info | `0x0801D000` | 4KB (0x1000) |
| ECDSA 公钥 | Bootloader `.ecdsa_pubkey` section | 65B + 4B magic |

---

## 五、变更记录

| 版本 | 日期 | 改动点 |
|------|------|--------|
| v1.0 | 2026-09-01 | 初始版本，汇总截至 2026-09-01 的全部进度 |
| v1.1 | 2026-09-01 | 新增里程碑时间线；移除今日修复项；章节重新编号 |
| v1.2 | 2026-09-01 | M6 拆分为 M6(OTA 数据传输 ✅) + M6.1(复位后 APP 启动 ⏳)；待验证项精简为 3 项 |
