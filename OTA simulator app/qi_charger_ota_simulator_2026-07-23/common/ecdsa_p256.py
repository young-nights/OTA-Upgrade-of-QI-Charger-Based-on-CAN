#!/usr/bin/env python3
"""
ECDSA P-256 helper utilities for LIME peripheral firmware signing.

Wire formats used across the toolchain (chosen to minimise churn versus the
previous Ed25519 design):

  - Curve:       NIST P-256 (secp256r1 / prime256v1)
  - Digest:      SHA-256 (ECDSA signs the hash, not the raw message)
  - Signature:   raw R || S, fixed 64 bytes (IEEE P1363), NOT ASN.1 DER.
                 This keeps the 0x30-offset 64-byte signature field in the
                 firmware package header and the [27 02 <64>] SecurityAccess
                 frame unchanged from the Ed25519 layout.
  - Public key:  uncompressed SEC1 point, 65 bytes (0x04 || X(32) || Y(32)).
  - Private key: raw 32-byte private scalar (SEC1 private value).

Keypair file layout (*.bin):
  - 32 bytes  private scalar
  - 65 bytes  uncompressed public key
  = 97 bytes total
Public-key-only file: 65 bytes.

Requires the `cryptography` package (pip3 install cryptography).
"""

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import (
    decode_dss_signature,
    encode_dss_signature,
)
from cryptography.exceptions import InvalidSignature

CURVE = ec.SECP256R1()
PRIVATE_KEY_SIZE = 32   # raw private scalar
PUBLIC_KEY_SIZE = 65    # uncompressed point: 0x04 || X || Y
SIGNATURE_SIZE = 64     # raw R || S (P1363)


def generate_keypair():
    """Generate a new P-256 keypair.

    Returns (private_key_obj, private_bytes(32), public_bytes(65)).
    """
    private_key = ec.generate_private_key(CURVE)
    return private_key, private_bytes(private_key), public_bytes(private_key)


def private_bytes(private_key) -> bytes:
    """Serialise a private key to its raw 32-byte scalar."""
    value = private_key.private_numbers().private_value
    return value.to_bytes(PRIVATE_KEY_SIZE, 'big')


def public_bytes(private_key) -> bytes:
    """Serialise the public key of `private_key` as an uncompressed 65-byte point."""
    return public_bytes_from_public(private_key.public_key())


def public_bytes_from_public(public_key) -> bytes:
    """Serialise a public key object as an uncompressed 65-byte SEC1 point."""
    from cryptography.hazmat.primitives.serialization import (
        Encoding,
        PublicFormat,
    )
    return public_key.public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)


def load_private_key(data: bytes):
    """Load a private key from a keypair file blob.

    Accepts:
      - PEM text  : -----BEGIN ... PRIVATE KEY-----
      - 32 bytes  : raw private scalar
      - 97 bytes  : 32-byte scalar + 65-byte public key (public part ignored)
    """
    stripped = data.lstrip()
    if stripped.startswith(b"-----BEGIN"):
        from cryptography.hazmat.primitives.serialization import load_pem_private_key
        return load_pem_private_key(data, password=None)
    if len(data) not in (PRIVATE_KEY_SIZE, PRIVATE_KEY_SIZE + PUBLIC_KEY_SIZE):
        raise ValueError(
            f"Invalid private key blob size: {len(data)} bytes "
            f"(expected PEM, {PRIVATE_KEY_SIZE}, or {PRIVATE_KEY_SIZE + PUBLIC_KEY_SIZE})"
        )
    scalar = int.from_bytes(data[:PRIVATE_KEY_SIZE], 'big')
    return ec.derive_private_key(scalar, CURVE)


def load_public_key(data: bytes):
    """Load a public key from a 65-byte uncompressed point (or a 97-byte keypair blob)."""
    if len(data) == PRIVATE_KEY_SIZE + PUBLIC_KEY_SIZE:
        data = data[PRIVATE_KEY_SIZE:]
    if len(data) != PUBLIC_KEY_SIZE:
        raise ValueError(
            f"Invalid public key size: {len(data)} bytes (expected {PUBLIC_KEY_SIZE})"
        )
    return ec.EllipticCurvePublicKey.from_encoded_point(CURVE, data)


def sign(private_key, message: bytes) -> bytes:
    """Sign `message` with SHA-256 ECDSA, returning a raw 64-byte R||S signature."""
    der = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')


def verify(public_key_or_bytes, message: bytes, signature: bytes) -> bool:
    """Verify a raw 64-byte R||S signature over `message`.

    `public_key_or_bytes` may be a public key object or a 65-byte encoded point.
    Returns True on success, False on any verification failure.
    """
    if isinstance(public_key_or_bytes, (bytes, bytearray)):
        public_key = load_public_key(bytes(public_key_or_bytes))
    else:
        public_key = public_key_or_bytes

    if len(signature) != SIGNATURE_SIZE:
        return False

    r = int.from_bytes(signature[:32], 'big')
    s = int.from_bytes(signature[32:], 'big')
    der = encode_dss_signature(r, s)
    try:
        public_key.verify(der, message, ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False
