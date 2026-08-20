# ZCANPRO OTA 配置指南

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档标题 | ZCANPRO CAN-UDS OTA ECU 刷写配置指南 |
| 适用平台 | AT32F426 MCU，Qi 无线充电模块 |
| 协议标准 | CAN 2.0B，ISO 14229 (UDS)，CAN-UDS |
| 工具版本 | ZCANPRO（周立功 CAN 分析工具） |
| 创建日期 | 2026-08-20 |

## 目录

1. [概述](#一概述)
2. [硬件准备](#二硬件准备)
3. [ZCANPRO 基本配置](#三zcanpro-基本配置)
4. [UDS 诊断配置](#四uds-诊断配置)
5. [固件文件准备](#五固件文件准备)
6. [Python 脚本方案（推荐）](#六python-脚本方案推荐)
7. [常见问题与排查](#七常见问题与排查)
8. [附录](#八附录)

---

## 一、概述

### 1.1 ZCANPRO 简介

ZCANPRO 是周立功（ZLG）公司开发的 CAN 总线分析与调试工具，支持 CAN/CANFD 报文收发、UDS 诊断、ECU 刷写等功能。本项目使用 ZCANPRO 的 UDS 诊断功能，通过 CAN-UDS 协议对 Qi 无线充电模块（AT32F426 MCU）进行 OTA 固件升级。

### 1.2 本文档目的

本文档提供 ZCANPRO 的完整配置步骤，用于实现 CAN-UDS 协议下的 ECU 刷写（OTA 升级）流程。涵盖：

- ZCANPRO 的基本 CAN 通信配置
- UDS 诊断参数与刷写流程配置
- SecurityAccess 签名传输的特殊处理
- 固件准备与 Python 脚本替代方案

### 1.3 适用硬件

| 参数 | 值 |
|------|-----|
| MCU | AT32F426KBU7 |
| CAN 标准 | Classical CAN 2.0B |
| 波特率 | 250 kbps |
| 帧格式 | 扩展帧（29-bit ID） |
| UDS 请求 ID | `0x18DA0D03` |
| UDS 响应 ID | `0x18DA030D` |

---

## 二、硬件准备

### 2.1 CAN 分析仪

推荐使用以下 ZLG 设备：

| 设备型号 | 说明 |
|----------|------|
| USBCAN-II | 双通道 CAN 分析仪，性价比高 |
| USBCAN-FD | 支持 CAN/CANFD，兼容性好 |
| USBCAN-2E-U | 经济型，支持标准 CAN |

### 2.2 接线说明

```
CAN 分析仪          Qi 充电模块
──────────          ───────────
  CANH  ───────────  CANH
  CANL  ───────────  CANL
  GND   ───────────  GND
```

### 2.3 终端电阻

CAN 总线两端必须各接 120Ω 终端电阻：

- **CAN 分析仪端**：使用设备内置 120Ω 电阻（通过跳线或软件启用）
- **Qi 充电模块端**：PCB 上已焊接 120Ω 电阻
- 使用万用表测量 CANH 与 CANL 之间电阻，应为 ~60Ω（两个 120Ω 并联）

### 2.4 注意事项

- 接线时确保 GND 连接，避免共模电压偏移
- CAN 总线长度尽量短（<1m 为佳）
- 刷写过程中不要断开 CAN 连接
- 建议使用屏蔽双绞线

---

## 三、ZCANPRO 基本配置

### 3.1 启动 ZCANPRO

1. 打开 ZCANPRO 软件
2. 连接 CAN 分析仪到 PC USB 端口

### 3.2 设备选择

1. 点击菜单栏「设备」→「打开设备」
2. 在设备列表中选择已连接的 USBCAN 设备
3. 选择对应的 CAN 通道（通常为 CAN1）

### 3.3 波特率配置

1. 在设备配置界面中设置波特率：**250 kbps**
2. 其他参数保持默认：
   - 工作模式：正常模式
   - 验收滤波：关闭（或配置为接受所有帧）

### 3.4 帧格式配置

1. 设置帧格式为 **扩展帧（Extended Frame）**
   - CAN 2.0B，29-bit ID
   - 这是 UDS 通信所要求的帧格式
2. 数据长度：8 字节

### 3.5 启动 CAN 通道

1. 点击「启动」按钮，CAN 通道开始工作
2. 在发送窗口发送测试帧，确认通信正常
3. 观察接收窗口是否有总线上的报文

---

## 四、UDS 诊断配置（关键章节）

### 4.1 基本参数配置

进入 ZCANPRO 的「UDS 诊断」或「ECU 刷写」功能模块，配置以下基本参数：

| 参数 | 值 | 说明 |
|------|-----|------|
| 请求 ID | `0x18DA0D03` | Host → ECU 的 UDS 请求地址 |
| 响应 ID | `0x18DA030D` | ECU → Host 的 UDS 响应地址 |
| 帧格式 | 扩展帧 (29-bit) | 必须使用扩展帧 |
| P2 超时 | 5000 ms | UDS 服务响应超时 |
| P2* 超时 | 5000 ms | NRC 0x78 后的扩展超时 |
| 数据长度 | 8 字节 | Classical CAN 固定 8 字节 |

> **重要**：P2 超时必须设为 5000ms 或更大。ECDSA P-256 签名验证（Step 5）在 MCU 上需要 200-800ms，Flash 擦除（Step 7）也需要一定时间。

### 4.2 刷写流程配置

以下是完整的 OTA 刷写流程，每一步在 ZCANPRO 中作为独立的诊断服务配置。

#### Step 1：触发 OTA — ECUReset

| 项目 | 内容 |
|------|------|
| 服务名称 | ECUReset |
| 服务 ID | `0x11` |
| 子功能 | `0x01`（硬复位） |
| 发送数据 | `11 01` |
| 期望响应 | `51 01`（肯定响应） |
| 超时 | 5000 ms |
| 说明 | APP 保存 `ota_state=DOWNLOADING` 后复位，Bootloader 检测到该状态后进入 Safe Mode |

#### Step 2：切换诊断会话 — DiagnosticSessionControl

| 项目 | 内容 |
|------|------|
| 服务名称 | DiagnosticSessionControl |
| 服务 ID | `0x10` |
| 子功能 | `0x02`（Programming Session） |
| 发送数据 | `10 02` |
| 期望响应 | `50 02` |
| 超时 | 5000 ms |
| 说明 | 切换到编程会话，解锁 Flash 写入权限 |

#### Step 3：安全访问 — 请求种子 (SecurityAccess Request Seed)

| 项目 | 内容 |
|------|------|
| 服务名称 | SecurityAccess |
| 服务 ID | `0x27` |
| 子功能 | `0x01`（请求种子） |
| 发送数据 | `27 01` |
| 期望响应 | `67 01 [seed0] [seed1] [seed2] [seed3]`（4 字节 seed） |
| 超时 | 5000 ms |
| 说明 | MCU 返回 4 字节随机 seed，用于后续 ECDSA 签名计算 |

#### Step 4：安全访问 — 传输签名块 (SecurityAccess Transfer Signature)

| 项目 | 内容 |
|------|------|
| 服务名称 | SecurityAccess |
| 服务 ID | `0x27` |
| 子功能 | `0x03`（传输签名数据块） |
| 发送数据 | `27 03 [blockSeq] [6字节签名数据]` |
| 期望响应 | `67 03 [blockSeq]` |
| 超时 | 2000 ms / 帧 |
| 说明 | 将 64 字节 ECDSA 签名分 ~11 帧传输，详见 [4.3 节](#43-securityaccess-特殊处理) |

**分帧传输详情**：

64 字节签名 ÷ 6 字节/帧 = 10 帧完整 + 1 帧（4 字节 + 2 字节 padding）

| 帧序号 | blockSeq | 数据内容 |
|--------|----------|----------|
| 第 1 帧 | `0x01` | `27 03 01 [sign[0:6]]` |
| 第 2 帧 | `0x02` | `27 03 02 [sign[6:12]]` |
| 第 3 帧 | `0x03` | `27 03 03 [sign[12:18]]` |
| ... | ... | ... |
| 第 10 帧 | `0x0A` | `27 03 0A [sign[54:60]]` |
| 第 11 帧 | `0x0B` | `27 03 0B [sign[60:64] 00 00]`（尾部补零） |

#### Step 5：安全访问 — 验证签名 (SecurityAccess Verify Signature)

| 项目 | 内容 |
|------|------|
| 服务名称 | SecurityAccess |
| 服务 ID | `0x27` |
| 子功能 | `0x02`（验证签名） |
| 发送数据 | `27 02` |
| 期望响应 | `67 02`（成功） |
| 超时 | 8000 ms |
| 说明 | MCU 使用预置公钥验证 `SHA256(seed)` + 签名。验签需要 200-800ms |

**可能的错误响应**：

| NRC | 含义 | 原因 |
|-----|------|------|
| `0x33` (SecurityAccessDenied) | 安全访问被拒绝 | 签名验证失败，检查密钥对是否匹配 |
| `0x35` (ExceededNumberOfAttempts) | 尝试次数超限 | 需要重新上电重置尝试计数器 |

#### Step 6：写数据 — WriteDataByIdentifier

| 项目 | 内容 |
|------|------|
| 服务名称 | WriteDataByIdentifier |
| 服务 ID | `0x2E` |
| 数据标识符 | `0x2010` |
| 发送数据 | `2E 20 10 [typeByte]` |
| 期望响应 | `6E 20 10 [typeByte]` |
| 超时 | 5000 ms |
| 说明 | 选择固件类型 |

**typeByte 取值**：

| 值 | 类型 | 说明 |
|----|------|------|
| `0x00` | APP | 应用程序固件（常规 OTA） |
| `0x01` | Bootloader | Bootloader 固件（升级引导程序） |

#### Step 7：擦除内存 — RoutineControl Erase Memory

| 项目 | 内容 |
|------|------|
| 服务名称 | RoutineControl |
| 服务 ID | `0x31` |
| 子功能 | `0x01`（启动例程） |
| 例程 ID | `0xFF00`（擦除内存） |
| 发送数据 | `31 01 FF 00` |
| 期望响应 | `71 01 FF 00` |
| 超时 | 10000 ms |
| 说明 | 擦除 APP_A 全部 24 个 2KB 扇区（共 48KB） |

> **注意**：Flash 擦除可能需要较长时间，P2 超时应设置足够大（≥10s）。

#### Step 8：请求下载 — RequestDownload

| 项目 | 内容 |
|------|------|
| 服务名称 | RequestDownload |
| 服务 ID | `0x34` |
| 发送数据 | `34 00 44 00 00 C0 00` |
| 期望响应 | `74 20 00 C0 00` |
| 超时 | 5000 ms |
| 说明 | 初始化下载状态。注意：此步骤不执行擦除，擦除已在 Step 7 完成 |

**数据格式解析**：

```
34        - SID (RequestDownload)
00        - dataFormatIdentifier (无压缩、无加密)
44        - addressAndLengthFormatIdentifier (4字节地址 + 4字节长度)
00 00     - memoryAddress (高 2 字节)
C0 00     - memorySize (低 2 字节 = 0xC000 = 49152 = 48KB)
```

#### Step 9：数据传输 — TransferData

| 项目 | 内容 |
|------|------|
| 服务名称 | TransferData |
| 服务 ID | `0x36` |
| 发送数据 | `36 [blockSeq] [6字节固件数据]` |
| 期望响应 | `76 [blockSeq]` |
| 超时 | 2000 ms / 帧 |
| 说明 | 逐帧传输固件数据，详见 [4.4 节](#44-transferdata-块序号配置) |

**传输参数**：

| 参数 | 值 |
|------|-----|
| 每帧有效载荷 | 6 字节 |
| 总帧数（48KB 固件） | ~8192 帧 |
| blockSeq 起始值 | `0x01` |
| blockSeq 递增方式 | +1 每帧 |
| blockSeq 回绕规则 | `0xFF` → `0x00` → `0x01` |

#### Step 10：传输签名 — TransferSignature

| 项目 | 内容 |
|------|------|
| 服务名称 | TransferSignature |
| 服务 ID | `0x38` |
| 发送数据 | `38 [blockSeq] [6字节签名数据]` |
| 期望响应 | `78 [blockSeq]` |
| 超时 | 2000 ms / 帧 |
| 说明 | 传输固件的 ECDSA P-256 签名（64 字节），分 ~11 帧 |

**分帧方式与 Step 4 相同**：

| 帧序号 | blockSeq | 数据内容 |
|--------|----------|----------|
| 第 1 帧 | `0x01` | `38 01 [sign[0:6]]` |
| 第 2 帧 | `0x02` | `38 02 [sign[6:12]]` |
| ... | ... | ... |
| 第 11 帧 | `0x0B` | `38 0B [sign[60:64] 00 00]`（尾部补零） |

#### Step 11：请求传输退出 — RequestTransferExit

| 项目 | 内容 |
|------|------|
| 服务名称 | RequestTransferExit |
| 服务 ID | `0x37` |
| 发送数据 | `37` |
| 期望响应 | `77` |
| 超时 | 10000 ms |
| 说明 | MCU 计算 CRC32，写 image_header（256 字节），更新 metadata |

> **注意**：此步骤涉及 Flash 写入操作，耗时较长，P2 超时应 ≥ 10s。

#### Step 12：ECU 复位

| 项目 | 内容 |
|------|------|
| 服务名称 | ECUReset |
| 服务 ID | `0x11` |
| 子功能 | `0x01`（硬复位） |
| 发送数据 | `11 01` |
| 期望响应 | `51 01` |
| 超时 | 5000 ms |
| 说明 | 看门狗复位，MCU 重启后加载新固件 |

#### Step 13：版本回读验证

| 项目 | 内容 |
|------|------|
| 服务名称 | ReadDataByIdentifier |
| 服务 ID | `0x22` |
| 数据标识符 | `0xF195` |
| 发送数据 | `22 F1 95` |
| 期望响应 | `62 F1 95 [版本字符串]` |
| 超时 | 5000 ms |
| 说明 | 等待复位完成（~2 秒）后回读版本号 |

**示例**：如果固件版本为 `1.0.0`，则响应为 `62 F1 95 31 2E 30 2E 30`（ASCII 编码）。

### 4.3 SecurityAccess 特殊处理

#### 问题说明

ZCANPRO 的标准 UDS 刷写工具通常只支持简单的种子-密钥（Seed-Key）模式的 SecurityAccess。而本项目的 SecurityAccess 使用 **ECDSA P-256 签名验证**，流程为三步：

1. `0x27 0x01`：请求种子（标准）
2. `0x27 0x03`：分帧传输 64 字节 ECDSA 签名（**非标准**）
3. `0x27 0x02`：请求验证签名（标准）

其中第 2 步的分帧签名传输是 ZCANPRO 标准功能可能不支持的。

#### 方案 A：ZCANPRO 自定义脚本/DLL 插件

如果 ZCANPRO 版本支持自定义 DLL 插件或脚本功能：

1. 编写 DLL 插件，实现 `0x27 0x03` 的分帧传输逻辑
2. 在 ZCANPRO 的 SecurityAccess 配置中加载该 DLL
3. DLL 需要实现：
   - 读取签名文件
   - 分割为 6 字节块
   - 逐帧发送并校验响应

#### 方案 B：Python 脚本（推荐）

由于 ZCANPRO 对自定义 SecurityAccess 的支持有限，**推荐使用 Python 脚本直接控制 CAN 通信**。详见 [第六章](#六python-脚本方案推荐)。

### 4.4 TransferData 块序号配置

#### blockSeq 规则

```
起始值:  0x01
递增:    每帧 +1
回绕:    0xFF → 0x00 → 0x01 → ... （继续递增）
```

#### 配置要点

| 配置项 | 值 |
|--------|-----|
| 起始序号 | `0x01` |
| 递增步长 | 1 |
| 回绕规则 | `0xFF` → `0x00`（ZCANPRO 的 blockSeq 配置应选择"循环"模式） |
| 每帧最大数据 | 6 字节（8 字节 CAN 帧 - 1 字节 SID - 1 字节 blockSeq） |

#### ZCANPRO 配置步骤

1. 在 TransferData 服务配置中，设置 blockSeq 参数
2. 选择"自动递增"模式
3. 设置起始值为 `0x01`
4. 确认回绕模式为循环（0xFF 后变为 0x00）

#### 数据帧示例

```
第 1 帧:  36 01 [firmware_byte_0..5]    ← blockSeq = 0x01
第 2 帧:  36 02 [firmware_byte_6..11]   ← blockSeq = 0x02
...
第255帧:  36 FF [firmware_byte_1524..1529] ← blockSeq = 0xFF
第256帧:  36 00 [firmware_byte_1530..1535] ← blockSeq = 0x00 (回绕)
第257帧:  36 01 [firmware_byte_1536..1541] ← blockSeq = 0x01
...
```

---

## 五、固件文件准备

### 5.1 固件格式

- 文件格式：纯二进制（`.bin`）
- 大小限制：≤ 48640 字节（48KB - 256 字节 image_header = 48384 字节有效载荷）
- 字节序：Little-Endian（与 AT32F426 一致）

### 5.2 签名文件生成

#### 使用 OpenSSL 生成 ECDSA P-256 密钥对

```bash
# 生成私钥
openssl ecparam -genkey -name prime256v1 -noout -out docs/keys/private.pem

# 从私钥导出公钥
openssl ec -in docs/keys/private.pem -pubout -out docs/keys/public.pem

# 查看私钥信息
openssl ec -in docs/keys/private.pem -text -noout
```

#### 使用 OpenSSL 对固件签名

```bash
# 计算 SHA256 摘要
openssl dgst -sha256 -binary firmware.bin > firmware.sha256

# 使用 ECDSA 私钥签名（IEEE P1363 格式，64 字节 R‖S）
# 注意: OpenSSL 默认输出 DER 格式，需转换为 IEEE P1363
openssl pkeyutl -sign -inkey docs/keys/private.pem -in firmware.sha256 \
    -pkeyopt digest:sha256 -out firmware.sig.der

# 将 DER 格式转换为 IEEE P1363 (raw R‖S, 64 字节)
# 使用 Python 脚本完成转换（见下文）
```

#### 使用 Python 生成签名（推荐）

```python
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.backends import default_backend
import hashlib

# Load private key
with open("docs/keys/private.pem", "rb") as f:
    private_key = serialization.load_pem_private_key(f.read(), password=None, backend=default_backend())

# Read firmware binary
with open("firmware.bin", "rb") as f:
    firmware = f.read()

# Compute SHA256 digest
digest = hashlib.sha256(firmware).digest()

# Sign with ECDSA P-256, output as IEEE P1363 (raw R||S, 64 bytes)
signature = private_key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))

# Convert DER to IEEE P1363 format
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
r, s = decode_dss_signature(signature)
sig_bytes = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')  # 64 bytes total

with open("firmware.sig", "wb") as f:
    f.write(sig_bytes)

print(f"Firmware size: {len(firmware)} bytes")
print(f"Signature size: {len(sig_bytes)} bytes")
print(f"Signature (hex): {sig_bytes.hex()}")
```

### 5.3 私钥位置

| 文件 | 路径 | 说明 |
|------|------|------|
| ECDSA 私钥 | `docs/keys/private.pem` | 用于签名固件 |
| ECDSA 公钥 | `docs/keys/public.pem` | 预置在 MCU Bootloader 中 |
| 签名文件 | `firmware.sig` | 64 字节，IEEE P1363 格式 |

> **安全警告**：私钥必须妥善保管，切勿泄露。生产环境中应使用 HSM 或安全密钥管理系统。

---

## 六、Python 脚本方案（推荐）

由于 ZCANPRO 的 UDS 刷写工具可能不支持自定义 `0x27 0x03` 子功能的分帧签名传输，**推荐使用 Python 脚本通过 ZLG 的 CAN 接口库直接控制整个 OTA 流程**。

### 6.1 环境准备

```bash
# 安装 python-can（支持 ZLG 设备）
pip install python-can

# 安装 cryptography 库（用于 ECDSA 签名）
pip install cryptography

# 安装 ZLG 驱动（根据设备型号选择）
# USBCAN-II: 安装 ZLG 驱动
# USBCAN-FD: 安装 ZCANPRO 驱动
```

### 6.2 完整 Python 脚本框架

```python
#!/usr/bin/env python3
"""
OTA ECU Flashing Script for Qi Charger (AT32F426) via CAN-UDS.
Uses python-can with ZLG USBCAN adapter.
"""

import can
import time
import hashlib
import struct
from pathlib import Path

# -- CAN/UDS Configuration ------------------------------------------------
CAN_CHANNEL = 0                    # CAN channel index (0 or 1)
CAN_BITRATE = 250000               # 250 kbps
UDS_REQ_ID = 0x18DA0D03            # Host -> ECU (29-bit extended)
UDS_RESP_ID = 0x18DA030D           # ECU -> Host (29-bit extended)
IS_EXTENDED = True                 # 29-bit extended frame
P2_TIMEOUT = 5.0                   # seconds (default UDS response timeout)
P2_EXTENDED_TIMEOUT = 8.0          # seconds (for ECDSA verification)

FIRMWARE_PATH = "firmware.bin"
SIGNATURE_PATH = "firmware.sig"
PRIVATE_KEY_PATH = "docs/keys/private.pem"

BLOCK_SIZE = 6                     # payload bytes per CAN frame for TransferData


class UDSError(Exception):
    """Raised when UDS service returns negative response."""
    def __init__(self, nrc, description=""):
        self.nrc = nrc
        self.description = description
        super().__init__(f"NRC 0x{nrc:02X}: {description}")


# NRC descriptions
NRC_MAP = {
    0x10: "generalReject",
    0x11: "serviceNotSupported",
    0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat",
    0x14: "responseTooLong",
    0x22: "conditionsNotCorrect",
    0x24: "requestSequenceError",
    0x25: "noResponseFromSubnetComponent",
    0x31: "requestOutOfRange",
    0x33: "securityAccessDenied",
    0x35: "exceededNumberOfAttempts",
    0x36: "requiredTimeDelayNotExpired",
    0x70: "uploadDownloadNotAccepted",
    0x71: "transferDataSuspended",
    0x72: "generalProgrammingFailure",
    0x73: "wrongBlockSequenceCounter",
    0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}


def setup_can_bus():
    """Initialize CAN bus with ZLG USBCAN adapter."""
    # For ZLG USBCAN-II / USBCAN-FD, use the 'zlg' interface
    # Adjust 'channel' and 'app_type' based on your specific hardware
    bus = can.interface.Bus(
        interface='zlg',
        channel=CAN_CHANNEL,
        bitrate=CAN_BITRATE,
        extended=IS_EXTENDED,
    )
    return bus


def send_uds(bus, data: bytes, timeout: float = P2_TIMEOUT) -> bytes:
    """
    Send a UDS request and wait for response.

    Args:
        bus: CAN bus interface
        data: UDS request payload (without CAN ID)
        timeout: Response timeout in seconds

    Returns:
        UDS response payload bytes

    Raises:
        UDSError: If negative response received
        TimeoutError: If no response within timeout
    """
    msg = can.Message(
        arbitration_id=UDS_REQ_ID,
        data=data,
        is_extended_id=True,
        dlc=8,
    )
    bus.send(msg)

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        rx = bus.recv(timeout=min(remaining, 0.1))
        if rx and rx.arbitration_id == UDS_RESP_ID:
            resp = bytes(rx.data)
            # Check for negative response
            if resp[0] == 0x7F:
                sid = resp[1]
                nrc = resp[2]
                desc = NRC_MAP.get(nrc, "unknown")
                raise UDSError(nrc, f"SID=0x{sid:02X}, {desc}")
            return resp
    raise TimeoutError(f"No UDS response within {timeout}s")


def step1_trigger_ota(bus):
    """Step 1: ECUReset - trigger OTA mode."""
    print("[Step  1] ECUReset (0x11 0x01) - Trigger OTA...")
    resp = send_uds(bus, bytes([0x11, 0x01]))
    assert resp[0] == 0x51, f"Unexpected response: {resp.hex()}"
    print(f"  Response: {resp.hex()} - OK")
    # Wait for MCU to reset and enter bootloader Safe Mode
    print("  Waiting 3s for MCU reset and bootloader entry...")
    time.sleep(3)


def step2_programming_session(bus):
    """Step 2: DiagnosticSessionControl -> Programming Session."""
    print("[Step  2] DiagnosticSessionControl (0x10 0x02)...")
    resp = send_uds(bus, bytes([0x10, 0x02]))
    assert resp[0] == 0x50 and resp[1] == 0x02, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - OK")


def step3_request_seed(bus) -> bytes:
    """Step 3: SecurityAccess - Request Seed."""
    print("[Step  3] SecurityAccess RequestSeed (0x27 0x01)...")
    resp = send_uds(bus, bytes([0x27, 0x01]))
    assert resp[0] == 0x67 and resp[1] == 0x01, f"Unexpected: {resp.hex()}"
    seed = resp[2:6]
    print(f"  Response: {resp.hex()}")
    print(f"  Seed: {seed.hex()}")
    return seed


def step4_transfer_signature(bus, signature: bytes):
    """
    Step 4: SecurityAccess - Transfer Signature Chunks.

    Sends 64-byte ECDSA signature in 6-byte chunks via 0x27 0x03.
    """
    print("[Step  4] SecurityAccess TransferSignature (0x27 0x03)...")

    # Split 64-byte signature into 6-byte chunks
    chunks = [signature[i:i+BLOCK_SIZE] for i in range(0, len(signature), BLOCK_SIZE)]

    for idx, chunk in enumerate(chunks):
        block_seq = idx + 1  # Start from 0x01

        # Pad last chunk to 6 bytes if needed
        if len(chunk) < BLOCK_SIZE:
            chunk = chunk + b'\x00' * (BLOCK_SIZE - len(chunk))

        data = bytes([0x27, 0x03, block_seq]) + chunk
        resp = send_uds(bus, data, timeout=2.0)
        assert resp[0] == 0x67 and resp[1] == 0x03, f"Unexpected at chunk {block_seq}: {resp.hex()}"
        print(f"  Chunk {block_seq:02X}: sent, response OK")

    print(f"  Transferred {len(chunks)} chunks ({len(signature)} bytes)")


def step5_verify_signature(bus):
    """Step 5: SecurityAccess - Verify Signature."""
    print("[Step  5] SecurityAccess VerifySignature (0x27 0x02)...")
    resp = send_uds(bus, bytes([0x27, 0x02]), timeout=P2_EXTENDED_TIMEOUT)
    assert resp[0] == 0x67 and resp[1] == 0x02, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - Signature verified!")


def step6_write_firmware_type(bus, fw_type: int = 0x00):
    """Step 6: WriteDataByIdentifier - Select firmware type."""
    type_name = "APP" if fw_type == 0x00 else "Bootloader"
    print(f"[Step  6] WriteDataByIdentifier (0x2E 0x2010) - Type: {type_name}...")
    resp = send_uds(bus, bytes([0x2E, 0x20, 0x10, fw_type]))
    assert resp[0] == 0x6E, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - OK")


def step7_erase_memory(bus):
    """Step 7: RoutineControl - Erase Flash memory."""
    print("[Step  7] RoutineControl EraseMemory (0x31 0x01 0xFF00)...")
    print("  Erasing 24 sectors (48KB)... this may take a few seconds.")
    resp = send_uds(bus, bytes([0x31, 0x01, 0xFF, 0x00]), timeout=15.0)
    assert resp[0] == 0x71, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - Erase complete!")


def step8_request_download(bus):
    """Step 8: RequestDownload - Initialize download state."""
    print("[Step  8] RequestDownload (0x34)...")
    # 34 00 44 00 00 C0 00
    # 00 = no compression/encryption
    # 44 = 4-byte addr + 4-byte length format
    # 00 00 = address (high bytes, set by linker)
    # C0 00 = 48KB size (0xC000)
    resp = send_uds(bus, bytes([0x34, 0x00, 0x44, 0x00, 0x00, 0xC0, 0x00]))
    assert resp[0] == 0x74, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - Download initialized!")


def step9_transfer_data(bus, firmware: bytes):
    """
    Step 9: TransferData - Send firmware in 6-byte chunks.

    blockSeq starts at 0x01, increments by 1, wraps 0xFF -> 0x00 -> 0x01.
    """
    print(f"[Step  9] TransferData (0x36) - Sending {len(firmware)} bytes...")

    total_chunks = (len(firmware) + BLOCK_SIZE - 1) // BLOCK_SIZE
    block_seq = 1  # Start from 0x01

    for i in range(0, len(firmware), BLOCK_SIZE):
        chunk = firmware[i:i+BLOCK_SIZE]

        # Pad last chunk if needed
        if len(chunk) < BLOCK_SIZE:
            chunk = chunk + b'\x00' * (BLOCK_SIZE - len(chunk))

        data = bytes([0x36, block_seq & 0xFF]) + chunk
        resp = send_uds(bus, data, timeout=2.0)
        assert resp[0] == 0x76, f"Unexpected at seq {block_seq:02X}: {resp.hex()}"

        # Advance block sequence counter
        block_seq = block_seq + 1
        if block_seq > 0xFF:
            block_seq = 0  # Wrap: 0xFF -> 0x00 (then increments to 0x01 on next)

        # Progress report every 1000 frames
        frame_num = i // BLOCK_SIZE + 1
        if frame_num % 1000 == 0 or frame_num == total_chunks:
            pct = frame_num * 100 // total_chunks
            print(f"  Progress: {frame_num}/{total_chunks} ({pct}%)")

    print(f"  Transfer complete! Sent {total_chunks} frames.")


def step10_transfer_firmware_signature(bus, signature: bytes):
    """Step 10: TransferSignature - Send firmware ECDSA signature."""
    print("[Step 10] TransferSignature (0x38) - Sending firmware signature...")

    chunks = [signature[i:i+BLOCK_SIZE] for i in range(0, len(signature), BLOCK_SIZE)]

    for idx, chunk in enumerate(chunks):
        block_seq = idx + 1
        if len(chunk) < BLOCK_SIZE:
            chunk = chunk + b'\x00' * (BLOCK_SIZE - len(chunk))

        data = bytes([0x38, block_seq & 0xFF]) + chunk
        resp = send_uds(bus, data, timeout=2.0)
        assert resp[0] == 0x78, f"Unexpected at chunk {block_seq}: {resp.hex()}"
        print(f"  Chunk {block_seq:02X}: sent, response OK")

    print(f"  Firmware signature transferred ({len(chunks)} chunks)")


def step11_transfer_exit(bus):
    """Step 11: RequestTransferExit - Finalize and write image header."""
    print("[Step 11] RequestTransferExit (0x37)...")
    print("  MCU computing CRC32 and writing image header (256B)...")
    resp = send_uds(bus, bytes([0x37]), timeout=10.0)
    assert resp[0] == 0x77, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - Transfer exited!")


def step12_ecu_reset(bus):
    """Step 12: ECUReset - Reboot into new firmware."""
    print("[Step 12] ECUReset (0x11 0x01)...")
    resp = send_uds(bus, bytes([0x11, 0x01]))
    assert resp[0] == 0x51, f"Unexpected: {resp.hex()}"
    print(f"  Response: {resp.hex()} - Resetting...")
    print("  Waiting 2s for MCU reboot...")
    time.sleep(2)


def step13_read_version(bus):
    """Step 13: ReadDataByIdentifier - Verify firmware version."""
    print("[Step 13] ReadDataByIdentifier (0x22 0xF195) - Version check...")
    resp = send_uds(bus, bytes([0x22, 0xF1, 0x95]))
    assert resp[0] == 0x62, f"Unexpected: {resp.hex()}"
    version_bytes = resp[3:]  # Skip 62 F1 95
    version = version_bytes.decode('ascii', errors='replace').rstrip('\x00')
    print(f"  Response: {resp.hex()}")
    print(f"  Firmware version: {version}")
    return version


def compute_firmware_signature(firmware_path: str, private_key_path: str) -> bytes:
    """
    Compute ECDSA P-256 signature of firmware binary.

    Returns:
        64-byte signature in IEEE P1363 format (R||S).
    """
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    from cryptography.hazmat.backends import default_backend

    with open(private_key_path, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None, backend=default_backend())

    with open(firmware_path, "rb") as f:
        firmware = f.read()

    digest = hashlib.sha256(firmware).digest()

    # Sign with prehashed SHA256
    der_sig = private_key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))

    # Convert DER to IEEE P1363 (raw R||S)
    r, s = utils.decode_dss_signature(der_sig)
    sig_bytes = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')

    return sig_bytes


def load_firmware_signature(signature_path: str) -> bytes:
    """Load pre-computed signature file (64 bytes, IEEE P1363)."""
    with open(signature_path, "rb") as f:
        sig = f.read()
    assert len(sig) == 64, f"Invalid signature length: {len(sig)} (expected 64)"
    return sig


def main():
    """Main OTA flashing procedure."""
    print("=" * 60)
    print("Qi Charger OTA Flashing Tool")
    print("MCU: AT32F426 | Protocol: CAN-UDS | Baud: 250kbps")
    print("=" * 60)

    # Load firmware
    firmware = Path(FIRMWARE_PATH).read_bytes()
    max_size = 48 * 1024 - 256  # 48KB - 256B header = 48384 bytes
    assert len(firmware) <= max_size, \
        f"Firmware too large: {len(firmware)} > {max_size} bytes"
    print(f"Firmware: {FIRMWARE_PATH} ({len(firmware)} bytes)")

    # Load or compute signature
    if Path(SIGNATURE_PATH).exists():
        signature = load_firmware_signature(SIGNATURE_PATH)
        print(f"Signature: {SIGNATURE_PATH} (loaded)")
    else:
        print(f"Signature file not found, computing from {PRIVATE_KEY_PATH}...")
        signature = compute_firmware_signature(FIRMWARE_PATH, PRIVATE_KEY_PATH)
        print(f"Signature computed ({len(signature)} bytes)")

    # Verify signature size
    assert len(signature) == 64, f"Invalid signature size: {len(signature)}"

    # Setup CAN bus
    print("\nConnecting to CAN bus...")
    bus = setup_can_bus()
    print("CAN bus ready.\n")

    try:
        # Execute OTA flow
        step1_trigger_ota(bus)
        step2_programming_session(bus)
        seed = step3_request_seed(bus)
        step4_transfer_signature(bus, signature)
        step5_verify_signature(bus)
        step6_write_firmware_type(bus, fw_type=0x00)  # APP firmware
        step7_erase_memory(bus)
        step8_request_download(bus)
        step9_transfer_data(bus, firmware)
        step10_transfer_firmware_signature(bus, signature)
        step11_transfer_exit(bus)
        step12_ecu_reset(bus)
        version = step13_read_version(bus)

        print("\n" + "=" * 60)
        print(f"OTA FLASHING COMPLETE!")
        print(f"Firmware version: {version}")
        print("=" * 60)

    except UDSError as e:
        print(f"\n[ERROR] UDS Error: {e}")
        print("Check NRC code in troubleshooting guide (Section 7).")
        raise
    except TimeoutError as e:
        print(f"\n[ERROR] Timeout: {e}")
        print("Check CAN wiring, baud rate, and terminal resistors.")
        raise
    finally:
        bus.shutdown()
        print("CAN bus shut down.")


if __name__ == "__main__":
    main()
```

### 6.3 运行方式

```bash
# 确保 ZLG 驱动已安装
# 将固件文件放在脚本同目录下，命名为 firmware.bin
# 将签名文件放在脚本同目录下，命名为 firmware.sig

python ota_flash.py
```

### 6.4 ZLG 接口适配说明

python-can 对 ZLG 设备的支持取决于操作系统和驱动版本：

| 操作系统 | 接口名 | 备注 |
|----------|--------|------|
| Windows | `zlg` 或 `zlgcan` | 需要安装 ZLG 官方驱动 |
| Linux | `socketcan` | 需要 SocketCAN 驱动支持 |

如果 `python-can` 不直接支持你的 ZLG 设备，可以使用 ZLG 提供的 Python SDK（`zlgcan`）替代：

```python
# 使用 ZLG 官方 Python SDK
import zlgcan

can = zlgcan.ZCAN()
dev = can.OpenDevice(zlgcan.ZCAN_USBCAN2, 0, 0)
# ... 参考 ZLG SDK 文档
```

---

## 七、常见问题与排查

### 7.1 NRC 错误码排查

| NRC | 错误名称 | 含义 | 排查方法 |
|-----|---------|------|----------|
| `0x33` | SecurityAccessDenied | 签名验证失败 | 检查密钥对是否匹配：确认签名用的私钥与 MCU 内预置公钥是同一对 |
| `0x35` | ExceededNumberOfAttempts | 安全访问尝试次数超限 | 重新上电复位 MCU，重置尝试计数器 |
| `0x22` | conditionsNotCorrect | 条件不满足 | 确认 MCU 已进入 Safe Mode（Bootloader），且处于 Programming Session |
| `0x24` | requestSequenceError | 请求顺序错误 | 确认按 Step 1-13 顺序执行，不要跳步 |
| `0x31` | requestOutOfRange | 请求超出范围 | 检查固件大小是否超出 Flash 容量，地址参数是否正确 |
| `0x72` | generalProgrammingFailure | Flash 擦除/写入失败 | 检查 Flash 是否被写保护，尝试重新上电后重试 |
| `0x73` | wrongBlockSequenceCounter | 块序号错误 | 检查 blockSeq 回绕逻辑，确认从 0x01 开始 |
| `0x70` | uploadDownloadNotAccepted | 下载请求被拒绝 | 确认已正确执行 Step 6（选择固件类型）和 Step 7（擦除内存） |

### 7.2 通信问题排查

| 问题 | 可能原因 | 解决方法 |
|------|---------|----------|
| 无响应 | CAN 波特率不匹配 | 确认分析仪和 MCU 都配置为 250 kbps |
| 无响应 | 接线错误 | 检查 CANH/CANL 接线，确认 GND 连接 |
| 无响应 | 终端电阻缺失 | 测量 CANH-CANL 电阻应为 ~60Ω |
| 无响应 | MCU 未进入 Safe Mode | 确认 Step 1 执行成功，MCU 已复位进入 Bootloader |
| 响应 ID 不匹配 | 响应 ID 配置错误 | 确认过滤器包含 `0x18DA030D` |
| 间歇性超时 | 总线干扰 | 缩短线缆长度，使用屏蔽线，检查地线 |

### 7.3 超时问题

| 服务 | 典型耗时 | 建议 P2 超时 |
|------|---------|-------------|
| `0x27 0x02` (验签) | 200-800 ms | ≥ 5000 ms |
| `0x31 0x01` (擦除) | 1-5 秒 | ≥ 10000 ms |
| `0x37` (传输退出) | 1-3 秒 | ≥ 10000 ms |
| 其他服务 | < 100 ms | ≥ 5000 ms |

### 7.4 固件验证

如果 OTA 完成后版本号回读失败：

1. 检查 `Step 13` 是否在复位后等待了足够时间（≥2 秒）
2. 确认固件签名正确（使用正确的私钥签名）
3. 检查固件是否超过 `48384` 字节（48KB - 256B header）
4. 查看 MCU 串口日志（如有调试接口）

---

## 八、附录

### 8.1 UDS 服务码速查表

| 服务码 | 服务名称 | 功能 |
|--------|---------|------|
| `0x10` | DiagnosticSessionControl | 切换诊断会话 |
| `0x11` | ECUReset | ECU 复位 |
| `0x22` | ReadDataByIdentifier | 读取数据标识符 |
| `0x27` | SecurityAccess | 安全访问（种子/签名） |
| `0x2E` | WriteDataByIdentifier | 写入数据标识符 |
| `0x31` | RoutineControl | 例程控制（擦除等） |
| `0x34` | RequestDownload | 请求下载 |
| `0x36` | TransferData | 数据传输 |
| `0x37` | RequestTransferExit | 请求传输退出 |
| `0x38` | TransferSignature | 传输签名（自定义扩展） |

### 8.2 NRC 错误码速查表

| NRC | 名称 | 说明 |
|-----|------|------|
| `0x10` | generalReject | 通用拒绝 |
| `0x11` | serviceNotSupported | 服务不支持 |
| `0x12` | subFunctionNotSupported | 子功能不支持 |
| `0x13` | incorrectMessageLengthOrInvalidFormat | 消息长度错误 |
| `0x14` | responseTooLong | 响应过长 |
| `0x22` | conditionsNotCorrect | 条件不满足 |
| `0x24` | requestSequenceError | 请求顺序错误 |
| `0x25` | noResponseFromSubnetComponent | 子网无响应 |
| `0x31` | requestOutOfRange | 请求超出范围 |
| `0x33` | securityAccessDenied | 安全访问拒绝 |
| `0x35` | exceededNumberOfAttempts | 尝试次数超限 |
| `0x36` | requiredTimeDelayNotExpired | 必要延时未到 |
| `0x70` | uploadDownloadNotAccepted | 上传/下载被拒 |
| `0x71` | transferDataSuspended | 数据传输暂停 |
| `0x72` | generalProgrammingFailure | 编程失败 |
| `0x73` | wrongBlockSequenceCounter | 块序号错误 |
| `0x7E` | subFunctionNotSupportedInActiveSession | 当前会话不支持子功能 |
| `0x7F` | serviceNotSupportedInActiveSession | 当前会话不支持服务 |

### 8.3 帧格式对照

| 格式 | ID 长度 | 数据长度 | 本项目使用 |
|------|---------|---------|-----------|
| 标准帧 (CAN 2.0A) | 11-bit | 0-8 字节 | ❌ |
| 扩展帧 (CAN 2.0B) | 29-bit | 0-8 字节 | ✅ |
| CANFD 帧 | 11/29-bit | 0-64 字节 | ❌ |

### 8.4 参考文档

| 文档 | 说明 |
|------|------|
| ISO 14229-1 | Unified Diagnostic Services (UDS) 规范 |
| ISO 11898 | CAN 总线物理层和数据链路层规范 |
| AT32F426 Datasheet | MCU 硬件规格 |
| ZLG ZCANPRO 用户手册 | ZCANPRO 工具使用说明 |
| RFC 6979 | ECDSA 确定性签名规范 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| v1.0 | 2026-08-20 | — | 初始版本，完整 OTA 刷写流程文档 |

---

## 九、ZCANPRO 手动发送 UDS 命令速查

### 9.1 ZCANPRO 数据列表字段说明

ZCANPRO 连接设备后，数据列表视图显示以下字段：

| 字段 | 说明 |
|------|------|
| 时间标识 | 帧时间戳 |
| 源设备类型 | CAN 分析仪型号 |
| 源设备 | 设备编号 |
| 源通道 | CAN 通道号 |
| 帧ID | CAN 标识符 |
| 帧类型 | 数据帧/远程帧 |
| CAN类型 | 标准帧/扩展帧 |
| 方向 | 发送(Tx)/接收(Rx) |
| 长度 | 数据字节数 |
| 数据 | 十六进制数据 |

### 9.2 触发 APP 进入 OTA 模式

在 APP 运行时发送 ECUReset，使 MCU 复位后 bootloader 进入 Safe Mode：

| 字段 | 值 |
|------|-----|
| 帧ID | `18DA0D03` |
| 帧类型 | 数据帧 |
| CAN类型 | 扩展帧 |
| 长度 | 2 |
| 数据 | `11 01` |

发送后 MCU 会复位，bootloader 检测到 `ota_state == DOWNLOADING` 后进入 Safe Mode。

### 9.3 Safe Mode 下完整 UDS 命令序列

| 步骤 | 说明 | 帧ID | 长度 | 数据 | 期望响应 | 超时 |
|------|------|------|------|------|----------|------|
| ① | 编程会话 | 18DA0D03 | 2 | `10 02` | `50 02` | 2s |
| ② | 请求 Seed | 18DA0D03 | 2 | `27 01` | `67 01` + 4B seed | 2s |
| ③ | 传输签名(×11帧) | 18DA0D03 | 8 | `27 03 seq [6B]` | `67 03 seq` | 每帧 2s |
| ④ | 验签 | 18DA0D03 | 2 | `27 02` | `67 02` | 5s |
| ⑤ | 固件类型 | 18DA0D03 | 4 | `2E 20 10 00` | `6E 20 10 00` | 2s |
| ⑥ | 擦除内存 | 18DA0D03 | 4 | `31 01 FF 00` | `71 01 FF 00` | 10s |
| ⑦ | 下载请求 | 18DA0D03 | 5 | `34 00 44 00 00` | `74 20 00 C0 00` | 2s |
| ⑧ | 传输数据(×8192帧) | 18DA0D03 | 8 | `36 seq [6B]` | `76 seq` | 每帧 2s |
| ⑨ | 传输签名(×11帧) | 18DA0D03 | 8 | `38 seq [6B]` | `78 seq` | 每帧 2s |
| ⑩ | 传输结束 | 18DA0D03 | 1 | `37` | `77` | 2s |
| ⑪ | ECU复位 | 18DA0D03 | 2 | `11 01` | `51 01` | 2s |
| ⑫ | 版本回读 | 18DA0D03 | 3 | `22 F1 95` | `62 F1 95 xx xx` | 2s |

### 9.4 注意事项

- 步骤②返回的 4B seed 需要用于计算 ECDSA 签名：`SHA256(seed)` → 用私钥签名 → 64B
- 步骤③的 blockSeq 从 0x01 开始，每帧递增，最后一帧不足 6B 时补 0x00
- 步骤④验签需要 200-800ms，超时设 5s
- 步骤⑥擦除 24 个 2KB 扇区，约 1-2 秒
- 步骤⑧ 48KB 固件需要 8192 帧，手动不现实，建议用 Python 脚本（见第六章）
- 步骤⑨固件签名也需要预先用私钥对固件 bin 文件计算 ECDSA 签名

### 9.5 单步测试建议

先测试前 4 步（①~④）验证 Safe Mode 进入和 SecurityAccess：

```
发送: 10 02          →  期望: 50 02
发送: 27 01          →  期望: 67 01 [seed0] [seed1] [seed2] [seed3]
... (计算签名并分帧传输) ...
发送: 27 02          →  期望: 67 02 (成功) 或 7F 27 33 (失败)
```

如果 ZCANPRO 数据列表中看到 `67 01` 响应，说明 Safe Mode 已正常进入。

---

## 十、ZCANPRO 界面配置建议

### 10.1 数据列表字段筛选

ZCANPRO 连接设备后，数据列表默认显示以下字段。对于 UDS OTA 刷写场景，部分字段可以隐藏以节省屏幕空间：

| 字段 | UDS OTA 是否需要 | 说明 |
|------|-----------------|------|
| 时间标识 | ✅ 保留 | 排查超时、响应时间需要 |
| 源设备类型 | ❌ 隐藏 | 固定不变，浪费空间 |
| 源设备 | ❌ 隐藏 | 同上 |
| 源通道 | ❌ 隐藏 | 只有一个 CAN 通道 |
| 帧ID | ✅ 保留 | 核心信息，区分请求(0x18DA0D03)/响应(0x18DA030D) |
| 帧类型 | ❌ 隐藏 | 固定为数据帧 |
| CAN类型 | ❌ 隐藏 | 固定为扩展帧 |
| 方向 | ✅ 保留 | 区分发送(Tx)/接收(Rx) |
| 长度 | ✅ 保留 | 判断帧格式 |
| 数据 | ✅ 保留 | 核心，查看 UDS 报文内容 |

建议只保留 **时间标识、帧ID、方向、长度、数据** 5 个字段。

### 10.2 发送模式对比

ZCANPRO 的数据发送分为 4 种模式：

| 模式 | 说明 | UDS OTA 是否适用 |
|------|------|-----------------|
| **普通发送** | 手动填写帧ID和hex数据，逐帧发送 | ✅ **推荐使用** |
| DBC发送 | 用 DBC 文件解析信号值，按信号名填写 | ❌ 不适用 |
| DBC发送(信号变化) | 按信号值变化自动发送 | ❌ 不适用 |
| 文件发送 | 从 .asc/.blf 录制文件回放 | ❌ 不适用 |

**各模式详解**：

**普通发送**：直接填写帧ID（如 `18DA0D03`）和原始 hex 数据（如 `10 02`），点击发送。UDS 诊断报文没有 DBC 定义，必须用原始字节方式发送。UDS OTA 刷写全程使用此模式。

**DBC发送**：需要导入 DBC 文件（CAN 数据库），通过信号名填值，工具自动打包成 CAN 帧。适用于广播报文（如生命周期 0x18FF260D）有 DBC 定义的场景，但 UDS 诊断报文没有 DBC，不适用。

**DBC发送(信号变化)**：在 DBC 发送基础上，可以设置信号值按步长递增/递减自动发送。用于功能测试场景（如模拟温度渐变、电压变化），与 UDS 刷写无关。

**文件发送**：加载之前录制的 .asc 或 .blf 文件，按时间戳回放。用于复现历史问题或自动化回放测试，与手动 UDS 刷写无关。
