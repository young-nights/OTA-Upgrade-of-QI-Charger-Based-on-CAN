#!/usr/bin/env python3
"""
Qi Charger UDS Firmware Upgrade Client (Simple Security Mode)

Simulates the CCU acting as a UDS client that performs a firmware OTA against the
Qi wireless charger module over CAN / ISO-TP, per the Qi charger SRS
(LIME-QI-PERIPH-SRS-001) and the common CAN protocol specification (REF-1).

Upgrade sequence:
1. Read current version and OTA state (DID 0xF195 / 0x2112 / 0x2113)
2. Switch to Programming Session
3. Security Access (Simple Seed & Key)
4. Select Firmware Type (APP or Bootloader)
5. Erase Memory (RoutineControl 0xFF00)
6. Request Download
7. Transfer Data (multiple blocks)
8. Request Transfer Exit
9. CCU Reset (activation)
10. Verify new firmware version and OTA state

For production ECDSA P-256 authentication, use qi_upgrade_client_ecdsa.py instead.

WARNING: The "simple" seed+key algorithm (key = seed + 0x5555) is a placeholder
for bench testing only. The Qi charger SRS v1.1 §2.3 / REF-1 §7.4 require ECDSA
P-256 challenge-response for SecurityAccess Level 1 in production.
"""

import argparse
import logging
import time
import sys
from pathlib import Path
import can
import isotp

# Add project root to path to import common modules
project_root = Path(__file__).resolve().parent.parent.parent
if str(project_root) not in sys.path:
    sys.path.insert(0, str(project_root))

from common.uds_constants import *
from qi_charger.qi_charger_constants import *


class QiFirmwareUpgradeClient:
    """UDS Firmware Upgrade Client for the Qi wireless charger (simple security)"""

    def __init__(self, bus: can.Bus):
        # Setup ISO-TP (Qi charger uses CAN 250 kbps per SRS §2.3)
        isotp_params = {
            'stmin': 10,                           # Minimum separation time (ms)
            'blocksize': 8,                        # Block size (frames per flow control)
            'tx_padding': 0x00,                    # Padding byte
            'rx_flowcontrol_timeout': 1000,        # Flow control timeout (ms)
            'rx_consecutive_frame_timeout': 1000,  # Consecutive frame timeout (ms)
        }

        # Extended 29-bit CAN ID addressing (0x18DA0D03 / 0x18DA030D)
        addr = isotp.Address(
            isotp.AddressingMode.Normal_29bits,
            txid=UDS_REQ_ID,
            rxid=UDS_RESP_ID
        )

        self.isotp_stack = isotp.CanStack(
            bus=bus,
            address=addr,
            params=isotp_params
        )

    def send_request(self, payload: bytes, timeout: float = 2.0):
        """Send UDS request and wait for response"""
        logging.info(f"→ TX: {len(payload)} bytes: {payload.hex(' ').upper()}")
        self.isotp_stack.send(payload)

        start_time = time.time()
        while (time.time() - start_time) < timeout:
            self.isotp_stack.process()
            if self.isotp_stack.available():
                response = self.isotp_stack.recv()
                if response:
                    logging.info(f"← RX: {len(response)} bytes: {response.hex(' ').upper()}")
                    return response
            time.sleep(0.001)

        logging.warning("No response received")
        return None

    def read_did(self, did: int, timeout: float = 2.0):
        """Read a DID and return its raw data bytes (or None on failure)"""
        payload = bytes([SID_READ_DATA_BY_ID, (did >> 8) & 0xFF, did & 0xFF])
        response = self.send_request(payload, timeout=timeout)
        if response and response[0] == 0x62:
            return response[3:]
        return None

    def read_software_version(self):
        """Read software version (DID 0xF195)"""
        data = self.read_did(DID_SOFTWARE_VERSION)
        if data is not None:
            return data.decode('utf-8', errors='ignore').rstrip('\x00')
        return None

    def switch_to_programming_session(self):
        """Step 1: Switch to Programming Session"""
        print("\n" + "=" * 70)
        print("STEP 1: Switch to Programming Session")
        print("=" * 70)

        payload = bytes([SID_DIAGNOSTIC_SESSION_CONTROL, SESSION_PROGRAMMING])
        response = self.send_request(payload)

        if response and response[0] == 0x50:
            print("✓ Switched to Programming Session")
            return True
        print("✗ Failed to switch session")
        return False

    def security_access(self):
        """Step 2: Perform Security Access (Seed & Key)"""
        print("\n" + "=" * 70)
        print("STEP 2: Security Access (Simple Seed & Key)")
        print("=" * 70)

        # Request seed
        print("  Requesting seed...")
        payload = bytes([SID_SECURITY_ACCESS, SECURITY_REQUEST_SEED])
        response = self.send_request(payload)

        if not response or response[0] != 0x67:
            print("✗ Failed to get seed")
            return False

        seed = (response[2] << 8) | response[3]
        print(f"  Received seed: 0x{seed:04X}")

        # Already unlocked (server returns zero seed)
        if seed == 0x0000:
            print("✓ Security already unlocked")
            return True

        # Calculate key (simple algorithm: key = seed + 0x5555)
        key = (seed + 0x5555) & 0xFFFF
        print(f"  Calculated key: 0x{key:04X}")

        # Send key
        print("  Sending key...")
        payload = bytes([SID_SECURITY_ACCESS, SECURITY_SEND_KEY]) + key.to_bytes(2, 'big')
        response = self.send_request(payload)

        if response and response[0] == 0x67:
            print("✓ Security Access UNLOCKED")
            return True
        print("✗ Security Access FAILED")
        return False

    def select_firmware_type(self, firmware_type):
        """Step 3: Select firmware type before upgrade (DID 0x2010)"""
        print("\n" + "=" * 70)
        print("STEP 3: Select Firmware Type")
        print("=" * 70)

        type_name = "APP" if firmware_type == FIRMWARE_TYPE_APP else "Bootloader"
        print(f"  Setting firmware type to: {type_name} (0x{firmware_type:02X})")

        payload = bytes([SID_WRITE_DATA_BY_ID,
                         (DID_FIRMWARE_TYPE >> 8) & 0xFF, DID_FIRMWARE_TYPE & 0xFF,
                         firmware_type])
        response = self.send_request(payload)

        if response and response[0] == 0x6E:
            print(f"✓ Firmware type set to {type_name}")
            return True
        print("✗ Failed to set firmware type")
        return False

    def erase_memory(self):
        """Step 4: Erase memory using RoutineControl 0xFF00"""
        print("\n" + "=" * 70)
        print("STEP 4: Erase Memory")
        print("=" * 70)

        payload = bytes([SID_ROUTINE_CONTROL, ROUTINE_START,
                         (ROUTINE_ERASE_MEMORY >> 8) & 0xFF, ROUTINE_ERASE_MEMORY & 0xFF])
        response = self.send_request(payload, timeout=10.0)

        if response and response[0] == 0x71:
            print("✓ Memory erased")
            return True
        print("✗ Failed to erase memory")
        return False

    def request_download(self, address: int, size: int):
        """Step 5: Request Download (0x34)"""
        print("\n" + "=" * 70)
        print("STEP 5: Request Download")
        print("=" * 70)
        print(f"  Address: 0x{address:08X}")
        print(f"  Size: {size} bytes")

        # Format: [0x34, dataFormat, addrAndLenFormat, address[4], size[4]]
        payload = bytes([SID_REQUEST_DOWNLOAD, DATA_FORMAT_UNCOMPRESSED_UNENCRYPTED, ADDR_LEN_FORMAT_44]) + \
            address.to_bytes(4, 'big') + \
            size.to_bytes(4, 'big')

        response = self.send_request(payload, timeout=5.0)

        if response and response[0] == 0x74:
            max_block = (response[2] << 8) | response[3]
            # maxNumberOfBlockLength (ISO 14229-1) includes the TransferData SID
            # and blockSequenceCounter, so the usable data payload is 2 bytes smaller.
            max_data = max_block - 2
            print(f"✓ Download accepted (max block length: {max_block} bytes, "
                  f"data payload: {max_data} bytes)")
            return max_data
        print("✗ Download rejected")
        return 0

    def transfer_data(self, firmware_data: bytes, max_block_size: int):
        """Step 6: Transfer firmware data in blocks (0x36)"""
        print("\n" + "=" * 70)
        print("STEP 6: Transfer Data")
        print("=" * 70)
        print(f"  Total size: {len(firmware_data)} bytes")
        print(f"  Block size: {max_block_size} bytes")

        block_sequence = 1
        offset = 0

        while offset < len(firmware_data):
            remaining = len(firmware_data) - offset
            block_size = min(max_block_size, remaining)
            block_data = firmware_data[offset:offset + block_size]

            payload = bytes([SID_TRANSFER_DATA, block_sequence]) + block_data
            response = self.send_request(payload, timeout=5.0)

            if not response or response[0] != 0x76:
                print(f"✗ Failed to transfer block #{block_sequence}")
                return False

            print(f"  ✓ Block #{block_sequence}: {block_size} bytes "
                  f"({offset + block_size}/{len(firmware_data)})")

            # ISO 14229-1: block sequence counter wraps 0xFF -> 0x00 -> 0x01
            block_sequence = (block_sequence + 1) & 0xFF
            offset += block_size

        print("✓ All data transferred")
        return True

    def request_transfer_exit(self):
        """Step 7: Request Transfer Exit (0x37)"""
        print("\n" + "=" * 70)
        print("STEP 7: Request Transfer Exit")
        print("=" * 70)

        payload = bytes([SID_REQUEST_TRANSFER_EXIT])
        response = self.send_request(payload, timeout=5.0)

        if response and response[0] == 0x77:
            print("✓ Transfer complete")
            return True
        print("✗ Transfer exit failed")
        return False

    def ccu_reset(self):
        """Step 8: Reset module to activate new image (0x11)"""
        print("\n" + "=" * 70)
        print("STEP 8: CCU Reset (activation)")
        print("=" * 70)

        payload = bytes([SID_ECU_RESET, RESET_HARD])
        response = self.send_request(payload)

        if response and response[0] == 0x51:
            print("✓ Reset initiated")
            print("  Waiting 3 seconds for module to restart...")
            time.sleep(3)
            return True
        print("✗ Reset failed")
        return False


def generate_test_firmware(size: int) -> bytes:
    """Generate dummy firmware data for bench testing"""
    header = b"LIME_QI_FW_V1.0\x00"
    body = (b"QI_FIRMWARE_DATA" * ((size // 16) + 1))[:max(0, size - len(header))]
    return (header + body)[:size]


def main():
    parser = argparse.ArgumentParser(description="Qi Charger UDS Firmware Upgrade (Simple Security)")
    parser.add_argument("--bustype", default="canalystii", help="CAN interface type (socketcan/canalystii/pcan/virtual)")
    parser.add_argument("--channel", default="0", help="CAN channel (e.g. can0 for socketcan, 0 for canalystii)")
    parser.add_argument("--bitrate", type=int, default=250000, help="CAN bitrate (Qi bus is 250 kbps)")
    parser.add_argument("--device", type=int, default=0, help="Device index for multi-device setups")
    parser.add_argument("--firmware", default=None, help="Firmware binary file to upload (optional)")
    parser.add_argument("--size", type=int, default=98304, help="Test firmware size in bytes if --firmware not given")
    parser.add_argument("--type", choices=['app', 'bootloader'], default='app', help="Firmware type (default: app)")
    parser.add_argument("--address", type=lambda x: int(x, 0), default=0x08000000, help="Flash download address (default: 0x08000000)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose logging")

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s"
    )

    # Connect to CAN bus
    if args.bustype == "socketcan":
        bus = can.Bus(interface=args.bustype, channel=args.channel)
    else:
        bus = can.Bus(interface=args.bustype, channel=args.channel,
                      bitrate=args.bitrate, device=args.device)

    logging.info(f"Connected to CAN bus ({args.bustype}, channel={args.channel}, device={args.device})")

    client = QiFirmwareUpgradeClient(bus)

    try:
        print("\n" + "=" * 70)
        print("QI CHARGER UDS FIRMWARE UPGRADE (SIMPLE SECURITY)")
        print("=" * 70)
        print(f"Request ID:  0x{UDS_REQ_ID:08X}")
        print(f"Response ID: 0x{UDS_RESP_ID:08X}")

        # Step 0: Read current version and OTA state
        print("\n" + "=" * 70)
        print("STEP 0: Read Current Version and OTA State")
        print("=" * 70)
        version = client.read_software_version()
        print(f"Current software version (0xF195): {version if version else 'unknown'}")
        slot = client.read_did(DID_ACTIVE_FIRMWARE_SLOT)
        if slot:
            print(f"Active firmware slot (0x2113): {FW_SLOT_NAMES.get(slot[0], hex(slot[0]))}")
        ota = client.read_did(DID_OTA_STATUS)
        if ota:
            print(f"OTA status (0x2112): {OTA_STATUS_NAMES.get(ota[0], hex(ota[0]))}")

        # Prepare firmware data
        if args.firmware:
            with open(args.firmware, 'rb') as f:
                firmware_data = f.read()
            print(f"\nLoaded firmware from {args.firmware}: {len(firmware_data)} bytes")
        else:
            firmware_data = generate_test_firmware(args.size)
            print(f"\nGenerated dummy firmware: {len(firmware_data)} bytes")

        firmware_type = FIRMWARE_TYPE_APP if args.type == 'app' else FIRMWARE_TYPE_BOOTLOADER

        # OTA sequence
        if not client.switch_to_programming_session():
            print("\n✗ Firmware upgrade FAILED at step 1 (Programming Session)")
            return 1
        if not client.security_access():
            print("\n✗ Firmware upgrade FAILED at step 2 (Security Access)")
            return 1
        if not client.select_firmware_type(firmware_type):
            print("\n✗ Firmware upgrade FAILED at step 3 (Select Firmware Type)")
            return 1
        if not client.erase_memory():
            print("\n✗ Firmware upgrade FAILED at step 4 (Erase Memory)")
            return 1
        max_block_size = client.request_download(args.address, len(firmware_data))
        if not max_block_size:
            print("\n✗ Firmware upgrade FAILED at step 5 (Request Download)")
            return 1
        if not client.transfer_data(firmware_data, max_block_size):
            print("\n✗ Firmware upgrade FAILED at step 6 (Transfer Data)")
            return 1
        if not client.request_transfer_exit():
            print("\n✗ Firmware upgrade FAILED at step 7 (Transfer Exit)")
            return 1
        if not client.ccu_reset():
            print("\n✗ Firmware upgrade FAILED at step 8 (CCU Reset)")
            return 1

        # Step 9: Verify new version and OTA state
        print("\n" + "=" * 70)
        print("STEP 9: Verify New Version and OTA State")
        print("=" * 70)
        # Return to default session before reading version
        client.send_request(bytes([SID_DIAGNOSTIC_SESSION_CONTROL, SESSION_DEFAULT]))
        new_version = client.read_software_version()
        if new_version:
            print(f"✓ Software Version (0xF195): {new_version}")
        else:
            print("✗ Failed to read software version")
        slot = client.read_did(DID_ACTIVE_FIRMWARE_SLOT)
        if slot:
            print(f"✓ Active firmware slot (0x2113): {FW_SLOT_NAMES.get(slot[0], hex(slot[0]))}")
        ota = client.read_did(DID_OTA_STATUS)
        if ota:
            print(f"✓ OTA status (0x2112): {OTA_STATUS_NAMES.get(ota[0], hex(ota[0]))}")

        print("\n" + "=" * 70)
        print("✓ FIRMWARE UPGRADE COMPLETED SUCCESSFULLY!")
        print("=" * 70)
        return 0

    except KeyboardInterrupt:
        logging.info("Interrupted")
        return 1
    except Exception as e:
        logging.error(f"Error: {e}", exc_info=True)
        return 1
    finally:
        bus.shutdown()


if __name__ == "__main__":
    sys.exit(main())
