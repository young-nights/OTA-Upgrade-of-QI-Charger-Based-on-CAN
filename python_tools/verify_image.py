#!/usr/bin/env python3
"""Verify XATO image integrity: Magic, Length, CRC32, ECDSA P-256 signature.

Usage:
    python verify_image.py <image_path> [--key <public_key.pem>]
    python verify_image.py app_slot_a.ota.bin
    python verify_image.py app_slot_a.ota.bin --key docs/keys/public.pem

Exit code: 0 = all pass, 1 = verification failed, 2 = file error.
"""

from __future__ import print_function

import argparse
import binascii
import os
import struct
import subprocess
import sys
import tempfile

IMAGE_MAGIC = 0x4F544158  # "XATO"
IMAGE_HEADER_SIZE = 256

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_KEY = os.path.join(REPO_ROOT, "docs", "keys", "public.pem")


def verify_magic(header):
    """Check 1: header magic number."""
    magic = struct.unpack("<I", header[0:4])[0]
    if magic != IMAGE_MAGIC:
        return False, "Magic 错误: 0x{:08X} (expect 0x4F544158)".format(magic)
    return True, "Magic 正确: XATO"


def verify_image_length(header, firmware_len):
    """Check 2: image_length within bounds."""
    img_len = struct.unpack("<I", header[4:8])[0]
    if img_len == 0:
        return False, "Image length 为 0"
    if img_len > firmware_len:
        return False, "Image length ({}) > 实际固件大小 ({})".format(img_len, firmware_len)
    return True, "Image length 正确: {} bytes".format(img_len)


def verify_crc32(header, firmware):
    """Check 3: CRC32 of firmware data (IEEE 802.3)."""
    stored_crc = struct.unpack("<I", header[8:12])[0]
    computed_crc = binascii.crc32(firmware) & 0xFFFFFFFF
    if stored_crc != computed_crc:
        return False, "CRC32 不匹配: stored=0x{:08X} computed=0x{:08X}".format(
            stored_crc, computed_crc
        )
    return True, "CRC32 正确: 0x{:08X}".format(stored_crc)


def verify_version(header):
    """Check 4: version string."""
    version = header[0x4C:0x5C].rstrip(b"\x00").decode("ascii", errors="replace")
    if not version:
        return True, "Version: (空)"
    return True, "Version: {}".format(version)


def verify_build_timestamp(header):
    """Check 5: build timestamp."""
    ts = struct.unpack("<I", header[0x5C:0x60])[0]
    if ts == 0:
        return True, "Build timestamp: 0 (未设置)"
    from datetime import datetime
    try:
        dt = datetime.utcfromtimestamp(ts)
        return True, "Build timestamp: {} UTC".format(dt.strftime("%Y-%m-%d %H:%M:%S"))
    except (OSError, OverflowError):
        return True, "Build timestamp: {} (raw)".format(ts)


def p1363_to_der(sig_bytes):
    """Convert 64-byte IEEE P1363 (R||S) signature to DER format for OpenSSL."""
    if len(sig_bytes) != 64:
        raise ValueError("Signature must be 64 bytes, got {}".format(len(sig_bytes)))

    r = int.from_bytes(sig_bytes[:32], "big")
    s = int.from_bytes(sig_bytes[32:], "big")

    def int_to_der_int(n):
        b = n.to_bytes(32, "big").lstrip(b"\x00")
        if not b:
            b = b"\x00"
        if b[0] & 0x80:
            b = b"\x00" + b
        return b"\x02" + bytes([len(b)]) + b

    r_der = int_to_der_int(r)
    s_der = int_to_der_int(s)
    seq = r_der + s_der
    return b"\x30" + bytes([len(seq)]) + seq


def verify_ecdsa(firmware, signature, public_key_path):
    """Check 6: ECDSA P-256 signature using OpenSSL CLI."""
    if not os.path.isfile(public_key_path):
        return False, "公钥文件不存在: {}".format(public_key_path)

    if len(signature) != 64:
        return False, "签名长度错误: {} bytes (expect 64)".format(len(signature))

    # Check if signature is all zeros
    if signature == b"\x00" * 64:
        return False, "签名全零 (placeholder，未实际签名)"

    try:
        der_sig = p1363_to_der(signature)
    except ValueError as e:
        return False, "签名格式错误: {}".format(e)

    # Write temp files for OpenSSL
    tmpdir = tempfile.mkdtemp(prefix="verify_")
    try:
        fw_path = os.path.join(tmpdir, "firmware.bin")
        sig_path = os.path.join(tmpdir, "signature.der")
        with open(fw_path, "wb") as f:
            f.write(firmware)
        with open(sig_path, "wb") as f:
            f.write(der_sig)

        # openssl dgst -sha256 -verify <pubkey> -signature <sig> <data>
        result = subprocess.Popen(
            [
                "openssl", "dgst", "-sha256",
                "-verify", public_key_path,
                "-signature", sig_path,
                fw_path,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = result.communicate()
        output = (stdout.decode() + stderr.decode()).strip()

        if result.returncode == 0 and "Verified OK" in output:
            return True, "ECDSA 签名验证通过"
        else:
            return False, "ECDSA 签名验证失败: {}".format(output)
    except FileNotFoundError:
        return False, "openssl 未安装或不在 PATH 中"
    finally:
        # Cleanup temp files
        import shutil
        shutil.rmtree(tmpdir, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify XATO image: Magic + Length + CRC32 + ECDSA P-256"
    )
    parser.add_argument("image", help="Path to .ota.bin image file")
    parser.add_argument(
        "--key", default=DEFAULT_KEY,
        help="ECDSA P-256 public key PEM (default: docs/keys/public.pem)",
    )
    args = parser.parse_args(argv)

    image_path = args.image
    key_path = args.key

    # ---- load image ----
    if not os.path.isfile(image_path):
        sys.stderr.write("ERROR: 文件不存在: {}\n".format(image_path))
        return 2

    with open(image_path, "rb") as f:
        data = f.read()

    if len(data) < IMAGE_HEADER_SIZE:
        sys.stderr.write("ERROR: 文件太小 ({} bytes)，不足 256 字节头\n".format(len(data)))
        return 2

    header = data[:IMAGE_HEADER_SIZE]
    firmware = data[IMAGE_HEADER_SIZE:]
    img_len_stored = struct.unpack("<I", header[4:8])[0]
    # Use stored length for signature verification (matches MCU behavior)
    firmware_for_verify = firmware[:img_len_stored] if img_len_stored <= len(firmware) else firmware

    # ---- run checks ----
    passed = 0
    failed = 0

    print("=" * 60)
    print("XATO Image Verification: {}".format(os.path.basename(image_path)))
    print("File size: {} bytes (header {} + firmware {})".format(
        len(data), IMAGE_HEADER_SIZE, len(firmware)
    ))
    print("=" * 60)

    checks = [
        ("Magic", lambda: verify_magic(header)),
        ("Image Length", lambda: verify_image_length(header, len(firmware))),
        ("CRC32", lambda: verify_crc32(header, firmware_for_verify)),
        ("Version", lambda: verify_version(header)),
        ("Build Timestamp", lambda: verify_build_timestamp(header)),
        ("ECDSA P-256", lambda: verify_ecdsa(
            firmware_for_verify,
            header[0x0C:0x4C],  # 64-byte signature at offset 0x0C
            key_path,
        )),
    ]

    for name, check_fn in checks:
        ok, msg = check_fn()
        status = "PASS" if ok else "FAIL"
        symbol = "\u2705" if ok else "\u274c"
        print("  {} [{}] {}".format(symbol, status, msg))
        if ok:
            passed += 1
        else:
            failed += 1

    print("=" * 60)
    print("Result: {} passed, {} failed".format(passed, failed))
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
