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
    decode_armv7_descriptor.py --tre --prrr=0x1f08022a --section 0xfc41040e
    decode_armv7_descriptor.py --tre --prrr=0x1f08022a --nmrr=0x01210121 --section 0x0001140e

**694 (2026-09-25) added `--tre`, `--prrr=`, `--nmrr=`, and the reason is a reading this tool got wrong
without saying so.** The table below is the ARMv7-A memory-attribute encoding for `TEX[2:0], C, B`
**with `SCTLR.TRE` clear**, and which of the two rules applies is a *property of the regime the
descriptor runs in*, not of the descriptor:

* the **payload's** own phase-1 table (`STAGE90_PMAP_DESC_SECTION_NORMAL_WB = 0x0001140e`, its page
  twin `0x0000045e`, `SO_ONLY = 0x00010c02`) runs with TRE **clear** - `xnu_entry_stub_sctlr_after =
  0x00c5487b`, bit 28 = 0 (548 §3) - so for those the table below *is* the rule, which is why the
  reference's "Normal, Write-back, write-allocate" is right about `0x0001140e`;
* a descriptor **XNU** runs under does not: with `xnu_live_sctlr = 0x30c5787d` TRE is **set**, and then
  those three bits are not a memory type at all - `i[1:0]` (C, B) and `i[2]`, which
  `ARM_TTE_BLOCK_ATTRINDX(i)` (`proc_reg.h:803`) writes into `ARM_TTE_BLOCK_TEX0SHIFT` = **bit 12**
  (so the index's bit 2 is the descriptor's **TEX[0]**, not TEX[2]), are an **index into `PRRR`**, and
  `PRRR`'s two-bit field for that index names the memory.

So the entry image's MMIO installs (`0x...1040e`, index 3) are the second kind, and read without
`--tre` this tool prints **`Reserved`** for them - true of the table, false of the mapping. Pass `--tre`
and, from the log, `--prrr=0x<xnu_live_prrr>`; add `--nmrr=0x<nmrr>` when the `PRRR` field is `0b10`
(the remap case) and you want the inner attribute named too.

**The two `PRRR` fields this tool is willing to name, and the evidence for each.** `PRRR_SETUP =
0x1F08022A` (`proc_reg.h:530`, the value this device installs) has `TR0..TR7 = 2,2,2,0,2,0,0,0`, and
`NMRR_SETUP = 0x01210121` (`:550`) has `IR0..IR7 = 1,0,2,0,1,0,0,0`:

| index | 0 | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- |
| Apple's name (`proc_reg.h:630-636`) | `WRITEBACK` | `WRITECOMB` | `WRITETHRU` | `DISABLE` | `INNERWRITEBACK` | `POSTED` |
| `NMRR` field | 1 | 0 | 2 | 0 | 1 | 0 |
| `NMRR` name (`:544-547`) | `WRITEBACK` | `DISABLED` | `WRITETHRU` | `DISABLED` | `WRITEBACK` | `DISABLED` |
| `PRRR` field | 2 | 2 | 2 | **0** | 2 | **0** |

`NMRR`'s field is exactly the inner attribute Apple's own name promises for each index - that is what
makes the table evidence rather than a lookup - and it is why **`0b00` is Strongly-ordered** (the field
sits on the two indices Apple calls `DISABLE`, and it is the field `entry_stubs.c` searches `PRRR` for
when it wants an uncached MMIO mapping) and why **`0b10` cannot itself name a type** (it sits on index 0,
write-back, *and* on index 1, write-combining) but must hand the attribute to `NMRR`'s field for the
same index. `0b01` and `0b11` are named by no index in any table this project uses, and are not guessed
here.
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

# PRRR's two-bit field, for the index (TEX[0], C, B) that selects it when SCTLR.TRE is set. Only two
# values are named, and the evidence is in the module docstring: 0b00 is the field the entry image's
# mapper searches for to get an uncached MMIO mapping, and 0b10 is the remap case, which names nothing
# by itself because it sits on both a write-back index and a write-combining one.
PRRR_FIELD = {
    0b00: "Strongly-ordered (the field the MMIO mapper searches PRRR for)",
    0b01: "unnamed here (no index this project uses has this field)",
    0b10: "Normal - the attribute comes from NMRR's field for the same index",
    0b11: "unnamed here (no index this project uses has this field)",
}

# NMRR's two-bit field, reached only when PRRR's field is 0b10. The names are the header's own
# (proc_reg.h:544-547).
NMRR_FIELD = {
    0b00: "Non-cacheable",
    0b01: "Write-Back, Write-Allocate",
    0b10: "Write-Through, no Write-Allocate",
    0b11: "Write-Back, no Write-Allocate",
}

# The value NMRR_SETUP installs, so --nmrr is only needed for a machine that differs.
NMRR_SETUP = 0x01210121

TRE = False
PRRR = None
NMRR = None

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


def attr_index(d, tex0_bitpos):
    """(TEX[0], C, B) - the PRRR/NMRR index when SCTLR.TRE is set."""
    return (bit(d, tex0_bitpos) << 2) | (bit(d, 3) << 1) | bit(d, 2)


def parse_num(raw):
    try:
        return int(raw.split("=", 1)[1], 0)
    except ValueError:
        raise SystemExit("%s needs a number" % raw.split("=", 1)[0])


def tre_rows(d, tex0_bitpos):
    """The rows a TRE-aware decode adds, and a note when --tre was not given."""
    if not TRE:
        return [("note", "SCTLR.TRE decides which rule names this attribute, and it is a property of "
                         "the regime the descriptor runs in: the payload's own phase-1 table runs with "
                         "TRE clear (548: xnu_entry_stub_sctlr_after = 0x00c5487b, bit 28 = 0) and the "
                         "table above is the rule; a descriptor XNU runs under does not "
                         "(xnu_live_sctlr = 0x30c5787d, bit 28 = 1) - for those pass --tre and "
                         "--prrr=0x<xnu_live_prrr>. The entry image's own MMIO installs (0x...1040e) "
                         "are the second kind, and without --tre this tool prints Reserved for them")]
    idx = attr_index(d, tex0_bitpos)
    rows = [("attr index (TRE)", "%d (TEX[0]=%d, C=%d, B=%d) = PRRR/NMRR bits [%d:%d]"
             % (idx, bit(d, tex0_bitpos), bit(d, 3), bit(d, 2), 2 * idx + 1, 2 * idx))]
    if PRRR is None:
        rows.append(("PRRR field", "unknown - pass --prrr=0x<xnu_live_prrr> to name it"))
        return rows
    field = (PRRR >> (2 * idx)) & 3
    rows.append(("PRRR field", "%s = %s" % (format(field, "02b"), PRRR_FIELD[field])))
    if field == 0b10 and NMRR is not None:
        inner = (NMRR >> (2 * idx)) & 3
        rows.append(("NMRR field", "%s = %s" % (format(inner, "02b"), NMRR_FIELD[inner])))
    return rows


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
    global TRE, PRRR, NMRR
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

        if raw == "--tre":
            TRE = True
            continue

        if raw.startswith("--prrr="):
            PRRR = parse_num(raw)
            TRE = True                        # a PRRR is only meaningful with TRE set
            continue

        if raw.startswith("--nmrr="):
            NMRR = parse_num(raw)
            TRE = True
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
        # TEX[0] is bit 12 in a section and bit 6 in a small page (Apple's ARM_TTE_BLOCK_TEX0SHIFT
        # and ARM_PTE_TEX0SHIFT), so it is passed in rather than assumed.
        fields = fields + tre_rows(d, 12 if mode == "--section" else 6)
        for name, value in fields:
            print("  %-24s %s" % (name, value))
        print()
        seen += 1

    if seen == 0:
        raise SystemExit("no descriptors given")
    return 0


if __name__ == "__main__":
    sys.exit(main())
