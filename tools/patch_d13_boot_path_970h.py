#!/usr/bin/env python3
"""Remove Darwin-13's PRE-SWITCH whole-TLB invalidate from the boot path (experiment 970h).

WHY.  970g (`tools/patch_d13_boot_path.py`) made D13's `__start` fall into the boot path instead of
taking the MMU fast path.  Its own goal was "4570's proven order" - but it left ONE instruction 4570
does not have: a whole-TLB invalidate at `osfmk/arm/locore.s:85` that runs BEFORE the TTBR0 write
(`:113`) and before the zero/map loops build the boot table.

    /* Clean TLB and instruction cache. */
    mov     r4, #0
    mcr     p15, 0, r4, c8, c7, 0      <-- the pre-switch TLB invalidate (this edit removes it)
    mcr     p15, 0, r4, c7, c5, 0
    mcr     p15, 0, r4, c2, c0, 2

THE HAZARD.  The payload jumps into the entry with the MMU **ON**: `sctlr_before = 0x00c5487b`
(SCTLR.M=1, caches off), TTBR0 = the payload's own table at PA `0x6c4000`.  The payload's L1
identity-maps `[0x80000000, 0x81000000)` (`src/mmu.c:5506`, `STAGE90_XNU_ENTRY_IDENTITY_LIMIT`
= 16 MiB), so at the jump VA `0x80a00000` - the boot table, `topOfKernelData` - already has a valid
identity **TLB entry** (VA == PA).  4570 relies on exactly that: its `_start` performs **no** TLB
maintenance before its TTBR0 write (`start.s:152`); its only `c8,c7,0` is at `start.s:337` in
`join_start`, AFTER the boot table is built.  Its first post-switch store - the `invalidate_tte` loop's
`str r11,[r5]` to the SAME VA `0x80a00000` (`start.s:166-170`) - does not fault, which is only possible
if a live identity entry survived the switch.  (ARMv7 does not auto-invalidate the TLB on a TTBR write.)

D13's pre-switch flush destroys that entry.  The very next store into the boot table - 970g edit 2's
zeroing loop, the first `str r3,[r5],#4` at `locore.s:134` targeting VA `0x80a00000` - must then WALK
the just-switched (stale) table, whose L1 slot index `0x80a` holds DRAM, not a descriptor: a translation
fault.  At that instant `cpsid if` is already set (`locore.s:55`) and VBAR is still the **payload's**
(D13 sets its own only at `:235`, after the table is built), and the payload's vector page is not mapped
by the new table - so the abort-vector fetch itself faults: a double fault, no handler, no print,
watchdog reset.  That is the whole-D13-line silence the 970g press showed.

THE FIX.  Remove the pre-switch flush, exactly as 4570's `_start` has none.  D13 already performs a safe
whole-TLB invalidate AFTER the table is built (`locore.s:274`, in `mmu_initialized`, the same place
4570 does it), so the removed one is redundant.  The TTBCR=0 write (`:87`, the `c2,c0,2` line) is a
different operation and STAYS.

WHAT IT DOES NOT DO.  It touches only `osfmk/arm/locore.s` (D13's entry file alone; 4570's entry is
`start.s` and is untouched).  It removes one instruction and adds a comment: the register state for the
`map:` loop is unchanged (`r4` stays 0 for the `c2,c0,2` write immediately below).

Idempotent and re-appliable: the comment token's presence is the "already applied" marker, and the
anchor is matched whole so a source update that moves it refuses loudly.  `tools/stage_d13_boot_path_970h.sh`
applies it; the proof of the change is by VALUE - the linked D13 entry image's `mmu_reinitialize` must
carry only ONE `c8,c7,0` (the `mmu_initialized` one), not two.

Order-independent with 970g's script: this anchor (the `mov r4,#0` / `c8,c7,0` / `c7,c5,0` / `c2,c0,2`
group) does not overlap 970g's anchors (`beq mmu_initialized`; the `/* Make our section mappings now. */`
point).  Either order leaves both edits in place.
"""
import sys

FILE = "osfmk/arm/locore.s"

# ---------------------------------------------------------------------------------------------------
# EDIT 3 - remove the pre-switch whole-TLB invalidate, verbatim, matched whole (leading four spaces,
# exact mnemonic spacing).  A source update that re-spaces it makes the count != 1 and we refuse rather
# than patch the wrong bytes.  `mcr p15,0,r4,c8,c7,0` also appears AFTER the switch at `:274`
# (`mmu_initialized`), but that line is not part of this anchor - the anchor is the four-line group.
# ---------------------------------------------------------------------------------------------------
ANCHOR3 = (
    "    /* Clean TLB and instruction cache. */\n"
    "    mov     r4, #0\n"
    "    mcr     p15, 0, r4, c8, c7, 0\n"
    "    mcr     p15, 0, r4, c7, c5, 0\n"
    "    mcr     p15, 0, r4, c2, c0, 2\n"
)
REPLACEMENT3 = (
    "    /* Clean TLB and instruction cache. */\n"
    "    mov     r4, #0\n"
    "    /* 970h edit 3: the TLB invalidate that stood here is REMOVED (tools/patch_d13_boot_path_970h.py).\n"
    "     *\n"
    "     * 4570's `_start` performs NO TLB maintenance before its TTBR0 write (its only invalidate is at\n"
    "     * `start.s:337`, in `join_start`, AFTER the boot table is built).  D13 instead carried this\n"
    "     * whole-TLB invalidate BEFORE the TTBR0 write at `:113` and before the zero/map loops build the\n"
    "     * table.  The payload jumps in with the MMU ON (`sctlr_before=0x00c5487b`, TTBR0 `0x6c4000`) and\n"
    "     * its L1 identity-maps `[0x80000000, 0x81000000)` (`src/mmu.c:5506`), so at the jump VA\n"
    "     * `0x80a00000` (the boot table, `topOfKernelData`) already has a valid identity TLB entry.  This\n"
    "     * invalidate destroys it; the very next store into the boot table - 970g edit 2's zeroing loop,\n"
    "     * `str r3,[r5],#4` to VA `0x80a00000` - must then WALK the just-switched table, whose slot `0x80a`\n"
    "     * is stale DRAM (an invalid descriptor), and faults.  With `cpsid if` set (`:55`) and VBAR still\n"
    "     * the payload's (D13 sets its own only at `:235`), the abort vector fetch itself faults: double\n"
    "     * fault, no print, watchdog reset - the whole D13 line silent, which is what the 970g press\n"
    "     * showed.  4570 boots because it keeps that identity entry LIVE across the switch.  D13 already\n"
    "     * has a safe whole-TLB invalidate AFTER the table is built (`:274`, `mmu_initialized`), so this\n"
    "     * one is redundant.  `r4` stays 0 for the TTBCR write just below. */\n"
    "    mcr     p15, 0, r4, c7, c5, 0\n"
    "    mcr     p15, 0, r4, c2, c0, 2\n"
)
TOKEN3 = "970h edit 3: the TLB invalidate that stood here is REMOVED"


def patch_once(src, anchor, replacement, patched_token, name):
    """Apply one delete-anchor/insert-replacement edit.  Returns (new_src, changed) or raises.

    `patched_token` appears ONLY in `replacement`; its presence means this edit has already run.
    """
    if patched_token in src:
        print(f"patch_d13_boot_path_970h: {name}: already applied (idempotent, left alone)")
        return src, False
    n = src.count(anchor)
    if n != 1:
        raise SystemExit(f"patch_d13_boot_path_970h: {name}: {n} matches for the anchor; Apple's source"
                         f" has moved or is ambiguous. Refusing rather than patching the wrong bytes.")
    out = src.replace(anchor, replacement, 1)
    if out == src or patched_token not in out:
        raise SystemExit(f"patch_d13_boot_path_970h: {name}: substitution did not insert the patched"
                         f" token; refusing")
    print(f"patch_d13_boot_path_970h: {name}: applied")
    return out, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_boot_path_970h.py <external/xnu-hd2-darwin13/xnu>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/" + FILE
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()

    src, _ = patch_once(src, ANCHOR3, REPLACEMENT3, TOKEN3, "edit 3 (pre-switch TLB flush)")

    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(src)

    # The by-value endpoint, re-derived: the group is now `mov r4,#0` / I-cache / TTBCR with NO
    # pre-switch `c8,c7,0`.  The only remaining whole-TLB invalidate is the post-build one at `:274`
    # (`mmu_initialized`).  One `c8,c7,0` in the whole file, not two.
    final = open(path, "r", encoding="utf-8", errors="surrogateescape").read()
    assert TOKEN3 in final, "edit 3 did not take"
    nac8 = final.count("mcr     p15, 0, r4, c8, c7, 0")
    assert nac8 == 1, f"expected exactly ONE c8,c7,0 (the post-build flush), found {nac8}"
    print(f"patch_d13_boot_path_970h: wrote {path} (one c8,c7,0 remains: the post-build flush)")
    return 0


if __name__ == "__main__":
    sys.exit(main())