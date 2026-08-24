#!/usr/bin/env python3
"""
Generate ECDSA P-256 Keypair for the Qi Charger OTA Client

This script generates an ECDSA P-256 keypair used for Qi charger firmware upgrade
authentication (SecurityAccess Level 1, per SRS v1.1 §2.3 / REF-5 FIPS 186-4 /
REF-1 §7.4).

Usage:
    python3 generate_keypair.py [--output-dir DIR]

Output files:
    - qi_ecdsa_p256_keypair.bin         # 32-byte private scalar + 65-byte public key (97 bytes)
    - qi_ecdsa_p256_keypair_public.bin  # Public key only (65 bytes, uncompressed, provisioned into the module)

The CCU/OTA client signs the challenge seed with the private scalar; the Qi
charger module verifies the signature with the provisioned public key. The two
MUST come from the same keypair, otherwise SecurityAccess fails with NRC 0x35.
"""

import argparse
import os
import sys
from pathlib import Path

# Add project root to path to import common modules
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

try:
    from common import ecdsa_p256
except ImportError:
    print("ERROR: `cryptography` not installed!")
    print()
    print("Install with:")
    print("  pip3 install cryptography")
    sys.exit(1)


def generate_keypair(output_dir: str = "."):
    """Generate ECDSA P-256 keypair and save to files"""

    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)

    # Generate keypair
    print("Generating ECDSA P-256 keypair...")
    _, private_scalar, public_key = ecdsa_p256.generate_keypair()

    # File paths
    keypair_file = os.path.join(output_dir, "qi_ecdsa_p256_keypair.bin")
    public_key_file = os.path.join(output_dir, "qi_ecdsa_p256_keypair_public.bin")

    # Save private keypair (scalar + public key)
    with open(keypair_file, 'wb') as f:
        f.write(private_scalar)   # 32-byte private scalar
        f.write(public_key)       # 65-byte uncompressed public key

    print(f"✓ Private keypair saved to: {keypair_file}")
    print(f"  Size: 97 bytes (32-byte private scalar + 65-byte public key)")

    # Save public key separately
    with open(public_key_file, 'wb') as f:
        f.write(public_key)

    print(f"✓ Public key saved to: {public_key_file}")
    print(f"  Size: 65 bytes (uncompressed 0x04||X||Y)")

    # Key information
    print()
    print("=" * 70)
    print("Keypair Information")
    print("=" * 70)
    print(f"Public Key (hex): {public_key.hex()}")
    print()

    # Security notice
    print("=" * 70)
    print("SECURITY NOTICE")
    print("=" * 70)
    print()
    print("⚠️  Keep qi_ecdsa_p256_keypair.bin PRIVATE!")
    print("   This file contains the private scalar.")
    print()
    print("✓  qi_ecdsa_p256_keypair_public.bin can be shared / provisioned into the module.")
    print()

    # Usage instructions
    print("=" * 70)
    print("Usage Instructions")
    print("=" * 70)
    print()
    print("Run the ECDSA P-256 OTA client with this keypair:")
    print("   python3 ../qi_upgrade_client_ecdsa.py \\")
    print("       --keypair ecdsa-p256-keys/qi_ecdsa_p256_keypair.bin")
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Generate ECDSA P-256 keypair for the Qi charger OTA client"
    )
    parser.add_argument(
        "--output-dir",
        default=".",
        help="Output directory for keypair files (default: current directory)"
    )

    args = parser.parse_args()

    try:
        generate_keypair(args.output_dir)
        print("✓ Keypair generation completed successfully!")
        return 0
    except Exception as e:
        print(f"✗ Error generating keypair: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
