# -*- coding: utf-8 -*-
"""把 Keil 裸 APP .bin 打成 256 字节 XATO 头 + 固件，供产线烧录或 OTA。

产线：输出文件从槽起始地址烧录（Slot A = 0x08005000）。
OTA：也可把输出路径填进 zcanpro_ext_ota.py 的 FIRMWARE_PATH。

依赖：标准库 + 同目录 zcanpro_ext_ota.py（不需要 ZCANPRO / cryptography）。
"""

from __future__ import print_function

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from zcanpro_ext_ota import (  # noqa: E402
    IMAGE_HEADER_SIZE,
    SLOT_A,
    load_ec_private_key,
    pack_image_if_needed,
    slot_name,
    validate_image,
)

REPO_ROOT = r"I:\GitHub-young-nights\ota-upgrade-of-qi-charger-based-on-can"
DEFAULT_BIN = os.path.join(REPO_ROOT, "qi_wireless_code", "mdk_project", "Objects", "qi_wireless.bin")
DEFAULT_KEY = os.path.join(REPO_ROOT, "docs", "keys", "private.pem")
DEFAULT_OUT = os.path.join(REPO_ROOT, "qi_wireless_code", "mdk_project", "Objects", "app_slot_a.ota.bin")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Pack Keil APP .bin with XATO header (CRC32 + ECDSA P-256)"
    )
    parser.add_argument("--bin", default=DEFAULT_BIN, help="Keil fromelf 产出的裸 APP .bin")
    parser.add_argument("--key", default=DEFAULT_KEY, help="ECDSA P-256 私钥 PEM（须与 Bootloader 公钥成对）")
    parser.add_argument("--out", default=DEFAULT_OUT, help="输出镜像，例如 app_slot_a.ota.bin")
    parser.add_argument("--version", default="1.0.0", help="写入头的版本字符串，默认 1.0.0")
    args = parser.parse_args(argv)

    if not os.path.isfile(args.bin):
        sys.stderr.write("找不到固件: %s\n" % args.bin)
        return 1
    if not os.path.isfile(args.key):
        sys.stderr.write("找不到私钥: %s\n" % args.key)
        return 1

    priv = load_ec_private_key(args.key)
    image = pack_image_if_needed(args.bin, priv, version=args.version)
    linked = validate_image(image)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    with open(args.out, "wb") as f:
        f.write(image)

    burn_addr = "0x08005000" if linked == SLOT_A else "0x08010800"
    print("输出: %s" % os.path.abspath(args.out))
    print("总长: %d  (头 %d + 固件 %d)" % (len(image), IMAGE_HEADER_SIZE, len(image) - IMAGE_HEADER_SIZE))
    print("链接: Slot %s  → 产线烧录地址 %s" % (slot_name(linked), burn_addr))
    return 0


if __name__ == "__main__":
    sys.exit(main())
