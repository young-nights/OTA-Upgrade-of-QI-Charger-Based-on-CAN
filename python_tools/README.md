# python_tools — 产线与调试工具集

本目录包含 Qi 无线充电模块 CAN-OTA 项目的产线打包、合并、校验脚本。

**所有脚本均在仓库根目录执行**，路径自动推算，不依赖当前工作目录。

---

## 脚本总览

| 脚本 | 功能 | 典型场景 |
|------|------|----------|
| `merge_prod_bin.py` | Bootloader + APP 合并为单一产线镜像 | 产线烧录前打包 |
| `verify_image.py` | XATO 镜像完整性 + 签名校验 | 打包后自检 / 产线抽检 |
| `pack_image.py`（位于 `qi_wireless_bootloader/tools/`） | 裸 bin → 签名 .ota.bin | 打包签名（依赖链见下文） |

---

## 1. merge_prod_bin.py — 产线镜像合并

将 Keil 编译产出的 `bootloader.bin` 与 `pack_image.py` 打包的 `app_slot_a.ota.bin` 合并为一个文件，用于产线一次性烧录。

### 用法

```bash
# 默认参数（推荐）
cd <仓库根>
python python_tools/merge_prod_bin.py

# 显式指定
python python_tools/merge_prod_bin.py \
  --boot qi_wireless_bootloader/mdk_project/Objects/bootloader.bin \
  --app  qi_wireless_code/mdk_project/Objects/app_slot_a.ota.bin \
  --out  python_tools/prod_image.bin

# 同时生成 Intel HEX
python python_tools/merge_prod_bin.py --hex
```

### 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--boot` | `qi_wireless_bootloader/.../bootloader.bin` | Bootloader 二进制 |
| `--app` | `qi_wireless_code/.../app_slot_a.ota.bin` | 已签名 APP 镜像 |
| `--out` | `python_tools/prod_image.bin` | 合并输出路径 |
| `--hex` | — | 同时生成 `.hex` 文件 |

### 输出布局

```
prod_image.bin:
  0x0000  Bootloader  20KB (不足 20KB 补 0xFF)
  0x5000  Slot A      (256B XATO 头 + 固件)
```

烧录地址：**`0x08000000`**（从 Flash 起始烧）。

### 约束

| 检查项 | 限制 |
|--------|------|
| Bootloader 大小 | ≤ 20KB (`0x5000`) |
| APP 镜像总长（含头） | ≤ 46KB (`0xB800`) |
| APP 镜像必须含 XATO 头 | 脚本会警告但不阻断 |

---

## 2. verify_image.py — 镜像离线校验

一键验证 XATO 镜像的 6 项完整性检查，与 Bootloader `boot_verify_image()` 的校验逻辑一致。

### 用法

```bash
# 验证打包后的 APP 镜像
python python_tools/verify_image.py qi_wireless_code/mdk_project/Objects/app_slot_a.ota.bin

# 指定公钥（默认使用 docs/keys/public.pem）
python python_tools/verify_image.py <image_path> --key docs/keys/public.pem
```

### 检查项

| # | 检查 | 说明 | 对应 MCU 失败码 |
|---|------|------|-----------------|
| 1 | Magic | 首 4 字节必须为 `0x4F544158` ("XATO") | `g_verify_fail_step = 1` |
| 2 | Image Length | 不能为 0，不能超出固件实际大小 | `g_verify_fail_step = 2` |
| 3 | CRC32 | IEEE 802.3 CRC 覆盖裸固件（不含头） | `g_verify_fail_step = 3` |
| 4 | Version | 读取头中版本字符串（信息性） | — |
| 5 | Build Timestamp | 读取打包时间戳（信息性） | — |
| 6 | ECDSA P-256 | SHA-256(固件) + OpenSSL 验签 | `g_verify_fail_step = 6` |

### 输出示例

```
============================================================
XATO Image Verification: app_slot_a.ota.bin
File size: 19312 bytes (header 256 + firmware 19056)
============================================================
  ✅ [PASS] Magic 正确: XATO
  ✅ [PASS] Image length 正确: 19056 bytes
  ✅ [PASS] CRC32 正确: 0xEA163241
  ✅ [PASS] Version: 1.0.0
  ✅ [PASS] Build timestamp: 2026-08-28 18:49:25 UTC
  ✅ [PASS] ECDSA 签名验证通过
============================================================
Result: 6 passed, 0 failed
============================================================
```

### 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 全部通过 |
| 1 | 有检查项失败 |
| 2 | 文件不存在或格式错误 |

### 依赖

- Python 3.6+
- OpenSSL CLI（签名验证需要，必须在 PATH 中）

---

## 3. 打包脚本依赖链

`verify_image.py` 和 `merge_prod_bin.py` 无第三方依赖（仅标准库 + OpenSSL CLI）。

`pack_image.py`（位于 `qi_wireless_bootloader/tools/`）依赖同目录的 `zcanpro_ext_ota.py`，负责：
- ECDSA P-256 签名生成
- XATO 头构造
- CRC32 计算

---

## 4. 典型工作流

```
Keil Rebuild → fromelf → qi_wireless.bin (裸 bin)
                            │
              qi_wireless_bootloader/tools/pack_image.py
                            │
                  app_slot_a.ota.bin (XATO 头 + 签名 + 固件)
                            │
                ┌───────────┴───────────┐
                │                       │
    python_tools/verify_image.py   python_tools/merge_prod_bin.py
    (校验：6 项全部 PASS)              │
                                  prod_image.bin (Boot + Slot A)
                                        │
                              J-Link / AT32 ISP 烧录 0x08000000
```

---

## 5. 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| `verify_image.py` 报 Magic 错误 | 输入文件不是 XATO 格式（可能是 prod_image.bin 或裸 bin） | 只验证 `.ota.bin` 文件 |
| `verify_image.py` 报签名全零 | 镜像未签名（placeholder） | 用 `pack_image.py` 重新打包 |
| `merge_prod_bin.py` 报 Bootloader 超大 | bootloader.bin > 20KB | 精简 Bootloader 代码 |
| `merge_prod_bin.py` 报 APP 超大 | 打包后 > 46KB | 减小 APP 代码或调整 IROM1 Size |
| `openssl` 命令找不到 | 系统未安装 OpenSSL | `sudo apt install openssl`（Linux）或安装 Git for Windows（自带） |
| `pack_image.py` 报找不到私钥 | `docs/keys/private.pem` 不存在 | 按文档生成密钥对 |
