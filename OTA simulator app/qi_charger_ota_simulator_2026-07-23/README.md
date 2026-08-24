# Qi Charger UDS FW OTA Simulator (Upper-Computer / 上位机)

This package contains the **CCU-side** UDS firmware-OTA clients for the Qi
wireless charger module. Use them as the upper-computer (上位机) to develop and
debug the Qi charger's UDS firmware-upgrade (OTA) server implementation.

The client plays the role of the **CCU** (UDS client, address `0x03`); the Qi
charger module you are developing is the **UDS server** (address `0x0D`).

---

## 1. What's in this package

```
qi_charger_ota_simulator/
├── requirements.txt                  # Python dependencies
├── README.md                         # this file
├── common/
│   ├── uds_constants.py              # shared UDS SID / session / NRC constants
│   ├── ecdsa_p256.py                 # ECDSA P-256 sign/verify helper
│   └── docs/common_can_protocol_spec.md   # REF-1 common protocol spec
└── qi_charger/
    ├── qi_charger_constants.py       # Qi CAN IDs, DIDs, routines, value tables
    ├── docs/qi_charger_srs.md        # Qi charger SRS v1.1
    └── simulator/
        ├── qi_upgrade_client.py          # SIMPLE security client (bench / bring-up)
        ├── qi_upgrade_client_ecdsa.py    # ECDSA P-256 client (SRS-compliant target)
        ├── README.md                     # detailed client usage
        └── ecdsa-p256-keys/
            ├── generate_keypair.py       # generate a test ECDSA P-256 keypair
            ├── test_ecdsa_crypto.py      # offline crypto self-test (no CAN needed)
            └── README.md
```

> These are **source files** — you must install a Python runtime and the
> dependencies below before running them (see §2).

---

## 2. Setup (required before first run)

The clients need **Python 3.8+** and a few libraries. They are NOT bundled.

```bash
# 1. Install Python dependencies
pip3 install -r requirements.txt

# 2. Install your CAN adapter's driver + set USB permissions
#    (e.g. CANalyst-II: pip3 install canalystii pyusb, plus a udev rule on Linux)

# 3. Wire the bus: 250 kbps, 120 Ω termination at both ends,
#    common ground between the adapter and the Qi module.
```

Run everything from the package root so the `common` / `qi_charger` packages
import correctly.

---

## 3. CAN configuration (Qi charger SRS v1.1 §2.3)

| Item | Value |
|------|-------|
| Data link | Classical CAN 2.0B, **250 kbps**, 29-bit extended IDs |
| UDS request (CCU → Qi) | `0x18DA0D03` |
| UDS response (Qi → CCU) | `0x18DA030D` |
| Firmware type: APP | `0x01` |
| Firmware type: Bootloader | `0x03` (optional) |
| Erase routine | `0xFF00` |
| SecurityAccess (0x27) | **ECDSA P-256** challenge-response (SRS v1.1) |

---

## 4. OTA sequence (REF-1 §9.1)

```
Programming Session (0x10 0x02)
  → SecurityAccess L1 (0x27)                 ← the only step that differs between the two clients
  → Select Firmware Type (0x2E 0x2010)
  → Erase Memory (0x31 0x01 0xFF00)
  → RequestDownload (0x34)
  → TransferData (0x36, sequence-numbered blocks)
  → RequestTransferExit (0x37)
  → CCUReset (0x11)
  → verify version (0x22 0xF195) and OTA DIDs (0x2112–0x2116)
```

---

## 5. Two clients — recommended development order

Both clients share the exact same OTA flow; they differ **only** in the
SecurityAccess (0x27) step.

| Client | SecurityAccess | Purpose |
|--------|----------------|---------|
| `qi_upgrade_client.py` | Simple seed+key (`key = seed + 0x5555`) | **Bring-up scaffold** — get the transport + OTA flow working first |
| `qi_upgrade_client_ecdsa.py` | ECDSA P-256 signature | **SRS-compliant target** — the required production behavior |

**Recommended two-stage approach (see the email / §7):**

1. **Stage 1 — simple client.** Implement a simple `seed + 0x5555` SecurityAccess
   on the module first, and use `qi_upgrade_client.py` to bring up and debug the
   whole OTA backbone (ISO-TP framing, session control, erase, download,
   TransferData block sequencing, reset, version read-back). This isolates the
   transport/flow problems from the crypto.

2. **Stage 2 — ECDSA client.** Once the backbone is solid, switch the module's
   SecurityAccess to **ECDSA P-256** and validate with
   `qi_upgrade_client_ecdsa.py`. Only the 0x27 step changes, so debugging is
   focused.

> ⚠️ The simple `seed + 0x5555` algorithm is a **development scaffold only**.
> The Qi charger SRS v1.1 §2.3 requires **ECDSA P-256** for SecurityAccess in
> production. Disable/remove the simple mode in production firmware.

---

## 6. Usage

### Offline crypto check (no CAN hardware needed) — do this first for ECDSA

```bash
cd qi_charger/simulator/ecdsa-p256-keys
python3 test_ecdsa_crypto.py       # verifies keypair / sign / verify / UDS 0x27 flow
python3 generate_keypair.py        # creates a test keypair (private + public .bin)
```

Provision the generated **public** key into the Qi module; the client signs
with the **private** key. Both MUST come from the same keypair, or
SecurityAccess fails with NRC `0x35`.

### Stage 1 — simple security client

```bash
cd qi_charger/simulator
python3 qi_upgrade_client.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --firmware /path/to/qi_app.bin --type app --verbose
```

### Stage 2 — ECDSA P-256 client

```bash
cd qi_charger/simulator
python3 qi_upgrade_client_ecdsa.py \
    --bustype canalystii --channel 0 --device 0 --bitrate 250000 \
    --keypair ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin \
    --firmware /path/to/qi_app.bin --type app --verbose
```

### Common options

| Option | Default | Notes |
|--------|---------|-------|
| `--bustype` | `canalystii` | `socketcan` / `canalystii` / `pcan` / `virtual` |
| `--channel` | `0` | `can0` for socketcan; `0` for canalystii |
| `--bitrate` | `250000` | Must match the Qi bus (250 kbps) |
| `--device` | `0` | Adapter index (single USB-CAN box = `0`) |
| `--firmware` | — | Real firmware binary; omit to send generated test data |
| `--type` | `app` | `app` or `bootloader` |
| `--keypair` | `ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin` | ECDSA client only |

See `qi_charger/simulator/README.md` for full details.

---

## 7. Reference documents

- `qi_charger/docs/qi_charger_srs.md` — Qi charger SRS **v1.1**
- `common/docs/common_can_protocol_spec.md` — common CAN/UDS/ISO-TP protocol (REF-1)
