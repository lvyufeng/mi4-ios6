#!/usr/bin/env python3
"""Patch only the kernel command line of an Android boot image v0/v1.

This is a surgical editor: it overwrites the 512-byte `cmdline` header field
(and, if needed, the 1024-byte `extra_cmdline` field) and leaves every other
byte of the image identical. Kernel, ramdisk, second stage, and any appended
Qualcomm device tree (QCDT) are preserved byte-for-byte.

The boot image `id` (SHA over kernel/ramdisk/second sizes) does not cover the
command line in standard mkbootimg, so it remains valid after a cmdline-only
change.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

BOOT_MAGIC = b"ANDROID!"
MAGIC_OFF = 0
PAGE_SIZE_OFF = 36
CMDLINE_OFF = 64
CMDLINE_LEN = 512
ID_OFF = 576
EXTRA_CMDLINE_OFF = 608
EXTRA_CMDLINE_LEN = 1024


def read_cmdline(data: bytes) -> str:
    main = data[CMDLINE_OFF : CMDLINE_OFF + CMDLINE_LEN].split(b"\0", 1)[0]
    extra = data[EXTRA_CMDLINE_OFF : EXTRA_CMDLINE_OFF + EXTRA_CMDLINE_LEN].split(b"\0", 1)[0]
    return (main + extra).decode("ascii", "replace")


def write_cmdline(data: bytearray, cmdline: str) -> None:
    raw = cmdline.encode("ascii")
    if len(raw) > CMDLINE_LEN + EXTRA_CMDLINE_LEN:
        raise SystemExit(
            f"new cmdline is {len(raw)} bytes; max is {CMDLINE_LEN + EXTRA_CMDLINE_LEN}"
        )
    main = raw[:CMDLINE_LEN]
    extra = raw[CMDLINE_LEN:]
    data[CMDLINE_OFF : CMDLINE_OFF + CMDLINE_LEN] = main.ljust(CMDLINE_LEN, b"\0")
    data[EXTRA_CMDLINE_OFF : EXTRA_CMDLINE_OFF + EXTRA_CMDLINE_LEN] = extra.ljust(
        EXTRA_CMDLINE_LEN, b"\0"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="source boot image")
    parser.add_argument("output", type=Path, help="patched boot image to write")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--cmdline", help="full replacement command line")
    group.add_argument(
        "--replace",
        nargs=2,
        metavar=("OLD", "NEW"),
        help="replace a substring within the existing command line",
    )
    args = parser.parse_args()

    data = bytearray(args.input.read_bytes())
    if data[MAGIC_OFF : MAGIC_OFF + 8] != BOOT_MAGIC:
        raise SystemExit(f"{args.input}: bad magic; not an Android boot image")

    old_cmdline = read_cmdline(data)
    if args.cmdline is not None:
        new_cmdline = args.cmdline
    else:
        old_sub, new_sub = args.replace
        if old_sub not in old_cmdline:
            raise SystemExit(f"substring not found in cmdline: {old_sub!r}")
        new_cmdline = old_cmdline.replace(old_sub, new_sub)

    write_cmdline(data, new_cmdline)
    args.output.write_bytes(bytes(data))

    print(f"input        = {args.input}")
    print(f"output       = {args.output}")
    print(f"old cmdline  = {old_cmdline}")
    print(f"new cmdline  = {new_cmdline}")
    print(f"input size   = {args.input.stat().st_size}")
    print(f"output size  = {args.output.stat().st_size}")


if __name__ == "__main__":
    main()
