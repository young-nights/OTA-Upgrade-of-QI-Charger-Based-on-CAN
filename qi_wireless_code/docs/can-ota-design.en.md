# CAN-OTA Firmware Upgrade Technical Design Document

**Project**: QI Wireless Charging Module CAN-OTA Upgrade Solution  
**MCU**: AT32F426KBU7-4 (Cortex-M4F, 180MHz, 128KB Flash, 20KB SRAM)  
**Version**: v1.0  
**Date**: 2026-07-28

---

## 1. Flash Layout

```
+---------------------------+ 0x08000000
|                           |
|     Bootloader (16KB)     |
|     0x08000000-0x08003FFF |
|                           |
+---------------------------+ 0x08004000
|                           |
|     APP_A (48KB)          |
|     Main Application      |
|     0x08004000-0x0800FFFF |
|                           |
+---------------------------+ 0x08010000
|                           |
|     APP_B (48KB)          |
|     Backup (unused in v1) |
|     0x08010000-0x0801BFFF |
|                           |
+---------------------------+ 0x0801C000
|                           |
|     OTA Flag Area (16KB)  |
|     0x0801C000-0x0801FFFF |
|                           |
+---------------------------+ 0x08020000
```

### 1.1 Region Description

| Region    | Start Addr  | Size | Purpose                                  |
|-----------|-------------|------|------------------------------------------|
| Bootloader| 0x08000000  | 16KB | Boot, OTA logic, CAN communication       |
| APP_A     | 0x08004000  | 48KB | Main application firmware                |
| APP_B     | 0x08010000  | 48KB | Backup partition (reserved, unused in v1)|
| OTA Flag  | 0x0801C000  | 16KB | OTA request flag, upgrade status          |

### 1.2 OTA Flag Definitions

| Address     | Value      | Meaning                         |
|-------------|------------|---------------------------------|
| 0x0801C000  | 0xFFFFFFFF | No OTA request (erased state)   |
| 0x0801C000  | 0x544F4152 | OTA upgrade request ("RAOT")    |
| 0x0801C000  | 0x544F4144 | OTA upgrade complete ("DAOT")   |

---

## 2. CAN-OTA Frame Format

### 2.1 CAN ID Assignment

| CAN ID | Direction          | Purpose    |
|--------|--------------------|------------|
| 0x100  | Host → Device      | Command    |
| 0x101  | Host → Device      | Data       |
| 0x102  | Device → Host      | ACK/NAK    |

### 2.2 Command Frame (CAN ID = 0x100)

```
Byte    Content         Description
[0]     Command Code    OTA_CMD_xxx
[1-4]   Parameters      Command-specific (little-endian)
[5-7]   Reserved        0x00
```

| Command | Value | Parameter Description                |
|---------|-------|--------------------------------------|
| START   | 0x01  | [1-4] = Firmware total size (uint32) |
| DATA    | 0x02  | (Not used in CMD frame, reference)   |
| VERIFY  | 0x03  | [1-4] = Expected CRC32 (uint32)      |
| RESET   | 0x04  | No parameter                         |
| QUERY   | 0x05  | No parameter                         |
| ABORT   | 0x06  | No parameter                         |

### 2.3 Data Frame (CAN ID = 0x101)

```
Byte    Content         Description
[0]     Sequence No.    0x00-0xFF, incrementing
[1]     Payload Length   Valid data bytes in this frame (1-4)
[2-5]   Payload         Up to 4 bytes of firmware data
[6-7]   CRC16           CRC16 of bytes[0-5] (little-endian)
```

Each frame carries a maximum of 4 bytes of payload. Firmware is split into 4-byte chunks.

### 2.4 ACK Frame (CAN ID = 0x102)

```
Byte    Content         Description
[0]     ACK/NAK         0x00=ACK, 0x01=NAK
[1]     Status Code     OTA_ACK_xxx
[2]     Acknowledged Seq Sequence number being acknowledged
[3-7]   Reserved        0x00
```

| Status Code  | Value | Meaning                      |
|--------------|-------|------------------------------|
| OK           | 0x00  | Success                      |
| CRC_ERR      | 0x01  | CRC16 verification failed    |
| SEQ_ERR      | 0x02  | Sequence number mismatch     |
| FLASH_ERR    | 0x03  | Flash write error            |
| LEN_ERR      | 0x04  | Length error                 |
| VERIFY_OK    | 0x05  | CRC32 verification passed    |
| VERIFY_FAIL  | 0x06  | CRC32 verification failed    |
| BUSY         | 0x07  | Device busy                  |
| SESSION_END  | 0x08  | Session ended, will reset    |

---

## 3. State Machine Design

### 3.1 Bootloader State Flow

```
                    +--------+
                    |  IDLE  |<-----------+
                    +---+----+            |
                        |                 |
              OTA_START |                 | ABORT / timeout
                        v                 |
                    +--------+            |
                    | READY  |----------->+
                    +---+----+
                        |
                  DATA  |
                        v
                    +---------+
                    |RECEIVING|<---+
                    +----+----+    |
                         |        | DATA (continue receiving)
           VERIFY / all  |        |
             received    v        |
                    +---------+    |
                    |VERIFYING|----+
                    +----+----+
                         |
                   CRC32 |
                   match |
                         v
                    +--------+
                    |  DONE  |-----> Jump to APP
                    +--------+

  ABORT from any state --> IDLE
  Error count >= 10 --> ERROR --> IDLE
```

### 3.2 State Descriptions

| State     | Description                           | Accepted Commands          |
|-----------|---------------------------------------|---------------------------|
| IDLE      | Waiting for OTA start command         | START, QUERY              |
| READY     | START received, waiting for data      | DATA, ABORT, QUERY        |
| RECEIVING | Receiving data packets                | DATA, VERIFY, ABORT       |
| VERIFYING | VERIFY received, checking CRC32       | (auto-complete)           |
| DONE      | Upgrade complete, ready to jump       | (auto-jump)               |
| ERROR     | Error state, wait for retry or timeout| START, ABORT, QUERY       |

---

## 4. Upgrade Sequence Diagram

```
    Host                                  Device (MCU Bootloader)
    |                                        |
    |  ====== Power On / Reset ======        |
    |                                        |
    |                               [Check OTA Flag]
    |                               [Init CAN]
    |                               [Start 10s Timeout]
    |                                        |
    |--- QUERY (0x100, CMD=0x05) ----------->|
    |                                        |
    |<-- ACK (state=IDLE) -------------------|
    |                                        |
    |--- START (0x100, CMD=0x01, size=N) --->|
    |                                        |
    |                               [Validate size]
    |                               [Erase APP_A]
    |                                        |
    |<-- ACK (OK) ---------------------------|
    |                                        |
    |--- DATA[0] (0x101, seq=0, 4B) -------->|
    |                               [Verify CRC16]
    |                               [Check sequence]
    |                               [Write to Flash]
    |                               [Read-back verify]
    |<-- ACK (OK, seq=0) --------------------|
    |                                        |
    |--- DATA[1] (0x101, seq=1, 4B) -------->|
    |<-- ACK (OK, seq=1) --------------------|
    |                                        |
    |          ... (repeat for all packets)   |
    |                                        |
    |--- DATA[N] (0x101, seq=N, 4B) -------->|
    |<-- ACK (OK, seq=N) --------------------|
    |                                        |
    |--- VERIFY (0x100, CMD=0x03, CRC32) --->|
    |                               [Calculate APP CRC32]
    |                               [Compare CRC32]
    |                                        |
    |<-- ACK (VERIFY_OK) --------------------|
    |                               [Clear OTA Flag]
    |                               [Jump to APP]
    |                                        |
```

### 4.1 Error Retransmission Flow

```
    Host                                  Device
    |                                        |
    |--- DATA[5] (seq=5, CRC error) -------->|
    |                                        |
    |<-- NAK (CRC_ERR, seq=5) --------------|
    |                                        |
    |--- DATA[5] (seq=5, retransmit) ------->|
    |                                        |
    |<-- ACK (OK, seq=5) --------------------|
```

---

## 5. Error Handling and Rollback Strategy

### 5.1 Error Types and Handling

| Error Type           | Handling                                          |
|----------------------|---------------------------------------------------|
| CRC16 mismatch       | Return NAK, host retransmits frame                |
| Sequence mismatch    | Return NAK, host retransmits from correct seq     |
| Flash erase failure  | Return NAK, state → ERROR                         |
| Flash write failure  | Return NAK, state → ERROR                         |
| Read-back mismatch   | Return NAK, state → ERROR                         |
| CRC32 verify fail    | Return NAK(VERIFY_FAIL), upgrade failed           |
| Consecutive errors ≥10 | State → ERROR, wait for new START               |
| Timeout (10s)        | Jump to existing APP (if valid)                    |

### 5.2 Rollback Strategy (v1)

v1 uses a single APP partition. Rollback strategy:

1. **Before upgrade**: APP calls `ota_trigger_request()` to write flag and reset
2. **During upgrade**: If OTA fails (timeout or error), Bootloader checks if original APP is valid
   - Original APP valid → Jump to original APP (rollback successful)
   - Original APP invalid → Wait for new OTA transfer
3. **After upgrade**: If new APP has issues, send ABORT command via CAN, then re-upgrade

**Note**: v1 does not support A/B hot-swap. Full rollback requires enabling APP_B in a future version.

---

## 6. APP-Side OTA Trigger Flow

### 6.1 Trigger Sequence

```
1. Call ota_trigger_request()
2.   -> Erase OTA flag area
3.   -> Write OTA_FLAG_MAGIC_REQUEST (0x544F4152)
4.   -> Call NVIC_SystemReset() for system reset
5. After reset, Bootloader starts
6.   -> Detects OTA flag
7.   -> Enters OTA mode, waits for host firmware data
```

### 6.2 Code Example

```c
#include "ota_trigger.h"

/* In CAN command handler or button callback */
void on_ota_command_received(void)
{
    /* Optional: save state to backup registers */
    /* ... */

    /* Trigger OTA upgrade (does not return) */
    ota_trigger_request();
}
```

### 6.3 Notes

- `ota_trigger_request()` does not return; the system resets immediately
- Save necessary runtime state before calling (backup registers, EEPROM, etc.)
- Ensure CAN bus is connected and host is ready to send firmware
- OTA flag uses Flash storage, limited by erase/write cycles (~100K)

---

## 7. Bootloader Startup Flow

```
         Power On / Reset
                |
                v
        [System Clock Init]
                |
                v
        [Check OTA Flag]
          /            \
    OTA Request     No OTA Request
         |                |
         v                v
  [Clear OTA Flag]  [Check APP Validity]
  [Init CAN]          /          \
  [Enter OTA Mode]  APP Valid   APP Invalid
         |              |            |
         |              v            v
         |      [Jump to APP]  [Enter OTA Mode]
         |                         |
         v                         v
  [Wait for CAN Data]      [Wait for CAN Data]
  [10s Timeout Check]      [Wait Forever]
         |
   Success / Timeout / Failure
    /         |          \
 Success  Timeout+Valid  Failure
   |          |           |
   v          v           v
[Jump APP] [Jump APP] [Wait Retry]
```

---

## 8. CAN Baud Rate Configuration

Default 500Kbps. Parameters (APB1 = 180MHz):

| Parameter   | Value | Description                    |
|-------------|-------|--------------------------------|
| bittime_div | 18    | Time quantum prescaler         |
| BTS1        | 14    | Bit time segment 1             |
| BTS2        | 5     | Bit time segment 2             |
| SJW         | 3     | Synchronization jump width     |
| Total Tq    | 20    | 1 + BTS1 + BTS2 = 20 Tq       |
| Actual Rate | 500K  | 180MHz / (18 × 20) = 500Kbps  |

To change baud rate, modify `CAN_BAUDRATE_xxx` macros in `ota_config.h`.

---

## 9. Performance Estimate

- Payload per frame: 4 bytes
- 48KB firmware requires: 48×1024 / 4 = 12,288 frames
- CAN 500Kbps theoretical time: ~150 bits/frame → 12288 × 150 / 500000 ≈ 3.7 seconds
- Including ACK response and processing: approximately 8-15 seconds for complete upgrade
