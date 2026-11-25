#!/usr/bin/env python3
"""Compute the MHI-AC-Ctrl device password derived from DEVICE_SECRET and the chip ID.

The firmware derives the password (when DEVICE_SECRET is set and no password was
set on the settings page or via DEVICE_PASSWORD) as
    HMAC-SHA256(key=DEVICE_SECRET, msg="MHI-AC-Ctrl:" + CHIP_ID)
with the first 10 bytes mapped to the alphabet below. CHIP_ID is the WiFi MAC
address of the board (12 hex digits, upper case). It is shown in the serial log,
on the AC status page and in your router's device list.

Usage:
    python tools/device_password.py 90:DA:72:74:52:90
    python tools/device_password.py 90DA72745290 --secret "my secret"

Without --secret, DEVICE_SECRET is read from secrets.ini in the project root.
"""
import argparse
import hashlib
import hmac
import pathlib
import re
import sys

CHARS = "abcdefghjkmnpqrstuvwxyz23456789"   # must match PW_CHARS in src/wifi_config.cpp
LENGTH = 10


def secret_from_ini(path: pathlib.Path) -> str:
    if not path.exists():
        sys.exit(f"{path} not found; pass --secret")
    m = re.search(r'-DDEVICE_SECRET=\\?"(.*?)\\?"', path.read_text(encoding="utf-8"))
    if not m:
        sys.exit(f"no DEVICE_SECRET in {path}; pass --secret")
    return m.group(1)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("chip_id", help="WiFi MAC address of the board, with or without separators")
    ap.add_argument("--secret", help="DEVICE_SECRET (default: read from secrets.ini)")
    args = ap.parse_args()

    chip_id = re.sub(r"[^0-9A-Fa-f]", "", args.chip_id).upper()
    if len(chip_id) != 12:
        sys.exit("chip ID must be 12 hex digits (the WiFi MAC address)")
    secret = args.secret
    if secret is None:
        secret = secret_from_ini(pathlib.Path(__file__).resolve().parent.parent / "secrets.ini")

    digest = hmac.new(secret.encode(), f"MHI-AC-Ctrl:{chip_id}".encode(), hashlib.sha256).digest()
    print("".join(CHARS[b % len(CHARS)] for b in digest[:LENGTH]))


if __name__ == "__main__":
    main()
