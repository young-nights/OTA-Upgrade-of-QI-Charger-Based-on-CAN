# Qi Charger OTA Clients

CCU-side UDS firmware-upgrade (OTA) clients for the Qi wireless charger module,
built from the Qi charger SRS (`../docs/qi_charger_srs.md`, LIME-QI-PERIPH-SRS-001)
and the common CAN protocol specification (`../../common/docs/common_can_protocol_spec.md`, REF-1).

The client plays the role of the **CCU** (UDS client, address `0x03`); the Qi
charger module is the **UDS server** (address `0x0D`).

| File | Security mode | Use |
|------|---------------|-----|
| `qi_upgrade_client.py` | Simple seed+key (`key = seed + 0x5555`) | Bench testing only — placeholder algorithm |
| `qi_upgrade_client_ecdsa.py` | ECDSA P-256 challenge-response | Production-representative; required by SRS v1.1 §2.3 / REF-1 §7.4 |
| `qi_charger_constants.py` (in `../`) | — | Qi CAN IDs, DIDs, routines, and value tables |
| `ecdsa-p256-keys/generate_keypair.py` | — | Generates the ECDSA P-256 keypair |

## CAN configuration (from SRS §2.3)

| Item | Value |
|------|-------|
| Data link | Classical CAN 2.0B, **250 kbps**, 29-bit extended IDs |
| UDS request (CCU → Qi) | `0x18DA0D03` |
| UDS response (Qi → CCU) | `0x18DA030D` |
| Firmware type: APP | `0x01` (note: differs from the display module's `0x00`) |
| Firmware type: Bootloader | `0x03` (optional, P1) |
| Erase routine | `0xFF00` |

## OTA sequence (REF-1 §9.1)

Programming Session (`0x10 0x02`) → SecurityAccess L1 (`0x27`) → Select Firmware
Type (`0x2E 0x2010`) → Erase Memory (`0x31 0x01 0xFF00`) → RequestDownload (`0x34`)
→ TransferData (`0x36`, sequence-numbered blocks) → RequestTransferExit (`0x37`) →
CCUReset (`0x11`) → verify version (`0x22 0xF195`) and OTA DIDs (`0x2112`–`0x2116`).

## Dependencies

```bash
pip3 install python-can python-can-isotp cryptography
```

## ECDSA P-256 keys

The private keypair is git-ignored and must be generated (or provided by the
firmware team so it matches the public key provisioned into the module):

```bash
cd ecdsa-p256-keys
python3 generate_keypair.py          # creates qi_ecdsa_p256_keypair.bin (+ _public.bin)
```

Crypto: NIST P-256, SHA-256 digest, 64-byte raw R‖S signature, 65-byte
uncompressed public key (see `../../common/ecdsa_p256.py`).

> The client hashes the challenge seed with SHA-256 and signs it with the
> **private** key; the module verifies with the **public** key. Both MUST come
> from the same keypair or SecurityAccess fails with NRC `0x35`.

## Usage

### ECDSA P-256 (recommended)

```bash
cd qi_charger/simulator
python3 qi_upgrade_client_ecdsa.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --keypair ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin \
    --firmware /path/to/qi_app_firmware.bin \
    --type app --verbose
```

### Simple security (bench only)

```bash
cd qi_charger/simulator
python3 qi_upgrade_client.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --firmware /path/to/qi_app_firmware.bin \
    --type app --verbose
```

### Common options

| Option | Default | Notes |
|--------|---------|-------|
| `--bustype` | `canalystii` | `socketcan` / `canalystii` / `pcan` / `virtual` |
| `--channel` | `0` | `can0` for socketcan; `0` for canalystii |
| `--bitrate` | `250000` | Must match the Qi bus (250 kbps) |
| `--device` | `0` | Device index (single USB-CAN box = `0`) |
| `--firmware` | — | Real firmware binary; omit to send generated test data |
| `--size` | `98304` | Test firmware size when `--firmware` not given |
| `--type` | `app` | `app` or `bootloader` |
| `--address` | `0x08000000` | Flash download address |
| `--keypair` | `ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin` | ECDSA client only |

## Hardware setup (USB-CAN box → Qi module)

For a laptop → USB CAN adapter (e.g. CANalyst-II) → CAN bus → Qi module setup:

1. Install permissions for the adapter (see repo-root `fix_usb_permission.sh` /
   `99-canalystii.rules` for CANalyst-II).
2. Confirm the adapter's CANH/CANL are wired to the Qi module, grounds are common,
   and the bus has 120 Ω termination at both ends.
3. Verify the bitrate is 250 kbps on both ends.
4. Use `--device 0` for a single adapter.
