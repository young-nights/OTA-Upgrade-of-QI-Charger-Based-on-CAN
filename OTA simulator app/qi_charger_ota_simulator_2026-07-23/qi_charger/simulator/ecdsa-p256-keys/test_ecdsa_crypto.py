#!/usr/bin/env python3
"""
Test ECDSA P-256 Cryptographic Operations (Qi Charger)

This script demonstrates ECDSA P-256 signature generation and verification
without requiring CAN hardware. Useful for validating the crypto and the
SecurityAccess flow used by qi_upgrade_client_ecdsa.py before connecting to a
real bus.

Wire formats (see common/ecdsa_p256.py):
  - Signature:  raw R||S, 64 bytes (IEEE P1363)
  - Public key: uncompressed SEC1 point, 65 bytes (0x04||X||Y)
  - Digest:     SHA-256
"""

import sys
import secrets
from pathlib import Path

# Add project root to path to import common modules
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

try:
    from common import ecdsa_p256
except ImportError:
    print("✗ Error: `cryptography` is not installed!")
    print("\nInstall with:")
    print("  pip3 install cryptography")
    sys.exit(1)


def test_keypair_generation():
    """Test ECDSA P-256 keypair generation"""
    print("\n" + "=" * 70)
    print("Test 1: ECDSA P-256 Keypair Generation")
    print("=" * 70)

    # Generate keypair
    private_key, private_scalar, public_key = ecdsa_p256.generate_keypair()

    print(f"✓ Generated ECDSA P-256 keypair")
    print(f"  Private scalar: {private_scalar.hex()}")
    print(f"  Public Key:     {public_key.hex()}")
    print(f"  Key sizes:")
    print(f"    - Private scalar: {len(private_scalar)} bytes")
    print(f"    - Public key:     {len(public_key)} bytes (uncompressed)")

    return private_key


def test_signature_generation(private_key):
    """Test signature generation"""
    print("\n" + "=" * 70)
    print("Test 2: ECDSA P-256 Signature Generation")
    print("=" * 70)

    # Generate random message (simulating UDS seed)
    message = secrets.token_bytes(32)
    print(f"Message (seed): {message.hex()}")

    # Sign the message
    signature = ecdsa_p256.sign(private_key, message)

    print(f"✓ Generated signature")
    print(f"  Signature (raw R||S): {signature.hex()}")
    print(f"  Length: {len(signature)} bytes")

    return message, signature


def test_signature_verification(private_key, message, signature):
    """Test signature verification"""
    print("\n" + "=" * 70)
    print("Test 3: ECDSA P-256 Signature Verification")
    print("=" * 70)

    if ecdsa_p256.verify(private_key.public_key(), message, signature):
        print("✓ Signature verified successfully!")
        return True
    print("✗ Signature verification failed!")
    return False


def test_tampered_signature(private_key, message, signature):
    """Test verification with tampered signature"""
    print("\n" + "=" * 70)
    print("Test 4: Tampered Signature Detection")
    print("=" * 70)

    # Tamper with signature
    tampered_signature = bytearray(signature)
    tampered_signature[0] ^= 0xFF  # Flip bits in first byte
    tampered_signature = bytes(tampered_signature)

    print(f"Original signature:  {signature.hex()}")
    print(f"Tampered signature:  {tampered_signature.hex()}")

    if ecdsa_p256.verify(private_key.public_key(), message, tampered_signature):
        print("✗ Tampered signature incorrectly verified!")
        return False
    print("✓ Tampered signature correctly rejected!")
    return True


def test_tampered_message(private_key, message, signature):
    """Test verification with tampered message"""
    print("\n" + "=" * 70)
    print("Test 5: Tampered Message Detection")
    print("=" * 70)

    # Tamper with message
    tampered_message = bytearray(message)
    tampered_message[0] ^= 0xFF  # Flip bits in first byte
    tampered_message = bytes(tampered_message)

    print(f"Original message:  {message.hex()}")
    print(f"Tampered message:  {tampered_message.hex()}")

    if ecdsa_p256.verify(private_key.public_key(), tampered_message, signature):
        print("✗ Tampered message incorrectly verified!")
        return False
    print("✓ Tampered message correctly rejected!")
    return True


def test_performance():
    """Test ECDSA P-256 performance"""
    print("\n" + "=" * 70)
    print("Test 6: Performance Benchmark")
    print("=" * 70)

    import time

    # Generate keypair
    private_key, _, _ = ecdsa_p256.generate_keypair()
    public_key = private_key.public_key()

    # Benchmark signature generation
    message = secrets.token_bytes(32)
    iterations = 1000

    start = time.time()
    for _ in range(iterations):
        signature = ecdsa_p256.sign(private_key, message)
    sign_duration = time.time() - start

    # Benchmark signature verification
    start = time.time()
    for _ in range(iterations):
        ecdsa_p256.verify(public_key, message, signature)
    verify_duration = time.time() - start

    print(f"Signature generation: {iterations} operations in {sign_duration:.3f}s")
    print(f"  Average: {(sign_duration / iterations) * 1000:.3f}ms per signature")
    print(f"  Rate: {iterations / sign_duration:.0f} signatures/sec")
    print()
    print(f"Signature verification: {iterations} operations in {verify_duration:.3f}s")
    print(f"  Average: {(verify_duration / iterations) * 1000:.3f}ms per verification")
    print(f"  Rate: {iterations / verify_duration:.0f} verifications/sec")


def test_uds_simulation():
    """Simulate UDS security access flow"""
    print("\n" + "=" * 70)
    print("Test 7: UDS Security Access Simulation")
    print("=" * 70)

    # Qi charger generates keypair (done at manufacturing)
    print("\n[Manufacturing] Qi charger generates ECDSA P-256 keypair...")
    qi_private_key, _, qi_public_key = ecdsa_p256.generate_keypair()

    print(f"  Qi charger Public Key: {qi_public_key.hex()}")
    print(f"  → Public key exported to CCU")

    # CCU stores Qi charger's public key
    ccu_stored_qi_public_key = qi_public_key
    print(f"\n[CCU] Stored Qi charger public key: {ccu_stored_qi_public_key.hex()}")

    # CCU requests seed (simulated UDS 0x27 0x01)
    print("\n[CCU → Qi charger] SecurityAccess RequestSeed (0x27 0x01)")

    # Qi charger generates random seed
    seed = secrets.token_bytes(32)
    print(f"[Qi charger → CCU] Seed: {seed.hex()}")

    # Qi charger signs the seed with its private key
    print(f"\n[Qi charger] Signing seed with private key...")
    signature = ecdsa_p256.sign(qi_private_key, seed)
    print(f"  Signature: {signature.hex()}")

    print(f"\n[CCU → Qi charger] SecurityAccess SendKey (0x27 0x02) + signature")
    print(f"  Signature: {signature.hex()}")

    # Verify signature with the stored public key
    print(f"\n[Qi charger] Verifying signature...")
    if ecdsa_p256.verify(ccu_stored_qi_public_key, seed, signature):
        print(f"  ✓ Signature valid - Security UNLOCKED")
        return True
    print(f"  ✗ Signature invalid - Security DENIED")
    return False


def main():
    print("=" * 70)
    print("ECDSA P-256 Cryptographic Operations Test Suite (Qi Charger)")
    print("=" * 70)
    print("\nECDSA P-256 is a public-key signature system with:")
    print("  • 128-bit security level")
    print("  • Signs a SHA-256 digest of the message")
    print("  • Raw signature size (R||S): 64 bytes")
    print("  • Uncompressed public key size: 65 bytes")
    print("  • Non-deterministic signatures (unless RFC 6979 is used)")

    all_tests_passed = True

    try:
        # Test 1: Keypair generation
        private_key = test_keypair_generation()

        # Test 2: Signature generation
        message, signature = test_signature_generation(private_key)

        # Test 3: Signature verification
        if not test_signature_verification(private_key, message, signature):
            all_tests_passed = False

        # Test 4: Tampered signature detection
        if not test_tampered_signature(private_key, message, signature):
            all_tests_passed = False

        # Test 5: Tampered message detection
        if not test_tampered_message(private_key, message, signature):
            all_tests_passed = False

        # Test 6: Performance
        test_performance()

        # Test 7: UDS simulation
        if not test_uds_simulation():
            all_tests_passed = False

        # Final result
        print("\n" + "=" * 70)
        if all_tests_passed:
            print("✓ ALL TESTS PASSED!")
        else:
            print("✗ SOME TESTS FAILED!")
        print("=" * 70)

        return 0 if all_tests_passed else 1

    except Exception as e:
        print(f"\n✗ Test suite failed with error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
