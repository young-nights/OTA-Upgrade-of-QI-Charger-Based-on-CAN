#!/usr/bin/env python3
"""
Qi Charger UDS Firmware Upgrade Client with ECDSA P-256 Authentication (FAST VERSION)

Simulates the CCU acting as a UDS client that performs a signed firmware OTA
against the Qi wireless charger module over CAN / ISO-TP, using ECDSA P-256
challenge-response for SecurityAccess Level 1 as required by the Qi charger SRS
(LIME-QI-PERIPH-SRS-001 v1.1 §2.3 / REF-5 FIPS 186-4) and the common CAN
protocol specification (REF-1 §7.4).

Crypto (per common/ecdsa_p256.py, aligned with the whole toolchain):
- Curve:     NIST P-256 (secp256r1)
- Digest:    SHA-256 (the seed is hashed, then the digest is signed)
- Signature: raw R || S, fixed 64 bytes (IEEE P1363), NOT ASN.1 DER
- Public key: uncompressed SEC1 point, 65 bytes (0x04 || X || Y)

Optimized for maximum throughput:
- stmin: 0ms (no artificial delay)
- blocksize: 0 (unlimited, no Flow Control until complete)
- Reduced logging frequency

Upgrade sequence:
1. Switch to Programming Session
2. Security Access (ECDSA P-256: request seed -> SHA-256 sign -> send 64-byte signature)
3. Select Firmware Type (APP or Bootloader)
4. Erase Memory (RoutineControl 0xFF00)
5. Request Download
6. Transfer Data (multiple blocks)
7. Request Transfer Exit
8. CCU Reset (activation)
9. Verify new firmware version and OTA state
"""

import argparse
import logging
import time
import sys
from pathlib import Path
from typing import Optional
import can
import isotp

# Add project root to path to import common modules
project_root = Path(__file__).resolve().parent.parent.parent
if str(project_root) not in sys.path:
    sys.path.insert(0, str(project_root))

from common.uds_constants import *
from qi_charger.qi_charger_constants import *

# Try to import the ECDSA P-256 helper (requires the `cryptography` package)
try:
    from common import ecdsa_p256
    HAS_ECDSA = True
except ImportError:
    HAS_ECDSA = False
    logging.warning("`cryptography` not installed. ECDSA disabled. Install with: pip3 install cryptography")


class QiFirmwareUpgradeClientEcdsa:
    """UDS Firmware Upgrade Client for the Qi charger with ECDSA P-256 authentication"""

    def __init__(
        self,
        bus: can.Bus,
        keypair_file: str = "qi_ecdsa_p256_keypair.bin",
    ):
        self.bus = bus
        self.keypair_file = keypair_file

        # Setup ISO-TP with FAST parameters
        isotp_params = {
            'stmin': 0,              # 0ms = maximum speed
            'blocksize': 0,          # 0 = unlimited (no Flow Control needed for entire transfer)
            'tx_padding': 0x00,
            'rx_flowcontrol_timeout': 1000,
            'rx_consecutive_frame_timeout': 1000,
        }

        # Extended 29-bit CAN ID addressing (0x18DA0D03 / 0x18DA030D)
        addr = isotp.Address(
            isotp.AddressingMode.Normal_29bits,
            txid=UDS_REQ_ID,
            rxid=UDS_RESP_ID
        )

        self.isotp_stack = isotp.CanStack(
            bus=self.bus,
            address=addr,
            params=isotp_params
        )

        # Load the Qi charger's keypair (used to sign the challenge seed)
        self.keypair = None
        if HAS_ECDSA:
            self._load_keypair()

    def _load_keypair(self):
        """Load the Qi charger's ECDSA P-256 keypair (32-byte private scalar)"""
        try:
            with open(self.keypair_file, 'rb') as f:
                key_data = f.read()
            self.keypair = ecdsa_p256.load_private_key(key_data)
            logging.info(f"✓ Loaded Qi charger keypair from {self.keypair_file}")
        except FileNotFoundError:
            logging.error(f"Keypair file not found: {self.keypair_file}")
            logging.error("Generate it with: python3 ecdsa-p256-keys/generate_keypair.py")
        except Exception as e:
            logging.error(f"Failed to load keypair: {e}")

    def _send_request(self, request: bytes, timeout: float = 2.0) -> Optional[bytes]:
        """Send UDS request and receive response"""
        logging.debug(f"→ TX: {len(request)} bytes: {request.hex(' ').upper()}")
        self.isotp_stack.send(request)

        start_time = time.time()
        while (time.time() - start_time) < timeout:
            self.isotp_stack.process()
            if self.isotp_stack.available():
                response = self.isotp_stack.recv()
                if response:
                    logging.debug(f"← RX: {len(response)} bytes: {response.hex(' ').upper()}")
                    return response
            time.sleep(0.001)

        logging.error("Request timeout!")
        return None

    def read_did(self, did: int, timeout: float = 2.0) -> Optional[bytes]:
        """Read a DID and return its raw data bytes (or None on failure)"""
        request = bytes([SID_READ_DATA_BY_ID, (did >> 8) & 0xFF, did & 0xFF])
        response = self._send_request(request, timeout=timeout)
        if response and response[0] == 0x62:
            return response[3:]
        return None

    def switch_to_programming_session(self) -> bool:
        """Step 1: Switch to programming session"""
        logging.info("\n[Step 1] Switching to Programming Session...")

        request = bytes([SID_DIAGNOSTIC_SESSION_CONTROL, SESSION_PROGRAMMING])
        response = self._send_request(request)

        if not response or response[0] != 0x50:
            logging.error("Failed to switch to programming session")
            return False

        logging.info("✓ Programming session activated")
        return True

    def security_access_ecdsa(self) -> bool:
        """Step 2: Security access with ECDSA P-256 challenge-response"""
        logging.info("\n[Step 2] Security Access (ECDSA P-256)...")

        if not HAS_ECDSA or self.keypair is None:
            logging.error("ECDSA not available or keypair not loaded")
            return False

        # 2.1 Request seed
        logging.info("  [2.1] Requesting seed...")
        request = bytes([SID_SECURITY_ACCESS, SECURITY_REQUEST_SEED])
        response = self._send_request(request)

        if not response or response[0] != 0x67:
            logging.error("Failed to request seed")
            return False

        seed = bytes(response[2:])
        logging.info(f"  ✓ Received seed ({len(seed)} bytes)")

        # Already unlocked (server returns an all-zero seed)
        if seed == b'\x00' * len(seed):
            logging.info("  ✓ Security already unlocked")
            return True

        # 2.2 Sign the seed (ecdsa_p256.sign hashes with SHA-256 internally)
        logging.info("  [2.2] Signing seed with ECDSA P-256 (SHA-256)...")
        signature = ecdsa_p256.sign(self.keypair, seed)  # raw R||S, 64 bytes

        # 2.3 Send signature
        logging.info("  [2.3] Sending signature...")
        request = bytes([SID_SECURITY_ACCESS, SECURITY_SEND_KEY]) + signature
        response = self._send_request(request, timeout=5.0)

        if not response or response[0] != 0x67:
            logging.error("  ✗ Security access denied")
            return False

        logging.info("  ✓ Security unlocked!")
        return True

    def select_firmware_type(self, firmware_type: int) -> bool:
        """Step 3: Select firmware type (DID 0x2010)"""
        type_name = "APP" if firmware_type == FIRMWARE_TYPE_APP else "Bootloader"
        logging.info(f"\n[Step 3] Selecting Firmware Type: {type_name} (0x{firmware_type:02X})...")

        request = bytes([SID_WRITE_DATA_BY_ID,
                         (DID_FIRMWARE_TYPE >> 8) & 0xFF, DID_FIRMWARE_TYPE & 0xFF,
                         firmware_type])
        response = self._send_request(request)

        if not response or response[0] != 0x6E:
            logging.error(f"Failed to set firmware type to {type_name}")
            return False

        logging.info(f"✓ Firmware type set to {type_name}")
        return True

    def erase_memory(self) -> bool:
        """Step 4: Erase flash memory (RoutineControl 0xFF00)"""
        logging.info("\n[Step 4] Erasing Memory...")

        request = bytes([SID_ROUTINE_CONTROL, ROUTINE_START,
                         (ROUTINE_ERASE_MEMORY >> 8) & 0xFF, ROUTINE_ERASE_MEMORY & 0xFF])
        response = self._send_request(request, timeout=10.0)

        if not response or response[0] != 0x71:
            logging.error("Failed to erase memory")
            return False

        logging.info("✓ Memory erased")
        return True

    def request_download(self, address: int, size: int) -> Optional[int]:
        """Step 5: Request download (0x34)"""
        logging.info(f"\n[Step 5] Requesting Download (addr=0x{address:08X}, size={size})...")

        request = bytes([SID_REQUEST_DOWNLOAD,
                         DATA_FORMAT_UNCOMPRESSED_UNENCRYPTED, ADDR_LEN_FORMAT_44]) + \
            address.to_bytes(4, 'big') + \
            size.to_bytes(4, 'big')

        response = self._send_request(request, timeout=5.0)

        if not response or response[0] != 0x74:
            logging.error("Failed to request download")
            return None

        if len(response) >= 4:
            max_block_size = (response[2] << 8) | response[3]
        else:
            max_block_size = 0x0802  # 2050 bytes default

        # maxNumberOfBlockLength (ISO 14229-1) includes the TransferData SID and
        # blockSequenceCounter, so the usable data payload is 2 bytes smaller.
        max_data_size = max_block_size - 2
        logging.info(f"✓ Download accepted (max block length: {max_block_size} bytes, "
                     f"data payload: {max_data_size} bytes)")
        return max_data_size

    def transfer_data(self, firmware_data: bytes, max_block_size: int) -> bool:
        """Step 6: Transfer firmware data (0x36, FAST VERSION)"""
        logging.info(f"\n[Step 6] Transferring Firmware Data ({len(firmware_data)} bytes)...")
        logging.info("  Using FAST mode: stmin=0ms, blocksize=unlimited")

        block_sequence = 1
        offset = 0
        start_time = time.time()

        while offset < len(firmware_data):
            block_size = min(max_block_size, len(firmware_data) - offset)
            block_data = firmware_data[offset:offset + block_size]

            request = bytes([SID_TRANSFER_DATA, block_sequence]) + block_data
            response = self._send_request(request, timeout=5.0)

            if not response or response[0] != 0x76:
                logging.error(f"Failed to transfer block #{block_sequence}")
                return False

            offset += block_size
            progress = (offset * 100) // len(firmware_data)

            # Reduce logging frequency: every 50 frames or 10% progress
            if block_sequence % 50 == 0 or progress % 10 == 0 or offset >= len(firmware_data):
                elapsed = time.time() - start_time
                throughput = offset / elapsed / 1024 if elapsed > 0 else 0
                logging.info(f"  Progress: {progress}% ({offset}/{len(firmware_data)} bytes, {throughput:.1f} KB/s)")

            # ISO 14229-1: block sequence counter wraps 0xFF -> 0x00 -> 0x01
            block_sequence = (block_sequence + 1) & 0xFF

        elapsed = time.time() - start_time
        throughput = len(firmware_data) / elapsed / 1024 if elapsed > 0 else 0
        logging.info(f"✓ All data transferred in {elapsed:.1f}s ({throughput:.1f} KB/s)")
        return True

    def request_transfer_exit(self) -> bool:
        """Step 7: Request transfer exit (0x37)"""
        logging.info("\n[Step 7] Requesting Transfer Exit...")

        request = bytes([SID_REQUEST_TRANSFER_EXIT])
        response = self._send_request(request, timeout=5.0)

        if not response or response[0] != 0x77:
            logging.error("Failed to exit transfer")
            return False

        logging.info("✓ Transfer exit successful")
        return True

    def ccu_reset(self) -> bool:
        """Step 8: Reset module to activate new image (0x11)"""
        logging.info("\n[Step 8] Resetting module (activation)...")

        request = bytes([SID_ECU_RESET, RESET_HARD])
        response = self._send_request(request, timeout=2.0)

        if not response or response[0] != 0x51:
            logging.warning("Reset response not received (may be normal)")

        logging.info("✓ Reset requested")
        return True

    def verify_version(self) -> bool:
        """Step 9: Verify new firmware version and OTA state"""
        logging.info("\n[Step 9] Verifying Firmware Version and OTA State...")

        time.sleep(3)

        # Return to default session before reading version
        self._send_request(bytes([SID_DIAGNOSTIC_SESSION_CONTROL, SESSION_DEFAULT]))

        # Software version (DID 0xF195) per QI-OTA-001
        data = self.read_did(DID_SOFTWARE_VERSION, timeout=3.0)
        ok = False
        if data is not None:
            version = data.decode('utf-8', errors='ignore').rstrip('\x00')
            logging.info(f"✓ New software version (0xF195): {version}")
            ok = True
        else:
            logging.warning("Could not read software version")

        # OTA status DIDs per QI-OTA-002
        slot = self.read_did(DID_ACTIVE_FIRMWARE_SLOT)
        if slot:
            logging.info(f"  Active firmware slot (0x2113): {FW_SLOT_NAMES.get(slot[0], hex(slot[0]))}")
        ota = self.read_did(DID_OTA_STATUS)
        if ota:
            logging.info(f"  OTA status (0x2112): {OTA_STATUS_NAMES.get(ota[0], hex(ota[0]))}")
        boot = self.read_did(DID_LAST_BOOT_REASON)
        if boot:
            logging.info(f"  Last boot reason (0x2115): {BOOT_REASON_NAMES.get(boot[0], hex(boot[0]))}")

        return ok


def generate_test_firmware(size: int = 98304) -> bytes:
    """Generate test firmware data for bench testing"""
    import secrets
    logging.info(f"Generating test firmware ({size} bytes)...")

    firmware = bytearray()
    firmware.extend(b"LIME_QI_FW_ECDSA_V1.0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
    firmware.extend(secrets.token_bytes(max(0, size - len(firmware))))

    logging.info(f"✓ Generated {len(firmware)} bytes of test firmware")
    return bytes(firmware[:size])


def main():
    parser = argparse.ArgumentParser(description="Qi Charger UDS Firmware Upgrade with ECDSA P-256 (FAST)")
    parser.add_argument("--bustype", default="canalystii", help="CAN interface type (socketcan/canalystii/pcan/virtual)")
    parser.add_argument("--channel", default="0", help="CAN channel (e.g. can0 for socketcan, 0 for canalystii)")
    parser.add_argument("--bitrate", type=int, default=250000, help="CAN bitrate (Qi bus is 250 kbps)")
    parser.add_argument("--device", type=int, default=0, help="Device index for multi-device setups")
    parser.add_argument("--keypair", default="ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin", help="Qi charger keypair file")
    parser.add_argument("--firmware", default=None, help="Real firmware binary file")
    parser.add_argument("--size", type=int, default=98304, help="Test firmware size in bytes if --firmware not given")
    parser.add_argument("--type", choices=['app', 'bootloader'], default='app', help="Firmware type (default: app)")
    parser.add_argument("--address", type=lambda x: int(x, 0), default=0x08000000, help="Flash download address (default: 0x08000000)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose logging (DEBUG level)")

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    if not HAS_ECDSA:
        logging.error("`cryptography` is not installed! Install with: pip3 install cryptography")
        return 1

    # Connect to CAN bus
    if args.bustype == "socketcan":
        bus = can.Bus(interface=args.bustype, channel=args.channel)
    else:
        bus = can.Bus(interface=args.bustype, channel=args.channel,
                      bitrate=args.bitrate, device=args.device)

    logging.info(f"Connected to CAN bus ({args.bustype}, channel={args.channel}, device={args.device})")

    client = QiFirmwareUpgradeClientEcdsa(bus, keypair_file=args.keypair)

    if client.keypair is None:
        logging.error("Failed to load Qi charger keypair")
        bus.shutdown()
        return 1

    # Load or generate firmware
    if args.firmware:
        logging.info(f"Loading firmware from file: {args.firmware}")
        try:
            with open(args.firmware, 'rb') as f:
                firmware_data = f.read()
            logging.info(f"✓ Loaded {len(firmware_data)} bytes from {args.firmware}")
        except Exception as e:
            logging.error(f"Failed to load firmware: {e}")
            bus.shutdown()
            return 1
    else:
        firmware_data = generate_test_firmware(args.size)

    firmware_type = FIRMWARE_TYPE_APP if args.type == 'app' else FIRMWARE_TYPE_BOOTLOADER

    logging.info("\n" + "=" * 70)
    logging.info("Starting FAST Qi Charger Firmware Upgrade with ECDSA P-256 Authentication")
    logging.info(f"  Request ID: 0x{UDS_REQ_ID:08X}   Response ID: 0x{UDS_RESP_ID:08X}")
    logging.info("=" * 70)

    try:
        overall_start = time.time()

        if not client.switch_to_programming_session():
            return 1
        if not client.security_access_ecdsa():
            return 1
        if not client.select_firmware_type(firmware_type):
            return 1
        if not client.erase_memory():
            return 1
        max_block_size = client.request_download(args.address, len(firmware_data))
        if not max_block_size:
            return 1
        if not client.transfer_data(firmware_data, max_block_size):
            return 1
        if not client.request_transfer_exit():
            return 1
        if not client.ccu_reset():
            return 1
        client.verify_version()

        overall_elapsed = time.time() - overall_start
        logging.info("\n" + "=" * 70)
        logging.info(f"✓ Firmware Upgrade Completed in {overall_elapsed:.1f} seconds!")
        logging.info("=" * 70)
        return 0

    except KeyboardInterrupt:
        logging.info("\nInterrupted by user")
        return 1
    except Exception as e:
        logging.error(f"Upgrade failed: {e}", exc_info=True)
        return 1
    finally:
        bus.shutdown()


if __name__ == "__main__":
    sys.exit(main())
