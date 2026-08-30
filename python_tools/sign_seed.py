#!/usr/bin/env python3
"""Generate SecurityAccess (0x27) signature for a given seed.

For manual testing: receive 4-byte seed from MCU, compute ECDSA P-256 signature,
output the hex CAN frames ready for ZCANPRO / manual send.

Usage:
    python sign_seed.py <seed_hex> [--key <private_key.pem>]
    python sign_seed.py A1B2C3D4
    python sign_seed.py A1B2C3D4 --key docs/keys/private.pem

Prerequisites:
    pip install ecdsa   (only dependency beyond stdlib)
    OR: use OpenSSL backend (default, no pip needed)
"""

from __future__ import print_function

import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_KEY = os.path.join(REPO_ROOT, "docs", "keys", "private.pem")

# CAN IDs (extended frame, match project config)
UDS_REQ_ID = "18DA0D03"
UDS_RESP_ID = "18DA030D"


def sha256_hash(data):
    """SHA-256 hash, returns 32 bytes."""
    return hashlib.sha256(data).digest()


def sign_with_openssl(seed_hash, private_key_path):
    """Sign using OpenSSL CLI (no pip dependency)."""
    # Create ECDSA key in DER format from PEM, sign raw hash
    tmpdir = tempfile.mkdtemp(prefix="sign_")
    try:
        # Write hash
        hash_path = os.path.join(tmpdir, "hash.bin")
        with open(hash_path, "wb") as f:
            f.write(seed_hash)

        # Sign with openssl pkeyutl (raw hash, no double-hashing)
        result = subprocess.Popen(
            [
                "openssl", "pkeyutl", "-sign",
                "-inkey", private_key_path,
                "-in", hash_path,
                "-pkeyopt", "digest:sha256",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = result.communicate()

        if result.returncode != 0:
            stderr_text = stderr.decode().strip()
            raise RuntimeError("OpenSSL sign failed: {}".format(stderr_text))

        # OpenSSL outputs DER-encoded signature, we need raw R||S (64 bytes)
        der_sig = stdout
        return der_to_p1363(der_sig)
    finally:
        import shutil
        shutil.rmtree(tmpdir, ignore_errors=True)


def sign_with_ecdsa_lib(seed_hash, private_key_path):
    """Sign using ecdsa Python library (pip install ecdsa)."""
    from ecdsa import SigningKey, NIST256p, der

    with open(private_key_path, "r") as f:
        pem_data = f.read()

    sk = SigningKey.from_pem(pem_data)
    # Sign the raw hash (do NOT let ecdsa library hash again)
    sig_der = sk.sign_digest(seed_hash, sigencode=der.sigencode)

    return der_to_p1363(sig_der)


def der_to_p1363(der_sig):
    """Convert DER-encoded ECDSA signature to IEEE P1363 R||S (64 bytes)."""
    # Parse DER sequence
    if der_sig[0] != 0x30:
        raise ValueError("Invalid DER signature: not a sequence")

    # Skip outer sequence
    idx = 2  # skip 0x30, length
    if der_sig[1] & 0x80:
        # Multi-byte length
        len_bytes = der_sig[1] & 0x7F
        idx = 2 + len_bytes

    # Parse R
    if der_sig[idx] != 0x02:
        raise ValueError("Invalid DER: expected INTEGER for R")
    idx += 1
    r_len = der_sig[idx]
    idx += 1
    r_bytes = der_sig[idx:idx + r_len]
    idx += r_len

    # Parse S
    if der_sig[idx] != 0x02:
        raise ValueError("Invalid DER: expected INTEGER for S")
    idx += 1
    s_len = der_sig[idx]
    idx += 1
    s_bytes = der_sig[idx:idx + s_len]

    # Pad to 32 bytes each
    r = r_bytes.rjust(32, b"\x00")
    s = s_bytes.rjust(32, b"\x00")

    return r + s  # 64 bytes


def generate_can_frames(sig_hex_64):
    """Generate all CAN frames for SecurityAccess signature transfer.

    Returns two methods:
    Method A: 27 02 single ISO-TP multi-frame (simpler)
    Method B: 27 03 chunked transfer (compatible with chunked receive)
    """
    sig_bytes = bytes.fromhex(sig_hex_64)

    # ---- Method A: 27 02 full signature in one ISO-TP ----
    # UDS payload: [27, 02, sig[0]..sig[63]] = 66 bytes
    uds_payload = bytes([0x27, 0x02]) + sig_bytes  # 66 bytes

    # ISO-TP multi-frame: first frame (FF) header
    total_len = len(uds_payload)  # 66
    ff = struct.pack(">IH", 0x1000 | total_len, 0)  # FF: 4-bit len + 0 padding
    # Wait, ISO-TP FF format: PCI byte 0x1X where X is high nibble of length
    # For 66 bytes: 0x10 | (66 >> 8) = 0x10, then 66 & 0xFF = 0x42
    ff_pci = bytes([0x10 | (total_len >> 8), total_len & 0xFF])
    ff_data = ff_pci + uds_payload[:6]  # first 6 data bytes
    remaining = uds_payload[6:]

    frames_a = []
    frames_a.append({
        "id": UDS_REQ_ID,
        "dlc": 8,
        "data": ff_data.hex().upper(),
        "note": "ISO-TP First Frame: 27 02 + sig[0:5]",
    })

    seq = 1
    while remaining:
        chunk = remaining[:7]
        remaining = remaining[7:]
        cf_pci = bytes([0x20 | (seq & 0x0F)])
        cf_data = cf_pci + chunk
        # Pad to 8 bytes
        cf_data = cf_data.ljust(8, b"\x00")
        frames_a.append({
            "id": UDS_REQ_ID,
            "dlc": 8,
            "data": cf_data.hex().upper(),
            "note": "ISO-TP Consecutive Frame #{}".format(seq),
        })
        seq += 1

    # ---- Method B: 27 03 chunked (6 bytes per frame) ----
    frames_b = []
    chunks = [sig_bytes[i:i + 6] for i in range(0, 64, 6)]

    for i, chunk in enumerate(chunks):
        block_seq = i + 1  # 1-based
        # UDS payload: [27, 03, blockSeq, sig_chunk...]
        uds_frame = bytes([0x27, 0x03, block_seq]) + chunk
        uds_frame = uds_frame.ljust(8, b"\x00")  # pad to 8 bytes
        frames_b.append({
            "id": UDS_REQ_ID,
            "dlc": 8,
            "data": uds_frame.hex().upper(),
            "note": "27 03 chunk #{} (blockSeq={}, sig[{}:{}])".format(
                i + 1, block_seq, i * 6, min(i * 6 + 6, 64)
            ),
        })

    # Final 27 02 verification frame (empty payload triggers verify)
    verify_frame = bytes([0x27, 0x02]).ljust(8, b"\x00")
    frames_b.append({
        "id": UDS_REQ_ID,
        "dlc": 8,
        "data": verify_frame.hex().upper(),
        "note": "27 02 Verify (triggers ECDSA check on MCU)",
    })

    return frames_a, frames_b


def print_frames(frames, title):
    """Pretty-print CAN frames for copy-paste."""
    print("")
    print(title)
    print("-" * 72)
    print("{:<6s} {:<4s} {:<20s} {}".format("No.", "DLC", "Data (hex)", "Note"))
    print("-" * 72)
    for i, f in enumerate(frames):
        print("{:<6d} {:<4d} {:<20s} {}".format(
            i + 1, f["dlc"], f["data"], f["note"]
        ))
    print("-" * 72)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate ECDSA P-256 signature for SecurityAccess seed"
    )
    parser.add_argument(
        "seed",
        help="4-byte seed in hex (e.g. A1B2C3D4), from MCU 67 01 response",
    )
    parser.add_argument(
        "--key", default=DEFAULT_KEY,
        help="ECDSA P-256 private key PEM (default: docs/keys/private.pem)",
    )
    args = parser.parse_args(argv)

    # ---- validate seed ----
    seed_hex = args.seed.replace(" ", "").replace(":", "")
    if len(seed_hex) != 8:
        sys.stderr.write("ERROR: Seed must be 8 hex chars (4 bytes), got {}\n".format(len(seed_hex)))
        return 1
    try:
        seed_bytes = bytes.fromhex(seed_hex)
    except ValueError:
        sys.stderr.write("ERROR: Invalid hex: {}\n".format(seed_hex))
        return 1

    # ---- validate key ----
    if not os.path.isfile(args.key):
        sys.stderr.write("ERROR: Private key not found: {}\n".format(args.key))
        return 1

    # ---- compute ----
    print("=" * 72)
    print("SecurityAccess Signature Generator")
    print("=" * 72)
    print("")
    print("Seed (from MCU):  {}".format(seed_hex.upper()))

    # SHA-256 of seed
    seed_hash = sha256_hash(seed_bytes)
    print("SHA-256(seed):    {}".format(seed_hash.hex().upper()))

    # ECDSA sign
    print("Signing with:     {}".format(os.path.basename(args.key)))
    print("")

    try:
        sig = sign_with_openssl(seed_hash, args.key)
    except Exception as e1:
        # Fallback to ecdsa library
        try:
            sig = sign_with_ecdsa_lib(seed_hash, args.key)
        except ImportError:
            sys.stderr.write(
                "ERROR: Neither OpenSSL CLI nor ecdsa library available.\n"
                "  Install: pip install ecdsa\n"
                "  Or install OpenSSL\n"
            )
            return 1
        except Exception as e2:
            sys.stderr.write("ERROR: Signing failed: {}\n".format(e2))
            return 1
    except Exception as e:
        sys.stderr.write("ERROR: Signing failed: {}\n".format(e))
        return 1

    sig_hex = sig.hex().upper()
    print("ECDSA Signature:  {}".format(sig_hex))
    print("")

    # ---- generate CAN frames ----
    frames_a, frames_b = generate_can_frames(sig_hex)

    print_frames(frames_a, "Method A: 27 02 单条 ISO-TP 多帧（推荐）")
    print("")
    print("  发送后 MCU 回复 67 02 = 解锁成功")
    print("  7F 27 35 = Invalid Key（签名校验失败）")
    print("  7F 27 36 = 超过失败次数（锁定 30 秒）")

    print_frames(frames_b, "Method B: 27 03 分块传输 + 27 02 触发校验")
    print("")
    print("  先发 11 帧 27 03（每帧 6 字节签名），最后发 27 02 触发校验")
    print("  MCU 回复 67 02 = 解锁成功")

    print("")
    print("=" * 72)
    print("完整操作流程：")
    print("  1. MCU 上电，发送 27 01 Request Seed")
    print("  2. MCU 回复 67 01 [seed: {}]".format(seed_hex.upper()))
    print("  3. 用本脚本生成签名: python sign_seed.py {}".format(seed_hex.upper()))
    print("  4. 按上面的帧列表逐条发送")
    print("  5. MCU 回复 67 02 = 解锁成功，可执行 31 FF00 等受保护服务")
    print("=" * 72)

    return 0


if __name__ == "__main__":
    sys.exit(main())
