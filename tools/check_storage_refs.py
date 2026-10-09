#!/usr/bin/env python3
"""Fail if a payload can address an MSM8974 storage controller.

Why this exists alongside the symbol tripwire
---------------------------------------------
`preflight_boot_check.sh` greps the payload's symbol table for storage names (`sdcc`,
`emmc`, `mmc`, `ufs`, `partition`, ...). That catches a *named* storage reference and is
blind to an unnamed one — and an unnamed one is the plausible case here. The payload
already writes raw literals to `0xfc4ab000` (PS_HOLD) and `0x0fa00000` (IMEM), so a store to
a storage register written the same way would look exactly like the code it already has.

What makes an address check possible
------------------------------------
On MSM8974 the whole `0xf9800000`-`0xf98fffff` megabyte contains **only** storage
controllers: `sdcc@f9824000` (eMMC), `sdcc@f98a4000` (SD), `sdcc@f9864000`, `sdcc@f98e4000`,
and their `sdhci@...4900` companions. Against
`external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi`: eight nodes in that
range, all storage, no ambiguity to resolve.

Two checks, because a 32-bit address reaches the instruction stream in two ways:

1. **`movt rN, #imm` with imm in [`0xf980`, `0xf98f`]** — how the compiler materialises an
   address. Confirmed on this payload: the GIC base appears as `movt r3, #63744` (`0xf900`)
   and PS_HOLD as `movt ..., #64586` (`0xfc4a`). So the high half of any storage address is
   a `movt` immediate in a 16-value window, which is exact and cheap.
2. **Literal-pool words** — a full 32-bit constant in `.text`/`.rodata` contents. A value
   loaded via `ldr rN, [pc, #k]` never appears as an immediate at all, so check 1 alone
   would miss it.

What this cannot catch, stated so the guarantee is not overstated
----------------------------------------------------------------
An address **computed** at runtime from a base register would evade both checks. The real
guarantee is not this tool: it is that neither the identity table nor the candidate L1 maps
`0xf9` beyond `0xf90fffff`, so a stray store to a storage controller takes a data abort —
which the payload's handler logs and skips — rather than reaching hardware. This check is the
early warning that someone has started naming, mapping or hard-coding storage; the mapping is
what actually prevents the write.

Usage:
    check_storage_refs.py <elf> [--objdump PATH]
    check_storage_refs.py --selftest
Exit 0 clean, 1 a reference was found, 2 a tool problem.
"""

import argparse
import re
import struct
import subprocess
import sys

STORAGE_LO = 0xF9800000
STORAGE_HI = 0xF98FFFFF
MOVT_LO, MOVT_HI = STORAGE_LO >> 16, STORAGE_HI >> 16   # 0xf980 .. 0xf98f

MOVT_RE = re.compile(r"\bmovt\s+r\d+,\s*#(0x[0-9a-fA-F]+|\d+)\b")
HEXDUMP_RE = re.compile(r"^\s*[0-9a-f]+\s+((?:[0-9a-f]{2,8}\s+){1,4})")
# The entry-image blob the payload embeds (`stage90_xnu_entry_blob`, 953's `xnu_arm_entry.bin`).
# `.text` opens at 0x8000 and the blob sits in the middle of it, so a word at file offset F is at
# vaddr F + 0x8000 while F is still below the blob's file offset - the two spaces coincide, which
# is why the exclusion can name the blob by its symbol vaddr directly.
BLOB_RE = re.compile(r"(?:^|\s)([0-9a-f]{8})\s+([0-9a-f]{8})\s+[A-Za-z]\s+stage90_xnu_entry_blob\s*$")


def find_blob(elf, nm):
    """The [lo, hi) vaddr range of `stage90_xnu_entry_blob`, or None.

    None (no such symbol) means "no exclusion" - a 4570 image, or an image built before the blob
    existed, has none and scans exactly as it did before this was added. `nm -S` carries the size on
    the symbol (`00080a44 00605c54 T stage90_xnu_entry_blob`), so the range is address..address+size,
    which is the entry image the payload embeds. A plain `nm` has no size column; `-S` is required.
    """
    for line in run([nm, "-S", elf]).splitlines():
        m = BLOB_RE.search(line)
        if m:
            addr, size = int(m.group(1), 16), int(m.group(2), 16)
            return (addr, addr + size)
    return None


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        sys.exit("check_storage_refs: %s not found" % cmd[0])
    except subprocess.CalledProcessError as exc:
        sys.exit("check_storage_refs: %s failed: %s" % (" ".join(cmd), exc))


def check_movt(disasm):
    """High halves of addresses, which is how the compiler builds a 32-bit constant.

    Returns (vaddr, value, line). objdump's disassembly line opens with the vaddr, so a `movt`
    emitted from inside the embedded blob is identifiable and excludable the same way a word is.
    """
    hits = []
    for line in disasm.splitlines():
        a = re.match(r"\s*([0-9a-f]+):", line)
        addr = int(a.group(1), 16) if a else None
        for m in MOVT_RE.finditer(line):
            val = int(m.group(1), 0)
            if MOVT_LO <= val <= MOVT_HI:
                hits.append((addr, val << 16, line.strip()))
    return hits


def check_words(hexdump):
    """Full 32-bit constants in section contents, which literal-pool loads use.

    Returns (vaddr, value, line); the hexdump's own address column is the file offset, and in this
    image the file offset equals the vaddr - `.text`'s LMA 0x8000 equals its file offset 0x8000, so
    the loader places the byte at offset F at address F. The caller filters by vaddr.
    """
    hits = []
    for line in hexdump.splitlines():
        m = HEXDUMP_RE.match(line)
        if not m:
            continue
        a = re.match(r"\s*([0-9a-f]+)", line)
        base = int(a.group(1), 16)
        # `objdump -s` prints 4 bytes per token (2 hex digits) or a full word (8); a token of a
        # single byte is not the 4-byte word we are looking for, so only 8-hex-digit tokens count.
        words = [h for h in m.group(1).split() if len(h) == 8]
        for i, h in enumerate(words):
            val = struct.unpack("<I", bytes.fromhex(h))[0]
            if STORAGE_LO <= val <= STORAGE_HI:
                hits.append((base + i * 4, val, line.strip()))
    return hits


def selftest():
    """Both checks must fire on the range and stay quiet just outside it."""
    # Realistic objdump shapes, not comment text: the whole point is that these values
    # appear as operands.
    fake_disasm = """
   9878:  e34f3900   movt  r3, #63744   ; 0xf900 -- GIC, must not fire
   a600:  e34f2982   movt  r2, #63874   ; 0xf982 -- storage, must fire
   a604:  e34f298f   movt  r2, #63887   ; 0xf98f -- last storage half, must fire
   a608:  e34f2990   movt  r2, #63888   ; 0xf990 -- one past, must not fire
   a60c:  e34f2c4a   movt  r2, #64586   ; 0xfc4a -- PS_HOLD, must not fire
"""
    got_movt = sorted(v for _a, v, _l in check_movt(fake_disasm))
    want_movt = sorted([0xF9820000, 0xF98F0000])
    if got_movt != want_movt:
        print("selftest FAILED (movt): want %s got %s"
              % ([hex(v) for v in want_movt], [hex(v) for v in got_movt]))
        return 1

    # objdump -s prints memory order, so a word is written byte-reversed from its value.
    # 0xf9824000 appears as "004082f9"; 0xfc4ab000 as "00b04afc". Getting this backwards is
    # exactly the kind of mistake this selftest exists to catch, so it is spelled out.
    fake_hex = """
 08000 004082f9 00b04afc 000000f9 000090f9
"""
    got_words = sorted(v for _a, v, _l in check_words(fake_hex))
    want_words = [0xF9824000]
    if got_words != want_words:
        print("selftest FAILED (words): want %s got %s"
              % ([hex(v) for v in want_words], [hex(v) for v in got_words]))
        return 1

    # **The blob-exclusion must fire inside and stay quiet outside.** A word whose vaddr is inside
    # `[blob_lo, blob_hi)` is dropped; one at the boundary or outside is kept. This is the shape the
    # 954 D13 arm hit - the storage-range constants are inert data in the ENTRY image the payload
    # embeds, not payload code - so the selftest pins the boundary both ways.
    blob = (0x1000, 0x2000)
    fake_hex2 = """
 01000 004082f9 00000000 00000000 00000000
 02000 004082f9 00000000 00000000 00000000
"""
    inside, outside = [], []
    for a, v, _l in check_words(fake_hex2):
        (inside if blob[0] <= a < blob[1] else outside).append(a)
    if inside != [0x1000] or outside != [0x2000]:
        print("selftest FAILED (blob): inside=%s outside=%s" % ([hex(a) for a in inside],
                                                                [hex(a) for a in outside]))
        return 1

    print("selftest ok: movt fires on 0xf982/0xf98f and not 0xf990/0xf900/0xfc4a;")
    print("             literal words fire on a storage constant and not on GIC/PS_HOLD;")
    print("             the blob exclusion keeps 0x2000 (the boundary) and drops 0x1000 (inside)")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", nargs="?")
    ap.add_argument("--objdump", default="arm-none-eabi-objdump")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.elf:
        ap.error("need an ELF, or --selftest")

    o = args.objdump
    nm = args.nm
    blob = find_blob(args.elf, nm)
    movt_raw = check_movt(run([o, "-d", args.elf]))
    word_raw = check_words(run([o, "-s", "-j", ".text", "-j", ".rodata", args.elf]))
    def in_blob(a):
        return blob is not None and a is not None and blob[0] <= a < blob[1]
    movt_hits = [h for h in movt_raw if not in_blob(h[0])]
    word_hits = [h for h in word_raw if not in_blob(h[0])]
    if blob is not None and len(word_raw) != len(word_hits):
        excluded = len(word_raw) - len(word_hits)
    else:
        excluded = 0

    print("storage-controller references in %s" % args.elf)
    print("  movt high halves checked: 0x%04x-0x%04x" % (MOVT_LO, MOVT_HI))
    print("  literal words checked:    0x%08x-0x%08x" % (STORAGE_LO, STORAGE_HI))
    if blob is not None:
        print("  embedded entry-image blob: 0x%08x-0x%08x (%d word(s) there not counted)"
              % (blob[0], blob[1], excluded))

    for a, val, line in movt_hits:
        print("  movt  vaddr %s  0x%08x  %s" % ("0x%08x" % a if a is not None else "?", val, line))
    for a, val, line in word_hits:
        print("  word  vaddr 0x%08x  0x%08x  %s" % (a, val, line))

    total = len(movt_hits) + len(word_hits)
    if total == 0:
        print("  none")
        return 0

    print("\nFAIL: %d storage-controller reference(s)." % total)
    print("      The payload must touch only MMIO, IMEM and PS_HOLD.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
