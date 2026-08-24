# Qi Charger ECDSA P-256 Keys

ECDSA P-256 keypair for Qi charger firmware-upgrade authentication (SecurityAccess
Level 1), per SRS v1.1 §2.3 (REF-5 FIPS 186-4) and the common CAN protocol
specification REF-1 §7.4.

Crypto (see `../../../common/ecdsa_p256.py`):

| Item | Value |
|------|-------|
| Curve | NIST P-256 (secp256r1 / prime256v1) |
| Digest | SHA-256 (the seed is hashed, then the digest is signed) |
| Signature | raw R‖S, fixed 64 bytes (IEEE P1363), NOT ASN.1 DER |
| Public key | uncompressed SEC1 point, 65 bytes (`0x04 ‖ X ‖ Y`) |
| Private key | raw 32-byte private scalar |

## Generate a keypair

The keys are **one-off test keys** — no keypair is committed to the repository
(all `*.bin` are git-ignored). Generate a fresh pair before use:

```bash
python3 generate_keypair.py
```

This produces:

| File | Contents |
|------|----------|
| `qi_ecdsa_p256_keypair.bin` | 32-byte private scalar + 65-byte public key (97 bytes) — used by the OTA client |
| `qi_ecdsa_p256_keypair_public.bin` | 65-byte uncompressed public key — provision into the Qi module |

## How it's used

1. The OTA client (`../qi_upgrade_client_ecdsa.py`) loads the **private**
   keypair, hashes the challenge seed with SHA-256, and signs the digest —
   returning a 64-byte raw R‖S signature.
2. The Qi charger module verifies the signature with the **public** key
   provisioned into it.

Both sides MUST use the same keypair. If the module was provisioned with a
different public key, SecurityAccess fails with NRC `0x35` (Invalid Key).

> For real hardware, obtain the keypair whose public key was provisioned into
> the Qi module (per SRS Open Item "ECDSA P-256 public key provisioning") rather
> than generating a fresh one.
