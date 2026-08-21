#!/usr/bin/env python3
"""Compute SecurityAccess (0x27 0x03) frames from a live UDS seed.

MCU 返回 4 字节 seed 后，本脚本对 SHA256(seed) 做 ECDSA P-256 签名，
并按 Classical CAN 8 字节上限拆成可粘贴到 ZCANPRO「普通发送」的帧。

ZCANPRO 不会自动执行本脚本。用法是：ZCANPRO 发 27 01 → 把应答里的
4 字节 seed 交给本脚本 → 按打印结果在 ZCANPRO 里逐帧发送 27 03，
最后再发 27 02。

Examples:
  python3 sign_security_access.py A3 5C 12 8F
  python3 sign_security_access.py A35C128F
  python3 sign_security_access.py 67 01 A3 5C 12 8F
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_PRIVATE_KEY = SCRIPT_DIR / "keys" / "private.pem"
DEFAULT_PUBLIC_KEY = SCRIPT_DIR / "keys" / "public.pem"

UDS_REQ_ID = "18DA0D03"
SA_SID = 0x27
SA_TRANSFER_SUB = 0x03
SA_VERIFY_SUB = 0x02
CAN_MAX_DLC = 8
# 27 03 seq 占 3 字节，Classical CAN 每帧最多再带 5 字节签名
SIG_BYTES_PER_FRAME = CAN_MAX_DLC - 3
SIGNATURE_LEN = 64


def parse_seed(tokens: list[str]) -> bytes:
    """Accept spaced hex, concatenated hex, or a full 67 01 + seed response."""
    hexstr = "".join(tokens).replace("0x", "").replace("0X", "").replace(",", "")
    hexstr = "".join(ch for ch in hexstr if ch not in " \t\r\n:-")
    try:
        raw = bytes.fromhex(hexstr)
    except ValueError as exc:
        raise ValueError(f"invalid hex: {tokens}") from exc

    if len(raw) == 4:
        return raw
    if len(raw) >= 6 and raw[0] == 0x67 and raw[1] == 0x01:
        return raw[2:6]
    raise ValueError(
        f"need 4-byte seed (got {len(raw)} bytes). "
        "Paste either A3 5C 12 8F or the full response 67 01 A3 5C 12 8F"
    )


def load_private_key(path: Path):
    data = path.read_bytes()
    return serialization.load_pem_private_key(data, password=None)


def load_public_key(path: Path):
    return serialization.load_pem_public_key(path.read_bytes())


def sign_seed(seed: bytes, private_key) -> bytes:
    digest = hashlib.sha256(seed).digest()
    der = private_key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    if len(sig) != SIGNATURE_LEN:
        raise RuntimeError(f"unexpected signature length {len(sig)}")
    return digest, sig


def verify_seed_signature(seed: bytes, digest: bytes, sig: bytes, public_key) -> None:
    r = int.from_bytes(sig[:32], "big")
    s = int.from_bytes(sig[32:], "big")
    der = utils.encode_dss_signature(r, s)
    public_key.verify(der, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))


def split_frames(signature: bytes) -> list[bytes]:
    frames = []
    offset = 0
    seq = 1
    while offset < len(signature):
        chunk = signature[offset:offset + SIG_BYTES_PER_FRAME]
        frames.append(bytes([SA_SID, SA_TRANSFER_SUB, seq]) + chunk)
        offset += len(chunk)
        seq += 1
    return frames


def fmt_hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Sign UDS SecurityAccess seed and print ZCANPRO 0x27 0x03 frames."
    )
    parser.add_argument(
        "seed",
        nargs="*",
        help="4-byte seed hex, or full MCU response starting with 67 01",
    )
    parser.add_argument(
        "--key",
        type=Path,
        default=DEFAULT_PRIVATE_KEY,
        help=f"ECDSA private key PEM (default: {DEFAULT_PRIVATE_KEY})",
    )
    parser.add_argument(
        "--pubkey",
        type=Path,
        default=DEFAULT_PUBLIC_KEY,
        help=f"ECDSA public key PEM used for self-check (default: {DEFAULT_PUBLIC_KEY})",
    )
    parser.add_argument(
        "--frames-only",
        action="store_true",
        help="print only data-field hex lines (no comments)",
    )
    args = parser.parse_args(argv)

    if args.seed:
        seed_tokens = args.seed
    else:
        try:
            typed = input("Paste seed or 67 01 + seed, then Enter: ").strip()
        except EOFError:
            parser.error("seed is required")
        if not typed:
            parser.error("seed is required")
        seed_tokens = typed.split()

    try:
        seed = parse_seed(seed_tokens)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if not args.key.is_file():
        print(f"error: private key not found: {args.key}", file=sys.stderr)
        return 2

    private_key = load_private_key(args.key)
    digest, signature = sign_seed(seed, private_key)

    if args.pubkey.is_file():
        try:
            verify_seed_signature(seed, digest, signature, load_public_key(args.pubkey))
            verified = True
        except InvalidSignature:
            print("error: signature does not match public key", file=sys.stderr)
            return 1
    else:
        verified = False

    frames = split_frames(signature)

    if args.frames_only:
        for frame in frames:
            print(fmt_hex(frame))
        print(f"{SA_SID:02X} {SA_VERIFY_SUB:02X}")
        return 0

    print(f"Seed          : {fmt_hex(seed)}")
    print(f"SHA256(seed)  : {fmt_hex(digest)}")
    print(f"Signature 64B : {fmt_hex(signature)}")
    print(f"Private key   : {args.key}")
    if verified:
        print(f"Self-verify   : OK ({args.pubkey})")
    else:
        print("Self-verify   : skipped (public key not found)")
    print()
    print("ZCANPRO cannot run this script. Copy the frames below into 普通发送.")
    print(f"帧ID = {UDS_REQ_ID}   CAN类型 = 扩展帧")
    print("每发一帧，等 Rx 18DA030D 返回 67 03 <序号> 再发下一帧。")
    print()

    for i, frame in enumerate(frames, start=1):
        seq = frame[2]
        print(
            f"  {i:2d}/{len(frames)}  长度={len(frame)}  "
            f"数据: {fmt_hex(frame):<32s}  期望: 67 03 {seq:02X}"
        )

    print()
    print("全部 27 03 成功后再发验签：")
    print(
        f"       长度=2  数据: {SA_SID:02X} {SA_VERIFY_SUB:02X}"
        f"                         期望: 67 02"
    )
    print("验签可能要 200-800 ms。失败常见为 7F 27 33（签名不对）或 7F 27 35（次数超限）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
