# ZCANPRO OTA Configuration Guide

> **Project**: Qi Wireless Charger Bootloader OTA  
> **MCU Platform**: AT32F426 (Cortex-M4F, 128KB Flash, 20KB SRAM)  
> **Document Version**: V1.0  
> **Date**: 2026-08-27

---

## 1. Safe Mode Entry Analysis

In `main.c`, the following 4 conditions trigger `enter_safe_mode()`:

| # | Condition | Scenario |
|---|-----------|----------|
| 1 | `g_meta.ota_state == OTA_STATE_DOWNLOADING` | Previous OTA download interrupted by power loss/reset, metadata not cleared |
| 2 | `select_boot_slot()` returns -1 | Metadata invalid (blank chip / Flash erased) |
| 3 | Both `try_boot_slot(boot_slot)` and `try_boot_slot(other_slot)` fail | No valid image in either slot |
| 4 | `boot_metadata_init()` uses defaults → `slot_a_valid=0, slot_b_valid=0` | First boot on blank chip |

**Common Cause**: Blank chip or erased metadata area — both slots have no valid image, `boot_verify_image()` magic check fails, entering safe_mode. This is normal behavior; safe_mode exists for this exact purpose — waiting for OTA download via CAN.

---

## 2. Flash Layout

```
0x08000000 ┌──────────────────┐ 20KB
           │   Bootloader     │
0x08005000 ├──────────────────┤ 46KB (0xB800)
           │   APP_A Slot     │ ← image_header_t(256B) + firmware
0x08010800 ├──────────────────┤ 46KB (0xB800)
           │   APP_B Slot     │ ← image_header_t(256B) + firmware
0x0801C000 ├──────────────────┤ 2KB
           │   Metadata Primary│
0x0801C800 ├──────────────────┤ 2KB
           │   Metadata Backup │
0x0801D000 ├──────────────────┤
           │   NVM / Other     │
0x08020000 └──────────────────┘ 128KB Flash End
```

### Image Header `image_header_t` (256 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 4 | magic | `0x4F544158` ("XATO") |
| 0x04 | 4 | image_length | Firmware data length (excluding header) |
| 0x08 | 4 | crc32 | CRC32 of firmware data |
| 0x0C | 64 | signature | ECDSA P-256 R‖S signature |
| 0x4C | 16 | version | "MAJOR.MINOR.PATCH\0" |
| 0x5C | 4 | build_timestamp | Unix timestamp |
| 0x60 | 160 | reserved | Padding to 256 bytes |

---

## 3. ZCANPRO Basic Channel Configuration

| Parameter | Value |
|-----------|-------|
| CAN Type | **CAN 2.0B (Extended Frame, 29-bit ID)** |
| Baud Rate | **250 kbps** |
| TX ID (ZCANPRO → MCU) | **0x18DA0D03** |
| RX ID (MCU → ZCANPRO) | **0x18DA030D** |

---

## 4. ISO-TP Frame Format Quick Reference

ZCANPRO requires manual ISO-TP framing (no automatic segmentation):

### Single Frame SF (UDS data ≤ 7 bytes)

```
Byte0: [0x0|len]    ← PCI type=0x00, lower nibble=data length
Byte1..ByteN: UDS data
ByteN+1..Byte7: 0x00 padding
```

### First Frame FF (UDS data > 7 bytes)

```
Byte0: [0x1|len_hi] ← PCI type=0x10, lower nibble=length high bits
Byte1: len_lo       ← Length low 8 bits
Byte2..Byte7: First 6 bytes of UDS data
```

### Consecutive Frame CF

```
Byte0: [0x2|SN]     ← PCI type=0x20, lower nibble=sequence number
Byte1..Byte7: Continuation data
```

SN starts at 1, increments by 1 per frame, wraps from 0x0F back to 0x01.

### Flow Control Frame FC (MCU → ZCANPRO)

```
Byte0: [0x30|FS]    ← FS=0x00(CTS), 0x01(Wait), 0x02(Overflow)
Byte1: BS           ← Block Size (0x00=no limit)
Byte2: STmin        ← CF separation time (0x01=1ms)
Byte3..Byte7: 0xCC padding
```

---

## 5. UDS Command Sequence (Step-by-Step)

### Step 1: Switch to Programming Session

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `02 10 02 00 00 00 00 00` |
| Parse | `02`=SF len 2, `10`=DiagnosticSessionControl, `02`=ProgrammingSession |
| Expected Response | `02 50 02 00 00 00 00 00` |
| Wait After TX | **100ms** |

### Step 2: SecurityAccess - Request Seed (0x27 0x01)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `02 27 01 00 00 00 00 00` |
| Parse | `02`=SF len 2, `27`=SecurityAccess, `01`=RequestSeed |
| Expected Response | `06 67 01 [seed0] [seed1] [seed2] [seed3]` |
| Wait After TX | **50ms** |

### Step 3: SecurityAccess - Transfer Signature (0x27 0x03)

ECDSA P-256 signature is 64 bytes; max 6 bytes per frame → 11 frames total.

**Frame format**: `[0x07, 0x27, 0x03, blockSeq, sig0..sig5]`

| Frame | blockSeq | Data |
|-------|----------|------|
| 1 | 0x01 | `07 27 03 01 [sig0..sig5]` |
| 2 | 0x02 | `07 27 03 02 [sig6..sig11]` |
| 3 | 0x03 | `07 27 03 03 [sig12..sig17]` |
| 4 | 0x04 | `07 27 03 04 [sig18..sig23]` |
| 5 | 0x05 | `07 27 03 05 [sig24..sig29]` |
| 6 | 0x06 | `07 27 03 06 [sig30..sig35]` |
| 7 | 0x07 | `07 27 03 07 [sig36..sig41]` |
| 8 | 0x08 | `07 27 03 08 [sig42..sig47]` |
| 9 | 0x09 | `07 27 03 09 [sig48..sig53]` |
| 10 | 0x0A | `07 27 03 0A [sig54..sig59]` |
| 11 | 0x0B | `07 27 03 0B [sig60..sig63] 00 00` |

**Per-frame interval**: Wait for positive response `03 67 03 [blockSeq]`, then **10~20ms** before next frame.

### Step 4: SecurityAccess - Verify Signature (0x27 0x02)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `02 27 02 00 00 00 00 00` |
| Parse | `02`=SF len 2, `27`=SecurityAccess, `02`=SendKey/Verify |
| Expected NRC 0x78 | `03 7F 27 78 00 00 00 00` (ResponsePending, not an error) |
| Expected Response | `02 67 02 00 00 00 00 00` |
| Wait Time | **200~800ms** (ECDSA verification) |

### Step 5: Select Firmware Type (0x2E 0x2010)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `04 2E 20 10 01 00 00 00` |
| Parse | `04`=SF len 4, `2E`=WriteDataByIdentifier, `2010`=DID, `01`=APP |
| Expected Response | `03 6E 20 10 00 00 00 00` |
| Wait After TX | **100ms** |

### Step 6: Erase Flash (0x31 0x01 0xFF00)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `04 31 01 FF 00 00 00 00` |
| Parse | `04`=SF len 4, `31`=RoutineControl, `01`=startRoutine, `FF00`=erase |
| Expected NRC 0x78 | `03 7F 31 78 00 00 00 00` (erasing, be patient) |
| Expected Response | `04 71 01 FF 00 00 00 00` |
| Wait Time | **1~2 seconds** (Flash erase) |

### Step 7: Request Download (0x34)

UDS data is 13 bytes → requires ISO-TP First Frame + Consecutive Frame.

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| FF Data | `10 0D 34 00 44 08 00 50` |
| CF_1 Data | `21 00 00 00 [size0] [size1] [size2] [size3] CC` |
| Parse | `34`=RequestDownload, `00`=dataFormatIdentifier, `44`=ALFID(4-byte addr+4-byte len), `08005000`=APP_A base |
| Expected Response | `04 74 20 01 00 00 00 00` (maxBlockLen=256) |
| Wait After TX | **100ms** |

**Size calculation**: Firmware file size in bytes (excluding the 256-byte header), big-endian. Example: 40KB firmware = `0x0000A000`.

### Step 8: TransferData (0x36) — Block Transfer

Each TransferData block UDS format: `[0x36, blockSeq, data...]`

**Max UDS data per block: 256 bytes** → ISO-TP requires FF + multiple CFs.

| Parameter | Value |
|-----------|-------|
| blockSeq start | 0x01 |
| blockSeq increment | +1/block, skip 0x00 (0xFF → 0x01) |
| ISO-TP CF interval | **1ms** (MCU FC STmin=0x01) |
| Block interval | After positive response `02 76 [BS]`, wait **10ms** |

**Example: 256-byte data block ISO-TP frames**

```
FF:  10 [len_hi] [len_lo] 36 [BS] [data0..data4]
FC:  30 00 01 CC CC CC CC CC   ← MCU flow control
CF:  21 [data5..data11]
CF:  22 [data12..data18]
CF:  23 [data19..data25]
...
CF:  2x [remaining data]
```

### Step 9: RequestTransferExit (0x37)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `01 37 00 00 00 00 00 00` |
| Parse | `01`=SF len 1, `37`=RequestTransferExit |
| Expected NRC 0x78 | `03 7F 37 78 00 00 00 00` (verifying image) |
| Expected Response | `01 77 00 00 00 00 00 00` |
| Wait Time | **100~500ms** (image verification + ECDSA signature check) |

### Step 10: ECUReset (0x11)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `02 11 01 00 00 00 00 00` |
| Parse | `02`=SF len 2, `11`=ECUReset, `01`=hardReset |
| Expected Response | `02 51 01 00 00 00 00 00` |
| Wait Time | **2000ms** (wait for MCU reset completion) |

### Step 11: Verify Version (after reset, reconnect)

| Item | Value |
|------|-------|
| TX ID | `0x18DA0D03` |
| Data | `03 22 F1 95 00 00 00 00` |
| Parse | `03`=SF len 3, `22`=ReadDataByIdentifier, `F195`=ECU Software Version |
| Expected Response | `xx 62 F1 95 [version string...]` |

---

## 6. Timing Summary

| Step | Command | Wait After TX | Special Notes |
|------|---------|---------------|---------------|
| 1 | 0x10 0x02 | **100ms** | — |
| 2 | 0x27 0x01 | **50ms** | Received seed |
| 3 | 0x27 0x03 ×11 | **10~20ms/frame** | Wait for positive response each frame |
| 4 | 0x27 0x02 | **Wait for NRC 0x78 to end** | Verification 200~800ms |
| 5 | 0x2E 0x2010 | **100ms** | — |
| 6 | 0x31 0x01 0xFF00 | **Wait for NRC 0x78 to end** | Erase 1~2 seconds |
| 7 | 0x34 | **100ms** | — |
| 8 | 0x36 × N | **10ms/block** | ISO-TP CF interval 1ms |
| 9 | 0x37 | **Wait for NRC 0x78 to end** | Verification 100~500ms |
| 10 | 0x11 0x01 | **2000ms** | Wait for reset |
| 11 | 0x22 0xF195 | — | Confirm version |

---

## 7. S3 Timeout Mechanism

| Parameter | Value |
|-----------|-------|
| S3 Timeout | **5000ms** |
| Timeout Behavior | Revert to Default Session, clear SecurityAccess unlock |
| NRC 0x78 Impact | **Does NOT trigger S3 timeout** (NRC 0x78 resets the timer) |

**Countermeasure**: Ensure total manual wait between steps does not exceed 5 seconds. No concern during NRC 0x78 waits.

### Manual Operation Timing (ZCANPRO Click-by-Click Scenario)

When sending commands manually in ZCANPRO, click the next command **within 3 seconds** of receiving a positive response, leaving 2 seconds of margin.

Timing requirements by stage:

| Stage | Can You Wait? | Reason |
|-------|---------------|--------|
| Step 1→2→3→4 (session + security) | ⚠️ Each step < 5s | S3 timeout clears unlock |
| Step 4 signature verify | ✅ OK to wait | NRC 0x78 keeps refreshing S3 timer |
| Step 4→5→6 (post-unlock) | ⚠️ Each step < 5s | S3 timeout reverts session |
| Step 6 erase | ✅ OK to wait | NRC 0x78 refreshes S3 |
| Step 7→8→9 (download transfer) | ⚠️ Each step < 5s | S3 timeout |
| Step 9 transferExit | ✅ OK to wait | NRC 0x78 refreshes S3 |
| Step 9→10 (exit → reset) | ⚠️ < 5s | Must be in current session before reset |

### TesterPresent Keepalive Scheme

If manual clicking cannot keep up with the 5-second interval, configure a **periodic TesterPresent auto-send task** in ZCANPRO, sending every **2 seconds**:

```
TX ID: 0x18DA0D03
Data: 02 3E 00 00 00 00 00 00
```

- `02` = SF length 2
- `3E` = TesterPresent
- `00` = sub-function (no positive response required)

This frame continuously refreshes the S3 timer, ensuring sufficient time for manual operation between steps. Steps that return NRC 0x78 (Step 4/6/9) can be waited on safely — MCU will send a positive response when done, and the next command should be sent within 3 seconds of receiving it.

---

## 8. SecurityAccess Lockout Mechanism

| Parameter | Value |
|-----------|-------|
| Max consecutive failures | **3** |
| Lockout duration | **60 seconds** |
| NRC during lockout | `0x36` (ExceededNumberOfAttempts) |
| Failure NRC | `0x35` (InvalidKey) |

After 3 verification failures, must wait 60 seconds before retry. Use correct ECDSA signature to avoid lockout.

---

## 9. Quick Verification Steps

First verify CAN communication is working, then proceed with full OTA:

1. Send `0x10 0x02` → Receive `0x50 0x02` ✅ Session switch OK
2. Send `0x27 0x01` → Receive `0x67 01 [seed]` ✅ Seed obtained

Both steps pass → proceed with Steps 3~11 for full OTA flow.

---

## 10. NRC Error Code Quick Reference

| NRC | Meaning | Common Cause |
|-----|---------|--------------|
| 0x11 | ServiceNotSupported | Wrong session or SID |
| 0x12 | SubfunctionNotSupported | Invalid sub-function value |
| 0x13 | IncorrectMessageLength | Data length mismatch |
| 0x22 | ConditionsNotCorrect | Not in Programming Session |
| 0x24 | RequestSequenceError | Wrong step order (e.g. download before erase) |
| 0x31 | RequestOutOfRange | DID/parameter out of bounds |
| 0x33 | SecurityAccessDenied | SecurityAccess not unlocked |
| 0x35 | InvalidKey | ECDSA signature verification failed |
| 0x36 | ExceededNumberOfAttempts | 3 consecutive failures, locked out |
| 0x37 | RequiredTimeDelayNotExpired | 60-second lockout not elapsed |
| 0x70 | UploadDownloadNotAccepted | Download request rejected |
| 0x71 | TransferDataAborted | Transfer aborted (sequence error or overflow) |
| 0x72 | GeneralProgrammingFailure | Flash write/erase failure |
| 0x73 | WrongBlockSequence | blockSeq not sequential |
| 0x78 | ResponsePending | **Not an error** — MCU processing long operation |

---

## Changelog

| Version | Date | Description |
|---------|------|-------------|
| V1.0 | 2026-08-27 | Initial release, complete ZCANPRO OTA configuration guide |
| V1.1 | 2026-08-27 | Added manual operation timing constraints, TesterPresent keepalive scheme |
