# Qi 无线充 CAN-UDS OTA 软件进度计划表

> 更新日期：2026-09-01

---

## 〇、里程碑时间线

| 里程碑                 | 计划日期   | 实际日期   | 状态      | 备注                                                 |
| ---------------------- | ---------- | ---------- | --------- | ---------------------------------------------------- |
| M1 需求与方案设计      | 2026-08-25 | 2026-08-25 | ✅ 完成   | Flash 分区、UDS 协议、签名方案确定                   |
| M2 Bootloader 基础功能 | 2026-08-28 | 2026-08-28 | ✅ 完成   | Safe Mode UDS 栈、Flash 读写、Metadata 管理          |
| M3 安全验签链路        | 2026-08-30 | 2026-08-31 | ✅ 完成   | ECDSA P-256 签名/验签、SecurityAccess、公钥嵌入      |
| M4 Python 工具链       | 2026-08-31 | 2026-09-01 | ✅ 完成   | sign_seed / verify_image / zcanpro_ext_ota 修复验证  |
| M5 链接地址修正        | 2026-09-01 | 2026-09-01 | ✅ 完成   | Keil IROM 与 .sct 统一，APP/BL 地址对齐              |
| M6 OTA 数据传输链路    | 2026-09-01 | 2026-09-01 | ✅ 完成   | 27 01/03/02 验签 + 0x34 下载 + 0x36 传输 + 0x37 退出 |
| M6.1 复位后 APP 启动   | 2026-09-02 | —         | ⏳ 待验证 | Reset 后 APP CAN 栈初始化，22 F1 95 响应             |
| M7 APP Trial Boot 确认 | 2026-09-03 | —         | 📋 待开发 | APP 侧 trial_confirm + ota_trial_poll                |
| M8 IAP 在线升级        | 2026-09-10 | —         | 📋 待开发 | 基于 UDS 的 IAP 升级流程，支持 APP 区域内自更新      |
| M9 夹臂检测逻辑        | 2026-09-15 | —         | 📋 待开发 | 夹臂状态检测与安全互锁，异常夹臂时禁止 OTA / 复位    |

---

## 一、已完成项 ✅

### 1. Bootloader 固件

| 模块                     | 状态    | 说明                                                                 |
| ------------------------ | ------- | -------------------------------------------------------------------- |
| Flash 分区方案           | ✅ 完成 | Boot 28KB + APP_A 42KB + APP_B 42KB + Metadata 2KB + Device Info 4KB |
| Safe Mode UDS 栈         | ✅ 完成 | 0x10/0x11/0x22/0x27/0x2E/0x31/0x34/0x36/0x37 全部实现                |
| SecurityAccess (0x27)    | ✅ 完成 | 27 01 seed 生成 + 27 03 分片接收 + 27 02 ECDSA 验签                  |
| TransferExit (0x37)      | ✅ 完成 | Flash 写入 + CRC32 校验 + ECDSA P-256 验签 + NRC 0x78 保活           |
| OTA Metadata 管理        | ✅ 完成 | 主备双份 + CRC32 校验 + power-loss safe 写入                         |
| Trial Boot 状态机        | ✅ 完成 | PENDING → ACTIVE → CONFIRMED / ROLLBACK，最多 3 次重试             |
| Image 验证 (boot_verify) | ✅ 完成 | 6 项检查：Magic + Length + CRC32 + Reset Handler + Pubkey + ECDSA    |
| 公钥嵌入                 | ✅ 完成 | SEC1 65 字节硬编码在`.ecdsa_pubkey` section，带 KEYP magic 防篡改  |
| Keil IROM 配置           | ✅ 完成 | `IROM(0x08000000,0x7000)` 与 `.sct` 一致                         |

### 2. APP 固件

| 模块                    | 状态    | 说明                                         |
| ----------------------- | ------- | -------------------------------------------- |
| UDS 栈                  | ✅ 完成 | 0x10/0x11/0x22/0x27/0x2E/0x31 基本实现       |
| SecurityAccess (APP 侧) | ✅ 完成 | 27 01/03/02 完整流程，ECDSA 验签             |
| CAN 驱动                | ✅ 完成 | 扩展帧收发，ISO-TP 多帧                      |
| Keil IROM 配置          | ✅ 完成 | `IROM(0x08007100,0xA700)` 与 `.sct` 一致 |

### 3. Python 工具链

| 脚本                   | 状态    | 说明                                                     |
| ---------------------- | ------- | -------------------------------------------------------- |
| `sign_seed.py`       | ✅ 完成 | OpenSSL 双重 hash 修复 + DER→P1363 截断修复             |
| `verify_image.py`    | ✅ 完成 | 新增 Reset Handler + 公钥一致性检查 +`--slot A/B` 参数 |
| `zcanpro_ext_ota.py` | ✅ 完成 | NRC 0x78 处理 + 线程硬超时 + 超时重发机制                |
| ECDSA 签名             | ✅ 完成 | Python ecdsa 库 + OpenSSL 双路径，P1363 格式输出         |
| 镜像打包               | ✅ 完成 | 现场打包（裸 bin + XATO 头）或读取已打包 ota.bin         |
