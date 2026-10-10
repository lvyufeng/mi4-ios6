#!/usr/bin/env python3
"""Make Darwin-13's `__start` boot path behave like 4570's (experiment 970g).

WHY.  The whole Darwin-13 entry->kernel line has emitted ZERO console output on hardware (970/968's two
presses, entry bin `7107b998`).  970f named an "occupied console slot" as the cause; that is RETRACTED
(doc section 6f).  The real cause is that D13's `__start` **never runs its boot path**, and the entry's
console window guard then refuses the table that is left.  Two edits make D13 match the 4570 tree that
boots:

  EDIT 1 - remove the MMU fast path.  `osfmk/arm/locore.s:57-61`:

      /* If MMU is initialized, go the quick way. */
      mrc     p15, 0, r4, c1, c0, 0
      and     r4, #0x1
      cmp     r4, #0x1
      beq     mmu_initialized          <-- taken, because the payload jumps in with SCTLR.M=1

  The payload always enters with the MMU on, so this branch is ALWAYS taken: the boot path below
  `mmu_reinitialize:` - which builds the boot table at `topOfKernelData` and sets
  `TTBR0 = topOfKernelData | 0x18` (a HIGH base) - never runs.  TTBR0 stays the payload's table,
  `stage90_l1_table`, which lives at PA `0x6c4000`, BELOW `0x80000000` (the real 970 capture reads
  `mmu_ttbr0_after=0x006c4000`, `mmu_l1_table=0x006c4000`).  The entry's `entry_live_init` refuses a
  table outside the kernel window (`entry_stubs.c:2308`: `l1 < 0x80000000u || (l1 & 0x3fffu)`) - it
  refuses with `why=1`, and the record that would report the refusal travels through the console it just
  refused, so the run is silent.  4570's `osfmk/arm/start.s:86` `_start` has NO such branch: it writes
  `TTBR0` unconditionally.  Removing the branch restores 4570's behaviour.

WHY THAT ALONE IS NOT ENOUGH - EDIT 2.  The 4570 tree that boots also **zeroes the boot translation
table** at `start.s:159-167` (the `invalidate_tte:` loop: `ARM_TTE_TYPE_FAULT` = 0 over 10240 entries =
`(PGBYTES/4 + PGBYTES/4*4)*2` = `0xa000` bytes = this project's `STAGE90_XNU_ENTRY_TABLE_BYTES`).  The
entry's own source states the dependency (`entry_stubs.c:718`): *"nothing the payload plants in the boot
table before the jump can survive, because `start.s`'s `invalidate_tte` writes FAULT over 10240 entries
starting at `topOfKernelData`"*.  That is TRUE of 4570 and FALSE of D13, whose `locore.s` boot path goes
from the TTBR0 write straight into the `map:` loop - it maps `[physBase, physBase+memSize)` and leaves
every slot above that holding whatever DRAM had there.  The console's own L1 slot is `0xde500000 >> 20`
= index `0xde5`, far above `memSize` (16 MiB), so it is left stale; `entry_section_install` refuses an
occupied slot, so even with a HIGH table the console would still be refused.  The proof it must be zeroed
is in the tree: the working 4570 capture reads `xnu_live_slot_before=0x00000000` - the slot was zero
before the entry's install.

THE FIX, exactly 4570's ordering - write TTBR0, zero the table, then map the PC section and the rest:

    write TTBR0 = topOfKernelData | 0x18
    zero [topOfKernelData, topOfKernelData + 10240 words)   <-- EDIT 2 (D13 omitted it)
    map the PC's own 1 MB section (already in locore.s)     <-- so the switch cannot fault
    map [physBase, physBase + memSize)                      <-- the existing `map:` loop

The PC's section is mapped *after* the zeroing and *before* the table is walked again, so the running
PC's translation cannot fault across the TTBR0 change (the section TLB entry taken at the payload's
`invalidate_tlbs`, before the TTBR0 write, is still cached - `entry_stubs.c`'s own comment says a
cacheable walk is coherent).  This is 4570's proven order, on 4570's proven code.

WHY THIS IS THE WHOLE FIX FOR THE FIRST WINDOW.  `xnu_live_console` is the FIRST key any run writes, and
it fires inside `arm_init` before `PE_init_platform` (`arm_init.c:150`) and hence before `arm_vm_init`
(`:168`).  So once the boot table exists and the console slot is free, the init block and the first
wrapped probes publish under it.  (Beyond `arm_vm_init` is a SECOND, separate D13 defect: D13's
`arm_vm_init.c:352-353` `bzero`s a fresh `cpu_ttb` where 4570 `bcopy`s the boot table - a later rung.
This patch is deliberately scoped to the first window.)

WHAT IT DOES NOT DO.  It touches only `osfmk/arm/locore.s`, which is D13's entry file alone - not 4570
(4570 has neither the fast path nor a missing zero; its entry is `start.s`).  Edit 2 is additive (a
zeroing loop copied from 4570); Edit 1 only removes dead-for-4570 code, so it cannot introduce a new
fault site.

Idempotent and re-appliable: each edit's absence is its "already patched" marker, and each anchor is
matched whole so a source update that moves it refuses loudly.  `tools/stage_d13_boot_path.sh` applies
it; the proof of the change is by VALUE - the linked D13 entry image's `__start` must no longer carry
`beq mmu_initialized`, and must carry the zeroing loop's `subs`/`bne` before the `map:` loop.
"""
import sys

FILE = "osfmk/arm/locore.s"

# ---------------------------------------------------------------------------------------------------
# EDIT 1 - the MMU fast path, verbatim, matched whole (leading four spaces, exact mnemonic spacing).
# A source update that re-spaces it makes the count != 1 and we refuse rather than patch the wrong bytes.
# ---------------------------------------------------------------------------------------------------
ANCHOR1 = (
    "    /* If MMU is initialized, go the quick way. */\n"
    "    mrc     p15, 0, r4, c1, c0, 0\n"
    "    and     r4, #0x1\n"
    "    cmp     r4, #0x1\n"
    "    beq     mmu_initialized\n"
)
REPLACEMENT1 = (
    "    /* 970g edit 1: the fast path is REMOVED (tools/patch_d13_boot_path.py).\n"
    "     *\n"
    "     * D13's `__start` branched to `mmu_initialized` when the MMU was already on, and the payload\n"
    "     * always jumps in with SCTLR.M=1 - so this branch was ALWAYS taken and the boot path below\n"
    "     * was dead.  That left the payload's LOW table (`0x6c4000`) as TTBR0 and never built the boot\n"
    "     * table at `topOfKernelData`, so the entry's console window guard refused (`entry_stubs.c:2308`)\n"
    "     * and the whole Darwin-13 entry line was silent (doc section 6f).\n"
    "     *\n"
    "     * 4570's `start.s` has no such branch: it writes TTBR0 = topOfKernelData|attr unconditionally\n"
    "     * and builds the boot table, and it boots.  This removes the branch so D13 does the same. */\n"
)
MARKER1_GONE = "beq     mmu_initialized"
MARKER1_PRESENT = "mmu_reinitialize:"
TOKEN1 = "970g edit 1: the fast path is REMOVED"

# ---------------------------------------------------------------------------------------------------
# EDIT 2 - zero the boot translation table after the TTBR0 write, before any section is mapped.  This is
# 4570 start.s's `invalidate_tte:` loop (10240 entries = 0xa000 bytes = STAGE90_XNU_ENTRY_TABLE_BYTES),
# which D13's boot path omits.  Registers used are the ones free at this point (r2, r3, r5): r4
# (topOfKernelData), r10 (virtBase), r11 (physBase), r12 (memSize) and r0 must all survive for the
# `map:` loop below, so none of them is touched.  r5 is copied from r4 and used as the running pointer;
# r5 held base|0x18 only until the `mcr` above consumed it.
# ---------------------------------------------------------------------------------------------------
ANCHOR2 = (
    "    /* Now, we have to set our TTB to this value. */\n"
    "    mcr     p15, 0, r5, c2, c0, 0\n"
    "\n"
    "    /* Make our section mappings now. */\n"
)
REPLACEMENT2 = (
    "    /* Now, we have to set our TTB to this value. */\n"
    "    mcr     p15, 0, r5, c2, c0, 0\n"
    "\n"
    "    /* 970g edit 2: zero the boot translation table, as 4570's start.s does.\n"
    "     *\n"
    "     * 4570 start.s:159-167 writes ARM_TTE_TYPE_FAULT (0) over 10240 entries starting at\n"
    "     * topOfKernelData; that is (PGBYTES/4 + PGBYTES/4*4)*2 = 0xa000 bytes, the same table size the\n"
    "     * entry layout reserves (STAGE90_XNU_ENTRY_TABLE_BYTES).  D13's boot path omitted the loop, so\n"
    "     * every slot the `map:` loop below does not reach - including the entry console's own L1 slot\n"
    "     * (0xde500000 >> 20 = 0xde5), which is far above memSize - kept stale DRAM.  The entry's\n"
    "     * `entry_section_install` refuses an occupied slot (entry_stubs.c:2122), so without this the\n"
    "     * console is refused even with a HIGH table; the working 4570 capture reads\n"
    "     * xnu_live_slot_before=0x00000000, i.e. the slot was zero before the install.\n"
    "     *\n"
    "     * Registers: r5 <- r4 (the table base); the pointer walks 10240 words.  r4, r10, r11, r12 and\n"
    "     * r0 are untouched for the `map:` loop; r2/r3/r5 are free here. */\n"
    "    mov     r5, r4              /* r5 = boot table base (topOfKernelData) */\n"
    "    mov     r3, #0              /* fault template: ARM_TTE_TYPE_FAULT == 0 */\n"
    "    mov     r2, #1024           /* r2 = PGBYTES >> 2 (TTEs per page) */\n"
    "    add     r2, r2, r2, LSL #2  /* r2 *= 5 (8 ttes + 2 ptes to clear) */\n"
    "    mov     r2, r2, LSL #1      /* r2 *= 2 -> 10240 words = 0xa000 bytes */\n"
    "_970g_zero_tte:\n"
    "    str     r3, [r5], #4\n"
    "    subs    r2, r2, #1\n"
    "    bne     _970g_zero_tte\n"
    "\n"
    "    /* Make our section mappings now. */\n"
)
MARKER2_GONE = "_970g_zero_tte"
TOKEN2 = "_970g_zero_tte"


def patch_once(src, anchor, replacement, patched_token, name):
    """Apply one delete-anchor/insert-replacement edit.  Returns (new_src, changed) or raises.

    `patched_token` is a string that appears ONLY in `replacement`; its presence means this edit has
    already run (idempotence), and its absence means we must find `anchor` exactly once.  Keying the
    "already patched" test on the anchor's absence would be wrong here, because for edit 1 the anchor's
    distinguishing line (`beq mmu_initialized`) is also what a naive absence test would look for.
    """
    if patched_token in src:
        print(f"patch_d13_boot_path: {name}: already applied (idempotent, left alone)")
        return src, False
    n = src.count(anchor)
    if n != 1:
        raise SystemExit(f"patch_d13_boot_path: {name}: {n} matches for the anchor; Apple's source has"
                         f" moved or is ambiguous. Refusing rather than patching the wrong bytes.")
    out = src.replace(anchor, replacement, 1)
    if out == src or patched_token not in out:
        raise SystemExit(f"patch_d13_boot_path: {name}: substitution did not insert the patched token;"
                         f" refusing")
    print(f"patch_d13_boot_path: {name}: applied")
    return out, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_boot_path.py <external/xnu-hd2-darwin13/xnu>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/" + FILE
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()

    src, _ = patch_once(src, ANCHOR1, REPLACEMENT1, TOKEN1, "edit 1 (fast path)")
    src, _ = patch_once(src, ANCHOR2, REPLACEMENT2, TOKEN2, "edit 2 (zero boot table)")

    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(src)

    # The two by-value endpoints, re-derived: the branch is gone, the zeroing loop is present, and the
    # boot-path label is still the thing the branch would have skipped to.
    final = open(path, "r", encoding="utf-8", errors="surrogateescape").read()
    assert MARKER1_GONE not in final, "edit 1 did not take"
    assert MARKER2_GONE in final, "edit 2 did not take"
    assert MARKER1_PRESENT in final, "the boot path label vanished"
    print(f"patch_d13_boot_path: wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())