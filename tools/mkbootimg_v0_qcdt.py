#!/usr/bin/env python3
"""Create a legacy Android boot image v0 with optional QCDT `dt_size` field.

Ubuntu's modern `mkbootimg --dtb` does not populate the old v0 `dt_size`
field used by legacy Qualcomm bootloaders. Cancro's bootloader rejects images
without that field/table (`remote: dtb not found`). This small packer creates
the legacy header layout used by the backed-up boot.img.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

BOOT_MAGIC = b"ANDROID!"
CMDLINE_LEN = 512
EXTRA_CMDLINE_LEN = 1024
NAME_LEN = 16
HEADER_FORMAT = "<8s10I16s512s8I1024s"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)


def align(value: int, page_size: int) -> int:
    return ((value + page_size - 1) // page_size) * page_size


def pad(blob: bytes, page_size: int) -> bytes:
    return blob + b"\0" * (align(len(blob), page_size) - len(blob))


def split_cmdline(cmdline: str) -> tuple[bytes, bytes]:
    raw = cmdline.encode("ascii")
    if len(raw) > CMDLINE_LEN + EXTRA_CMDLINE_LEN:
        raise SystemExit(f"cmdline is {len(raw)} bytes; max is {CMDLINE_LEN + EXTRA_CMDLINE_LEN}")
    main = raw[:CMDLINE_LEN].ljust(CMDLINE_LEN, b"\0")
    extra = raw[CMDLINE_LEN:].ljust(EXTRA_CMDLINE_LEN, b"\0")
    return main, extra


def boot_id(kernel: bytes, ramdisk: bytes, second: bytes) -> tuple[int, ...]:
    h = hashlib.sha1()
    for blob in (kernel, ramdisk, second):
        h.update(blob)
        h.update(struct.pack("<I", len(blob)))
    digest = h.digest() + b"\0" * 12
    return struct.unpack("<8I", digest[:32])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--ramdisk", type=Path)
    parser.add_argument("--second", type=Path)
    parser.add_argument("--dt", type=Path, help="legacy QCDT/dt.img blob for v0 dt_size field")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cmdline", default="")
    parser.add_argument("--board", default="")
    parser.add_argument("--base", default="0x00000000")
    parser.add_argument("--kernel_offset", default="0x00008000")
    parser.add_argument("--ramdisk_offset", default="0x02000000")
    parser.add_argument("--second_offset", default="0x00f00000")
    parser.add_argument("--tags_offset", default="0x01e00000")
    parser.add_argument("--pagesize", default="2048")
    args = parser.parse_args()

    base = int(args.base, 0)
    page_size = int(args.pagesize, 0)
    kernel = args.kernel.read_bytes()
    ramdisk = args.ramdisk.read_bytes() if args.ramdisk else b""
    second = args.second.read_bytes() if args.second else b""
    dt = args.dt.read_bytes() if args.dt else b""

    board = args.board.encode("ascii")
    if len(board) > NAME_LEN:
        raise SystemExit(f"board name is {len(board)} bytes; max {NAME_LEN}")
    board = board.ljust(NAME_LEN, b"\0")
    cmdline, extra_cmdline = split_cmdline(args.cmdline)
    image_id = boot_id(kernel, ramdisk, second)

    header = struct.pack(
        HEADER_FORMAT,
        BOOT_MAGIC,
        len(kernel),
        base + int(args.kernel_offset, 0),
        len(ramdisk),
        base + int(args.ramdisk_offset, 0),
        len(second),
        base + int(args.second_offset, 0),
        base + int(args.tags_offset, 0),
        page_size,
        len(dt),
        0,
        board,
        cmdline,
        *image_id,
        extra_cmdline,
    )
    if len(header) > page_size:
        raise SystemExit(f"header {len(header)} bytes does not fit page size {page_size}")

    image = b"".join(
        [
            pad(header, page_size),
            pad(kernel, page_size),
            pad(ramdisk, page_size),
            pad(second, page_size),
            pad(dt, page_size),
        ]
    )
    args.output.write_bytes(image)
    print(f"wrote {args.output}")
    print(f"kernel_size={len(kernel)} ramdisk_size={len(ramdisk)} second_size={len(second)} dt_size={len(dt)} page_size={page_size}")
    print(f"sha256={hashlib.sha256(image).hexdigest()}")


if __name__ == "__main__":
    main()
