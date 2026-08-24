# Common CAN Protocol Specification

**Version:** 1.1  
**Date:** 2026-07-22  
**Status:** Draft for new peripheral integration  

## 1. Purpose

This document defines the common CAN protocol architecture for vehicle
peripheral modules controlled by the CCU.

All new peripherals SHALL use this architecture unless a project-specific
exception is approved. Peripheral-specific SRS documents SHALL reference this
document and define only the assigned node address, device-specific DIDs,
status payloads, routines, and acceptance criteria.

## 2. Scope

This specification covers:

- CAN physical and data link configuration
- 29-bit extended addressing for diagnostic communication
- UDS diagnostic services over ISO-TP
- ECDSA P-256 security access for protected operations
- Firmware download and reset flow
- Peripheral lifecycle broadcast messages
- Common DIDs and negative response handling
- Common supplier verification requirements

This specification does not define peripheral business logic such as display
page rendering, charger power regulation, sensor sampling, or actuator control.
Those requirements belong in each peripheral SRS.

## 3. Network Model

The CCU is the diagnostic master. Each peripheral is a diagnostic target with
one assigned source address.

| Node | Address | Role |
|------|---------|------|
| CCU | 0x03 | Diagnostic master |
| Peripheral | Assigned in peripheral SRS | Diagnostic target |

Multiple peripherals may share the same CAN bus. Each peripheral SHALL use a
unique source address and unique lifecycle status PGN assignment.

## 4. CAN Layer

| Parameter | Requirement |
|-----------|-------------|
| CAN standard | CAN 2.0B, Classical CAN |
| Bitrate | 250 kbps |
| Diagnostic ID format | 29-bit extended |
| Max DLC | 8 bytes |
| Byte order | Little-endian for multi-byte payload fields unless explicitly stated |
| Termination | Per vehicle harness design; end nodes require 120 ohm termination |

## 5. Diagnostic CAN Identifiers

UDS diagnostic messages SHALL use J1939-style PDU1 addressing with PF `0xDA`.

CAN ID layout (per ISO 15765-2 normal fixed addressing):

```text
0x18DA{TA}{SA}

Priority: 6
PF:       0xDA
TA:       Target address (N_TA — destination, occupies PS field in J1939 PDU1)
SA:       Source address (N_SA — sender)
```

For a peripheral with address `PA`, the diagnostic IDs are:

| Direction | CAN ID | Meaning |
|-----------|--------|---------|
| CCU -> Peripheral | `0x18DAPA03` | Diagnostic request (TA=`PA`, SA=`0x03`) |
| Peripheral -> CCU | `0x18DA03PA` | Diagnostic response (TA=`0x03`, SA=`PA`) |

The peripheral SHALL accept requests addressed to its own address only (N_TA matches its assigned address).

## 6. ISO-TP Transport

UDS payloads SHALL be carried over ISO-TP using 29-bit normal addressing.

| Parameter | Requirement |
|-----------|-------------|
| Single frame payload | 0 to 7 bytes |
| Multi-frame payload | 8 to 4095 bytes |
| Flow control | Receiver SHALL send FC after First Frame |
| Block Size | `0x00` preferred, unlimited consecutive frames |
| STmin | 1 ms default |
| N_As | 1000 ms |
| N_Ar | 1000 ms |
| N_Bs | 1000 ms |
| N_Cr | 1000 ms |

The peripheral SHALL reject malformed ISO-TP transfers and SHALL recover without
requiring a power cycle.

## 7. UDS Services

The following UDS services are common across peripherals.

| SID | Service | Required | Security |
|-----|---------|----------|----------|
| 0x10 | DiagnosticSessionControl | Yes | None |
| 0x11 | CCUReset | Yes | None |
| 0x22 | ReadDataByIdentifier | Yes | None |
| 0x27 | SecurityAccess | Yes | None |
| 0x2E | WriteDataByIdentifier | Yes | Level 1 for protected DIDs |
| 0x31 | RoutineControl | Yes | Level 1 for protected routines |
| 0x34 | RequestDownload | Yes if firmware update is supported | Level 1 |
| 0x36 | TransferData | Yes if firmware update is supported | Level 1 |
| 0x37 | RequestTransferExit | Yes if firmware update is supported | Level 1 |
| 0x3E | TesterPresent | Yes | None |

Peripheral SRS documents MAY define additional services only when needed.

### 7.1 Response Timing

| Parameter | Value |
|-----------|-------|
| P2 (server response time) | <= 50 ms |
| P2* (enhanced response time, after NRC `0x78`) | <= 5000 ms |

If the peripheral cannot respond within P2, it SHALL send NRC `0x78`
(requestCorrectlyReceivedResponsePending) to extend the timeout to P2*. This is
expected during operations such as ECDSA P-256 signature verification, flash erase,
or CRC computation.

### 7.2 suppressPositiveResponse

Bit 7 of the sub-function parameter in DiagnosticSessionControl (`0x10`),
CCUReset (`0x11`), and TesterPresent (`0x3E`) is the suppressPositiveResponse
bit. When set, the peripheral SHALL execute the request but SHALL NOT send a
positive response. Negative responses SHALL still be sent regardless of this bit.

### 7.3 Sessions

| Session | Value | Purpose |
|---------|-------|---------|
| Default | 0x01 | Normal operation and read-only diagnostics |
| Programming | 0x02 | Firmware update and flash operations |
| Extended | 0x03 | Protected configuration and extended diagnostics |

Non-default sessions SHALL timeout after 5 seconds without TesterPresent. On
timeout, the peripheral SHALL return to Default Session and clear the security
unlock state.

Session switching rules:

- Switching from one non-default session to another (e.g., Extended to
  Programming) SHALL clear the current security unlock state. The CCU must
  re-authenticate after the session change.
- Entering Default Session SHALL abort any in-progress firmware transfer and
  clear the security unlock state.
- The peripheral SHALL respond to DiagnosticSessionControl in any session.

### 7.4 SecurityAccess

SecurityAccess Level 1 SHALL use ECDSA P-256 challenge-response in production.

Flow:

1. CCU sends `[0x27, 0x01]`.
2. Peripheral responds `[0x67, 0x01, seed...]` with a 32-byte random seed.
3. CCU hashes the seed with SHA-256 and signs the digest using its private key.
4. CCU sends `[0x27, 0x02, signature...]` with a 64-byte ECDSA P-256 signature (raw R||S).
5. Peripheral verifies the signature using the provisioned public key.
6. Peripheral responds `[0x67, 0x02]` if verification succeeds.

Security requirements:

- The seed SHALL be unique per request.
- The production seed source SHALL be a hardware RNG or approved cryptographic RNG.
- The public key SHALL be stored in read-only or otherwise protected memory.
- Protected operations SHALL fail with NRC `0x33` when security is not unlocked.
- Invalid signatures SHALL fail with NRC `0x35`.
- Three consecutive failed attempts SHALL trigger lockout with NRC `0x36`
  (exceededNumberOfAttempts). During lockout, further SecurityAccess requests
  SHALL be rejected with NRC `0x37` (requiredTimeDelayNotExpired).
- The lockout delay SHALL be between 1 and 30 seconds (supplier-defined).
- The failed attempt counter MAY be cleared on successful session reset
  (DiagnosticSessionControl to Default). It is not required to persist across
  power cycles.

## 8. Common DIDs

Standard identification DIDs:

| DID | Name | Access | Format |
|-----|------|--------|--------|
| 0xF195 | Software Version | Read | UTF-8 string |
| 0xF18C | Serial Number | Read | UTF-8 string |
| 0xF18D | Bootloader Version | Read | UTF-8 string |
| 0xF191 | Hardware Version | Read | UTF-8 string |

Firmware management DIDs:

| DID | Name | Access | Format |
|-----|------|--------|--------|
| 0x2010 | Firmware Type | Read/Write | `uint8` |
| 0x2012 | Resource Package Version | Read | UTF-8 string, optional |

Default firmware type values:

| Value | Meaning |
|-------|---------|
| 0x01 | APP firmware |
| 0x02 | Resource package, if supported |
| 0x03 | Bootloader, if supported |

### 8.1 Version String Format

All version DIDs (`0xF195`, `0xF18D`, `0xF191`) SHALL use semantic
versioning format `MAJOR.MINOR.PATCH` encoded as UTF-8 (e.g., `1.2.3`). Version
strings SHALL be null-terminated and SHALL NOT exceed 16 bytes including the null
terminator.

### 8.2 DID 0x2010 Access Control

DID `0x2010` (Firmware Type) write access SHALL require Programming Session and
SecurityAccess Level 1. It is read-only in Default and Extended sessions.

### 8.3 Multi-DID Read

Peripherals SHALL support reading multiple DIDs in a single ReadDataByIdentifier
(`0x22`) request, up to the ISO-TP payload limit. If any requested DID is
unsupported, the peripheral SHALL return NRC `0x31` for the entire request.

### 8.4 Peripheral-Specific DIDs

Peripheral-specific DIDs SHALL be defined by each peripheral SRS. New
peripheral DIDs SHOULD use a dedicated range to avoid collisions.

## 9. Firmware Upgrade

### 9.1 UDS Download Flow

Peripherals that support firmware update SHALL implement the common UDS download
flow:

| Step | Action | UDS Service |
|------|--------|-------------|
| 1 | Enter Programming Session | `0x10 0x02` |
| 2 | Unlock SecurityAccess Level 1 | `0x27 0x01` / `0x27 0x02` |
| 3 | Select firmware type by writing DID `0x2010` | `0x2E` |
| 4 | Erase target memory region | `0x31 0x01 0xFF00` (RoutineControl) |
| 5 | Request download with address and size | `0x34` (RequestDownload) |
| 6 | Transfer firmware data in sequence-numbered blocks | `0x36` (TransferData) |
| 7 | End transfer and trigger validation | `0x37` (RequestTransferExit) |
| 8 | Peripheral validates CRC32 and ECDSA P-256 signature | Internal |
| 9 | Reset to activate new image | `0x11` (CCUReset) |

### 9.2 Transfer Requirements

- Block sequence counter SHALL start at `0x01`.
- Block sequence counter SHALL increment by 1 for each successive block,
  wrapping `0xFF` → `0x00` → `0x01` (per ISO 14229-1). `0x00` is only
  reached through wrap-around and SHALL NOT be used as the first block.
- The maximum TransferData block size SHALL be negotiated via the
  RequestDownload response (`lengthFormatIdentifier` and
  `maxNumberOfBlockLength`). Suppliers SHALL document the supported maximum
  block size (recommended minimum: 256 bytes).
- The peripheral SHALL require SecurityAccess Level 1 for RequestDownload,
  Erase routine, and Firmware Type DID write. Requests without prior unlock
  SHALL be rejected with NRC `0x33`.
- If flash erase or signature verification exceeds P2, the peripheral SHALL
  send NRC `0x78` (ResponsePending) to keep the session alive.

### 9.3 Validation Requirements

- The peripheral SHALL validate CRC32 before marking a firmware image valid.
- The peripheral SHALL validate the ECDSA P-256 firmware signature before booting a
  downloaded image.
- The peripheral SHALL reject an image with invalid length, invalid CRC, invalid
  signature, unsupported format, or unsupported firmware type.
- The peripheral SHALL preserve the previous valid image or enter a safe failure
  mode if validation fails.

### 9.4 Dual-Slot Firmware Layout (Optional)

Peripherals SHOULD implement a dual-slot (A/B) firmware layout to prevent
bricking during OTA. When dual-slot is implemented:

- The active application slot SHALL NOT be erased or overwritten during
  download. New firmware SHALL be written to the inactive slot only.
- Each slot SHALL contain a complete standalone executable image and image
  metadata (at minimum: image length, firmware version, CRC32, signature,
  slot validity state).
- Boot metadata updates (slot activation, trial boot confirmation, rollback)
  SHALL be atomic or power-loss tolerant. Power loss at any point SHALL result
  in a deterministic valid state, never a corrupted intermediate.
- The supplier SHALL provide the final flash memory map, slot sizes, maximum
  image size, and metadata layout with the release package.

Peripherals that do not implement dual-slot SHALL document their safe recovery
mechanism (e.g., failsafe bootloader with re-download capability).

### 9.5 Trial Boot and Rollback (Optional, requires Dual-Slot)

When dual-slot is implemented, peripherals SHOULD support trial boot with
automatic rollback:

- After CCUReset, the bootloader SHALL boot the pending image in trial mode.
- The application SHALL confirm the new image only after completing core
  initialization (CAN communication, safety subsystem, NVM access).
- If the new image fails to confirm within a supplier-defined timeout (default
  10 s), the bootloader SHALL roll back to the previous valid slot.
- If the new image resets repeatedly before confirmation, the bootloader SHALL
  roll back after a supplier-defined retry limit (default 3 attempts).
- The previous valid slot SHALL remain bootable until the new image is
  confirmed.
- After rollback, the peripheral SHALL report the rollback event through
  device-specific DIDs defined in the peripheral SRS.

### 9.6 Power-Loss and Failure Recovery

The following recovery behavior SHALL apply regardless of whether dual-slot is
implemented:

| Failure Point | Required Recovery |
|---------------|-------------------|
| Power loss before RequestDownload | Boot previous valid image |
| Power loss during TransferData | Boot previous valid image; download target invalid |
| Power loss during validation | Boot previous valid image |
| Invalid CRC or signature | Reject image, preserve previous valid image |

When dual-slot is implemented, the following additional requirements apply:

| Failure Point | Required Recovery |
|---------------|-------------------|
| Power loss after pending activation written but before reset | Bootloader activates valid pending slot or keeps previous active slot based on atomic metadata |
| Power loss during trial boot | Retry trial boot until retry limit, then roll back |
| New image watchdog reset before confirmation | Retry then roll back per retry limit |

Any OTA failure before image confirmation SHALL preserve the previous confirmed
image. The peripheral SHALL NOT enter an unbootable state after interruption at
any OTA step.

Peripheral SRS documents SHALL define maximum firmware size, device-specific
OTA DIDs, and additional acceptance criteria.

## 10. Lifecycle Status Broadcast

Each peripheral SHALL publish lifecycle status using a J1939-style PDU2
broadcast message.

CAN ID layout:

```text
0x18FF{GE}{SA}

Priority: 6
PF:       0xFF
GE:       Peripheral status group extension assigned in the SRS
SA:       Peripheral source address
```

Byte 0 SHALL contain the common lifecycle state:

| Value | State | Meaning |
|-------|-------|---------|
| 0x01 | BOOTUP | Peripheral has powered on or reset |
| 0x02 | INITIALIZING | Peripheral is initializing hardware/software |
| 0x03 | OPERATIONAL | Peripheral is ready for normal operation |
| 0x04 | DEGRADED | Peripheral is operating with reduced capability |
| 0x05 | FAULT | Peripheral has a critical fault |
| 0x06 | SHUTDOWN | Peripheral is preparing for power-off |

Bytes 1-7 SHALL be zero unless a peripheral SRS defines an extended status
payload.

Broadcast requirements:

- BOOTUP SHALL be sent within 100 ms of power-on or reset.
- OPERATIONAL SHALL be sent when initialization completes.
- DEGRADED or FAULT SHALL be sent on state change within 100 ms.
- SHUTDOWN SHOULD be sent before graceful power-off when possible.
- While in OPERATIONAL or DEGRADED, the peripheral SHOULD send periodic status
  broadcasts at a default rate of 1 Hz (1000 ms). The peripheral SRS MAY
  define a configurable period or disable periodic broadcast.

## 11. Negative Response Codes

| NRC | Name | Common use |
|-----|------|------------|
| 0x11 | ServiceNotSupported | Unsupported SID |
| 0x12 | SubFunctionNotSupported | Unsupported sub-function |
| 0x13 | IncorrectMessageLengthOrInvalidFormat | Invalid request length or format |
| 0x22 | ConditionsNotCorrect | Wrong session, state, or precondition |
| 0x24 | RequestSequenceError | Operation order is invalid |
| 0x31 | RequestOutOfRange | DID, routine, or parameter not supported |
| 0x33 | SecurityAccessDenied | Security level not unlocked |
| 0x35 | InvalidKey | Signature/key verification failed |
| 0x36 | ExceedNumberOfAttempts | Authentication attempts exceeded |
| 0x37 | RequiredTimeDelayNotExpired | Retry delay not expired |
| 0x70 | UploadDownloadNotAccepted | Download precondition failed |
| 0x71 | TransferDataSuspended | Transfer could not continue |
| 0x72 | GeneralProgrammingFailure | Flash, CRC, or signature validation failure |
| 0x73 | WrongBlockSequenceCounter | TransferData sequence mismatch |
| 0x78 | RequestCorrectlyReceivedResponsePending | Server needs more time; resets client timeout to P2* |

## 12. CAN Bus-Off Recovery

If the CAN controller enters the bus-off state (transmit error counter >= 256
per ISO 11898-1), the peripheral SHALL attempt automatic recovery after
detecting 128 occurrences of 11 consecutive recessive bits. The peripheral SHALL
NOT require a power cycle to recover from bus-off.

After bus-off recovery, the peripheral SHALL:

- Send a BOOTUP or OPERATIONAL lifecycle broadcast to signal its return.
- Resume normal UDS server operation.
- Not clear any active fault state that existed before bus-off.

The supplier SHALL document the bus-off recovery timing with the release package.

## 13. Common Supplier Verification

Each peripheral supplier SHALL provide evidence for:

- CAN bitrate and ID filtering correctness.
- UDS service positive and negative responses.
- ISO-TP single-frame and multi-frame behavior.
- Session timeout and TesterPresent behavior.
- SecurityAccess valid and invalid signature behavior.
- Protected DID and routine access control.
- Firmware download, validation, and reset behavior.
- Lifecycle broadcast timing and state transitions.
- CAN bus-off recovery without power cycle.
- 24-hour CAN communication stability.

Peripheral SRS documents SHALL add device-specific verification items.

## 14. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-05-11 | Lime FW Team | Initial common CAN protocol specification. |
| 1.1 | 2026-07-22 | Lime FW Team | SecurityAccess (0x27) and firmware secure-boot signature migrated from Ed25519 to ECDSA P-256 (FIPS 186-4 / NIST secp256r1): seed hashed with SHA-256 before signing, 64-byte raw R‖S signature (IEEE P1363, not DER), 65-byte uncompressed SEC1 public key. TransferData aligned with ISO 14229-1 (usable payload = maxNumberOfBlockLength − 2; block sequence counter wraps 0xFF → 0x00 → 0x01). |
