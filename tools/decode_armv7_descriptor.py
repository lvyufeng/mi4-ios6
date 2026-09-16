#!/usr/bin/env python3
"""Decode ARMv7-A short-descriptor page table entries.

Field positions follow Apple's own ARM pmap, external/xnu-4570.1.46/osfmk/arm/proc_reg.h
(ARM_PTE_*), which is the reference this project builds against. Cross-checking a
descriptor against a second, independent decoder is worth the twenty lines: the
project's own headers treat the low 20 bits as one opaque ATTR_MASK, so a wrong
hand-decode would sit in the documentation and be believed.

Usage:
    decode_armv7_descriptor.py 0x00010c02 [more descriptors...]
    decode_armv7_descriptor.py --section 0x00010c02
    decode_armv7_descriptor.py --smallpage 0x12
"""

import sys

# TEX[2:0], C, B -> memory type (ARMv7-A short-descriptor memory attribute encoding)
MEMORY_ATTRS = {
    (0b000, 0, 0): "Strongly-ordered",
    (0b000, 0, 1): "Device",
    (0b000, 1, 0): "Normal, Non-cacheable",
    (0b000, 1, 1): "Reserved",
    (0b001, 0, 0): "Normal, Non-cacheable",
    (0b001, 0, 1): "Reserved",
    (0b001, 1, 0): "Normal, Write-through",
    (0b001, 1, 1): "Normal, Write-back, write-allocate",
    (0b010, 0, 0): "Device, non-shareable",
    (0b010, 0, 1): "Reserved",
}

# Access permission encodings, indexed by (AP[2], AP[1:0]).
AP_SECTION = {
    (0, 0b00): "no access (fault)",
    (0, 0b01): "PL1 RW, PL0 no access",
    (0, 0b10): "PL1 RW, PL0 RO",
    (0, 0b11): "PL1 RW, PL0 RW (full access)",
    (1, 0b00): "reserved",
    (1, 0b01): "PL1 RO, PL0 no access",
    (1, 0b10): "PL1 RO, PL0 RO",
    (1, 0b11): "PL1 RO, PL0 RO",
}

# On a small page, bit4 is AP[0] in the classic layout but the Access Flag (AF) in
# the ARMv7 layout XNU uses (ARM_PTE_AF == ARM_PTE_AP0), so AP is only AP[1:0].
AP_SMALLPAGE = {
    (0, 0b00): "PL1 RW, PL0 no access",
    (0, 0b01): "PL1 RW, PL0 RO",
    (0, 0b10): "PL1 RO, PL0 no access",
    (0, 0b11): "PL1 RO, PL0 RO",
}


def bit(v, n):
    return (v >> n) & 1


def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def memtype(tex, c, b):
    key = (tex, c, b)
    if key in MEMORY_ATTRS:
        return MEMORY_ATTRS[key]
    if tex & 0b100:
        return "Normal, cached via PRRR/NMRR indirection (TEX[0],C,B is the index)"
    return "unknown"


def decode_section(d):
    tex = bits(d, 14, 12)
    c, b = bit(d, 3), bit(d, 2)
    ap = (bit(d, 15), bits(d, 11, 10))
    return [
        ("type", "section (1MB)" if bits(d, 1, 0) == 0b10 else "NOT A SECTION"),
        ("supersection", "yes (16MB)" if bit(d, 18) else "no"),
        ("PA base", hex(d & 0xFFF00000)),
        ("XN (bit 4)", "1 (execute-never)" if bit(d, 4) else "0 (executable)"),
        ("AP[2] (bit 15)", bit(d, 15)),
        ("AP[1:0] (bits 11:10)", format(bits(d, 11, 10), "02b")),
        ("AP meaning", AP_SECTION.get(ap, "reserved")),
        ("TEX[2:0] (bits 14:12)", format(tex, "03b")),
        ("C (bit 3)", c),
        ("B (bit 2)", b),
        ("memory type", memtype(tex, c, b)),
        ("S (bit 16)", "%d%s" % (bit(d, 16), "" if bit(d, 16) else "")),
        ("shareable", "shareable" if bit(d, 16) else "non-shareable"),
        ("nG (bit 17)", "not global" if bit(d, 17) else "global"),
        ("domain (bits 8:5)", bits(d, 8, 5)),
    ]


def decode_smallpage(d):
    tex = bits(d, 8, 6)
    c, b = bit(d, 3), bit(d, 2)
    return [
        ("type", "small page (4KB)" if bit(d, 1) else "NOT A SMALL PAGE"),
        ("PA base", hex(d & 0xFFFFF000)),
        ("XN (bit 0)", "1 (execute-never)" if bit(d, 0) else "0 (executable)"),
        ("AF (bit 4)", "1 (access flag set)" if bit(d, 4) else "0 (access flag clear)"),
        ("AP[1] (bit 9)", bit(d, 9)),
        ("AP[0] (bit 5)", bit(d, 5)),
        ("AP meaning", AP_SMALLPAGE.get((bit(d, 9), bit(d, 5)), "reserved")),
        ("TEX[2:0] (bits 8:6)", format(tex, "03b")),
        ("C (bit 3)", c),
        ("B (bit 2)", b),
        ("memory type", memtype(tex, c, b)),
        ("shareable", "shareable" if bit(d, 10) else "non-shareable (S=0)"),
        ("nG (bit 11)", "not global" if bit(d, 11) else "global"),
    ]


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2

    # Mode is sticky and per-descriptor, so a mixed invocation works:
    #   --section 0x10c02 --smallpage 0x12
    mode = None
    seen = 0

    for raw in args:
        if raw in ("--section", "--smallpage"):
            mode = raw
            continue

        if mode is None:
            raise SystemExit(
                "ambiguous descriptor %s: pass --section or --smallpage first "
                "(both formats have bit 1 set)" % raw
            )

        try:
            d = int(raw, 0)
        except ValueError:
            raise SystemExit("not a number: %s" % raw)

        kind = "section" if mode == "--section" else "small page"
        print("descriptor %s (%s)" % (raw, kind))
        fields = decode_section(d) if mode == "--section" else decode_smallpage(d)
        for name, value in fields:
            print("  %-24s %s" % (name, value))
        print()
        seen += 1

    if seen == 0:
        raise SystemExit("no descriptors given")
    return 0


if __name__ == "__main__":
    sys.exit(main())
