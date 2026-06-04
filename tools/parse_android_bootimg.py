#!/usr/bin/env python3
"""Parse and optionally extract Android boot image v0/v1-style files.

This is intentionally small and dependency-free for local inspection of the
Xiaomi Mi 4 (`cancro`) boot/recovery backups. It does not modify the input image.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
from pathlib import Path

BOOT_MAGIC = b"ANDROID!"
HEADER_FORMAT = "<8s10I16s512s8I1024s"
HEADER_FIELDS = [
    "magic",
    "kernel_size",
    "kernel_addr",
    "ramdisk_size",
    "ramdisk_addr",
    "second_size",
    "second_addr",
    "tags_addr",
    "page_size",
    "dt_size",
    "unused",
    "name",
    "cmdline",
    "id0",
    "id1",
    "id2",
    "id3",
    "id4",
    "id5",
    "id6",
    "id7",
    "extra_cmdline",
]


def align(value: int, page_size: int) -> int:
    return ((value + page_size - 1) // page_size) * page_size


def c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_boot_image(path: Path, extract_dir: Path | None) -> None:
    data = path.read_bytes()
    header_size = struct.calcsize(HEADER_FORMAT)
    if len(data) < header_size:
        raise SystemExit(f"{path}: too small for Android boot image header")

    values = struct.unpack(HEADER_FORMAT, data[:header_size])
    header = dict(zip(HEADER_FIELDS, values))
    if header["magic"] != BOOT_MAGIC:
        raise SystemExit(f"{path}: bad magic {header['magic']!r}; expected {BOOT_MAGIC!r}")

    page_size = int(header["page_size"])
    if page_size <= 0:
        raise SystemExit(f"{path}: invalid page size {page_size}")

    kernel_off = page_size
    ramdisk_off = kernel_off + align(int(header["kernel_size"]), page_size)
    second_off = ramdisk_off + align(int(header["ramdisk_size"]), page_size)
    dt_off = second_off + align(int(header["second_size"]), page_size)

    board = c_string(header["name"])
    cmdline = c_string(header["cmdline"] + header["extra_cmdline"])

    print(f"=== {path} ===")
    print(f"magic={header['magic'].decode('ascii')}")
    print(f"page_size={page_size}")
    for key in [
        "kernel_size",
        "kernel_addr",
        "ramdisk_size",
        "ramdisk_addr",
        "second_size",
        "second_addr",
        "tags_addr",
        "dt_size",
    ]:
        value = int(header[key])
        print(f"{key}={value} (0x{value:x})")
    print(f"board={board}")
    print(f"cmdline={cmdline}")
    print(f"kernel_off=0x{kernel_off:x}")
    print(f"ramdisk_off=0x{ramdisk_off:x}")
    print(f"second_off=0x{second_off:x}")
    print(f"dt_off=0x{dt_off:x}")

    parts = [
        ("kernel", kernel_off, int(header["kernel_size"])),
        ("ramdisk.gz", ramdisk_off, int(header["ramdisk_size"])),
    ]
    if int(header["second_size"]):
        parts.append(("second", second_off, int(header["second_size"])))
    if int(header["dt_size"]):
        parts.append(("dt.img", dt_off, int(header["dt_size"])))

    for name, offset, size in parts:
        blob = data[offset : offset + size]
        if len(blob) != size:
            raise SystemExit(f"{path}: {name} truncated: expected {size}, got {len(blob)}")
        print(f"part={name} offset=0x{offset:x} size={size} sha256={sha256(blob)}")
        if extract_dir is not None:
            extract_dir.mkdir(parents=True, exist_ok=True)
            (extract_dir / name).write_bytes(blob)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Android boot/recovery image to parse")
    parser.add_argument(
        "--extract-dir",
        type=Path,
        help="Optional directory for extracted kernel/ramdisk/dt artifacts",
    )
    args = parser.parse_args()
    parse_boot_image(args.image, args.extract_dir)


if __name__ == "__main__":
    main()
