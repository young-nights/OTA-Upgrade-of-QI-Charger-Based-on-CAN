# ZCANPRO 扩展脚本 CAN-UDS OTA 使用说明

> **脚本**: 仓库 `qi_wireless_bootloader\tools\zcanpro_ext_ota.py`（可复制到 `D:\Program Files (x86)\ZCANPRO\scripts\`）  
> **入口**: ZCANPRO → 高级功能 → 扩展脚本  
> **Python**: ZCANPRO 要求 **Python 3.8 32 位**  
> **ECU**: AT32F426 Qi 无线充（地址 `0x0D`）  
> **总线**: Classical CAN 2.0B，250 kbps，29-bit 扩展帧

---

## 1. 脚本做什么

在 ZCANPRO 里一键完成 MCU 自身 OTA，不用手动点 UDS、也不用自己组 ISO-TP。流程：

1. （可选）让正在跑的 APP 进 Bootloader  
2. Programming Session + ECDSA P-256 解锁（`0x27 0x01` + 一条 ISO-TP `0x27 0x02` 带 64 字节签名）  
3. 选择 APP 固件类型，擦除非活跃槽  
4. 读 DID `0x2114`，核镜像 Reset Handler 是否指向该槽  
5. 分块下发镜像（`0x34` 长度为 **含 256 字节头的总长**），`TransferExit` 验签  
6. 复位后读 `0x22 F189`，并用 `0x27` 是否 NRC `0x11` 确认已在 APP

ISO-TP 由 ZCANPRO 的 `zcanpro.uds_request()` 完成，接口与官方 `scripts\demo.py` 相同。

---

## 2. 准备文件

Windows 仓库根目录：

```text
I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can
```

| 文件 | 说明 |
|------|------|
| 升级镜像 | Keil 编出的 APP `.bin`：`qi_wireless_code\mdk_project\Objects\qi_wireless.bin`，或已带 XATO 头的 `.ota.bin` |
| `private.pem` | ECDSA P-256 私钥，必须与 Bootloader 内公钥成对 |

私钥必须与烧进 Bootloader 的公钥成对。量产请换正式密钥。把 PEM 放到：

```text
I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can\docs\keys\private.pem
```

镜像两种情况：

- 文件开头已是 XATO 头（魔数 `0x4F544158`）：脚本原样下发  
- 普通 Keil `.bin`：脚本用私钥现场加 256 字节头（长度、CRC32、签名）再下发；总长须 ≤ 槽大小 `0xB800`（46KB）

产线要得到可烧录文件时，用同目录 `pack_image.py`（见第 2.1 节），不要依赖 ZCANPRO。

Slot A / Slot B 必须用各自 scatter 链接的 bin，不能拿 A 的包去升 B。

| 槽 | scatter | 代码起始 | 适用 |
|----|---------|----------|------|
| A | `qi_wireless_code\mdk_project\scatter\app_slot_a.sct` | `0x08005100` | 空片首次下载，或当前活跃槽为 B |
| B | `qi_wireless_code\mdk_project\scatter\app_slot_b.sct` | `0x08010900` | 当前 APP 跑在 Slot A（最常见的二次升级） |

### 2.1 命令行打包 `pack_image.py`

不需要 ZCANPRO。把 Keil 裸 `.bin` 打成带 XATO 头的 `.ota.bin`：

```text
python pack_image.py --bin I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can\qi_wireless_code\mdk_project\Objects\qi_wireless.bin --key I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can\docs\keys\private.pem --out I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can\qi_wireless_code\mdk_project\Objects\app_slot_a.ota.bin --version 1.0.0
```

在 Windows 上若仓库就在上述路径，也可直接：

```text
python pack_image.py
```

| 参数 | 含义 |
|------|------|
| `--bin` | Keil `fromelf` 产出的裸 APP（Slot A 链接地址 `0x08005100`） |
| `--key` | 与 Bootloader 公钥成对的 PEM 私钥 |
| `--out` | 输出文件 |
| `--version` | 写入头的版本，默认 `1.0.0` |

产线把输出文件烧到 **Slot A 起始 `0x08005000`**（含 256 字节头），Bootloader 烧到 `0x08000000`。  
CAN OTA 可把 `--out` 填进 `zcanpro_ext_ota.py` 的 `FIRMWARE_PATH`，也可直接填裸 `.bin` 让扩展脚本现场打包。

---

## 3. 改脚本顶部配置

用记事本打开 `zcanpro_ext_ota.py`，改：

```python
REPO_ROOT = r"I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can"
FIRMWARE_PATH = REPO_ROOT + r"\qi_wireless_code\mdk_project\Objects\qi_wireless.bin"
PRIVATE_KEY_PATH = REPO_ROOT + r"\docs\keys\private.pem"
RESET_APP_TO_BOOT = True                       # APP 在跑则先复位进 Boot
DOWNLOAD_ADDR = 0x08005000                     # 仅兜底；实际 0x34 地址取自 DID 0x2114
TRANSFER_BLOCK_DATA = 128                      # 每块数据字节，须 1..254
```

| 项 | 说明 |
|----|------|
| `RESET_APP_TO_BOOT = True` | 板子当前跑 APP。脚本先发 `0x10 0x02` + `0x11`，等 2.5 s 再升级 |
| `RESET_APP_TO_BOOT = False` | 已在 Bootloader Safe Mode（无有效 APP 或正在等下载） |
| `FIRMWARE_PATH` | APP 在 Slot A 跑时，MCU 会擦 Slot B，必须提供 Slot B 链接的 bin |
| `DOWNLOAD_ADDR` | MCU 忽略该地址、写入刚擦的槽。脚本擦除后用 DID `0x2114` 的槽基址填 `0x34` |
| `TRANSFER_BLOCK_DATA` | MCU 单次 TransferData 最大 254 字节，128 较稳 |

---

## 4. 打开 CAN 通道（必须先做）

脚本用 `zcanpro.get_buses()` 取已打开的通道。通道没开会直接退出。

| 项 | 值 |
|----|-----|
| 波特率 | **250 kbps** |
| 帧格式 | **扩展帧 29-bit** |
| 类型 | **Classical CAN**（不要开 CAN FD） |
| 终端电阻 | 按总线，设备端通常 120 Ω |

脚本里的诊断 ID（界面不用再配一遍）：

| 方向 | CAN ID |
|------|--------|
| 主机 → MCU | `0x18DA0D03` |
| MCU → 主机 | `0x18DA030D` |

---

## 5. 导入并运行

1. MCU 上电，CANH / CANL / GND 接到适配器。  
2. ZCANPRO 启动设备，通道为已打开。  
3. **高级功能 → 扩展脚本**。  
4. 打开 `D:\Program Files (x86)\ZCANPRO\scripts\zcanpro_ext_ota.py`。  
5. 点 **运行**。  
6. 日志出现 `======== OTA 成功 ========` 即完成。

**停止** 会触发 `z_notify("stop")`，正在传的循环会退出。

---

## 6. 正常日志顺序

```text
======== Qi CAN-UDS OTA ========
总线 [{'busID': ..., ...}]
---- APP 进 Boot ----          （仅 RESET_APP_TO_BOOT=True）
---- Programming ----          0x10 0x02
---- SecurityAccess ----       0x27 0x01 seed → 0x27 0x02 签名
---- DID 0x2010 APP ----       0x2E 20 10 01
---- 擦除 ----                 0x31 01 FF 00（可能较慢）
擦除目标 Slot A/B              0x22 21 14，并核对镜像链接地址
---- RequestDownload ----      0x34（size=文件总长，含 256B 头）
---- TransferData ----         0x36，隔一段打印进度
---- TransferExit ----         0x37 验 CRC + ECDSA
---- Reset ----                0x11
版本: ...                      0x22 F1 89
复位后 0x27 NRC 0x11，已在 APP
======== OTA 成功 ========
```

擦除和验签可能超过 50 ms。脚本把 `enhanced_timeout_ms` 设为 5000，对应 MCU 的 NRC `0x78`。

---

## 7. 失败对照

| 日志/现象 | 原因 | 处理 |
|-----------|------|------|
| 请先打开 CAN 通道 | 设备未启动 | 先打开通道 |
| 找不到固件 / 找不到私钥 | 路径错误 | 改脚本顶部两个 PATH |
| UDS 无应答 | 波特率/帧类型错、接线、MCU 没起来 | 250k 扩展帧；看 PB6 串口是 APP 还是 BOOT |
| NRC `0x22` | 不在 Programming | 确认 0x10 成功；S3 超时则重跑 |
| NRC `0x33` | 未解锁 | 检查 0x27 |
| NRC `0x35` | 私钥和 MCU 公钥不匹配 | 换密钥或重编 Bootloader |
| NRC `0x36` | 连续验签失败已锁定 | 等约 60 秒再跑 |
| NRC `0x24` | 没擦就下载 | 整段重跑，不要跳步 |
| NRC `0x72` | CRC/签名失败，或镜像链接地址不在本槽 | 用对应槽的 bin；裸 bin 交给脚本打包 |
| NRC `0x73` | 块序号错 | 不要同时发其它诊断；重跑 |
| 镜像链接 Slot 与 MCU 写入槽不符 | APP 在 A 却喂了 A 的 bin（会写入 B） | 换成 `app_slot_b.sct` 链接的镜像 |
| 镜像超过槽大小 / length 不一致 | 文件大于 46KB 或头字段损坏 | 检查 scatter 与打包结果 |
| 复位后 0x27 仍返回 seed | 未跳转 APP，仍在 Bootloader | 查验签、链接地址、串口 `BOOT`/`APP` |
| 仍停在 NRC 0x78 | 擦除/验签超过 5 s | 再跑；查验签时是否被 IWDG 复位 |
| 进 Boot 后全无应答 | 2.5 s 不够或没进 Safe Mode | 加长等待，或确认已在 Boot 后设 `RESET_APP_TO_BOOT = False` |

调试串口 USART1（**PB6 TX，115200 8N1**）：`APP` 心跳=应用在跑，`BOOT`=Bootloader。OTA 过程应变 `APP` → `BOOT` → 再 `APP`。

---

## 8. 前置条件

- Bootloader 已烧到 `0x08000000`，公钥与 `private.pem` 匹配  
- CAN 收发器正常，与适配器共地  
- APP 有效：`RESET_APP_TO_BOOT = True`  
- 两槽都无效、上电即 Safe Mode：`RESET_APP_TO_BOOT = False`  

不需要安装 `python-can` 或 `cryptography`。只用标准库 + ZCANPRO 自带的 `zcanpro`。ZCANPRO 扩展脚本需安装 **Python 3.8 32 位**，并把其目录加入系统 PATH。

---

## 9. 和其它工具

| 文件 | 用途 |
|------|------|
| `qi_wireless_bootloader\tools\zcanpro_ext_ota.py` | 本仓库的一键 OTA 脚本（本说明） |
| ZCANPRO `scripts\demo.py` | 官方示例，不是本项目 OTA |

---

## 10. 注意

- 升级过程中不要拔 CAN、断电或关闭通道。  
- 测试私钥仅供开发；量产必须换钥并重编 Bootloader。  
- 日志会打印 seed 等诊断数据，仅实验室使用。
