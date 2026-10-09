#!/usr/bin/env python3
"""Verify the 958 memory-total port: D13 recognises the device's full RAM, and the map is untouched.

WHY THIS EXISTS.  `tools/patch_d13_memory_total.py` makes D13's `arm_vm_init` read `/defaults
hw.memsize` into `max_mem`, so `hw.memsize` reports the device's 3 GB (`RAM_DEVICE_TOTAL`) while
`gMemSize`/`mem_size`/`sane_size` stay the payload's boot bank.  The port is correct only if FOUR
facts hold together, and any one can silently drift:

  1. `max_mem` IS the reported total - `bsd/kern/kern_mib.c` serves `hw.memsize` from `&max_mem`
     (if it moved to `mem_size`/`sane_size`, raising `max_mem` would change nothing observable);
  2. the D13 edit is present AND byte-neutral off (the guard wraps both the include and the marker;
     the `#else` arm is Apple's line exactly) - so the shipped arms do not move;
  3. the boot bank is UNTOUCHED - `gMemSize = args->memSize` and the managed map still maps `gMemSize`
     from the fixed `MANAGED_BASE` (956's premise: the map is capped at 1 GiB, so the 3 GB must NOT
     ride it);
  4. the payload publishes the two DIFFERENT quantities correctly - `/memory/reg` the boot bank
     (`RAM_BOOT_BANK_SIZE` = 0x5e500000) and `/defaults hw.memsize` the device total
     (`RAM_DEVICE_TOTAL` = 0xC0000000 = 3 GiB, above the boot bank - the whole distinction);
  5. `mem_size` and `sane_size` are NEVER raised off the boot bank (964, refining 915 §5).  They are a
     DIFFERENT quantity from `max_mem`: `mem_size` sizes the pmap's page tables and `sane_size` sizes
     kalloc/zones, so setting them to the region SUM while the allocator owns one bank OVER-PROMISES
     (and a multi-region pmap sizes its tables from the FIRST region - 917).  The stale 915 §5 row
     said "set max_mem/mem_size/sane_size = the sum"; this fact refuses a port that follows it.  Both
     are only ever assigned `gMemSize`.

The 4570 tree is the CONTROL: it ships no D13 marker (so the discriminator reads the right tree) and
its `arm_vm_init` still clamps `mem_size` DOWN to `memory_size`, which is what makes the raised total
inert there (the window is far below the total).

Usage:
    check_d13_memory_total.py --tree <XNU_TREE>
    check_d13_memory_total.py --selftest
Exit 0 when the facts hold, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(REPO, "src", "stage90.h")
MAIN = os.path.join(REPO, "src", "stage90_main.c")

MARK = "STAGE90_XNU_MEM_TOTAL"
RAM_PHYS_BASE = 0x80000000
RAM_CONSOLE_BASE = 0xDE500000
BOOT_BANK = RAM_CONSOLE_BASE - RAM_PHYS_BASE       # 0x5e500000
DEVICE_TOTAL = 0xC0000000                           # 3.000 GiB, the DT-declared bank sum (957)
GIB = 1024 * 1024 * 1024


def is_d13(tree):
    """The same discriminator every D13 script uses: 4570 ships no osfmk/sys/types.h."""
    return os.path.isfile(os.path.join(tree, "osfmk", "sys", "types.h"))


def refuse(msg):
    print("check_d13_memory_total: %s" % msg, file=sys.stderr)
    return 1


_HEXC = re.compile(r"0x[0-9A-Fa-f]+[uU]?")
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def _def(text, name, depth=0):
    """Read a `#define NAME <value>` as an int, or None if absent/unresolvable.

    The header writes the boot bank as `(RAM_CONSOLE_BASE - RAM_PHYS_BASE)` - a subtraction of two
    NAMED constants, not hex literals - so a symbol is resolved by looking it up recursively (a
    depth cap stops a cycle from looping).
    """
    m = re.search(r"#define\s+%s\s+(.+?)\s*(?:/\*|$)" % re.escape(name), text, re.M)
    if not m:
        return None
    expr = m.group(1).strip()
    e = re.fullmatch(r"\(\s*(\S+)\s*-\s*(\S+)\s*\)", expr)
    if e and depth < 8:
        a = _token(text, e.group(1), depth + 1)
        b = _token(text, e.group(2), depth + 1)
        if a is None or b is None:
            return None
        return a - b
    return _token(text, expr, depth + 1)


def _token(text, tok, depth):
    if _HEXC.fullmatch(tok):
        return int(tok.rstrip("uU"), 16)
    if _IDENT.fullmatch(tok) and depth < 8:
        return _def(text, tok, depth)
    return None


def _facts_payload(hdr_text, main_text):
    """The payload side: the two quantities are named, distinct, and correctly published."""
    phys = _def(hdr_text, "RAM_PHYS_BASE")
    cons = _def(hdr_text, "RAM_CONSOLE_BASE")
    if phys != RAM_PHYS_BASE or cons != RAM_CONSOLE_BASE:
        return ("src/stage90.h moved RAM_PHYS_BASE/RAM_CONSOLE_BASE (%s/%s, not 0x%08x/0x%08x) - the "
                "boot-bank arithmetic the port binds to has changed" % (phys, cons, RAM_PHYS_BASE, RAM_CONSOLE_BASE))
    bank = _def(hdr_text, "RAM_BOOT_BANK_SIZE")
    if bank != BOOT_BANK:
        return ("RAM_BOOT_BANK_SIZE is %s, not 0x%x (RAM_CONSOLE_BASE - RAM_PHYS_BASE) - the linear "
                "bank the pmap maps is no longer the boot bank" % (bank if bank is not None else "<absent>", BOOT_BANK))
    total = _def(hdr_text, "RAM_DEVICE_TOTAL")
    if total != DEVICE_TOTAL:
        return ("RAM_DEVICE_TOTAL is %s, not 0x%08x - the device total the 957 measurement binds to has "
                "moved" % (total if total is not None else "<absent>", DEVICE_TOTAL))
    if total != 3 * GIB:
        return "RAM_DEVICE_TOTAL 0x%x is not 3 GiB - the goal's clause is about 3 GB" % total
    if not (total > bank):
        return ("RAM_DEVICE_TOTAL 0x%x is not above RAM_BOOT_BANK_SIZE 0x%x - the total and the linear "
                "bank are the same number, which is the confusion this port exists to prevent" % (total, bank))

    if not re.search(r'apple_dt_prop_u32\s*\(\s*b\s*,\s*"hw\.memsize"\s*,\s*RAM_DEVICE_TOTAL\s*\)', main_text):
        return ("src/stage90_main.c no longer publishes `/defaults hw.memsize` as RAM_DEVICE_TOTAL - "
                "the property D13 reads into max_mem carries the wrong number (or is gone)")
    if not re.search(r'apple_dt_prop_u32_array\s*\(\s*b\s*,\s*"reg"\s*,\s*memory_reg\s*,\s*ARRAY_SIZE\(memory_reg\)\s*\)', main_text):
        return "src/stage90_main.c no longer emits /memory/reg from memory_reg[]"
    if not re.search(r"RAM_PHYS_BASE\s*,\s*RAM_BOOT_BANK_SIZE\s*,", main_text):
        return ("memory_reg[] is not {RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE} - /memory/reg is not the boot "
                "bank the payload asserts against")
    return ""


def _facts_d13(avm_text, mib_text):
    """The D13 side: the edit is present and byte-neutral off, and the map is untouched."""
    if MARK not in avm_text:
        return ("D13's arm_vm_init.c is missing the %s guard - the memory-total port is not staged "
                "(run tools/stage_d13_memory_total.sh)" % MARK)
    if "entry_xnu_mem_total_arm_on" not in avm_text:
        return "D13's arm_vm_init.c carries the guard but no `entry_xnu_mem_total_arm_on` marker"
    if not re.search(r'PE_get_default\s*\(\s*"hw\.memsize"\s*,\s*&_hw_memsize\s*,\s*sizeof\(_hw_memsize\)\s*\)', avm_text):
        return ("D13's arm_vm_init.c no longer reads `PE_get_default(\"hw.memsize\", ...)` - the value "
                "the port recognises is gone")
    if not re.search(r"#else\n\s*max_mem = mem_size = sane_size = gMemSize;\n#endif /\* %s \*/" % MARK, avm_text):
        return ("D13's guarded assignment no longer has Apple's line as its `#else` arm - the default "
                "object is not byte-neutral (the shipped arms could move)")
    if not re.search(r"#ifdef %s\n#include <pexpert/pexpert\.h>" % MARK, avm_text):
        return ("D13's `#include <pexpert/pexpert.h>` is not inside the guard - an undefined macro would "
                "still pull the include, so the default object is not as the tree shipped it")
    # the map is untouched (956's premise)
    if not re.search(r"gMemSize\s*=\s*args->memSize\s*;", avm_text):
        return "D13 no longer sets `gMemSize = args->memSize` - the boot bank the map uses has moved"
    if not re.search(r"l2_cache_to_range\s*\(\s*managedCachePA\s*,\s*managedBaseVA\s*,[^;]*?gMemSize\s*,\s*TRUE\s*\)", avm_text, re.S):
        return ("D13's managed map no longer maps `gMemSize` - the total could have reached the linear "
                "map, which 956 caps at 1 GiB")
    if not re.search(r"#define\s+MANAGED_BASE\s+0xC0000000", avm_text):
        return ("D13's MANAGED_BASE is no longer the fixed 0xC0000000 - 956's 1 GiB ceiling (and the "
                "reason the total must NOT ride the map) has moved")
    # fact 5 (964): mem_size and sane_size must NOT be raised off the boot bank.  The stale 915 §5 row
    # would set max_mem/mem_size/sane_size = the region sum; setting the last two there over-promises the
    # allocator (they size the pmap page tables and kalloc/zones - 958 §3).  Both are only ever assigned
    # gMemSize, possibly through a chain (`max_mem = mem_size = sane_size = gMemSize;`) - so the check
    # reads each assignment STATEMENT, takes the terminal RHS after the last `=`, and requires it to be
    # exactly `gMemSize`.  A port that gives either a second writer (a region sum, RAM_DEVICE_TOTAL, an
    # arithmetic expression) is refused here, structurally, before it can ship.
    for match in re.finditer(r"(?:^|[;{=])\s*(mem_size|sane_size)\s*=\s*([^;]*);", avm_text, re.M):
        var, rhs = match.group(1), match.group(2)
        terminal = rhs.rsplit("=", 1)[-1].strip()
        if terminal != "gMemSize":
            return ("D13's arm_vm_init.c assigns `%s` off the boot bank (`%s = %s`) - 964: %s sizes the "
                    "%s, and raising it to the region sum while the allocator owns one bank over-promises "
                    "(the stale 915 §5 row).  It must only ever be `gMemSize`."
                    % (var, var, terminal, var,
                       "pmap page tables" if var == "mem_size" else "kalloc and the zones"))
    # fact 1: max_mem is the reported total
    if not re.search(r"SYSCTL_QUAD\s*\(\s*_hw\s*,\s*HW_MEMSIZE\s*,[^;]*?&max_mem\s*,", mib_text, re.S):
        return ("bsd/kern/kern_mib.c no longer serves `hw.memsize` from `&max_mem` - `max_mem` is not "
                "the reported total, so raising it changes nothing observable")
    return ""


def _facts_4570(avm_text):
    """The control: 4570 must not carry the D13 marker, and its clamp must make the total inert."""
    if MARK in avm_text:
        return ("this tree was read as 4570 (no osfmk/sys/types.h) but its arm_vm_init.c names %s - "
                "the D13 discriminator is picking the wrong tree" % MARK)
    if not re.search(r"if\s*\(\s*\(memory_size != 0\)\s*&&\s*\(mem_size > memory_size\)\s*\)\s*\n\s*mem_size = memory_size;", avm_text):
        return ("4570's arm_vm_init no longer clamps `mem_size` down to `memory_size` - the raised total "
                "would no longer be inert on 4570")
    return ""


def selftest():
    base_avm = (
        "#include <arm/arch.h>\n"
        "#ifdef %s\n#include <pexpert/pexpert.h>\nint entry_xnu_mem_total_arm_on(void) { return 1; }\n#endif\n"
        "gMemSize = args->memSize;\n"
        "l2_cache_to_range(managedCachePA, managedBaseVA, phys_to_virt(cpu_ttb), gMemSize, TRUE);\n"
        "#define MANAGED_BASE    0xC0000000\n"
        "#ifdef %s\n"
        "    max_mem = mem_size = sane_size = gMemSize;\n"
        "    { PE_get_default(\"hw.memsize\", &_hw_memsize, sizeof(_hw_memsize)); }\n"
        "#else\n    max_mem = mem_size = sane_size = gMemSize;\n#endif /* %s */\n" % (MARK, MARK, MARK)
    )
    mib = "SYSCTL_QUAD(_hw, HW_MEMSIZE, memsize, CTLFLAG_RD | CTLFLAG_KERN | CTLFLAG_LOCKED, &max_mem, \"\");\n"
    hdr = ("#define RAM_CONSOLE_BASE     0xde500000u\n#define RAM_PHYS_BASE        0x80000000u\n"
           "#define RAM_BOOT_BANK_SIZE   (RAM_CONSOLE_BASE - RAM_PHYS_BASE)\n"
           "#define RAM_DEVICE_TOTAL     0xC0000000u\n")
    mn = ('apple_dt_prop_u32(b, "hw.memsize", RAM_DEVICE_TOTAL);\n'
          'apple_dt_prop_u32_array(b, "reg", memory_reg, ARRAY_SIZE(memory_reg));\n'
          'static const uint32_t memory_reg[] = { RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, };\n')
    a4570 = ("arm_vm_init(uint64_t memory_size, boot_args * args)\n"
             "{\n    if ((memory_size != 0) && (mem_size > memory_size))\n        mem_size = memory_size;\n}\n")

    cases = [
        ("the port as staged", base_avm, mib, hdr, mn, True),
        ("no guard at all", base_avm.replace(MARK, "NOT_IT"), mib, hdr, mn, False),
        ("no arm marker", base_avm.replace("int entry_xnu_mem_total_arm_on(void) { return 1; }\n", ""), mib, hdr, mn, False),
        ("the PE_get_default read removed", base_avm.replace('PE_get_default("hw.memsize", &_hw_memsize, sizeof(_hw_memsize));', ""), mib, hdr, mn, False),
        ("the #else arm is not Apple's line", base_avm.replace("#else\n    max_mem = mem_size = sane_size = gMemSize;\n#endif", "#else\n    max_mem = gMemSize;\n#endif"), mib, hdr, mn, False),
        ("the include escapes the guard", base_avm.replace("#ifdef %s\n#include <pexpert/pexpert.h>" % MARK, "#include <pexpert/pexpert.h>\n#ifdef %s" % MARK), mib, hdr, mn, False),
        ("hw.memsize stops reading max_mem", base_avm, mib.replace("&max_mem", "&sane_size"), hdr, mn, False),
        ("the map stops using gMemSize", base_avm.replace("gMemSize, TRUE", "0x40000000, TRUE"), mib, hdr, mn, False),
        ("MANAGED_BASE becomes derived", base_avm.replace("#define MANAGED_BASE    0xC0000000", "#define MANAGED_BASE    gVirtBase"), mib, hdr, mn, False),
        ("the device total moved", base_avm, mib, hdr.replace("0xC0000000u", "0x40000000u"), mn, False),
        ("the total is not above the bank", base_avm, mib, hdr.replace("0xC0000000u", "0x5e500000u"), mn, False),
        ("mem_size raised to the region sum (915 §5)",
         base_avm.replace("max_mem = mem_size = sane_size = gMemSize;\n    {",
                          "max_mem = gMemSize;\n    mem_size = sane_size = RAM_DEVICE_TOTAL;\n    {"), mib, hdr, mn, False),
        ("sane_size alone raised off the bank",
         base_avm.replace("max_mem = mem_size = sane_size = gMemSize;\n    {",
                          "max_mem = mem_size = gMemSize;\n    sane_size = mem_size + gMemSize;\n    {"), mib, hdr, mn, False),
        ("reg stops being the boot bank", base_avm, mib, hdr, mn.replace("RAM_BOOT_BANK_SIZE, }", "RAM_DEVICE_TOTAL, }"), False),
        ("hw.memsize stops being the total", base_avm, mib, hdr, mn.replace("RAM_DEVICE_TOTAL", "RAM_BOOT_BANK_SIZE"), False),
    ]
    bad = 0
    for label, a, mi, h, m, want in cases:
        payload = _facts_payload(h, m) == ""
        d13 = _facts_d13(a, mi) == ""
        got = payload and d13
        if got != want:
            print("check_d13_memory_total --selftest: %s -> ok=%s wanted %s" % (label, got, want), file=sys.stderr)
            bad += 1
    if _facts_4570(base_avm) == "":
        print("check_d13_memory_total --selftest: a 4570 text naming %s was not refused" % MARK, file=sys.stderr)
        bad += 1
    if _facts_4570(a4570) != "":
        print("check_d13_memory_total --selftest: the clean 4570 control was refused", file=sys.stderr)
        bad += 1
    if bad:
        print("check_d13_memory_total --selftest: FAIL (%d)" % bad, file=sys.stderr)
        return 1
    print("check_d13_memory_total --selftest: ok - every premise is caught when it moves, and the 4570 "
          "control reads clean")
    return 0


def check_tree(tree):
    hdr = open(HDR, encoding="utf-8", errors="replace").read()
    main = open(MAIN, encoding="utf-8", errors="replace").read()
    msg = _facts_payload(hdr, main)
    if msg:
        return refuse(msg)

    avm_path = os.path.join(tree, "osfmk", "arm", "arm_vm_init.c")
    if not os.path.isfile(avm_path):
        return refuse("%s has no osfmk/arm/arm_vm_init.c - not an XNU tree" % tree)
    avm = open(avm_path, encoding="utf-8", errors="replace").read()

    if not is_d13(tree):
        msg = _facts_4570(avm)
        if msg:
            return refuse(msg)
        print("check_d13_memory_total: %s is 4570 (no osfmk/sys/types.h) - no memory-total port, and "
              "its mem_size clamp keeps the raised total inert (SKIPPED on 4570)." % tree)
        return 0

    mib_path = os.path.join(tree, "bsd", "kern", "kern_mib.c")
    if not os.path.isfile(mib_path):
        return refuse("%s has no bsd/kern/kern_mib.c - the `hw.memsize` premise cannot be checked" % tree)
    mib = open(mib_path, encoding="utf-8", errors="replace").read()
    msg = _facts_d13(avm, mib)
    if msg:
        return refuse(msg)
    print("check_d13_memory_total: %s ok - arm_vm_init reads /defaults hw.memsize into max_mem "
          "(hw.memsize = 0x%08x = 3 GiB) while the managed map stays the boot bank 0x%08x."
          % (tree, DEVICE_TOTAL, BOOT_BANK))
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
    trees = [os.path.join(REPO, "external", "xnu-hd2-darwin13", "xnu"),
             os.path.join(REPO, "external", "xnu-4570.1.46")]
    rc = 0
    for t in trees:
        if not os.path.isdir(t):
            print("check_d13_memory_total: %s absent - skipped" % t, file=sys.stderr)
            continue
        rc |= check_tree(t)
    return rc


if __name__ == "__main__":
    sys.exit(main())