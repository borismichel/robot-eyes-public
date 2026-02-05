#!/usr/bin/env python3
"""
Sign DeskBuddy firmware with HMAC-SHA256

Usage:
    python sign_firmware.py <firmware.bin> <key_hex> [output.bin]

Arguments:
    firmware.bin  - Input firmware binary
    key_hex       - 32-byte signing key as hex string (64 characters)
    output.bin    - Output signed firmware (default: firmware_signed.bin)

Example:
    python sign_firmware.py firmware.bin 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

The signed firmware has the HMAC-SHA256 signature (32 bytes) appended to the end.
"""

import sys
import hmac
import hashlib
import os

def sign_firmware(input_path: str, key_hex: str, output_path: str = None) -> str:
    # Validate key
    if len(key_hex) != 64:
        raise ValueError(f"Key must be 64 hex characters (32 bytes), got {len(key_hex)}")

    try:
        key = bytes.fromhex(key_hex)
    except ValueError:
        raise ValueError("Key must be valid hexadecimal")

    # Read firmware
    with open(input_path, 'rb') as f:
        firmware = f.read()

    print(f"Firmware size: {len(firmware)} bytes")

    # Compute HMAC-SHA256
    signature = hmac.new(key, firmware, hashlib.sha256).digest()
    print(f"Signature: {signature.hex()}")

    # Determine output path
    if output_path is None:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_signed{ext}"

    # Write signed firmware
    with open(output_path, 'wb') as f:
        f.write(firmware)
        f.write(signature)

    total_size = len(firmware) + len(signature)
    print(f"Signed firmware: {output_path} ({total_size} bytes)")

    return output_path

def generate_key() -> str:
    """Generate a random 32-byte key"""
    key = os.urandom(32)
    return key.hex()

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nTo generate a new key:")
        print(f"  python {sys.argv[0]} --generate-key")
        sys.exit(1)

    if sys.argv[1] == '--generate-key':
        key = generate_key()
        print(f"Generated signing key: {key}")
        print("\nStore this key securely. Set it on device via API or compile it in.")
        sys.exit(0)

    if len(sys.argv) < 3:
        print("Error: Missing key argument")
        print(__doc__)
        sys.exit(1)

    input_path = sys.argv[1]
    key_hex = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else None

    try:
        sign_firmware(input_path, key_hex, output_path)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
