#!/usr/bin/env python3
"""
Qi Wireless Charger-Specific UDS Constants

This module contains Qi charger-specific Data Identifiers (DIDs), RoutineControl
IDs, and value tables that extend the standard UDS protocol for the Qi wireless
charger peripheral.

All values are taken from the Qi charger SRS (LIME-QI-PERIPH-SRS-001) and the
common CAN protocol specification (REF-1, common_can_protocol_spec.md).

Custom DIDs use the manufacturer-specific range (0x2000-0x2FFF) as per
ISO 14229-1. Qi charger-specific DIDs use the sub-range 0x2100-0x21FF.
"""

# ===== Diagnostic CAN Identifiers (SRS §2.3, §4) =====
# Extended 29-bit J1939-style addressing: 0x18DA{TA}{SA}
#   CCU source address (SA) = 0x03
#   Qi charger address (TA) = 0x0D
UDS_REQ_ID = 0x18DA0D03    # CCU (0x03) -> Qi charger (0x0D)
UDS_RESP_ID = 0x18DA030D   # Qi charger (0x0D) -> CCU (0x03)

# Lifecycle / status broadcast (J1939 PDU2): 0x18FF{GE}{SA}, GE = 0x26
BROADCAST_ID = 0x18FF260D  # Qi charger (0x0D) -> all nodes

# Node addresses
ADDR_CCU = 0x03
ADDR_QI_CHARGER = 0x0D

# ===== Common Firmware Management DIDs (REF-1 §8) =====
DID_FIRMWARE_TYPE = 0x2010                      # Firmware type selection (write in Prog. session + SA L1)

# ===== Firmware Type Values (SRS §12.1) =====
# NOTE: These differ from the display peripheral. On the Qi charger, APP is 0x01.
FIRMWARE_TYPE_APP = 0x01                        # Application firmware (P0, required)
FIRMWARE_TYPE_RESOURCE = 0x02                   # Resource package - NOT applicable, rejected NRC 0x31
FIRMWARE_TYPE_BOOTLOADER = 0x03                 # Bootloader update (P1, optional)

# ===== Qi Charger-Specific DIDs (SRS §7.2, range 0x2100-0x21FF) =====
DID_CHARGER_CAPABILITY = 0x2100                 # Charger capability bitfield (4 bytes, read-only)
DID_CHARGER_ENABLE = 0x2101                     # Charger enable (1 byte, Extended + SA L1, volatile)
DID_CHARGING_STATE = 0x2102                     # Charging state (1 byte, read-only)
DID_DEVICE_PRESENT = 0x2103                     # Device present (1 byte, read-only)
DID_OUTPUT_POWER = 0x2104                       # Output power (uint16, 0.1 W, read-only)
DID_INPUT_VOLTAGE = 0x2105                      # Input voltage (uint16, 0.01 V, read-only)
DID_INPUT_CURRENT = 0x2106                      # Input current (uint16, mA, read-only)
DID_COIL_TEMPERATURE = 0x2107                   # Coil temperature (uint8, degC offset -50)
DID_PCB_TEMPERATURE = 0x2108                    # PCB temperature (uint8, degC offset -50)
DID_FOD_STATUS = 0x2109                         # Foreign object detection status (1 byte)
DID_ALIGNMENT_STATUS = 0x210A                   # Alignment status (1 byte)
DID_FAULT_CODE = 0x210B                         # Active fault code (1 byte, read-only)
DID_THERMAL_DERATING_LEVEL = 0x210C             # Thermal derating level (0-100%, read-only)
DID_POWER_LIMIT = 0x210D                        # Power limit (uint16, 0.1 W, Extended + SA L1)
DID_HEARTBEAT_PERIOD = 0x210E                   # Heartbeat/broadcast period (uint16, ms, Extended + SA L1)
DID_OPERATING_MODE = 0x210F                     # Operating mode (1 byte, Extended + SA L1, persistent)
DID_LAST_FAULT_DETAIL = 0x2110                  # Last fault detail (4 bytes, read-only)
DID_ENERGY_DELIVERED = 0x2111                   # Energy delivered (uint32, 0.1 Wh, read-only)
DID_OTA_STATUS = 0x2112                         # OTA status (1 byte, read-only)
DID_ACTIVE_FIRMWARE_SLOT = 0x2113               # Active firmware slot (1 byte, read-only)
DID_PENDING_FIRMWARE_SLOT = 0x2114              # Pending firmware slot (1 byte, read-only)
DID_LAST_BOOT_REASON = 0x2115                   # Last boot reason (1 byte, read-only)
DID_ROLLBACK_COUNTER = 0x2116                   # OTA rollback counter (1 byte, read-only)
DID_IDLE_TIMEOUT = 0x2117                       # LOW_POWER idle timeout (uint16, min, Extended + SA L1)
DID_CLAMP_STATE = 0x2118                        # Clamp state (1 byte, read-only)

# ===== RoutineControl IDs (SRS §10) =====
ROUTINE_ERASE_MEMORY = 0xFF00                   # Common firmware upgrade erase routine (SA L1)
ROUTINE_CLEAR_FAULTS = 0x2100                   # Clear latched charger faults (SA L1)
ROUTINE_SELF_TEST = 0x2101                      # Run charger self-test (SA L1)
ROUTINE_FOD_CALIBRATION_CHECK = 0x2102          # Validate FOD calibration data (SA L1)

# ===== Charging State Values (SRS §6.1) - DID 0x2102 =====
CHARGING_STATE_DISABLED = 0x00
CHARGING_STATE_STANDBY = 0x01
CHARGING_STATE_DEVICE_DETECTED = 0x02
CHARGING_STATE_NEGOTIATING = 0x03
CHARGING_STATE_CHARGING = 0x04
CHARGING_STATE_CHARGE_COMPLETE = 0x05
CHARGING_STATE_SUSPENDED_THERMAL = 0x06
CHARGING_STATE_SUSPENDED_FOD = 0x07
CHARGING_STATE_FAULT = 0x08
CHARGING_STATE_SERVICE_MODE = 0x09
CHARGING_STATE_LOW_POWER = 0x0A

CHARGING_STATE_NAMES = {
    0x00: "DISABLED",
    0x01: "STANDBY",
    0x02: "DEVICE_DETECTED",
    0x03: "NEGOTIATING",
    0x04: "CHARGING",
    0x05: "CHARGE_COMPLETE",
    0x06: "SUSPENDED_THERMAL",
    0x07: "SUSPENDED_FOD",
    0x08: "FAULT",
    0x09: "SERVICE_MODE",
    0x0A: "LOW_POWER",
}

# ===== Operating Mode Values (SRS §7.5) - DID 0x210F =====
OPERATING_MODE_NORMAL = 0x00
OPERATING_MODE_SERVICE = 0x01
OPERATING_MODE_MANUFACTURING = 0x02
OPERATING_MODE_SHIPPING = 0x03

# ===== OTA Status Values (SRS §7.6) - DID 0x2112 =====
OTA_STATUS_IDLE = 0x00                          # No OTA operation active
OTA_STATUS_DOWNLOADING = 0x01                   # Downloading to inactive slot
OTA_STATUS_VALIDATING = 0x02                    # Validating downloaded image
OTA_STATUS_PENDING_ACTIVATION = 0x03            # Pending activation after validation
OTA_STATUS_TRIAL_BOOT = 0x04                    # Trial boot of newly activated image
OTA_STATUS_CONFIRMED = 0x05                     # New image confirmed
OTA_STATUS_ROLLED_BACK = 0x06                   # Rolled back to previous image
OTA_STATUS_FAILED = 0x07                        # OTA failed; previous image preserved

OTA_STATUS_NAMES = {
    0x00: "IDLE",
    0x01: "DOWNLOADING",
    0x02: "VALIDATING",
    0x03: "PENDING_ACTIVATION",
    0x04: "TRIAL_BOOT",
    0x05: "CONFIRMED",
    0x06: "ROLLED_BACK",
    0x07: "FAILED",
}

# ===== Firmware Slot Values (SRS §7.7) - DIDs 0x2113 / 0x2114 =====
FW_SLOT_A = 0x00
FW_SLOT_B = 0x01
FW_SLOT_NONE_PENDING = 0xFE
FW_SLOT_INVALID = 0xFF

FW_SLOT_NAMES = {
    0x00: "Slot A",
    0x01: "Slot B",
    0xFE: "No pending slot",
    0xFF: "Invalid/unknown slot",
}

# ===== Boot Reason Values (SRS §7.8) - DID 0x2115 =====
BOOT_REASON_POWER_ON = 0x00
BOOT_REASON_UDS_RESET = 0x01
BOOT_REASON_WATCHDOG = 0x02
BOOT_REASON_OTA_ACTIVATION = 0x03
BOOT_REASON_OTA_ROLLBACK = 0x04
BOOT_REASON_BROWNOUT = 0x05
BOOT_REASON_UNKNOWN = 0xFF

BOOT_REASON_NAMES = {
    0x00: "Power-on reset",
    0x01: "UDS CCUReset",
    0x02: "Watchdog reset",
    0x03: "OTA activation reset",
    0x04: "OTA rollback reset",
    0x05: "Brownout/supply reset",
    0xFF: "Unknown reset reason",
}

# ===== Fault Code Values (SRS §8.1) - DID 0x210B =====
FAULT_NONE = 0x00
FAULT_FIRMWARE_VALIDATION = 0x01
FAULT_CALIBRATION_INVALID = 0x02
FAULT_HARDWARE = 0x03
FAULT_INPUT_OVERVOLTAGE = 0x04
FAULT_INPUT_UNDERVOLTAGE = 0x05
FAULT_FOREIGN_OBJECT = 0x06
FAULT_COIL_OVERTEMP = 0x07
FAULT_PCB_OVERTEMP = 0x08
FAULT_NEGOTIATION_FAILURE = 0x09
FAULT_INPUT_OVERCURRENT = 0x0A
FAULT_QI_RX_TIMEOUT = 0x0B
FAULT_UNKNOWN = 0xFF

# ===== Common Lifecycle States (REF-1 §10, broadcast byte 0) =====
LIFECYCLE_BOOTUP = 0x01
LIFECYCLE_INITIALIZING = 0x02
LIFECYCLE_OPERATIONAL = 0x03
LIFECYCLE_DEGRADED = 0x04
LIFECYCLE_FAULT = 0x05
LIFECYCLE_SHUTDOWN = 0x06


if __name__ == "__main__":
    # Self-test
    print("Qi Charger Constants Module - Self Test")
    print("=" * 60)
    print(f"UDS Request ID:   0x{UDS_REQ_ID:08X} (CCU 0x03 -> Qi 0x0D)")
    print(f"UDS Response ID:  0x{UDS_RESP_ID:08X} (Qi 0x0D -> CCU 0x03)")
    print(f"Broadcast ID:     0x{BROADCAST_ID:08X}")
    print(f"Firmware Type DID: 0x{DID_FIRMWARE_TYPE:04X}")
    print(f"Firmware Type APP: 0x{FIRMWARE_TYPE_APP:02X}")
    print(f"Firmware Type Bootloader: 0x{FIRMWARE_TYPE_BOOTLOADER:02X}")
    print(f"Erase Memory Routine: 0x{ROUTINE_ERASE_MEMORY:04X}")
    print(f"OTA Status DID: 0x{DID_OTA_STATUS:04X}")
    print(f"Active Slot DID: 0x{DID_ACTIVE_FIRMWARE_SLOT:04X}")
    print("=" * 60)
