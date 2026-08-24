#!/usr/bin/env python3
"""
UDS Protocol Constants - ISO 14229-1 (Unified Diagnostic Services)

This module contains standard UDS protocol definitions shared across all peripherals.
All constants follow ISO 14229-1 specification.

Standards Reference:
- ISO 14229-1:2020 - Road vehicles — Unified diagnostic services (UDS)
  https://www.iso.org/standard/72439.html
  https://en.wikipedia.org/wiki/Unified_Diagnostic_Services

Note: ISO-TP transport layer (ISO 15765-2) is implemented separately via can-isotp library.
"""

# ===== UDS Service Identifiers (SID) =====
SID_DIAGNOSTIC_SESSION_CONTROL = 0x10  # Switch diagnostic session
SID_ECU_RESET = 0x11                   # Reset ECU
SID_SECURITY_ACCESS = 0x27             # Unlock security-protected services
SID_COMMUNICATION_CONTROL = 0x28       # Control communication parameters
SID_TESTER_PRESENT = 0x3E              # Keep session alive
SID_ACCESS_TIMING_PARAMETER = 0x83     # Read/write timing parameters
SID_SECURED_DATA_TRANSMISSION = 0x84   # Secure data transmission
SID_CONTROL_DTC_SETTING = 0x85         # Enable/disable DTC setting
SID_RESPONSE_ON_EVENT = 0x86           # Configure response on event
SID_LINK_CONTROL = 0x87                # Control link behavior
SID_READ_DATA_BY_ID = 0x22             # Read data by identifier
SID_READ_MEMORY_BY_ADDRESS = 0x23      # Read memory by address
SID_READ_SCALING_DATA_BY_ID = 0x24     # Read scaling data
SID_READ_DATA_BY_PERIODIC_ID = 0x2A    # Read data periodically
SID_DYNAMICALLY_DEFINE_DATA_ID = 0x2C  # Define data identifier
SID_WRITE_DATA_BY_ID = 0x2E            # Write data by identifier
SID_WRITE_MEMORY_BY_ADDRESS = 0x3D     # Write memory by address
SID_CLEAR_DIAGNOSTIC_INFORMATION = 0x14 # Clear DTCs
SID_READ_DTC_INFORMATION = 0x19        # Read DTC information
SID_INPUT_OUTPUT_CONTROL = 0x2F       # Control I/O
SID_ROUTINE_CONTROL = 0x31             # Start/stop routines
SID_REQUEST_DOWNLOAD = 0x34            # Initiate data download (flash)
SID_REQUEST_UPLOAD = 0x35              # Initiate data upload (read)
SID_TRANSFER_DATA = 0x36               # Transfer data block
SID_REQUEST_TRANSFER_EXIT = 0x37       # Exit transfer mode
SID_REQUEST_FILE_TRANSFER = 0x38       # File transfer operations

# ===== Diagnostic Session Types =====
SESSION_DEFAULT = 0x01                 # Default session (normal operation)
SESSION_PROGRAMMING = 0x02             # Programming session (firmware update)
SESSION_EXTENDED = 0x03                # Extended diagnostic session
SESSION_SAFETY_SYSTEM = 0x04           # Safety system diagnostic session

# ===== ECU Reset Types =====
RESET_HARD = 0x01                      # Hard reset (power cycle)
RESET_KEY_OFF_ON = 0x02                # Key off/on reset
RESET_SOFT = 0x03                      # Soft reset (application restart)
RESET_ENABLE_RAPID_POWER_SHUTDOWN = 0x04
RESET_DISABLE_RAPID_POWER_SHUTDOWN = 0x05

# ===== Security Access Sub-functions =====
SECURITY_REQUEST_SEED = 0x01           # Request random seed (odd numbers: 0x01, 0x03, ...)
SECURITY_SEND_KEY = 0x02               # Send computed key (even numbers: 0x02, 0x04, ...)

# Security levels (examples - application specific)
SECURITY_LEVEL_1 = 0x01                # Level 1 seed request
SECURITY_LEVEL_1_KEY = 0x02            # Level 1 key response
SECURITY_LEVEL_2 = 0x03                # Level 2 seed request (higher privilege)
SECURITY_LEVEL_2_KEY = 0x04            # Level 2 key response

# ===== Routine Control Sub-functions =====
ROUTINE_START = 0x01                   # Start routine
ROUTINE_STOP = 0x02                    # Stop routine
ROUTINE_REQUEST_RESULTS = 0x03         # Request routine results

# ===== Response Behavior =====
SID_POSITIVE_RESPONSE_OFFSET = 0x40    # Positive response = SID + 0x40
SID_NEGATIVE_RESPONSE = 0x7F           # Negative response code

# ===== Negative Response Codes (NRC) =====
NRC_POSITIVE_RESPONSE = 0x00                    # Not an error - positive response
NRC_GENERAL_REJECT = 0x10                       # General reject
NRC_SERVICE_NOT_SUPPORTED = 0x11                # Service not supported
NRC_SUB_FUNCTION_NOT_SUPPORTED = 0x12           # Sub-function not supported
NRC_INCORRECT_MESSAGE_LENGTH = 0x13             # Incorrect message length or format
NRC_RESPONSE_TOO_LONG = 0x14                    # Response too long
NRC_BUSY_REPEAT_REQUEST = 0x21                  # Busy, repeat request
NRC_CONDITIONS_NOT_CORRECT = 0x22               # Conditions not correct
NRC_REQUEST_SEQUENCE_ERROR = 0x24               # Request sequence error
NRC_NO_RESPONSE_FROM_SUBNET_COMPONENT = 0x25    # No response from subnet
NRC_FAILURE_PREVENTS_EXECUTION = 0x26           # Failure prevents execution
NRC_REQUEST_OUT_OF_RANGE = 0x31                 # Request out of range
NRC_SECURITY_ACCESS_DENIED = 0x33               # Security access denied
NRC_INVALID_KEY = 0x35                          # Invalid key
NRC_EXCEED_NUMBER_OF_ATTEMPTS = 0x36            # Exceeded max number of attempts
NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37      # Time delay not expired
NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70         # Upload/download not accepted
NRC_TRANSFER_DATA_SUSPENDED = 0x71              # Transfer suspended
NRC_GENERAL_PROGRAMMING_FAILURE = 0x72          # General programming failure
NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73         # Wrong block sequence counter
NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING = 0x78  # Response pending
NRC_SUB_FUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7E # Sub-function not supported in active session
NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7F      # Service not supported in active session
NRC_RPM_TOO_HIGH = 0x81                         # Engine RPM too high
NRC_RPM_TOO_LOW = 0x82                          # Engine RPM too low
NRC_ENGINE_IS_RUNNING = 0x83                    # Engine is running
NRC_ENGINE_IS_NOT_RUNNING = 0x84                # Engine is not running
NRC_ENGINE_RUN_TIME_TOO_LOW = 0x85              # Engine run time too low
NRC_TEMPERATURE_TOO_HIGH = 0x86                 # Temperature too high
NRC_TEMPERATURE_TOO_LOW = 0x87                  # Temperature too low
NRC_VEHICLE_SPEED_TOO_HIGH = 0x88               # Vehicle speed too high
NRC_VEHICLE_SPEED_TOO_LOW = 0x89                # Vehicle speed too low
NRC_THROTTLE_PEDAL_TOO_HIGH = 0x8A              # Throttle/pedal too high
NRC_THROTTLE_PEDAL_TOO_LOW = 0x8B               # Throttle/pedal too low
NRC_TRANSMISSION_RANGE_NOT_IN_NEUTRAL = 0x8C    # Transmission not in neutral
NRC_TRANSMISSION_RANGE_NOT_IN_GEAR = 0x8D       # Transmission not in gear
NRC_BRAKE_SWITCH_NOT_CLOSED = 0x8F              # Brake switch not closed
NRC_SHIFTER_LEVER_NOT_IN_PARK = 0x90            # Shifter lever not in park
NRC_TORQUE_CONVERTER_CLUTCH_LOCKED = 0x91       # Torque converter clutch locked
NRC_VOLTAGE_TOO_HIGH = 0x92                     # Voltage too high
NRC_VOLTAGE_TOO_LOW = 0x93                      # Voltage too low

# ===== Standard Data Identifiers (DID) - ISO 14229-1 Annex B =====
# Vehicle Information (0xF1xx range)
DID_BOOT_SOFTWARE_ID = 0xF180                   # Boot software identification
DID_APPLICATION_SOFTWARE_ID = 0xF181            # Application software identification
DID_APPLICATION_DATA_ID = 0xF182                # Application data identification
DID_BOOT_SOFTWARE_FINGERPRINT = 0xF183          # Boot software fingerprint
DID_APPLICATION_SOFTWARE_FINGERPRINT = 0xF184   # Application software fingerprint
DID_APPLICATION_DATA_FINGERPRINT = 0xF185       # Application data fingerprint
DID_ACTIVE_DIAGNOSTIC_SESSION = 0xF186          # Active diagnostic session
DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER = 0xF187  # Spare part number
DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_NUMBER = 0xF188  # ECU software number
DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_VERSION = 0xF195  # ECU software version
DID_SYSTEM_SUPPLIER_ID = 0xF18A                 # System supplier identifier
DID_ECU_MANUFACTURING_DATE = 0xF18B             # ECU manufacturing date and time
DID_ECU_SERIAL_NUMBER = 0xF18C                  # ECU serial number
DID_SUPPORTED_FUNCTIONAL_UNITS = 0xF18D         # Supported functional units
DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER = 0xF18E  # Kit assembly part number
DID_VIN = 0xF190                                # Vehicle identification number (VIN)
DID_VEHICLE_MANUFACTURER_ECU_HARDWARE_NUMBER = 0xF191  # ECU hardware number
DID_SYSTEM_SUPPLIER_ECU_HARDWARE_NUMBER = 0xF192  # System supplier hardware number
DID_SYSTEM_SUPPLIER_ECU_HARDWARE_VERSION = 0xF193  # System supplier hardware version
DID_SYSTEM_SUPPLIER_ECU_SOFTWARE_NUMBER = 0xF194  # System supplier software number
DID_SYSTEM_SUPPLIER_ECU_SOFTWARE_VERSION = 0xF195  # System supplier software version
DID_EXHAUST_REGULATION_OR_TYPE_APPROVAL_NUMBER = 0xF196  # Exhaust regulation
DID_SYSTEM_NAME_OR_ENGINE_TYPE = 0xF197         # System name or engine type
DID_REPAIR_SHOP_CODE_OR_TESTER_SERIAL_NUMBER = 0xF198  # Repair shop code
DID_PROGRAMMING_DATE = 0xF199                   # Programming date
DID_CALIBRATION_REPAIR_SHOP_CODE_OR_CALIBRATION_EQUIPMENT_SERIAL_NUMBER = 0xF19A
DID_CALIBRATION_DATE = 0xF19B                   # Calibration date
DID_CALIBRATION_EQUIPMENT_SOFTWARE_NUMBER = 0xF19C  # Calibration equipment software
DID_ECU_INSTALLATION_DATE = 0xF19D              # ECU installation date
DID_ODX_FILE = 0xF19E                           # ODX file identifier
DID_ENTITY = 0xF19F                             # Entity

# Commonly used standard DIDs (aliases for convenience)
DID_SOFTWARE_VERSION = DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_VERSION  # 0xF195 (version number)
DID_HARDWARE_VERSION = DID_VEHICLE_MANUFACTURER_ECU_HARDWARE_NUMBER  # 0xF191
DID_SERIAL_NUMBER = DID_ECU_SERIAL_NUMBER       # 0xF18C

# ===== Custom DIDs (Manufacturer Specific, Range 0x2000-0x2FFF) =====
# Note: Device-specific custom DIDs should be defined in their respective modules
# Example: display/display_constants.py for display-specific DIDs

# ===== Address and Length Format Identifiers =====
# Used in RequestDownload/RequestUpload services
# Format: high nibble = size of memorySize parameter, low nibble = size of memoryAddress parameter
ADDR_LEN_FORMAT_44 = 0x44  # 4-byte address, 4-byte size (most common)
ADDR_LEN_FORMAT_24 = 0x24  # 2-byte address, 4-byte size
ADDR_LEN_FORMAT_34 = 0x34  # 3-byte address, 4-byte size
ADDR_LEN_FORMAT_42 = 0x42  # 4-byte address, 2-byte size

# ===== Data Format Identifiers =====
# Compression method (high nibble) and encryption method (low nibble)
DATA_FORMAT_UNCOMPRESSED_UNENCRYPTED = 0x00


def get_positive_response_sid(request_sid: int) -> int:
    """
    Calculate positive response SID for a given request SID.

    Args:
        request_sid: Request service identifier

    Returns:
        Positive response SID (request_sid + 0x40)
    """
    return request_sid + SID_POSITIVE_RESPONSE_OFFSET


def is_positive_response(response: bytes) -> bool:
    """
    Check if a response is a positive response.

    Args:
        response: UDS response bytes

    Returns:
        True if positive response, False otherwise
    """
    if len(response) < 1:
        return False
    return response[0] != SID_NEGATIVE_RESPONSE


def get_nrc_description(nrc: int) -> str:
    """
    Get human-readable description of Negative Response Code.

    Args:
        nrc: Negative Response Code

    Returns:
        Description string
    """
    nrc_descriptions = {
        0x00: "Positive Response",
        0x10: "General Reject",
        0x11: "Service Not Supported",
        0x12: "Sub-Function Not Supported",
        0x13: "Incorrect Message Length or Invalid Format",
        0x14: "Response Too Long",
        0x21: "Busy, Repeat Request",
        0x22: "Conditions Not Correct",
        0x24: "Request Sequence Error",
        0x25: "No Response From Subnet Component",
        0x26: "Failure Prevents Execution of Requested Action",
        0x31: "Request Out of Range",
        0x33: "Security Access Denied",
        0x35: "Invalid Key",
        0x36: "Exceeded Number of Attempts",
        0x37: "Required Time Delay Not Expired",
        0x70: "Upload/Download Not Accepted",
        0x71: "Transfer Data Suspended",
        0x72: "General Programming Failure",
        0x73: "Wrong Block Sequence Counter",
        0x78: "Request Correctly Received - Response Pending",
        0x7E: "Sub-Function Not Supported In Active Session",
        0x7F: "Service Not Supported In Active Session",
    }
    return nrc_descriptions.get(nrc, f"Unknown NRC: 0x{nrc:02X}")


if __name__ == "__main__":
    # Self-test
    print("UDS Constants Module - Self Test")
    print("=" * 60)
    print(f"Diagnostic Session Control: 0x{SID_DIAGNOSTIC_SESSION_CONTROL:02X}")
    print(f"Read Data By ID: 0x{SID_READ_DATA_BY_ID:02X}")
    print(f"Positive Response: 0x{get_positive_response_sid(SID_READ_DATA_BY_ID):02X}")
    print(f"Software Version DID: 0x{DID_SOFTWARE_VERSION:04X}")
    print(f"NRC Invalid Key: {get_nrc_description(NRC_INVALID_KEY)}")
    print("=" * 60)
