#!/usr/bin/env python3
"""Make `arm_vm_init.c`'s `MEM_SIZE_MAX` a PORT, not a default (experiment 911b).

WHY.  Apple caps the recognisable physical RAM at 1 GiB: `osfmk/arm/arm_vm_init.c` defines
`MEM_SIZE_MAX 0x40000000`, and the constant is used THREE ways inside `arm_vm_init`:

    the clamp        `if (mem_size > MEM_SIZE_MAX) mem_size = MEM_SIZE_MAX;`
    `pmap_bootstrap((gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000);`
    the page-table   pre-init loop bound, the same expression.

The clamp is already satisfied in the entry image (the payload sets `args->memSize` to
`STAGE90_XNU_ENTRY_SIZE`), so the OBSERVABLE effect of raising the ceiling is the two folds: the
pmap's `virtual_space_start` moves off `0xc0000000`.  The goal's newest clause asks XNU to recognise
the Mi 4's ~3 GB, which requires the ceiling to move past 1 GiB (911b); recognising the *second bank*
is 911c.  The value must not map the Android ram_console (`0xde500000`) or MMIO (`>=0xf9000000`) as
RAM, nor overflow `gPhysBase + mem_size` past 32 bits; the hole-free high-bank top minus the console,
`0x5e500000`, is the safe ceiling (`tools/check_mem_size_max.py` refuses a value above it).

WHAT.  ONE guarded edit around the define, plus an OBJECT-level arm marker so `build_entry.sh` can
`nm` which arm the linked entry image was built as rather than trusting the command line (the shape
`src/platform/stage90_root_media.c`'s `entry_root_media_card_arm_on/_off` uses):

    #ifdef STAGE90_XNU_MEM_SIZE_MAX
    #if (STAGE90_XNU_MEM_SIZE_MAX != 0x40000000)
    #define MEM_SIZE_MAX STAGE90_XNU_MEM_SIZE_MAX
    int entry_xnu_mem_size_max_arm_on(void)  { return 1; }
    #else
    #define MEM_SIZE_MAX 0x40000000
    int entry_xnu_mem_size_max_arm_off(void) { return 0; }
    #endif
    #else
    #define MEM_SIZE_MAX 0x40000000
    #endif

**THE DEFAULT OBJECT IS BYTE-IDENTICAL AND THAT IS THE WHOLE SAFETY ARGUMENT.**  An UNDEFINED
`STAGE90_XNU_MEM_SIZE_MAX` must leave the object byte-for-byte as Apple shipped it, so the bytes the
ladder has run since 903 do not move - the same shape `tools/hfs_patch_root_rw.py` uses.  That is why
the OUTER `#else` arm is Apple's `#define` and NOTHING ELSE: an unconditional `..._arm_off()` symbol
there would enter the symbol table, shift `.text` by 0x10 and move every following symbol
(`arm_vm_prot_init`, `arm_vm_init`).  `tools/check_mem_size_max.py` re-derives both properties - the
marker is inert when the macro is undefined, and the default object is byte-identical.

Idempotent and re-appliable: a site already carrying the guard is left alone; a missing anchor is a
loud error, so a source update cannot silently drop the port.  This mirrors
`tools/hfs_patch_root_rw.py` / `tools/hfs_patch_mount_markers.py`: one tracked definition of an edit
to the untracked, re-provisionable `external/` tree.  `tools/stage_hfs.sh` applies it alongside the
HFS edits, so a fresh `external/` re-provision gets it too.
"""
import re
import sys

FILE = "osfmk/arm/arm_vm_init.c"
# Apple's own line, matched whole and end-anchored so a source update that changes the value or the
# spacing refuses rather than silently patching the wrong bytes.
ANCHOR = "#define MEM_SIZE_MAX 0x40000000\n"
MARK = "STAGE90_XNU_MEM_SIZE_MAX"
# The guarded block.  Note BOTH marker arms live INSIDE `#ifdef STAGE90_XNU_MEM_SIZE_MAX`, so an
# undefined macro emits the plain `#define` and no symbol at all (the byte-identity property).
REPLACEMENT = (
    "/* 911b: the physical-memory ceiling is a PORT, not a default (tools/patch_mem_size_max.py).\n"
    " * Undefined STAGE90_XNU_MEM_SIZE_MAX leaves this object byte-for-byte as Apple shipped it; the\n"
    " * guarded form below is what makes that true (no marker symbol is emitted in the default build).\n"
    " * The safe raised value is 0x5e500000: the hole-free high-bank top minus the ram_console at\n"
    " * 0xde500000 (the MMIO block starts at 0xf9000000). tools/check_mem_size_max.py re-derives this. */\n"
    "#ifdef STAGE90_XNU_MEM_SIZE_MAX\n"
    "#if (STAGE90_XNU_MEM_SIZE_MAX != 0x40000000)\n"
    "#define MEM_SIZE_MAX STAGE90_XNU_MEM_SIZE_MAX\n"
    "int entry_xnu_mem_size_max_arm_on(void)  { return 1; }\n"
    "#else\n"
    "#define MEM_SIZE_MAX 0x40000000\n"
    "int entry_xnu_mem_size_max_arm_off(void) { return 0; }\n"
    "#endif /* STAGE90_XNU_MEM_SIZE_MAX != default */\n"
    "#else\n"
    "#define MEM_SIZE_MAX 0x40000000\n"
    "#endif /* STAGE90_XNU_MEM_SIZE_MAX */\n"
)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_mem_size_max.py <external/xnu-4570.1.46>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/" + FILE
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()

    if MARK in src:
        # Already patched.  Verify the guard is still the shape that keeps the default object
        # byte-identical: the OUTER `#else` arm must be Apple's define and must NOT carry a symbol.
        outer = re.search(r"#else\n(#define MEM_SIZE_MAX 0x40000000\n)#endif /\* STAGE90_XNU_MEM_SIZE_MAX \*/", src)
        if outer is None:
            print(f"patch_mem_size_max: {MARK} is present but the outer guard is not the expected"
                  f" form in {path}; refusing so a moved/relaxed guard is not read as a present one.",
                  file=sys.stderr)
            return 1
        if "entry_xnu_mem_size_max_arm_off" in outer.group(1):
            print(f"patch_mem_size_max: the outer #else arm carries a marker symbol in {path} - that"
                  f" breaks the default object's byte-identity; refusing.", file=sys.stderr)
            return 1
        print(f"patch_mem_size_max: {FILE} already carries the 911b guard (idempotent, left alone)")
        return 0

    if ANCHOR not in src:
        print(f"patch_mem_size_max: anchor not found in {path}:\n  {ANCHOR!r}", file=sys.stderr)
        print("  Apple's MEM_SIZE_MAX define has moved or changed; refusing rather than raising a"
              " ceiling the port cannot prove it edited.", file=sys.stderr)
        return 1
    if src.count(ANCHOR) != 1:
        print(f"patch_mem_size_max: {src.count(ANCHOR)} matches for the anchor in {path}; refusing"
              " an ambiguous edit.", file=sys.stderr)
        return 1

    patched = src.replace(ANCHOR, REPLACEMENT, 1)
    if patched == src:
        print("patch_mem_size_max: substitution made no change; refusing", file=sys.stderr)
        return 1
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(patched)
    print(f"patch_mem_size_max: made MEM_SIZE_MAX a guarded 911b port in {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())