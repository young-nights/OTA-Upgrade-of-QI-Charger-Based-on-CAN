# 车载 Qi 无线充电模块 — OTA 升级方案讨论总结

> **文档编号**: QI-OTA-SUMMARY-001
> **版本**: V1.0
> **日期**: 2026-08-15
> **主控芯片**: AT32F426KBU7-4 (CAN MCU)
> **开发框架**: 裸机（Bare Metal）

---

## 一、背景

本项目采用**双 MCU 架构**：CAN MCU (AT32F426) 负责 CAN 通信、诊断和 GPIO 控制；Qi 无线充芯片负责充电核心功能。系统需要支持两个独立的 OTA 升级通道：

1. **MCU 自身 OTA**：通过 CAN/UDS 升级 AT32F426 的固件
2. **Qi 芯片 OTA**：通过 MCU 桥接（CAN → UART）升级 Qi 无线充芯片固件

---

## 二、SRS 文档扫描结果

### 2.1 文档清单

| 文档 | 关键内容 |
|------|----------|
| `Qi Wireless Charger SRS update 0723.txt` | 协议更新：DID 0xF189→0xF195；SecurityAccess 从 Ed25519 迁移到 **ECDSA P-256**；附带 OTA 上位机模拟器 |
| `common_can_protocol_spec_cn_翻译0517.docx` | 通用 CAN/UDS/ISO-TP 协议架构：固件下载流程、双槽布局、试启动回滚、掉电恢复 |
| `qi_charger_srs_cn_翻译0517.docx` | Qi 充电器专用 SRS：节点地址 0x0D、充电状态机、DID 定义、OTA 需求 |
| `qi_srs_review_todo 20260714.xlsx` | 审查待办清单 |

### 2.2 安全访问变更（重要）

**SecurityAccess 从 Ed25519 → ECDSA P-256**（SRS update 0723）：

- seed 先用 SHA-256 哈希，再签名
- 签名格式：R‖S 固定 64 字节（IEEE P1363，非 ASN.1 DER）
- 公钥：65 字节非压缩 SEC1 点（0x04 ‖ X ‖ Y）
- 私钥：32 字节标量
- 公钥预置到模块中，不匹配返回 NRC 0x35

---

## 三、MCU 自身 OTA 升级方案

### 3.1 UDS 固件下载流程（SRS §9 / common §9.1）

```
0x10 0x02       →  进入 Programming Session
0x27            →  SecurityAccess Level 1 (ECDSA P-256)
0x2E 0x2010     →  选择固件类型 (APP=0x01, Bootloader=0x03)
0x31 0x01 0xFF00 →  擦除目标存储区
0x34            →  RequestDownload (地址+大小)
0x36            →  TransferData (块序号从0x01开始，0xFF后回绕到0x01)
0x37            →  RequestTransferExit
0x11            →  CCUReset (激活新镜像)
0x22 0xF195     →  回读版本确认
```

### 3.2 Flash 布局（128KB）

```
+---------------------------+ 0x08000000
|     Bootloader (16KB)     |
+---------------------------+ 0x08004000
|     APP_A (48KB)          |  ← 主应用区
+---------------------------+ 0x08010000
|     APP_B (48KB)          |  ← 备份区 (双槽 A/B)
+---------------------------+ 0x0801C000
|     OTA 标志位 (16KB)      |  ← 升级状态/元数据
+---------------------------+ 0x08020000
```

| 区域 | 起始地址 | 大小 | 用途 |
|------|----------|------|------|
| Bootloader | 0x08000000 | 16KB | 启动引导、OTA 逻辑、签名验证 |
| APP_A | 0x08004000 | 48KB | 主应用程序 |
| APP_B | 0x08010000 | 48KB | 备份槽（MCU 自身 OTA） |
| OTA 标志位 | 0x0801C000 | 16KB | OTA 请求标志、升级状态、启动元数据 |

### 3.3 双槽 A/B 布局要求（SRS §12.2 / common §9.4）

| 要求 | 说明 |
|------|------|
| 活跃槽在下载期间不可擦除 | 新固件只写入非活跃槽 |
| 每个槽包含独立可执行镜像 | 镜像长度、版本、CRC32、签名、有效性状态 |
| 启动元数据更新必须原子/掉电安全 | 任何时刻掉电都必须有确定性的有效状态 |

### 3.4 试启动与回滚（SRS §12.2 / common §9.5）

| 参数 | 默认值 |
|------|--------|
| 试启动确认超时 | 10s |
| 重试限制 | 3 次失败启动 |
| 试启动期间功率输出 | 禁用 |
| 回滚机制 | 新镜像未确认 → 回滚到之前有效槽 |

### 3.5 掉电恢复要求（SRS §12 / common §9.6）

| 故障点 | 恢复行为 |
|--------|----------|
| RequestDownload 之前掉电 | 启动之前的有效镜像 |
| TransferData 期间掉电 | 启动之前的有效镜像；下载目标无效 |
| 验证期间掉电 | 启动之前的有效镜像 |
| CRC 或签名无效 | 拒绝镜像，保留之前的有效镜像 |
| 写入待定激活后、复位前掉电 | Bootloader 原子元数据决定有效槽 |
| 试启动期间掉电 | 重试试用启动直到重试次数上限，然后回滚 |
| 新镜像确认前看门狗复位 | 按重试次数上限重试后回滚 |

### 3.6 OTA 相关 DID（SRS §7）

| DID | 名称 | 说明 |
|-----|------|------|
| 0x2010 | 固件类型 | 读/写，0x01=APP, 0x03=Bootloader |
| 0x2112 | OTA 状态 | 0x00=空闲, 0x01=下载中, 0x02=验证中, 0x03=待激活, 0x04=试启动, 0x05=已确认, 0x06=已回滚, 0x07=失败 |
| 0x2113 | 活动固件槽位 | 0x00=A, 0x01=B, 0xFF=无效 |
| 0x2114 | 待定固件槽位 | 0x00=A, 0x01=B, 0xFE=无, 0xFF=无效 |
| 0x2115 | 最近启动原因 | 0x00=上电, 0x01=UDS, 0x02=WDG, 0x03=OTA激活, 0x04=OTA回滚, 0x05=掉电 |
| 0x2116 | 回滚计数器 | 自制造以来的 OTA 回滚次数 |
| 0xF189 | 软件版本 | 活动槽位的语义版本（MAJOR.MINOR.PATCH） |
| 0xF18D | Bootloader 版本 | 读 |

---

## 四、Qi 芯片 OTA 升级方案（通过 MCU 桥接）

### 4.1 架构选择：实时中继

```
CCU ──CAN──→ MCU (AT32F426) ──UART──→ Qi 芯片
              (实时转发，不存储)
```

**选择实时中继的原因**：

AT32F426 只有 128KB Flash，MCU 自身 OTA 双槽布局已占满，没有额外空间缓存 Qi 固件：

| 区域 | 大小 | 用途 |
|------|------|------|
| Bootloader | 16KB | 启动引导 + OTA 逻辑 |
| APP_A | 48KB | 主应用 |
| APP_B | 48KB | 备份槽（MCU 自身 OTA） |
| OTA 标志位 | 16KB | 升级状态 |
| **剩余** | **0KB** | 无法存储 Qi 固件 |

因此 MCU 只能做「透传桥梁」，不能缓存 Qi 固件。

### 4.2 Qi 芯片 UART IAP 协议

MCU 与 Qi 芯片之间通过 UART 通信，帧格式：

| 字段 | 长度 | 说明 |
|------|------|------|
| 帧起始头 | 2 字节 | 固定 `0x55 0xAA` |
| 帧长度 | 1 字节 | 仅包含数据长度 |
| 帧命令 | 1 字节 | `0xCC` = IAP 流程 |
| 帧数据 | n 字节 | IAP 子命令 + 数据 |
| 帧流水号 | 1 字节 | 1~255，递增循环 |
| 帧校验 | 1 字节 | 累加和取低 8 位 |

IAP 子命令：

| 子命令 | 方向 | 含义 |
|--------|------|------|
| `0x01` | MCU → Qi | 准备升级（含固件大小） |
| `0x02` | MCU → Qi | 发送固件数据（含地址 + 最多 22 字节数据） |

### 4.3 Qi 芯片 OTA 完整流程

```
CCU (Host)                MCU (AT32F426)              Qi 芯片
    │                          │                          │
    │── CAN: 0x10 0x02 ──────→│                          │
    │   (进入编程会话)          │                          │
    │                          │                          │
    │── CAN: 0x27 (ECDSA) ──→│                          │
    │   (安全认证)              │                          │
    │                          │                          │
    │── CAN: 0x2E 0x2010 ───→│                          │
    │   (选择固件类型=Qi)       │                          │
    │                          │                          │
    │── CAN: 0x34 (下载请求) ─→│                          │
    │                          │                          │
    │── CAN: 0x36 (数据[0]) ─→│── UART: 0xCC 0x01 ─────→│
    │                          │   (IAP准备, 含固件大小)    │
    │                          │←── UART: ACK ────────────│
    │                          │                          │
    │── CAN: 0x36 (数据[1]) ─→│── UART: 0xCC 0x02 ─────→│
    │                          │   (数据: addr + 22字节)   │
    │                          │←── UART: ACK ────────────│
    │                          │                          │
    │        ... 逐帧转发 ...   │                          │
    │                          │                          │
    │── CAN: 0x36 (数据[N]) ─→│── UART: 0xCC 0x02 ─────→│
    │                          │←── UART: ACK ────────────│
    │                          │                          │
    │── CAN: 0x37 (传输结束) ─→│                          │
    │── CAN: 0x11 (复位) ────→│── 复位 Qi 芯片 ─────────→│
```

**关键点**：

- MCU 收到每个 CAN `0x36` TransferData 帧后，**实时**打包成 UART `0xCC 0x02` 帧发给 Qi 芯片
- Qi 芯片的 UART IAP 协议每帧最多传输 **22 字节**数据
- MCU 不需要存储 Qi 固件，只做协议转换
- 两个 OTA 过程（MCU 自身 + Qi 芯片）是**独立的**

### 4.4 优缺点分析

| 维度 | 实时中继（当前方案） | 预下载到 MCU（备选） |
|------|---------------------|---------------------|
| Flash 占用 | 不占用 | 需要额外 20~50KB |
| MCU Flash 空间 | 够用（128KB 双槽已满） | 不够用 |
| 链路依赖 | CAN 和 UART 必须同时在线 | 两阶段解耦 |
| UART 失败重试 | 需要从头重新传输 | 可从 MCU Flash 重试 |
| 实现复杂度 | 低 | 高（需管理 Qi 固件存储区） |

---

## 五、两个 OTA 通道的关系

| 维度 | MCU 自身 OTA | Qi 芯片 OTA |
|------|-------------|-------------|
| 升级对象 | AT32F426 固件 | Qi 无线充芯片固件 |
| 传输链路 | CCU → CAN → MCU | CCU → CAN → MCU → UART → Qi |
| MCU 角色 | OTA 目标节点 | 透传桥梁 |
| 存储需求 | 双槽 A/B（96KB） | 无（实时转发） |
| 签名算法 | ECDSA P-256 | 由 Qi 芯片自行验证（如有） |
| 回滚机制 | 双槽试启动 + 自动回滚 | Qi 芯片自身机制 |
| 会话要求 | Programming Session + SecurityAccess Level 1 | 同左 |
| 互斥性 | OTA 期间停止充电、停止广播 | 同左 |

**注意**：两个 OTA 不能同时进行。MCU 进入 Programming Session 后，充电功率输出和周期性广播均停止。

---

## 六、上位机模拟器

SRS update 0723 附带了 Python OTA 上位机模拟器，可用于开发联调：

### 6.1 环境准备

```bash
# Python 3.8+
pip3 install -r requirements.txt
# 依赖：python-can、can-isotp、cryptography
```

### 6.2 离线自检（无需 CAN 硬件）

```bash
cd qi_charger/simulator/ecdsa-p256-keys
python3 test_ecdsa_crypto.py     # 验证签名/验签及 0x27 流程
python3 generate_keypair.py      # 生成测试密钥对
```

### 6.3 运行 OTA（以 CANalyst-II 为例）

```bash
# 第一阶段 —— 简单安全版客户端（seed+0x5555）
python3 qi_upgrade_client.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --firmware /path/to/qi_app.bin --type app --verbose

# 第二阶段 —— ECDSA P-256 客户端
python3 qi_upgrade_client_ecdsa.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --keypair ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin \
    --firmware /path/to/qi_app.bin --type app --verbose
```

### 6.4 建议开发顺序

1. **第一阶段**：用简单版客户端（seed+0x5555）打通 OTA 主干流程（ISO-TP 分帧、会话控制、固件类型选择、擦除、下载、复位、版本回读）
2. **第二阶段**：切换到 ECDSA P-256，仅修改 SecurityAccess 步骤

---

## 七、CAN 配置参数

| 参数 | 值 |
|------|-----|
| CAN 标准 | Classical CAN 2.0B |
| 波特率 | 250 kbps |
| 诊断 ID 格式 | 29 位扩展帧 |
| UDS 请求（CCU → Qi） | 0x18DA0D03 |
| UDS 响应（Qi → CCU） | 0x18DA030D |
| 生命周期广播 | 0x18FF260D |
| UDS 服务端地址 | 0x0D（Qi 充电器） |
| UDS 客户端地址 | 0x03（CCU） |

---

## 八、变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| V1.0 | 2026-08-15 | 初稿，基于 SRS 文档扫描和方案讨论整理 | Mr.Hu |
