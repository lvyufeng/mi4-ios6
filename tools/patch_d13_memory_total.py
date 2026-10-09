#!/usr/bin/env python3
"""Make Darwin-13's `arm_vm_init` RECOGNISE the device's full RAM (experiment 958).

WHY.  The goal's clause 「安装好的xnu，能够正确识别xiaomi 4的3GB内存」 asks XNU to recognise 3 GB.  957
measured the device's own `/memory/reg` = `(0,0x60000000)+(0x80000000,0x60000000)` = `0xC0000000` =
3.000 GiB.  D13's `arm_vm_init.c` sets

    327   max_mem = mem_size = sane_size = gMemSize;   /* gMemSize = args->memSize, the BOOT BANK */

and reports `gMemSize` (the bank the payload maps, `0x5e500000`) as the total.  Three of those four
globals feed the MACHINE, not the report: `mem_size` sizes pmap.c's page tables (pmap.c:3492-3526),
`sane_size` sizes kalloc/zones (kalloc.c:350, vm_init.c:154).  Only **`max_mem`** is the reported
total — `bsd/kern/kern_mib.c:365` serves `hw.memsize` from `&max_mem`, and `startup.c:198` copies it
into `machine_info.max_mem`.  So the edit moves `max_mem` ALONE.

WHY NOT `mem_size`/`sane_size`.  Raising them to 3 GiB would over-size the pmap page tables and the
zone map for RAM that is not (and cannot be) linearly mapped — the ARM32 kernel VA is < 2 GiB and D13's
managed map is `gMemSize` long from the fixed `MANAGED_BASE` (956).  `gMemSize`/`mem_size`/`sane_size`
stay the boot bank; recognising the 3 GB rides the report, exactly as 956 requires.

THE CHANNEL.  `/defaults hw.memsize`, which XNU already treats as "physical ram size": 4570's
`arm_init` reads it into `xmaxmem` (`arm_init.c:282`) and D13 ships the same `PE_get_default`
(`pexpert/gen/bootargs.c:318`).  The payload sets it to `RAM_DEVICE_TOTAL` = `0xC0000000`
(`src/stage90_main.c`).  On 4570 the read is inert: `memSize` (the window) is far below the total, so
the `if ((memory_size != 0) && (mem_size > memory_size))` clamp never fires.

WHY THE EDIT IS GUARDED, AND BYTE-NEUTRAL OFF.  An UNDEFINED `STAGE90_XNU_MEM_TOTAL` must leave the
object as the tree shipped it (the shipped D13 arms do not move).  Both the include and the marker are
inside `#ifdef STAGE90_XNU_MEM_TOTAL`, and the assignment's `#else` arm is Apple's line EXACTLY — the
same shape `tools/patch_mem_size_max.py` uses.  The marker `entry_xnu_mem_total_arm_on` lives at FILE
scope (a nested function is not C) so `build_entry.sh` can `nm` the arm the object was built as rather
than trust the command line.

Idempotent and re-appliable: a site already carrying the guard is left alone; a missing anchor is a
loud error, so a source update cannot silently drop the port.  `tools/stage_d13_memory_total.sh` applies
it, and `tools/check_d13_memory_total.py` (in `make check`) re-derives the effect and refuses drift.
"""
import re
import sys

FILE = "osfmk/arm/arm_vm_init.c"
MARK = "STAGE90_XNU_MEM_TOTAL"

# The include and the file-scope marker go just before `arm_vm_init`, NOT at the top of the file.
# WHY: this file's three `panic("...")` calls expand `__FILE__ ":" __LINE__` into .rodata string
# literals (measured: lines 189/220/269/290).  An insert ABOVE them shifts every one by the number of
# lines added, so an identical-code build differs from the tree's in .rodata.str1.1 alone - the object
# would not be byte-neutral even though the code is.  Placing the insert after the LAST `panic` (line
# 290) and before `arm_vm_init` (306) shifts only the assignment block (327+), which carries no
# `__LINE__`, so the default object is byte-for-byte the tree's.  The anchor is the function
# signature - unique, and the preprocessor include is file-scope wherever it sits.
INCLUDE_ANCHOR = "void arm_vm_init(uint32_t mem_limit, boot_args * args)\n"
INCLUDE_REPLACEMENT = (
    "/* 958: recognise the device's full RAM (tools/patch_d13_memory_total.py).  Placed here, after the\n"
    " * last __LINE__-bearing panic, so the default object stays byte-for-byte the tree's; the marker is\n"
    " * at FILE scope so build_entry.sh can nm the arm this object was built as; both it and the include\n"
    " * are inside the guard so an undefined STAGE90_XNU_MEM_TOTAL changes nothing. */\n"
    "#ifdef STAGE90_XNU_MEM_TOTAL\n"
    "#include <pexpert/pexpert.h>\n"
    "int entry_xnu_mem_total_arm_on(void)  { return 1; }\n"
    "#endif /* STAGE90_XNU_MEM_TOTAL */\n"
    "void arm_vm_init(uint32_t mem_limit, boot_args * args)\n"
)

# Apple's own line, matched whole and end-anchored so a source update that changes the spacing refuses.
ASSIGN_ANCHOR = "    max_mem = mem_size = sane_size = gMemSize;\n"
ASSIGN_REPLACEMENT = (
    "#ifdef STAGE90_XNU_MEM_TOTAL\n"
    "    /* 958: the DEVICE's full RAM (hw.memsize); gMemSize/mem_size/sane_size stay the boot bank so\n"
    "     * the linear map is untouched (956: the map is capped at 1 GiB).  Only max_mem is reported. */\n"
    "    max_mem = mem_size = sane_size = gMemSize;\n"
    "    {\n"
    "        uint32_t _hw_memsize = 0u;\n"
    "        if (PE_get_default(\"hw.memsize\", &_hw_memsize, sizeof(_hw_memsize)) != 0 && _hw_memsize != 0u) {\n"
    "            max_mem = (uint64_t)_hw_memsize;\n"
    "        }\n"
    "    }\n"
    "#else\n"
    "    max_mem = mem_size = sane_size = gMemSize;\n"
    "#endif /* STAGE90_XNU_MEM_TOTAL */\n"
)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_memory_total.py <external/xnu-hd2-darwin13/xnu>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/" + FILE
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()

    if MARK in src:
        # Already patched.  Verify the shape that keeps the default object byte-neutral: the marker and
        # the include are guarded, and the assignment's #else arm is Apple's line with no extra symbol.
        if "entry_xnu_mem_total_arm_on" not in src:
            print(f"patch_d13_memory_total: {MARK} present but no arm marker in {path}; refusing so a"
                  f" removed marker is not read as a present one.", file=sys.stderr)
            return 1
        if "#else\n    max_mem = mem_size = sane_size = gMemSize;\n#endif /* STAGE90_XNU_MEM_TOTAL */" not in src:
            print(f"patch_d13_memory_total: {MARK} present but the #else arm is not Apple's line exactly"
                  f" in {path}; the default object's byte-neutrality cannot be trusted. Refusing.",
                  file=sys.stderr)
            return 1
        print(f"patch_d13_memory_total: {FILE} already carries the 958 guard (idempotent, left alone)")
        return 0

    for anchor, what in ((INCLUDE_ANCHOR, "the <arm/arch.h> include"),
                         (ASSIGN_ANCHOR, "the max_mem/mem_size/sane_size assignment")):
        if src.count(anchor) != 1:
            print(f"patch_d13_memory_total: {src.count(anchor)} matches for {what} in {path}; Apple's"
                  f" source has moved or is ambiguous. Refusing rather than patching the wrong bytes.",
                  file=sys.stderr)
            return 1

    patched = src.replace(INCLUDE_ANCHOR, INCLUDE_REPLACEMENT, 1)
    patched = patched.replace(ASSIGN_ANCHOR, ASSIGN_REPLACEMENT, 1)
    if patched == src:
        print("patch_d13_memory_total: substitution made no change; refusing", file=sys.stderr)
        return 1
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(patched)
    print(f"patch_d13_memory_total: made arm_vm_init.c recognise /defaults hw.memsize as max_mem in {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())