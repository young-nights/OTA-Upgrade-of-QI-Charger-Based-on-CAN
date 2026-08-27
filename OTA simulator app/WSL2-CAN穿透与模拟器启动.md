# WSL2 CAN 盒穿透与 OTA 模拟器启动

适用发行版：**Ubuntu-24.04-HDD**（用户 `nights`）。  
本发行版没有 systemd，拔插 USB 或重启 WSL 后 **udev 规则不会自动恢复权限**，每次都要按下面做一遍。

模拟器不是 exe，不能双击。它是 Python 命令行上位机，必须在本发行版终端里启动。

---

## 0. 一次看清：穿透和模拟器是两件事

| 步骤 | 在哪做 | 作用 |
|------|--------|------|
| `usbipd` 绑定 + 附加 | **Windows PowerShell（管理员）** | 把 Windows 侧 USB CAN 盒交给 WSL |
| `lsusb` + `chmod 666` | **Ubuntu-24.04-HDD** | 确认设备进了 WSL，并放开 libusb 权限 |
| `source .venv` + 跑 client | **Ubuntu-24.04-HDD** | 启动 OTA 模拟器 |

同一只 USB 盒 **不能** 同时给 Windows ZCANPRO 和 WSL 用。挂进 WSL 后，ZCANPRO 会看不到设备。

---

## 1. 重启 WSL2 或拔插盒子之后：重新穿透

### 1.1 Windows：列出设备

以管理员打开 PowerShell：

```powershell
usbipd list
```

关注 `Connected` 表。当前实测过的盒子：

```
BUSID   VID:PID     DEVICE           STATE
1-1     04d8:0053   WinUSB Device    Attached / Not shared / Shared
```

`BUSID` 拔插后会变（可能变成 `1-2`、`2-1` 等），**以本次 `usbipd list` 为准**，不要抄死 `1-1`。

`STATE` 含义：

| STATE | 含义 | 下一步 |
|-------|------|--------|
| `Not shared` | Windows 独占，WSL 看不见 | 先 `bind`，再 `attach` |
| `Shared` | 已共享，但还没进当前 WSL | `attach` |
| `Attached` | 已经在某个 WSL 里 | 到 WSL 里做第 2 节 |

### 1.2 Windows：绑定（只需每台电脑做一次，换口后 BUSID 变了要再 bind）

```powershell
usbipd bind --busid 1-1
```

把 `1-1` 换成当前 BUSID。失败且提示权限时，确认窗口是**管理员** PowerShell。

### 1.3 Windows：附加到本发行版

先保证 WSL 已启动（任意开一个 Ubuntu-24.04-HDD 终端即可）。

```powershell
usbipd attach --wsl Ubuntu-24.04-HDD --busid 1-1
```

可选：让 WSL 重启后自动再挂上（仍建议每次用 `usbipd list` 核对）：

```powershell
usbipd attach --wsl Ubuntu-24.04-HDD --busid 1-1 --auto-attach
```

把设备还给 Windows / ZCANPRO：

```powershell
usbipd detach --busid 1-1
```

### 1.4 WSL：确认已经进来

在 **Ubuntu-24.04-HDD** 里：

```bash
lsusb
```

应能看到类似：

```
Bus 001 Device 003: ID 04d8:0053 Microchip Technology, Inc. USB CANFD DEBUG
```

没有这行：回到 1.1，检查 `attach` 的发行版名是不是 `Ubuntu-24.04-HDD`（不是 `Ubuntu-24.04`）。

---

## 2. WSL：放开 USB 权限（每次重启 / 拔插都要做）

本发行版无 systemd，`chmod` 不会持久。

```bash
# 按 VID:PID 找到设备节点并放开写权限
for d in /sys/bus/usb/devices/*; do
  v=$(cat "$d/idVendor" 2>/dev/null) || continue
  p=$(cat "$d/idProduct" 2>/dev/null) || continue
  if [ "$v" = "04d8" ] && [ "$p" = "0053" ]; then
    bus=$(printf '%03d' "$(cat "$d/busnum")")
    dev=$(printf '%03d' "$(cat "$d/devnum")")
    node="/dev/bus/usb/$bus/$dev"
    echo "USB node: $node"
    sudo chmod 666 "$node"
    ls -l "$node"
  fi
done
```

权限应为 `crw-rw-rw-`。`sudo` 需要本机密码。

---

## 3. 启动 OTA 模拟器

程序目录（不要停在上一级 `OTA simulator app`）：

```
/home/nights/embedded_item/ota-upgrade-of-qi-charger-based-on-can/OTA simulator app/qi_charger_ota_simulator_2026-07-23
```

### 3.1 进入虚拟环境

```bash
cd "/home/nights/embedded_item/ota-upgrade-of-qi-charger-based-on-can/OTA simulator app/qi_charger_ota_simulator_2026-07-23"
source .venv/bin/activate
cd qi_charger/simulator
python3 qi_upgrade_client_ecdsa.py -h
```

能打印参数说明即启动成功。`.venv` 已装好 `python-can` / `can-isotp` / `canalystii` / `cryptography`。

若提示没有 `.venv`：

```bash
cd "/home/nights/embedded_item/ota-upgrade-of-qi-charger-based-on-can/OTA simulator app/qi_charger_ota_simulator_2026-07-23"
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt 'canalystii>=0.1.0'
```

### 3.2 不插 CAN 的自检

```bash
python3 ecdsa-p256-keys/test_ecdsa_crypto.py
```

应全部 `PASSED`。

### 3.3 对着 MCU 跑（当前固件用 ECDSA 客户端）

必须在 `qi_charger/simulator` 目录下执行。

```bash
python3 qi_upgrade_client_ecdsa.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --keypair ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin \
    --firmware /path/to/qi_app.bin \
    --type app --verbose
```

省略 `--firmware` 时会发随机测试数据，只能测流程，不能当正式升级包。

没有 `qi_ecdsa_p256_keypair.bin` 时先生成：

```bash
python3 ecdsa-p256-keys/generate_keypair.py
```

`--keypair` 要的是 **32 字节 raw 私钥 .bin**。仓库里的 `智嵌物联CAN/keys/private.pem` 是 PEM，不能直接填。公钥必须和 MCU `boot_verify.c` 里那把一致，否则 SecurityAccess 返回 NRC `0x35`。

简单算法客户端（仅当固件是 `seed + 0x5555`；**当前 bootloader 不是**）：

```bash
python3 qi_upgrade_client.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --type app --verbose
```

### 3.4 总线与 MCU 侧前提

| 项 | 值 |
|----|----|
| 链路 | Classical CAN 2.0B，**不要 CAN FD** |
| 波特率 | 250 kbps |
| 请求 ID | `0x18DA0D03`（扩展帧） |
| 响应 ID | `0x18DA030D` |
| MCU | 空片或 Boot Safe Mode 才会应答；正常 APP 目前缺收发器初始化 |

---

## 4. 当前这只盒子的已知限制

穿透成功 **不等于** 模拟器已经能收发。

实测挂进本发行版的设备：

- VID:PID `04d8:0053`
- 字符串 `NXP SEMICONDUCTORS` / `USB CANFD DEBUG`
- Windows 侧显示为 WinUSB Device
- USB 端点 5 个 bulk，**不是**创芯 CANalyst-II 协议

模拟器默认 `--bustype canalystii` 是创芯协议。对这只盒子会出现：打开后 USB 命令超时，甚至进程崩溃。

因此：

1. **创芯 CANalyst-II**：按第 1～3 节即可在 WSL 里跑模拟器。
2. **现在这只 NXP/WinUSB 盒**：请 `usbipd detach` 还给 Windows，用 ZCANPRO（经典 CAN、250 kbps、扩展帧、ISO-TP 单帧例如 `02 10 02 CC CC CC CC CC`）。
3. **ZQWL（VID `0x3562` 虚拟串口）**：不是 canalystii，本模拟器也没有对应 `--bustype`。

---

## 5. 故障对照

| 现象 | 处理 |
|------|------|
| `usbipd list` 没有盒子 | 换口、换线，Windows 设备管理器看是否枚举 |
| `lsusb` 没有 `04d8:0053` | `attach` 的发行版名写错，或盒子还在 Windows / 被 ZCANPRO 占用 |
| pyusb `Access denied` / Errno 13 | 第 2 节 `chmod 666` 没做，或拔插后又变回 `664` |
| canalystii “No Canalyst-II USB device found” | 盒子没进 WSL，或 VID:PID 不是创芯 |
| `Unexpected USB product string: USB CANFD DEBUG` 后超时 | 当前盒子不是创芯协议，见第 4 节 |
| `Keypair file not found` | 先跑 `generate_keypair.py`，或把 `--keypair` 指到实际 .bin |
| MCU 完全无响应 | 通道不是 250k 经典 CAN；未带 ISO-TP PCI；MCU 在 APP 而非 Safe Mode |

---

## 6. 最短抄写清单

Windows 管理员 PowerShell：

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl Ubuntu-24.04-HDD --busid <BUSID>
```

Ubuntu-24.04-HDD：

```bash
lsusb
# 然后执行第 2 节 chmod 脚本

cd "/home/nights/embedded_item/ota-upgrade-of-qi-charger-based-on-can/OTA simulator app/qi_charger_ota_simulator_2026-07-23"
source .venv/bin/activate
cd qi_charger/simulator
python3 qi_upgrade_client_ecdsa.py -h
```
