#!/usr/bin/env python3
"""Generate a deterministic non-proprietary 32-bit ARM Mach-O fixture.

The fixture is intentionally inert. It models public Mach-O segment/section
layout for Mi4 iOS6 Stage44 loader-preflight work, but it is not an Apple
kernelcache and it is never executed by the target payload.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

MH_MAGIC = 0xFEEDFACE
CPU_TYPE_ARM = 12
CPU_SUBTYPE_ARM_V7 = 9
MH_PRELOAD = 5
LC_SEGMENT = 1
LC_SYMTAB = 2
LC_UNIXTHREAD = 5

VM_BASE = 0x80008000
PAGE = 0x1000


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def fixed16(name: str) -> bytes:
    raw = name.encode("ascii")
    if len(raw) > 16:
        raise ValueError(f"name too long for Mach-O fixed16 field: {name}")
    return raw + b"\0" * (16 - len(raw))


class Segment:
    def __init__(self, name: str, vmaddr: int, vmsize: int, maxprot: int, initprot: int):
        self.name = name
        self.vmaddr = vmaddr
        self.vmsize = vmsize
        self.maxprot = maxprot
        self.initprot = initprot
        self.sections: list[dict[str, object]] = []
        self.fileoff = 0
        self.filesize = 0
        self.flags = 0

    def add_section(self, sectname: str, payload: bytes, align_pow2: int = 2, flags: int = 0) -> None:
        self.sections.append({
            "sectname": sectname,
            "payload": payload,
            "align": align_pow2,
            "flags": flags,
            "addr": 0,
            "offset": 0,
        })

    @property
    def cmdsize(self) -> int:
        return 56 + len(self.sections) * 68


def build_segments() -> list[Segment]:
    segments: list[Segment] = []

    text = Segment("__TEXT", VM_BASE, 0x2000, 0x5, 0x5)
    text.add_section("__text", b"ST44-TEXT-NOEXEC\0", align_pow2=4)
    segments.append(text)

    data = Segment("__DATA", VM_BASE + 0x2000, 0x2000, 0x3, 0x3)
    data.add_section("__const", b"ST44-DATA-CONST\0", align_pow2=4)
    segments.append(data)

    linkedit = Segment("__LINKEDIT", VM_BASE + 0x4000, 0x1000, 0x1, 0x1)
    segments.append(linkedit)

    prelink_text = Segment("__PRELINK_TEXT", VM_BASE + 0x5000, 0x1000, 0x5, 0x5)
    prelink_text.add_section("__text", b"ST44-PRELINK-TEXT-NOEXEC\0", align_pow2=4)
    segments.append(prelink_text)

    prelink_info = Segment("__PRELINK_INFO", VM_BASE + 0x6000, 0x2000, 0x1, 0x1)
    prelink_info.add_section(
        "__info",
        b'<stage44-prelink-info generated="true" proprietary="false" executable="false"/>\0',
        align_pow2=2,
    )
    prelink_info.add_section("__kernel", b"ST44-PRELINK-KERNEL-METADATA\0", align_pow2=2)
    prelink_info.add_section("__kexts", b"ST44-PRELINK-KEXTS-METADATA\0", align_pow2=2)
    segments.append(prelink_info)

    prelink_state = Segment("__PRELINK_STATE", VM_BASE + 0x8000, 0x1000, 0x1, 0x1)
    prelink_state.add_section("__kernel", b"ST44-PRELINK-STATE-KERNEL\0", align_pow2=2)
    prelink_state.add_section("__kexts", b"ST44-PRELINK-STATE-KEXTS\0", align_pow2=2)
    segments.append(prelink_state)

    return segments


def layout_segments(segments: list[Segment], sizeofcmds: int) -> int:
    payload_offset = align(28 + sizeofcmds, 0x10)
    cursor = payload_offset
    for segment in segments:
        segment_payload_start = cursor
        max_end = cursor
        for section in segment.sections:
            section_alignment = 1 << int(section["align"])
            cursor = align(cursor, section_alignment)
            offset_in_segment = cursor - segment_payload_start
            if offset_in_segment >= segment.vmsize:
                raise ValueError(f"section {segment.name},{section['sectname']} does not fit segment")
            section["offset"] = cursor
            section["addr"] = segment.vmaddr + offset_in_segment
            payload = section["payload"]
            if not isinstance(payload, bytes):
                raise TypeError("payload must be bytes")
            cursor += len(payload)
            max_end = max(max_end, cursor)
        if segment.sections:
            segment.fileoff = segment_payload_start
            segment.filesize = align(max_end - segment_payload_start, 0x10)
            cursor = segment_payload_start + segment.filesize
        else:
            segment.fileoff = cursor
            segment.filesize = 0
    return cursor


def pack_segment(segment: Segment) -> bytes:
    out = bytearray()
    out += struct.pack(
        "<II16sIIIIIIII",
        LC_SEGMENT,
        segment.cmdsize,
        fixed16(segment.name),
        segment.vmaddr,
        segment.vmsize,
        segment.fileoff,
        segment.filesize,
        segment.maxprot,
        segment.initprot,
        len(segment.sections),
        segment.flags,
    )
    for section in segment.sections:
        out += struct.pack(
            "<16s16sIIIIIIIII",
            fixed16(str(section["sectname"])),
            fixed16(segment.name),
            int(section["addr"]),
            len(section["payload"]),
            int(section["offset"]),
            int(section["align"]),
            0,
            0,
            int(section["flags"]),
            0,
            0,
        )
    return bytes(out)


def build_fixture() -> bytes:
    segments = build_segments()
    symtab_size = 24
    unixthread_size = 16
    sizeofcmds = sum(segment.cmdsize for segment in segments) + symtab_size + unixthread_size
    file_size = layout_segments(segments, sizeofcmds)

    ncmds = len(segments) + 2
    out = bytearray(file_size)
    offset = 0
    header = struct.pack(
        "<IIIIIII",
        MH_MAGIC,
        CPU_TYPE_ARM,
        CPU_SUBTYPE_ARM_V7,
        MH_PRELOAD,
        ncmds,
        sizeofcmds,
        0,
    )
    out[offset:offset + len(header)] = header
    offset += len(header)

    for segment in segments:
        command = pack_segment(segment)
        out[offset:offset + len(command)] = command
        offset += len(command)

    # LC_SYMTAB with zero symbols and a bounded empty string table at EOF.
    symtab = struct.pack("<IIIIII", LC_SYMTAB, symtab_size, 0, 0, file_size, 0)
    out[offset:offset + symtab_size] = symtab
    offset += symtab_size

    # Minimal LC_UNIXTHREAD-like marker: flavor, entry-looking PC. Not executable.
    unixthread = struct.pack("<IIII", LC_UNIXTHREAD, unixthread_size, 0, VM_BASE)
    out[offset:offset + unixthread_size] = unixthread
    offset += unixthread_size

    if offset != 28 + sizeofcmds:
        raise AssertionError("load-command size mismatch")

    for segment in segments:
        for section in segment.sections:
            payload = section["payload"]
            section_offset = int(section["offset"])
            if not isinstance(payload, bytes):
                raise TypeError("payload must be bytes")
            out[section_offset:section_offset + len(payload)] = payload

    return bytes(out)


def c_array(data: bytes, symbol_prefix: str) -> str:
    digest = hashlib.sha256(data).hexdigest()
    lines = [
        "/* Generated by tools/mkmacho_fixture.py.",
        " * Non-proprietary inert Mach-O fixture for Stage44 loader preflight.",
        " * Contains no Apple binary code and is never executed.",
        f" * sha256={digest}",
        " */",
        "#include \"stage44.h\"",
        "",
        f"const uint8_t {symbol_prefix}_embedded_macho[] __attribute__((aligned(4))) = {{",
    ]
    for base in range(0, len(data), 12):
        chunk = data[base:base + 12]
        rendered = ", ".join(f"0x{byte:02x}u" for byte in chunk)
        lines.append(f"    {rendered},")
    lines += [
        "};",
        "",
        f"const uint32_t {symbol_prefix}_embedded_macho_size = sizeof({symbol_prefix}_embedded_macho);",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-output", type=Path, required=True, help="generated C source path")
    parser.add_argument("--bin-output", type=Path, required=True, help="raw Mach-O fixture output path")
    parser.add_argument("--symbol-prefix", default="stage44", help="C symbol prefix")
    args = parser.parse_args()

    data = build_fixture()
    args.bin_output.parent.mkdir(parents=True, exist_ok=True)
    args.c_output.parent.mkdir(parents=True, exist_ok=True)
    args.bin_output.write_bytes(data)
    args.c_output.write_text(c_array(data, args.symbol_prefix))

    digest = hashlib.sha256(data).hexdigest()
    print(f"wrote {args.bin_output} size={len(data)} sha256={digest}")
    print(f"wrote {args.c_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
