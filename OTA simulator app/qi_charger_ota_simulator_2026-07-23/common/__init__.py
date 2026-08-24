"""
Common utilities and constants shared across all peripheral simulators.

This package contains:
- uds_constants: ISO 14229-1 UDS protocol constants
- Future: can_utils, isotp_utils, crypto_utils, etc.
"""

from .uds_constants import *

__all__ = [
    # Re-export all UDS constants
    'SID_DIAGNOSTIC_SESSION_CONTROL',
    'SID_ECU_RESET',
    'SID_READ_DATA_BY_ID',
    'SID_SECURITY_ACCESS',
    'SID_WRITE_DATA_BY_ID',
    'SID_ROUTINE_CONTROL',
    'SID_REQUEST_DOWNLOAD',
    'SID_TRANSFER_DATA',
    'SID_REQUEST_TRANSFER_EXIT',
    'SID_TESTER_PRESENT',
    # ... (all constants are re-exported via *)
]
