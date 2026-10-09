#!/usr/bin/env python3
"""Verify Darwin 13's fixed managed-map base, the fact the entry window's 1 GiB ceiling rests on.

WHY THIS EXISTS.  `build_entry.sh` refuses an `ENTRY_WINDOW` >= 1 GiB on a D13 tree, because D13's
managed map is built from a FIXED VA.  The whole refusal rests on four source facts:
  1. `MANAGED_BASE` is `0xC0000000` and `managedBaseVA` is ASSIGNED it (not derived from memSize);
  2. `l2_cache_to_range(..., managedBaseVA, ttb, gMemSize, TRUE)` maps `gMemSize`, so the window IS
     the length;
  3. `L1_SIZE` is `0x4000` and `tte_offset` masks the index to 12 bits, so byte `0x4000` is a
     DIFFERENT (wrapped) index rather than a fault;
  4. `xnu_entry_jump.c` hands XNU `STAGE90_XNU_ENTRY_SIZE` as `memSize`, so the refusal's variable is
     the one that reaches `gMemSize` (`gMemSize = args->memSize`).
A comment in one of these files cannot catch a tree that drifts - a D13 whose base became
`gVirtBase + ...` (the 4570 shape) would make the 1 GiB refusal WRONG (too strict), and a tree whose
`tte_offset` stopped masking would turn "silent corruption" into "fault", changing the argument.  So
the refusal verifies its premise here, and refuses if any of the four moves.

The 4570 half is the CONTROL: 4570 derives its span and ships no `MANAGED_BASE`, so the check asserts
that and treats it as "the refusal does not apply" - the same D13-detection (`osfmk/sys/types.h`) the
build uses, so the two cannot disagree about which tree is which.

Usage:
    check_d13_managed_base.py --tree <XNU_TREE>
    check_d13_managed_base.py --selftest
Exit 0 when the D13 facts hold (or the tree is 4570), 1 when they have moved, 2 usage.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JUMP = os.path.join(REPO, "src", "xnu_entry_jump.c")

MANAGED_BASE = 0xC0000000
L1_BYTES = 0x4000
# The first L1 byte the managed map writes: (((0xC0000000 >> 20) & 0xfff) << 2) = 0x3000.
MANAGED_FIRST_BYTE = (((MANAGED_BASE >> 20) & 0xFFF) << 2)


def is_d13(tree):
    """The same discriminator `build_entry.sh` and `build_xnu_arm_kernel.sh` use - one value, one
    definition (`osfmk/sys/types.h` is D13's legacy private header; 4570 ships none)."""
    return os.path.isfile(os.path.join(tree, "osfmk", "sys", "types.h"))


def refuse(msg):
    print("check_d13_managed_base: %s" % msg, file=sys.stderr)
    return 1


def _facts_d13(avm_text, pmap_text, jump_text):
    """Return "" if the four D13 facts hold, else a refusal naming the one that moved.

    Split from the file reads so `--selftest` can feed a mutated tree and assert each fact is caught.
    """
    m = re.search(r"#define\s+MANAGED_BASE\s+(0x[0-9A-Fa-f]+)", avm_text)
    if not m:
        return "D13's arm_vm_init.c has no `#define MANAGED_BASE` - the 1 GiB ceiling's premise is gone"
    if int(m.group(1), 16) != MANAGED_BASE:
        return ("D13's MANAGED_BASE is %s, not 0x%08x - the managed L1 start byte is not 0x%04x, so the "
                "1 GiB ceiling build_entry.sh refuses is the wrong number"
                % (m.group(1), MANAGED_BASE, MANAGED_FIRST_BYTE))
    if not re.search(r"managedBaseVA\s*=\s*MANAGED_BASE\s*;", avm_text):
        return ("D13 no longer assigns `managedBaseVA = MANAGED_BASE;` - it derives the base from "
                "memSize (the 4570 shape), so the fixed-base argument the refusal makes does not hold")
    if not re.search(r"l2_cache_to_range\s*\(\s*managedCachePA\s*,\s*managedBaseVA\s*,"
                     r"[^;]*?gMemSize\s*,\s*TRUE\s*\)", avm_text, re.S):
        return ("D13's managed `l2_cache_to_range` no longer maps `gMemSize`, so the window is not the "
                "map length and the ceiling is not the window's")
    lm = re.search(r"#define\s+L1_SIZE\s+(0x[0-9A-Fa-f]+)", pmap_text)
    if not lm or int(lm.group(1), 16) != L1_BYTES:
        return ("D13's L1_SIZE is %s, not 0x%x - the 1 GiB map may no longer fill/thereafter overflow "
                "the table, which is the silent-corruption half of the refusal"
                % (lm.group(1) if lm else "<absent>", L1_BYTES))
    if not re.search(r"#define\s+tte_offset\(addr\)\s*\(\s*\(\s*\(addr\s*>>\s*0x14\)\s*&\s*0xfff\s*\)"
                     r"\s*<<\s*2\s*\)", pmap_text):
        return ("D13's `tte_offset` no longer masks the L1 index to 12 bits (`(addr >> 0x14) & 0xfff`), "
                "so an overflow would fault instead of wrapping - the refusal's failure mode moved")
    if not re.search(r"a->memSize\s*=\s*STAGE90_XNU_ENTRY_SIZE\s*;", jump_text):
        return ("src/xnu_entry_jump.c no longer sets `a->memSize = STAGE90_XNU_ENTRY_SIZE;` - the "
                "window the refusal bounds is not the value that reaches XNU's gMemSize")
    if not re.search(r"gMemSize\s*=\s*args->memSize\s*;", avm_text):
        return ("D13 no longer sets `gMemSize = args->memSize;` - memSize reaches the managed map by "
                "another name and the refusal's chain is broken")
    return ""


def _facts_4570(avm_text):
    """The control: 4570 must NOT carry the D13 shape, or `is_d13` is reading the wrong tree."""
    if "MANAGED_BASE" in avm_text:
        return ("this tree was read as 4570 (no osfmk/sys/types.h) but its arm_vm_init.c names "
                "MANAGED_BASE - the D13 discriminator is picking the wrong tree, so the D13-only "
                "refusal in build_entry.sh would not fire on the tree that needs it")
    return ""


def selftest():
    base = ("#define MANAGED_BASE    0xC0000000\n"
            "managedBaseVA = MANAGED_BASE;\n"
            "gMemSize = args->memSize;\n"
            "l2_cache_to_range(managedCachePA, managedBaseVA, phys_to_virt(cpu_ttb), gMemSize, TRUE);\n")
    pmap = ("#define L1_SIZE 0x4000\n"
            "#define tte_offset(addr) (((addr >> 0x14) & 0xfff) << 2)\n")
    jump = "a->memSize = STAGE90_XNU_ENTRY_SIZE;\n"
    cases = [
        ("the D13 tree as it is", base, pmap, jump, True),
        ("a moved MANAGED_BASE", base.replace("0xC0000000", "0xA0000000"), pmap, jump, False),
        ("base derived from memSize", base.replace("managedBaseVA = MANAGED_BASE;",
                                                   "managedBaseVA = gVirtBase + gMemSize;"), pmap, jump, False),
        ("the map no longer gMemSize long", base.replace("gMemSize, TRUE", "0x40000000, TRUE"),
         pmap, jump, False),
        ("L1_SIZE shrunk", base, pmap.replace("L1_SIZE 0x4000", "L1_SIZE 0x2000"), jump, False),
        ("tte_offset stops masking", base, pmap.replace("& 0xfff) << 2", "& 0x1fff) << 2"), jump, False),
        ("the window stops being memSize", base, pmap, "a->memSize = 0x01000000;\n", False),
        ("gMemSize stops coming from args->memSize", base.replace("gMemSize = args->memSize;", ""),
         pmap, jump, False),
        ("no MANAGED_BASE at all", "#define MEM_SIZE_MAX 0x40000000\n", pmap, jump, False),
    ]
    bad = 0
    for label, b, p, j, want in cases:
        got = _facts_d13(b, p, j) == ""
        if got != want:
            print("check_d13_managed_base --selftest: %s -> ok=%s wanted %s" % (label, got, want),
                  file=sys.stderr)
            bad += 1
    if _facts_4570(base) == "":
        print("check_d13_managed_base --selftest: a 4570 text naming MANAGED_BASE was not refused",
              file=sys.stderr)
        bad += 1
    if _facts_4570("#define MEM_SIZE_MAX 0x40000000\n") != "":
        print("check_d13_managed_base --selftest: a clean 4570 text was refused", file=sys.stderr)
        bad += 1
    if bad:
        print("check_d13_managed_base --selftest: FAIL (%d)" % bad, file=sys.stderr)
        return 1
    print("check_d13_managed_base --selftest: ok - every premise of the 1 GiB refusal is caught when "
          "it moves, and the 4570 control reads clean")
    return 0


def check_tree(tree):
    """Check one tree; returns 0/1. D13 must carry the four premises, 4570 the control."""
    avm_path = os.path.join(tree, "osfmk", "arm", "arm_vm_init.c")
    pmap_path = os.path.join(tree, "osfmk", "arm", "pmap.h")
    if not os.path.isfile(avm_path):
        return refuse("%s has no osfmk/arm/arm_vm_init.c - not an XNU tree" % tree)
    avm = open(avm_path, encoding="utf-8", errors="replace").read()
    if not os.path.isfile(JUMP):
        return refuse("%s is missing - the memSize chain cannot be checked" % JUMP)
    jump = open(JUMP, encoding="utf-8", errors="replace").read()

    if not is_d13(tree):
        msg = _facts_4570(avm)
        if msg:
            return refuse(msg)
        print("check_d13_managed_base: %s is 4570 (no osfmk/sys/types.h) - no fixed managed base, so "
              "the D13-only 1 GiB ceiling does not apply (SKIPPED on 4570)." % tree)
        return 0

    msg = _facts_d13(avm, open(pmap_path, encoding="utf-8", errors="replace").read(), jump)
    if msg:
        return refuse(msg)
    print("check_d13_managed_base: %s ok - D13's managed map is `gMemSize` long from the fixed "
          "0xC0000000; its L1 fills to byte 0x%04x at 1 GiB, the ceiling build_entry.sh refuses."
          % (tree, L1_BYTES))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree")
    ap.add_argument("--verbose", action="store_true", help="accepted for the build's call shape; output is always this detailed")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if args.tree:
        return check_tree(args.tree)
    # No tree named: check BOTH repo trees. The D13 half is the assertion; the 4570 half is the
    # control that proves `is_d13` reads the right tree. `make check` runs this form.
    trees = [os.path.join(REPO, "external", "xnu-hd2-darwin13", "xnu"),
             os.path.join(REPO, "external", "xnu-4570.1.46")]
    rc = 0
    for t in trees:
        if not os.path.isdir(t):
            print("check_d13_managed_base: %s absent - skipped" % t, file=sys.stderr)
            continue
        rc |= check_tree(t)
    return rc


if __name__ == "__main__":
    sys.exit(main())