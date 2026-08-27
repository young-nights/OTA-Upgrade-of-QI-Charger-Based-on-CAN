#!/usr/bin/env python3
"""Pack a linked APP .bin into a CAN-UDS OTA image (256-byte header + firmware).

The host must send this file via UDS 0x36.  Slot A and Slot B binaries must be
linked at their own addresses (see mdk_project/scatter/app_slot_a.sct and
app_slot_b.sct).  After 0x31 erase, read DID 0x2114 for the target slot and
send the matching packed image.

Header (little-endian):
  magic u32 0x4F544158, image_length u32, crc32 u32,
  ecdsa P-256 signature 64 bytes (IEEE P1363 R||S),
  version 16 bytes, build_timestamp u32, reserved 160 bytes.

Example:
  python3 tools/pack_image.py \\
      --bin qi_wireless_code/mdk_project/Objects/qi_wireless.bin \\
      --key "OTA simulator app/keys/private.pem" \\
      --version 1.0.0 \\
      --out app_slot_a.ota.bin
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

IMAGE_MAGIC = 0x4F544158
HEADER_SIZE = 256
SIG_SIZE = 64
VERSION_SIZE = 16
RESERVED_SIZE = 160


def crc32_ieee(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def sign_p1363(priv_pem: Path, message: bytes) -> bytes:
    """ECDSA P-256 over SHA-256(message), raw R||S (64 bytes)."""
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec, utils

        key = serialization.load_pem_private_key(priv_pem.read_bytes(), password=None)
        der = key.sign(message, ec.ECDSA(hashes.SHA256()))
        r, s = utils.decode_dss_signature(der)
        return r.to_bytes(32, "big") + s.to_bytes(32, "big")
    except ImportError:
        pass

    openssl = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(priv_pem)],
        input=message,
        capture_output=True,
        check=False,
    )
    if openssl.returncode != 0:
        sys.stderr.write(openssl.stderr.decode("utf-8", errors="replace"))
        raise SystemExit("signing failed (install python3-cryptography or openssl)")
    parse = subprocess.run(
        ["openssl", "asn1parse", "-inform", "DER"],
        input=openssl.stdout,
        capture_output=True,
        check=False,
    )
    if parse.returncode != 0:
        raise SystemExit("could not parse ECDSA DER signature")
    ints: list[int] = []
    for line in parse.stdout.decode("utf-8", errors="replace").splitlines():
        if "INTEGER" in line and ":" in line:
            hexpart = line.rsplit(":", 1)[-1].strip()
            if hexpart:
                ints.append(int(hexpart, 16))
    if len(ints) < 2:
        raise SystemExit("DER signature did not contain R,S")
    return ints[0].to_bytes(32, "big") + ints[1].to_bytes(32, "big")


def pack(bin_path: Path, key_path: Path, version: str, out_path: Path) -> None:
    image = bin_path.read_bytes()
    if not image:
        raise SystemExit("input bin is empty")
    crc = crc32_ieee(image)
    sig = sign_p1363(key_path, image)
    if len(sig) != SIG_SIZE:
        raise SystemExit("signature is not 64 bytes")

    ver = version.encode("utf-8")[: VERSION_SIZE - 1] + b"\x00"
    ver = ver.ljust(VERSION_SIZE, b"\x00")
    ts = int(time.time()) & 0xFFFFFFFF
    header = struct.pack("<III", IMAGE_MAGIC, len(image), crc)
    header += sig
    header += ver
    header += struct.pack("<I", ts)
    header += b"\x00" * RESERVED_SIZE
    if len(header) != HEADER_SIZE:
        raise SystemExit(f"header size {len(header)} != {HEADER_SIZE}")

    out_path.write_bytes(header + image)
    print(
        f"wrote {out_path}  header=256 image={len(image)} total={256 + len(image)} crc32=0x{crc:08X}"
    )


def main() -> None:
    p = argparse.ArgumentParser(description="Pack APP bin + ECDSA header for CAN-UDS OTA")
    p.add_argument("--bin", required=True, type=Path, help="linked APP .bin (no header)")
    p.add_argument("--key", required=True, type=Path, help="ECDSA P-256 private key PEM")
    p.add_argument("--version", default="1.0.0")
    p.add_argument("--out", required=True, type=Path)
    args = p.parse_args()
    pack(args.bin, args.key, args.version, args.out)


if __name__ == "__main__":
    main()
