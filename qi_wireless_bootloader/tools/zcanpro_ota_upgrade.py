#!/usr/bin/env python3
"""
ZCANPRO OTA Upgrade Script for Qi Wireless Charger Bootloader

Performs a complete CAN-UDS OTA firmware upgrade against the Qi wireless
charger bootloader. Supports two CAN backends:
  1. python-can (socketcan, canalystii, pcan, virtual, etc.)
  2. ZCANPRO API (ZLG ZCANPRO, if the `zcanpro` module is available)

ISO-TP is implemented manually (no can-isotp dependency) so the script
works in ZCANPRO environments where the isotp library may not be installed.

Usage examples:

  # With python-can (virtual bus for testing):
  python3 zcanpro_ota_upgrade.py \\
      --bustype virtual --channel vcan0 \\
      --firmware app_slot_a.ota.bin \\
      --keypair /path/to/private.pem

  # With python-can (socketcan):
  python3 zcanpro_ota_upgrade.py \\
      --bustype socketcan --channel can0 \\
      --firmware app_slot_a.ota.bin \\
      --keypair /path/to/private.pem

  # With python-can (canalystii):
  python3 zcanpro_ota_upgrade.py \\
      --bustype canalystii --channel 0 --device 0 \\
      --firmware app_slot_a.ota.bin \\
      --keypair /path/to/private.pem

  # With ZCANPRO API:
  python3 zcanpro_ota_upgrade.py \\
      --bustype zcanpro --channel 0 \\
      --firmware app_slot_a.ota.bin \\
      --keypair /path/to/private.pem

  # Verbose debug output:
  python3 zcanpro_ota_upgrade.py \\
      --bustype virtual --channel vcan0 \\
      --firmware app.ota.bin --keypair private.pem -v

Requirements:
  - cryptography (for ECDSA P-256): pip3 install cryptography
  - python-can (if using --bustype socketcan/canalystii/pcan/virtual):
      pip3 install python-can
  - zcanpro module (if using --bustype zcanpro): install ZLG ZCANPRO SDK

Firmware format:
  The firmware file must be a pack_image.py output containing a 256-byte
  XATO header followed by the raw firmware binary. See tools/pack_image.py.
"""

from __future__ import annotations

import argparse
import logging
import struct
import sys
import threading
import time
import zlib
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# UDS / ISO-TP Constants
# ---------------------------------------------------------------------------

# CAN IDs (29-bit extended frames)
UDS_REQ_ID = 0x18DA0D03  # ZCANPRO -> MCU
UDS_RESP_ID = 0x18DA030D  # MCU -> ZCANPRO

# ISO-TP PCI types
PCI_SF = 0x00  # Single Frame
PCI_FF = 0x10  # First Frame
PCI_CF = 0x20  # Consecutive Frame
PCI_FC = 0x30  # Flow Control

# ISO-TP parameters (matching MCU configuration)
ISOTP_STMIN_MS = 1    # Minimum separation time between CFs (ms)
ISOTP_BS = 0          # Block size (0 = unlimited, no FC from receiver)
ISOTP_FC_TIMEOUT_MS = 1000  # Flow Control timeout
ISOTP_CF_TIMEOUT_MS = 1000  # Consecutive Frame timeout

# UDS Service IDs
SID_DIAGNOSTIC_SESSION_CONTROL = 0x10
SID_ECU_RESET = 0x11
SID_SECURITY_ACCESS = 0x27
SID_TESTER_PRESENT = 0x3E
SID_READ_DATA_BY_ID = 0x22
SID_WRITE_DATA_BY_ID = 0x2E
SID_ROUTINE_CONTROL = 0x31
SID_REQUEST_DOWNLOAD = 0x34
SID_TRANSFER_DATA = 0x36
SID_REQUEST_TRANSFER_EXIT = 0x37
SID_NEGATIVE_RESPONSE = 0x7F
SID_POSITIVE_RESPONSE_OFFSET = 0x40

# Session types
SESSION_DEFAULT = 0x01
SESSION_PROGRAMMING = 0x02

# Security Access sub-functions
SECURITY_REQUEST_SEED = 0x01
SECURITY_SEND_KEY = 0x02

# ECU Reset sub-types
RESET_HARD = 0x01

# Routine Control sub-functions
ROUTINE_START = 0x01

# Routine IDs
ROUTINE_ERASE_MEMORY = 0xFF00

# Data format / address-length format identifiers
DATA_FORMAT_UNCOMPRESSED = 0x00
ADDR_LEN_FORMAT_44 = 0x44

# DIDs
DID_FIRMWARE_TYPE = 0x2010
DID_SOFTWARE_VERSION = 0xF195

# Firmware type values
FIRMWARE_TYPE_APP = 0x01

# Image header constants
IMAGE_MAGIC = 0x4F544158  # "XATO" in little-endian
IMAGE_HEADER_SIZE = 256

# NRC codes
NRC_RESPONSE_PENDING = 0x78

# S3 timeout (seconds) - send TesterPresent before this expires
S3_TIMEOUT_S = 4.0  # MCU S3=5s, use 4s for safety margin

# Max UDS payload (per ISO 14229 / MCU config)
MAX_UDS_PAYLOAD = 4095


# ---------------------------------------------------------------------------
# NRC Description
# ---------------------------------------------------------------------------

_NRC_DESC = {
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


def _nrc_desc(nrc: int) -> str:
    return _NRC_DESC.get(nrc, f"Unknown NRC: 0x{nrc:02X}")


# ---------------------------------------------------------------------------
# ECDSA P-256 Helper (inline, no external dependency beyond `cryptography`)
# ---------------------------------------------------------------------------

def _ecdsa_sign(private_key, message: bytes) -> bytes:
    """Sign *message* with SHA-256 ECDSA, returning raw 64-byte R||S."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils

    der = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def _load_private_key(path: Path):
    """Load an ECDSA P-256 private key from a PEM or raw binary file."""
    data = path.read_bytes()
    stripped = data.lstrip()
    if stripped.startswith(b"-----BEGIN"):
        from cryptography.hazmat.primitives.serialization import load_pem_private_key
        return load_pem_private_key(data, password=None)
    if len(data) in (32, 97):
        from cryptography.hazmat.primitives.asymmetric import ec
        scalar = int.from_bytes(data[:32], "big")
        return ec.derive_private_key(scalar, ec.SECP256R1())
    raise ValueError(
        f"Unsupported key format ({len(data)} bytes). "
        "Expected PEM text, 32-byte raw scalar, or 97-byte keypair blob."
    )


# ---------------------------------------------------------------------------
# CAN Backend Abstraction
# ---------------------------------------------------------------------------

class CanBackend:
    """Abstract CAN bus interface. Subclasses wrap python-can or ZCANPRO."""

    def send(self, arb_id: int, data: bytes, is_extended: bool = True) -> None:
        raise NotImplementedError

    def recv(self, timeout: float = 0.1) -> Optional[tuple[int, bytes]]:
        """Return (arb_id, data) or None on timeout."""
        raise NotImplementedError

    def shutdown(self) -> None:
        pass


class PythonCanBackend(CanBackend):
    """Backend using the python-can library."""

    def __init__(self, bustype: str, channel: str, bitrate: int = 250000,
                 device: int = 0):
        import can as _can

        kwargs: dict = {
            "interface": bustype,
            "channel": channel,
            "bitrate": bitrate,
        }
        # canalystii uses 'device' for the adapter index
        if bustype == "canalystii":
            kwargs["device"] = device

        self._bus = _can.Bus(**kwargs)
        logging.info(
            "python-can connected: interface=%s channel=%s bitrate=%d",
            bustype, channel, bitrate,
        )

    def send(self, arb_id: int, data: bytes, is_extended: bool = True) -> None:
        import can as _can
        msg = _can.Message(
            arbitration_id=arb_id,
            is_extended_id=is_extended,
            data=data,
        )
        self._bus.send(msg)

    def recv(self, timeout: float = 0.1) -> Optional[tuple[int, bytes]]:
        import can as _can
        msg = self._bus.recv(timeout=timeout)
        if msg is None:
            return None
        return msg.arbitration_id, bytes(msg.data)

    def shutdown(self) -> None:
        self._bus.shutdown()


class ZcanproBackend(CanBackend):
    """Backend using the ZLG ZCANPRO Python SDK."""

    # ZCANPRO device types
    _ZCAN_USBCAN_2E_U = 21
    # Frame flags
    _ZCAN_FRAME_FLAG = 0x00  # Standard data frame
    _ZCAN_EFF_FLAG = 0x80    # Extended frame flag

    def __init__(self, channel: int = 0, device: int = 0,
                 bitrate: int = 250000):
        try:
            import zcanpro as _zcan
        except ImportError:
            raise ImportError(
                "The 'zcanpro' module is not installed. "
                "Install the ZLG ZCANPRO SDK or use --bustype socketcan/canalystii."
            )
        self._zcan = _zcan
        self._channel = channel
        self._dev_handle = None
        self._ch_handle = None

        # Open device
        self._dev_handle = _zcan.ZCAN_OpenDevice(self._ZCAN_USBCAN_2E_U, device, 0)
        if self._dev_handle is None or self._dev_handle == 0:
            raise RuntimeError(f"ZCAN_OpenDevice failed (device={device})")
        logging.info("ZCANPRO device opened (handle=%s)", self._dev_handle)

        # Init CAN channel
        init_config = _zcan.ZCAN_CHANNEL_INIT_CONFIG()
        init_config.can_type = 1  # CANFD = 0, CAN = 1
        init_config.config.acc_code = 0
        init_config.config.acc_mask = 0xFFFFFFFF
        init_config.config.mode = 0  # normal mode

        # Timing for 250 kbps (CAN 2.0)
        # These values work for most ZLG adapters; adjust if needed.
        init_config.config.timing0 = 0x03  # SJW + BRP
        init_config.config.timing1 = 0x1C  # TSEG1 + TSEG2

        self._ch_handle = _zcan.ZCAN_InitCAN(self._dev_handle, channel, init_config)
        if self._ch_handle is None or self._ch_handle == 0:
            raise RuntimeError(f"ZCAN_InitCAN failed (channel={channel})")

        if _zcan.ZCAN_StartCAN(self._ch_handle) != 0:
            raise RuntimeError("ZCAN_StartCAN failed")
        logging.info("ZCANPRO channel %d started at %d bps", channel, bitrate)

    def send(self, arb_id: int, data: bytes, is_extended: bool = True) -> None:
        zcan = self._zcan
        msg = zcan.ZCAN_Transmit_Data()
        msg.transmit_type = 0  # normal
        msg.frame.can_id = arb_id
        if is_extended:
            msg.frame.can_id |= 0x80000000  # EFF flag in CAN ID
        msg.frame.can_dlc = len(data)
        msg.frame.data = list(data[:8])

        ret = zcan.ZCAN_Transmit(self._ch_handle, msg, 1)
        if ret != 1:
            raise RuntimeError(f"ZCAN_Transmit returned {ret}")

    def recv(self, timeout: float = 0.1) -> Optional[tuple[int, bytes]]:
        zcan = self._zcan
        count = zcan.ZCAN_GetReceiveNum(self._ch_handle, 0)  # 0=CAN
        if count == 0:
            time.sleep(min(timeout, 0.005))
            count = zcan.ZCAN_GetReceiveNum(self._ch_handle, 0)
            if count == 0:
                return None

        msgs = (zcan.ZCAN_Receive_Data * 1)()
        got = zcan.ZCAN_Receive(self._ch_handle, msgs, 1, int(timeout * 1000))
        if got < 1:
            return None

        frame = msgs[0].frame
        arb_id = frame.can_id & 0x1FFFFFFF  # mask out flags
        data = bytes(list(frame.data[:frame.can_dlc]))
        return arb_id, data

    def shutdown(self) -> None:
        if self._ch_handle:
            self._zcan.ZCAN_ResetCAN(self._ch_handle)
        if self._dev_handle:
            self._zcan.ZCAN_CloseDevice(self._dev_handle)
        logging.info("ZCANPRO device closed")


def create_can_backend(args: argparse.Namespace) -> CanBackend:
    """Factory: create the appropriate CAN backend from CLI args."""
    if args.bustype == "zcanpro":
        return ZcanproBackend(
            channel=int(args.channel),
            device=args.device,
            bitrate=args.bitrate,
        )
    return PythonCanBackend(
        bustype=args.bustype,
        channel=args.channel,
        bitrate=args.bitrate,
        device=args.device,
    )


# ---------------------------------------------------------------------------
# ISO-TP Layer (manual implementation, no can-isotp dependency)
# ---------------------------------------------------------------------------

class IsoTpLayer:
    """
    Manual ISO-TP (ISO 15765-2) implementation over a CanBackend.

    Handles segmentation and reassembly of UDS messages that exceed 7 bytes.
    Uses 29-bit extended CAN IDs.

    Key parameters (matching MCU bootloader configuration):
      - STmin: 1 ms (minimum time between consecutive frames)
      - BS: 0 (unlimited block size, no intermediate FC from receiver)
      - FC timeout: 1000 ms
      - CF timeout: 1000 ms
    """

    def __init__(self, bus: CanBackend, tx_id: int = UDS_REQ_ID,
                 rx_id: int = UDS_RESP_ID, stmin_ms: int = ISOTP_STMIN_MS,
                 bs: int = ISOTP_BS):
        self._bus = bus
        self._tx_id = tx_id
        self._rx_id = rx_id
        self._stmin_ms = stmin_ms
        self._bs = bs

        # RX state for incoming multi-frame messages
        self._rx_buffer = bytearray()
        self._rx_expected_len = 0
        self._rx_next_seq = 0
        self._rx_active = False

    # ---- TX path ----

    def send(self, payload: bytes) -> None:
        """Segment *payload* into CAN frames and send with ISO-TP framing."""
        length = len(payload)
        if length <= 7:
            # Single Frame
            sf_data = bytes([PCI_SF | length]) + payload
            self._bus.send(self._tx_id, sf_data.ljust(8, b"\x00"))
            return

        # First Frame
        ff_pci = PCI_FF | (length >> 8)
        ff_data = bytes([ff_pci, length & 0xFF]) + payload[:6]
        self._bus.send(self._tx_id, ff_data)

        # Wait for Flow Control frame from receiver
        fc = self._wait_for_fc()
        if fc is None:
            raise TimeoutError("ISO-TP: no Flow Control received after First Frame")
        fc_bs = fc[1]
        fc_stmin = fc[2]
        actual_stmin = self._interpret_stmin(fc_stmin)

        # Send Consecutive Frames
        offset = 6
        seq = 1
        blocks_sent = 0
        while offset < length:
            cf_data = bytes([PCI_CF | (seq & 0x0F)]) + payload[offset:offset + 7]
            self._bus.send(self._tx_id, cf_data.ljust(8, b"\x00"))
            offset += 7
            seq = (seq + 1) & 0x0F
            blocks_sent += 1

            if actual_stmin > 0:
                time.sleep(actual_stmin / 1000.0)

            # If BS > 0 and we've sent BS frames, wait for next FC
            if fc_bs > 0 and blocks_sent >= fc_bs:
                fc = self._wait_for_fc()
                if fc is None:
                    raise TimeoutError("ISO-TP: no Flow Control between blocks")
                fc_bs = fc[1]
                fc_stmin = fc[2]
                actual_stmin = self._interpret_stmin(fc_stmin)
                blocks_sent = 0

    def _wait_for_fc(self, timeout_ms: int = ISOTP_FC_TIMEOUT_MS) -> Optional[bytes]:
        """Wait for a Flow Control frame from the receiver."""
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            result = self._bus.recv(timeout=0.01)
            if result is None:
                continue
            arb_id, data = result
            if arb_id == self._rx_id and len(data) >= 1 and (data[0] & 0xF0) == PCI_FC:
                return data
        return None

    @staticmethod
    def _interpret_stmin(raw: int) -> int:
        """Convert FC STmin byte to milliseconds."""
        if raw <= 0x7F:
            return raw  # 0-127 ms
        if 0xF1 <= raw <= 0xF9:
            return 1  # 100-900 µs, round up to 1 ms
        return 0

    # ---- RX path ----

    def recv(self, timeout_s: float = 2.0) -> Optional[bytes]:
        """
        Receive and reassemble a complete ISO-TP message.

        Returns the full UDS payload bytes, or None on timeout.
        Handles SF, FF (sends FC), and CF automatically.
        """
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            result = self._bus.recv(timeout=0.01)
            if result is None:
                continue

            arb_id, data = result
            if arb_id != self._rx_id or len(data) < 1:
                continue

            pci = data[0] & 0xF0

            if pci == PCI_SF:
                sf_len = data[0] & 0x0F
                payload = data[1:1 + sf_len]
                if self._rx_active:
                    self._rx_active = False
                return bytes(payload)

            elif pci == PCI_FF:
                if len(data) < 2:
                    continue
                self._rx_expected_len = ((data[0] & 0x0F) << 8) | data[1]
                self._rx_buffer = bytearray(data[2:])
                self._rx_next_seq = 1
                self._rx_active = True
                # Send Flow Control (BS=0, STmin=1ms)
                fc_frame = bytes([PCI_FC, self._bs, self._stmin_ms]) + b"\x00" * 5
                self._bus.send(self._tx_id, fc_frame)

            elif pci == PCI_CF:
                if not self._rx_active:
                    continue
                seq = data[0] & 0x0F
                if seq != (self._rx_next_seq & 0x0F):
                    logging.warning(
                        "ISO-TP: CF seq mismatch: expected %d, got %d",
                        self._rx_next_seq & 0x0F, seq,
                    )
                    self._rx_active = False
                    return None
                self._rx_buffer.extend(data[1:])
                self._rx_next_seq += 1

                if len(self._rx_buffer) >= self._rx_expected_len:
                    self._rx_active = False
                    return bytes(self._rx_buffer[:self._rx_expected_len])

            elif pci == PCI_FC:
                # Unexpected FC (we're receiving, not transmitting)
                pass

        self._rx_active = False
        return None


# ---------------------------------------------------------------------------
# UDS Client (uses IsoTpLayer for transport)
# ---------------------------------------------------------------------------

class UdsClient:
    """High-level UDS request/response client over ISO-TP."""

    def __init__(self, isotp: IsoTpLayer):
        self._isotp = isotp
        self._keepalive_s = S3_TIMEOUT_S
        self._last_activity = time.time()

    def send_request(self, request: bytes, timeout_s: float = 5.0,
                     label: str = "") -> Optional[bytes]:
        """
        Send a UDS request and wait for the final response.

        NRC 0x78 (ResponsePending) automatically extends the timeout.
        TesterPresent responses (0x7E) are silently consumed.
        """
        logging.debug("→ TX [%s]: %s", label, request.hex(" ").upper())
        self._isotp.send(request)
        self._last_activity = time.time()

        deadline = time.time() + timeout_s
        while time.time() < deadline:
            # Send TesterPresent if approaching S3 timeout
            if time.time() - self._last_activity >= self._keepalive_s:
                self._send_tester_present()

            response = self._isotp.recv(timeout_s=0.05)
            if response is None:
                continue

            self._last_activity = time.time()
            logging.debug("← RX [%s]: %s", label, response.hex(" ").upper())

            if len(response) < 1:
                continue

            # Silently consume TesterPresent positive responses
            if response[0] == (SID_TESTER_PRESENT + SID_POSITIVE_RESPONSE_OFFSET):
                continue

            # Handle negative response
            if response[0] == SID_NEGATIVE_RESPONSE and len(response) >= 3:
                nrc = response[2]
                if nrc == NRC_RESPONSE_PENDING:
                    logging.info("  … NRC 0x78 (ResponsePending), extending timeout")
                    deadline = time.time() + timeout_s
                    continue
                logging.warning(
                    "  NRC 0x%02X (%s) for SID 0x%02X",
                    nrc, _nrc_desc(nrc), response[1],
                )
                return response

            return response

        logging.error("UDS request timeout (%s)", label)
        return None

    def _send_tester_present(self) -> None:
        """Send TesterPresent to keep the session alive (S3 timeout guard)."""
        try:
            self._isotp.send(bytes([SID_TESTER_PRESENT, 0x00]))
            self._last_activity = time.time()
            logging.debug("  → TesterPresent sent")
        except Exception as exc:
            logging.debug("TesterPresent failed: %s", exc)

    def read_did(self, did: int, timeout_s: float = 3.0) -> Optional[bytes]:
        """Read a DID and return its raw data bytes (or None on failure)."""
        request = bytes([SID_READ_DATA_BY_ID, (did >> 8) & 0xFF, did & 0xFF])
        response = self.send_request(request, timeout_s=timeout_s,
                                     label=f"ReadDID 0x{did:04X}")
        if response and response[0] == (SID_READ_DATA_BY_ID + SID_POSITIVE_RESPONSE_OFFSET):
            return response[3:]
        return None


# ---------------------------------------------------------------------------
# OTA Upgrader (full sequence)
# ---------------------------------------------------------------------------

class OtaUpgrader:
    """
    Orchestrates the complete Qi wireless charger OTA upgrade sequence:

    1. Switch to Programming Session
    2. SecurityAccess (ECDSA P-256)
    3. Select Firmware Type (APP)
    4. Erase Flash
    5. Request Download
    6. Transfer Data
    7. Request Transfer Exit (image + signature verification)
    8. ECU Reset
    9. Verify Version
    """

    def __init__(self, uds: UdsClient, keypair_path: Path):
        self._uds = uds
        self._keypair_path = keypair_path
        self._private_key = _load_private_key(keypair_path)
        logging.info("ECDSA P-256 private key loaded from %s", keypair_path)

    # ------------------------------------------------------------------
    # Step 1: Switch to Programming Session
    # ------------------------------------------------------------------

    def switch_to_programming_session(self) -> bool:
        logging.info("")
        logging.info("[Step 1/9] Switching to Programming Session (0x10 0x02)...")
        request = bytes([SID_DIAGNOSTIC_SESSION_CONTROL, SESSION_PROGRAMMING])
        response = self._uds.send_request(request, timeout_s=5.0,
                                          label="DiagSessionCtrl")
        if not response or response[0] != (SID_DIAGNOSTIC_SESSION_CONTROL + SID_POSITIVE_RESPONSE_OFFSET):
            logging.error("✗ Failed to switch to Programming Session")
            return False
        logging.info("✓ Programming Session activated")
        return True

    # ------------------------------------------------------------------
    # Step 2: SecurityAccess (ECDSA P-256)
    # ------------------------------------------------------------------

    def security_access_ecdsa(self) -> bool:
        logging.info("")
        logging.info("[Step 2/9] Security Access (ECDSA P-256)...")

        # 2a: Request Seed (0x27 0x01)
        logging.info("  [2a] Requesting seed (0x27 0x01)...")
        request = bytes([SID_SECURITY_ACCESS, SECURITY_REQUEST_SEED])
        response = self._uds.send_request(request, timeout_s=5.0,
                                          label="SecAccess ReqSeed")
        if not response or response[0] != (SID_SECURITY_ACCESS + SID_POSITIVE_RESPONSE_OFFSET):
            logging.error("✗ Failed to request seed")
            return False

        seed = bytes(response[2:])
        logging.info("  ✓ Seed received: %s (%d bytes)", seed.hex(" ").upper(), len(seed))

        # Already unlocked (all-zero seed)
        if seed == b"\x00" * len(seed):
            logging.info("  ✓ Security already unlocked")
            return True

        # 2b: Sign seed with ECDSA P-256 (SHA-256 hash, R||S 64 bytes)
        logging.info("  [2b] Signing seed (SHA-256 + ECDSA P-256)...")
        signature = _ecdsa_sign(self._private_key, seed)
        logging.info("  ✓ Signature: %s... (%d bytes)", signature[:8].hex(), len(signature))

        # 2c: Send signature (0x27 0x02 + 64 bytes)
        logging.info("  [2c] Sending signature (0x27 0x02 + 64 bytes)...")
        request = bytes([SID_SECURITY_ACCESS, SECURITY_SEND_KEY]) + signature
        response = self._uds.send_request(request, timeout_s=10.0,
                                          label="SecAccess SendKey")
        if not response or response[0] != (SID_SECURITY_ACCESS + SID_POSITIVE_RESPONSE_OFFSET):
            nrc = response[2] if response and len(response) >= 3 else None
            if nrc == 0x36:
                logging.error("✗ Security Access locked (too many attempts). Wait 60s and retry.")
            elif nrc == 0x35:
                logging.error("✗ Invalid signature (key mismatch with MCU)")
            else:
                logging.error("✗ Security Access denied (NRC=0x%02X)", nrc or 0)
            return False

        logging.info("  ✓ Security Access unlocked!")
        return True

    # ------------------------------------------------------------------
    # Step 3: Select Firmware Type
    # ------------------------------------------------------------------

    def select_firmware_type(self, fw_type: int = FIRMWARE_TYPE_APP) -> bool:
        type_name = "APP" if fw_type == FIRMWARE_TYPE_APP else "Bootloader"
        logging.info("")
        logging.info("[Step 3/9] Selecting Firmware Type: %s (0x2E 0x2010 0x%02X)...",
                     type_name, fw_type)
        request = bytes([
            SID_WRITE_DATA_BY_ID,
            (DID_FIRMWARE_TYPE >> 8) & 0xFF, DID_FIRMWARE_TYPE & 0xFF,
            fw_type,
        ])
        response = self._uds.send_request(request, timeout_s=5.0,
                                          label="WriteDID FwType")
        if not response or response[0] != (SID_WRITE_DATA_BY_ID + SID_POSITIVE_RESPONSE_OFFSET):
            logging.error("✗ Failed to set firmware type")
            return False
        logging.info("✓ Firmware type set to %s", type_name)
        return True

    # ------------------------------------------------------------------
    # Step 4: Erase Flash
    # ------------------------------------------------------------------

    def erase_flash(self) -> bool:
        logging.info("")
        logging.info("[Step 4/9] Erasing Flash (0x31 0x01 0xFF00)...")
        logging.info("  This may take 1-2 seconds (NRC 0x78 expected)...")
        request = bytes([
            SID_ROUTINE_CONTROL, ROUTINE_START,
            (ROUTINE_ERASE_MEMORY >> 8) & 0xFF, ROUTINE_ERASE_MEMORY & 0xFF,
        ])
        response = self._uds.send_request(request, timeout_s=15.0,
                                          label="RoutineCtrl Erase")
        if not response or response[0] != (SID_ROUTINE_CONTROL + SID_POSITIVE_RESPONSE_OFFSET):
            logging.error("✗ Flash erase failed")
            return False
        logging.info("✓ Flash erased successfully")
        return True

    # ------------------------------------------------------------------
    # Step 5: Request Download
    # ------------------------------------------------------------------

    def request_download(self, address: int, size: int) -> Optional[int]:
        logging.info("")
        logging.info("[Step 5/9] Requesting Download (0x34)...")
        logging.info("  Address: 0x%08X, Size: %d bytes", address, size)
        request = bytes([
            SID_REQUEST_DOWNLOAD,
            DATA_FORMAT_UNCOMPRESSED, ADDR_LEN_FORMAT_44,
        ]) + address.to_bytes(4, "big") + size.to_bytes(4, "big")

        response = self._uds.send_request(request, timeout_s=5.0,
                                          label="ReqDownload")
        if not response or response[0] != (SID_REQUEST_DOWNLOAD + SID_POSITIVE_RESPONSE_OFFSET):
            logging.error("✗ Request Download failed")
            return None

        if len(response) >= 4:
            max_block_len = (response[2] << 8) | response[3]
        else:
            max_block_len = 0x0802  # default: 2050 bytes

        # maxBlockLength includes SID + blockSeqCounter, so usable payload is -2
        max_data = max_block_len - 2
        logging.info("✓ Download accepted (max block: %d bytes, payload: %d bytes)",
                     max_block_len, max_data)
        return max_data

    # ------------------------------------------------------------------
    # Step 6: Transfer Data
    # ------------------------------------------------------------------

    def transfer_data(self, firmware: bytes, max_payload: int) -> bool:
        logging.info("")
        logging.info("[Step 6/9] Transferring firmware data (%d bytes)...", len(firmware))
        logging.info("  Max payload per block: %d bytes", max_payload)

        block_seq = 1
        offset = 0
        total = len(firmware)
        start_time = time.time()
        last_log_seq = 0

        while offset < total:
            chunk_size = min(max_payload, total - offset)
            chunk = firmware[offset:offset + chunk_size]

            request = bytes([SID_TRANSFER_DATA, block_seq]) + chunk
            response = self._uds.send_request(request, timeout_s=5.0,
                                              label=f"TransferData #{block_seq}")
            if not response:
                logging.error("✗ No response for block #%d", block_seq)
                return False
            if response[0] != (SID_TRANSFER_DATA + SID_POSITIVE_RESPONSE_OFFSET):
                nrc = response[2] if len(response) >= 3 else 0xFF
                logging.error("✗ Transfer failed at block #%d (NRC=0x%02X: %s)",
                              block_seq, nrc, _nrc_desc(nrc))
                return False

            offset += chunk_size
            progress_pct = offset * 100 // total

            # Log progress every 5% or every 50 blocks
            if (progress_pct // 5 > (progress_pct - chunk_size * 100 // total) // 5
                    or block_seq - last_log_seq >= 50
                    or offset >= total):
                elapsed = time.time() - start_time
                speed = offset / elapsed / 1024 if elapsed > 0 else 0
                logging.info(
                    "  Progress: %3d%% (%d/%d bytes, %.1f KB/s, block #%d)",
                    progress_pct, offset, total, speed, block_seq,
                )
                last_log_seq = block_seq

            # Advance block sequence counter (skip 0x00)
            block_seq = (block_seq + 1) & 0xFF
            if block_seq == 0:
                block_seq = 1

        elapsed = time.time() - start_time
        speed = total / elapsed / 1024 if elapsed > 0 else 0
        logging.info("✓ All data transferred in %.1f s (%.1f KB/s, %d blocks)",
                     elapsed, speed, block_seq - 1 if block_seq > 1 else 255)
        return True

    # ------------------------------------------------------------------
    # Step 7: Request Transfer Exit
    # ------------------------------------------------------------------

    def request_transfer_exit(self) -> bool:
        logging.info("")
        logging.info("[Step 7/9] Requesting Transfer Exit (0x37)...")
        logging.info("  MCU will verify image CRC32 + ECDSA signature...")
        request = bytes([SID_REQUEST_TRANSFER_EXIT])
        response = self._uds.send_request(request, timeout_s=15.0,
                                          label="ReqTransferExit")
        if not response or response[0] != (SID_REQUEST_TRANSFER_EXIT + SID_POSITIVE_RESPONSE_OFFSET):
            nrc = response[2] if response and len(response) >= 3 else 0xFF
            logging.error("✗ Transfer Exit failed (NRC=0x%02X: %s)", nrc, _nrc_desc(nrc))
            return False
        logging.info("✓ Transfer Exit successful (image verification passed)")
        return True

    # ------------------------------------------------------------------
    # Step 8: ECU Reset
    # ------------------------------------------------------------------

    def ecu_reset(self) -> bool:
        logging.info("")
        logging.info("[Step 8/9] ECU Reset (0x11 0x01)...")
        request = bytes([SID_ECU_RESET, RESET_HARD])
        response = self._uds.send_request(request, timeout_s=3.0,
                                          label="ECUReset")
        if not response or response[0] != (SID_ECU_RESET + SID_POSITIVE_RESPONSE_OFFSET):
            logging.warning("⚠ Reset response not received (may be normal if MCU resets quickly)")
        else:
            logging.info("✓ ECU Reset accepted")
        logging.info("  Waiting for MCU to reboot...")
        time.sleep(3.0)
        return True

    # ------------------------------------------------------------------
    # Step 9: Verify Version
    # ------------------------------------------------------------------

    def verify_version(self) -> bool:
        logging.info("")
        logging.info("[Step 9/9] Verifying new firmware version...")

        # Return to default session first
        logging.info("  Switching back to Default Session...")
        request = bytes([SID_DIAGNOSTIC_SESSION_CONTROL, SESSION_DEFAULT])
        self._uds.send_request(request, timeout_s=3.0, label="DiagSessionCtrl Default")
        time.sleep(0.5)

        # Read software version (DID 0xF195)
        data = self._uds.read_did(DID_SOFTWARE_VERSION, timeout_s=3.0)
        if data is not None:
            version = data.decode("utf-8", errors="replace").rstrip("\x00")
            logging.info("✓ New software version (0xF195): %s", version)
            return True
        else:
            logging.warning("⚠ Could not read software version (MCU may still be booting)")
            return False

    # ------------------------------------------------------------------
    # Run the full OTA sequence
    # ------------------------------------------------------------------

    def run(self, firmware_path: Path, address: int = 0x08005000) -> bool:
        """
        Execute the complete OTA upgrade sequence.

        Args:
            firmware_path: Path to the .bin file (XATO header + firmware).
            address: Flash download address (default: 0x08005000 for APP_A).

        Returns:
            True on success, False on failure.
        """
        # Load firmware
        logging.info("Loading firmware: %s", firmware_path)
        firmware = firmware_path.read_bytes()
        if len(firmware) < IMAGE_HEADER_SIZE:
            logging.error("✗ Firmware file too small (%d bytes)", len(firmware))
            return False

        # Validate XATO header
        magic = struct.unpack_from("<I", firmware, 0)[0]
        if magic != IMAGE_MAGIC:
            logging.error("✗ Invalid firmware header (magic=0x%08X, expected 0x%08X)",
                          magic, IMAGE_MAGIC)
            logging.error("  Use tools/pack_image.py to create a valid firmware image")
            return False

        image_length = struct.unpack_from("<I", firmware, 4)[0]
        total_size = IMAGE_HEADER_SIZE + image_length
        if len(firmware) < total_size:
            logging.error("✗ Firmware file truncated (have %d, need %d)", len(firmware), total_size)
            return False

        version_raw = firmware[0x4C:0x5C]
        version_str = version_raw.split(b"\x00")[0].decode("utf-8", errors="replace")
        crc32_val = struct.unpack_from("<I", firmware, 8)[0]

        logging.info("  Version: %s", version_str)
        logging.info("  Image length: %d bytes", image_length)
        logging.info("  CRC32: 0x%08X", crc32_val)
        logging.info("  Total (header + image): %d bytes", total_size)
        logging.info("  Target address: 0x%08X", address)

        logging.info("")
        logging.info("=" * 70)
        logging.info("Starting Qi Wireless Charger OTA Upgrade")
        logging.info("  TX ID: 0x%08X  →  RX ID: 0x%08X", UDS_REQ_ID, UDS_RESP_ID)
        logging.info("=" * 70)

        overall_start = time.time()

        # Step 1: Programming Session
        if not self.switch_to_programming_session():
            return False

        # Step 2: Security Access
        if not self.security_access_ecdsa():
            return False

        # Step 3: Select firmware type
        if not self.select_firmware_type(FIRMWARE_TYPE_APP):
            return False

        # Step 4: Erase flash
        if not self.erase_flash():
            return False

        # Step 5: Request download
        max_payload = self.request_download(address, len(firmware))
        if max_payload is None:
            return False

        # Step 6: Transfer data
        if not self.transfer_data(firmware, max_payload):
            return False

        # Step 7: Transfer exit (image + signature verification)
        if not self.request_transfer_exit():
            return False

        # Step 8: ECU Reset
        if not self.ecu_reset():
            return False

        # Step 9: Verify version
        self.verify_version()

        elapsed = time.time() - overall_start
        logging.info("")
        logging.info("=" * 70)
        logging.info("✓ OTA Upgrade Completed Successfully in %.1f seconds!", elapsed)
        logging.info("=" * 70)
        return True


# ---------------------------------------------------------------------------
# CLI Entry Point
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Qi Wireless Charger CAN-UDS OTA Upgrade Script",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # python-can with virtual bus (testing):
  %(prog)s --bustype virtual --channel vcan0 \\
      --firmware app.ota.bin --keypair private.pem

  # python-can with socketcan:
  %(prog)s --bustype socketcan --channel can0 \\
      --firmware app.ota.bin --keypair private.pem

  # ZCANPRO API:
  %(prog)s --bustype zcanpro --channel 0 \\
      --firmware app.ota.bin --keypair private.pem

Firmware file format:
  Use tools/pack_image.py to create the .ota.bin file from a Keil .bin output.
  The file must contain a 256-byte XATO header followed by the firmware image.

Keypair file format:
  PEM-encoded ECDSA P-256 private key, or 32-byte/97-byte raw binary key.
  Generate with: OTA simulator app/.../generate_keypair.py
""",
    )

    parser.add_argument(
        "--bustype",
        default="socketcan",
        choices=["socketcan", "canalystii", "pcan", "virtual", "zcanpro"],
        help="CAN bus backend type (default: socketcan)",
    )
    parser.add_argument(
        "--channel",
        default="can0",
        help="CAN channel: 'can0' for socketcan, '0' for canalystii/zcanpro, "
             "'vcan0' for virtual (default: can0)",
    )
    parser.add_argument(
        "--device",
        type=int,
        default=0,
        help="Device index for multi-device setups (default: 0)",
    )
    parser.add_argument(
        "--bitrate",
        type=int,
        default=250000,
        help="CAN bus bitrate in bps (default: 250000)",
    )
    parser.add_argument(
        "--firmware",
        required=True,
        help="Path to firmware .bin file with XATO header (from pack_image.py)",
    )
    parser.add_argument(
        "--keypair",
        required=True,
        help="Path to ECDSA P-256 private key (PEM or raw binary)",
    )
    parser.add_argument(
        "--address",
        type=lambda x: int(x, 0),
        default=0x08005000,
        help="Flash download address (default: 0x08005000 = APP_A)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Enable verbose DEBUG logging",
    )

    args = parser.parse_args()

    # Setup logging
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    # Validate firmware file
    fw_path = Path(args.firmware)
    if not fw_path.is_file():
        logging.error("Firmware file not found: %s", fw_path)
        return 1

    # Validate keypair file
    key_path = Path(args.keypair)
    if not key_path.is_file():
        logging.error("Keypair file not found: %s", key_path)
        return 1

    # Check cryptography library
    try:
        from cryptography.hazmat.primitives.asymmetric import ec  # noqa: F401
    except ImportError:
        logging.error(
            "The 'cryptography' package is required for ECDSA P-256.\n"
            "Install it with: pip3 install cryptography"
        )
        return 1

    # Create CAN backend
    logging.info("Initializing CAN backend: %s", args.bustype)
    try:
        bus = create_can_backend(args)
    except Exception as exc:
        logging.error("Failed to initialize CAN backend: %s", exc)
        return 1

    # Create ISO-TP and UDS layers
    isotp_layer = IsoTpLayer(bus)
    uds = UdsClient(isotp_layer)

    # Create and run upgrader
    upgrader = OtaUpgrader(uds, key_path)

    try:
        success = upgrader.run(fw_path, address=args.address)
        return 0 if success else 1
    except KeyboardInterrupt:
        logging.info("\nInterrupted by user")
        return 130
    except Exception as exc:
        logging.error("OTA upgrade failed: %s", exc, exc_info=args.verbose)
        return 1
    finally:
        bus.shutdown()


if __name__ == "__main__":
    sys.exit(main())
