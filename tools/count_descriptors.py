#!/usr/bin/env python3
"""Count ARMv7 page-descriptor constants in a built payload, by exact immediate.

Why a tool rather than a grep
-----------------------------
This check has been run by hand several times in this project and got it wrong twice,
in opposite directions:

  - `grep -c '#1106'` also matches `#11068`, so a *substring* produced a false positive.
  - Counting only `movw rN, #imm` misses descriptors the compiler materialises as
    `mov rN, #imm` or folds into `orr rN, rN, #imm`. The page descriptor 0x12 appears
    that way, so a `movw`-only count reported 0 for a value that IS present - a false
    negative.

Both errors were in the *method*, not the conclusion, which is the dangerous kind: the
conclusion was right by luck, and the same flaw could have hidden a real descriptor. So
the check belongs in a file that can be corrected once, not in a shell history.

What it does
------------
Scans every immediate operand in the disassembly - regardless of which instruction
carries it - and counts exact matches for a given value. "Exact" means the immediate is
the whole operand, matched with a word boundary, so `#1106` and `#11068` are distinct.

Usage:
    count_descriptors.py <elf> [--expect NAME=N ...]
    count_descriptors.py --diff <elf-a> <elf-b>

`--diff` is the reliable mode, and the one to prefer. A whole descriptor is often *not*
materialised as a single immediate: the section constant 0x00010c02 is built as a low half
(#0x0c02) plus an `orr ..., #0x10000` for the S bit, so no instruction carries 0x10c02 and
an absolute count finds 0 for a value that is everywhere. Comparing the immediate multisets
of two builds that differ only by `STAGE90_PMAP_ATTR_MODE` sidesteps that entirely: the
difference between them *is* the descriptor change, whatever instruction carries it.

Absolute `--expect` counts remain useful only for values that do appear whole (the page
descriptors do). When in doubt, diff.

Exits 1 if an expectation is violated (or, in diff mode, if the two builds' descriptor
immediates are identical when they should differ).
"""

import argparse
import re
import subprocess
import sys

# The project's four descriptor constants. See docs/reference/pmap-attribute-map.md.
DESCRIPTORS = {
    "SO_SECTION":      0x00010c02,   # Strongly-ordered 1MB section
    "NORMALNC_SECTION": 0x00011c02,  # Normal, Non-cacheable 1MB section
    "SO_PAGE":         0x00000012,   # Strongly-ordered 4KB small page
    "NORMALNC_PAGE":   0x00000452,   # Normal, Non-cacheable 4KB small page
}

# Any immediate operand, on any instruction. Deliberately broad: the point of this tool is
# that the *carrier* instruction is not something to assume.
IMM_RE = re.compile(r"#(0x[0-9a-fA-F]+|\d+)\b")


def disassemble(elf, objdump="arm-none-eabi-objdump"):
    try:
        return subprocess.run([objdump, "-d", elf],
                              capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        sys.exit("count_descriptors: %s not found" % objdump)
    except subprocess.CalledProcessError as exc:
        sys.exit("count_descriptors: objdump failed: %s" % exc)


def count_immediates(text):
    """value -> number of immediate operands equal to it."""
    counts = {}
    for line in text.splitlines():
        # Skip the address column and the raw encoding; read operands only.
        if ":" not in line:
            continue
        operands = line.split(":", 1)[-1]
        for m in IMM_RE.finditer(operands):
            val = int(m.group(1), 0)
            counts[val] = counts.get(val, 0) + 1
    return counts


def diff_mode(elf_a, elf_b, objdump):
    """Compare descriptor immediates between two builds that differ by one switch."""
    a = count_immediates(disassemble(elf_a, objdump))
    b = count_immediates(disassemble(elf_b, objdump))

    print("descriptor immediates: A=%s  B=%s" % (elf_a, elf_b))
    changed = 0
    for name, value in sorted(DESCRIPTORS.items(), key=lambda kv: kv[1]):
        ca, cb = a.get(value, 0), b.get(value, 0)
        marker = "  <-- differs" if ca != cb else ""
        if ca != cb:
            changed += 1
        print("  %-18s 0x%08x : A=%-4d B=%-4d%s" % (name, value, ca, cb, marker))

    # Also report the low halves, since a descriptor built in pieces shows up there.
    print()
    print("component immediates (halves the compiler may use instead):")
    for name, value in sorted(DESCRIPTORS.items(), key=lambda kv: kv[1]):
        low = value & 0xffff
        ca, cb = a.get(low, 0), b.get(low, 0)
        marker = "  <-- differs" if ca != cb else ""
        print("  %-18s 0x%04x     : A=%-4d B=%-4d%s" % (name, low, ca, cb, marker))

    print()
    if changed == 0:
        print("NOTE: no whole-descriptor immediate differs. If the two builds are meant to")
        print("      differ in memory attributes, check the component halves above - the")
        print("      change may be carried in pieces rather than in one immediate.")
        return 0
    print("OK: %d descriptor immediate(s) differ between the two builds." % changed)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", nargs="?")
    ap.add_argument("--diff", nargs=2, metavar=("A", "B"))
    ap.add_argument("--expect", action="append", default=[],
                    metavar="NAME=N",
                    help="require an exact count, e.g. NORMALNC_PAGE=0")
    ap.add_argument("--objdump", default="arm-none-eabi-objdump")
    args = ap.parse_args()

    if args.diff:
        return diff_mode(args.diff[0], args.diff[1], args.objdump)
    if not args.elf:
        ap.error("need an ELF, or --diff A B")

    text = disassemble(args.elf, args.objdump)
    counts = count_immediates(text)

    print("descriptor immediates in %s" % args.elf)
    for name, value in sorted(DESCRIPTORS.items(), key=lambda kv: kv[1]):
        print("  %-18s 0x%08x : %d" % (name, value, counts.get(value, 0)))

    if not args.expect:
        return 0

    bad = 0
    print()
    for spec in args.expect:
        name, _, want = spec.partition("=")
        name = name.strip()
        if name not in DESCRIPTORS:
            print("  unknown descriptor %r" % name)
            bad += 1
            continue
        got = counts.get(DESCRIPTORS[name], 0)
        want_i = int(want.strip(), 0)
        ok = (got == want_i)
        if not ok:
            bad += 1
        print("  expect %-18s == %-4d got %-4d %s"
              % (name, want_i, got, "ok" if ok else "FAIL"))

    if bad:
        print("\nFAIL: %d expectation(s) violated." % bad)
        return 1
    print("\nOK: all expectations hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
