#!/usr/bin/env python3
"""Stage 992's iOS 7 commpage alias into the D13 tree: open the six dormant IOS7LAB gates (idempotent).

The D13 tree already CONTAINS the 4570/HD2 line's proven fix for exactly the fault 991's press stopped
on - launchd aborted 231x at `0xffff4090` with a translation fault - and it is compiled out on D13 by
six board gates.  This patcher appends `|| (defined(STAGE90_XNU_COMMPAGE_ALIAS) &&
STAGE90_XNU_COMMPAGE_ALIAS)` to each of them, so `-DSTAGE90_XNU_COMMPAGE_ALIAS=1` opens them and a
build without the define emits the tree's original text byte for byte (`defined()` first, so the
identifier is never evaluated undefined - `mi4-off-option-two-spellings`).

The six lines are BYTE-IDENTICAL `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`:
  osfmk/arm/pmap.c              x4  (:205 the 0xFFFF4000U define, :2848 pmap_create_sharedpage's
                                    alias pmap_enter + its three asserts + the log marker, and the
                                    two pte-template attr sites :3242/:3434 that add
                                    L2_ACCESS_USER|L2_ACCESS_APX|L2_NX_BIT for the alias va)
  osfmk/arm/commpage/commpage.c x2  (:57 the iOS 7 count bytes at _COMM_PAGE_BASE_ADDRESS+0x34..0x36
                                    - libdispatch sizes its heap from +0x36 - and :99 the cache-line
                                    byte at +0x26 plus the `IOS7LAB commpage cpus:` report)

A SEVENTH LINE, the same block's boot marker, is staged as the equivalent `printf(...)`: this tree's
`osfmk/arm/pmap.c:409` is `#define kprintf(args...)` - a no-op for the WHOLE file - so the 4570 line's
kprintf marker can never reach pmap.o or the boot log here (by value: the string is in no object, OFF
or ON).  `printf` is live in the same file (`printf("npvhash=%d\n", ...)`, :2685).  The line sits
INSIDE the gated block, so switch OFF still compiles the tree's original bytes.

Why this is the fix, by value: `osfmk/arm/cpu_capabilities.h:13` puts this tree's control ABI at
`_COMM_PAGE32_BASE_ADDRESS` = 0x40000000 and `pmap.c`'s `leo_pte_build` grants it `L2_ACCESS_USER`,
so a D13 boot WITHOUT the alias maps the commpage in userspace only at 0x40000000 - while the real
iOS 7 dyld reads it at 0xffff4000 (the `_COMM_PAGE_*` addresses in the shared cache's libsystem), the
page the 992 fix maps there in kernel_pmap, user read-only and XN.  0xffff4000 is free to nest:
`arm_vm_init.c:413-423` maps the high vectors at 0xffff0000 as an L2_SMALL_PAGE, so the L1 slot for
the region is an L2 table and slot 0xf4 is empty.

Idempotent, LINE-based (`mi4-an-edit-on-a-prefix-deletes-the-suffix`: the old line is a PREFIX of
the new one, so a substring replace would re-match its own output).  Each file's match count is
asserted against the exact expected total; a file with a partial or double edit is REFUSED, not
re-guessed.  Re-run: counts say "already staged" and nothing is written.

993 ADDITIONS (2026-10-12) - the 992 press REGRESSED, and by value the cause was the premise this
docstring used to state at line 30-32 ("slot 0xf4 is empty"): it was never INITIALIZED.  arm_vm_init
steals one page for the high-vector L2 table and writes exactly ONE slot (VECTORS_BASE, 0xf0); the
other 255 are raw DRAM. 992's alias pmap_enter was the FIRST enter into slot 0xf4, read the leftover
0xFFD5A0xx as an "old mapping" and died in pmap_enter_options (kernel data abort at VA 0xc1233b14,
pc 0x80028b90).  So this patcher now also stages, gated on the SAME switch:
  osfmk/arm/arm_vm_init.c   - zero the stolen L2 page right after `va_vecpt` is computed (the
                              tree's own convention: l2_cache_to_range(zero=TRUE),
                              pmap_grab_page's bzero), reporting nonzero slots before/after by value
  osfmk/arm/pmap.c          - the alias block MEASURES slot 0xf4 before the enter (pre/post prints)
                              and fail-soft clears it if nonzero, turning a missed zero into a
                              reading instead of a second panic
Each insertion is anchored on ONE exact line, refuses any other state, and is idempotent.
"""
import sys
from pathlib import Path

OLD = "#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO"
NEW = ("#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO"
       " || (defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS)")

GATES = {
    "osfmk/arm/pmap.c": (4, "IOS7LAB_COMMPAGE_ALIAS"),
    "osfmk/arm/commpage/commpage.c": (2, "IOS7LAB commpage cpus"),
}

# The block's boot marker, staged as a LIVE printf (pmap.c:409's `#define kprintf(args...)` is a no-op
# for the whole file).  Both literals carry the source's two-character \n escape, not a newline.
MARKER_OLD = '    kprintf("IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN\\n");'
MARKER_NEW = '    printf("IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN\\n");'


def patch_marker(path: Path) -> bool:
    """Stage the alias block's boot marker as a LIVE printf.  True if a write happened.

    Same LINE discipline as the gates (`mi4-an-edit-on-a-prefix-deletes-the-suffix`: `printf(` is a
    SUFFIX of `kprintf(`, so this must be whole-line equality, never a substring replace), and any
    state other than (one kprintf line) or (one printf line) is REFUSED, not re-guessed.
    """
    text = path.read_text()
    lines = text.split("\n")
    at_old = [i for i, ln in enumerate(lines) if ln == MARKER_OLD]
    at_new = [i for i, ln in enumerate(lines) if ln == MARKER_NEW]
    if not at_old and len(at_new) == 1:
        print(f"  {path}: boot marker already staged as printf (line {at_new[0] + 1})")
        return False
    if len(at_old) != 1 or at_new:
        print(f"patch_d13_commpage_alias: REFUSING: {path} has {len(at_old)} kprintf + {len(at_new)} "
              f"printf marker line(s), expected exactly one of the two - the alias block was edited "
              f"by hand; not repaired in place", file=sys.stderr)
        raise SystemExit(5)
    lines[at_old[0]] = MARKER_NEW
    path.write_text("\n".join(lines))
    print(f"  {path}: boot marker staged as printf (line {at_old[0] + 1}; kprintf is a no-op at :409)")
    return True


def patch_one(path: Path, expect: int, anchor: str) -> bool:
    """True if a write happened; False if already staged.  Raises SystemExit on every refusal."""
    text = path.read_text()
    if anchor not in text:
        print(f"patch_d13_commpage_alias: REFUSING: {path} carries no [{anchor}] - this is not the "
              f"tree this patch was written for", file=sys.stderr)
        raise SystemExit(3)
    lines = text.split("\n")
    at_old = [i for i, ln in enumerate(lines) if ln == OLD]
    at_new = [i for i, ln in enumerate(lines) if ln == NEW]
    if len(at_old) == 0 and len(at_new) == expect:
        print(f"  {path}: already staged ({len(at_new)} gates carry the switch)")
        return False
    if len(at_old) + len(at_new) != expect:
        print(f"patch_d13_commpage_alias: REFUSING: {path} has {len(at_old)} original + "
              f"{len(at_new)} staged gate lines, expected {expect} total - a partial or double edit "
              f"is not repaired in place", file=sys.stderr)
        raise SystemExit(4)
    for i in at_old:
        lines[i] = NEW
    path.write_text("\n".join(lines))
    print(f"  {path}: {len(at_old)} gate line(s) opened, at line(s) "
          f"{', '.join(str(i + 1) for i in at_old)}")
    return True


# ---- 993: the stolen high-vector L2 TABLE is raw DRAM --------------------------------------------
# 992's press died in `pmap_enter_options` (kernel data abort, pc 0x80028b90, faulting VA 0xc1233b14;
# 2026-10-12): the alias `pmap_enter` was the FIRST EVER enter into the 0xFFFF4xxx slot of the L2
# table `arm_vm_init` steals for the high vectors.  That table had exactly ONE slot written -
# VECTORS_BASE 0xFFFF0000, slot 0xf0 - and slot 0xf4 (0xFFFF4000) still held raw DRAM (0xFFD5A0xx).
# pmap_enter_options read that leftover as an "old mapping", computed pa_index(0xFFD5A000) = 0x7FD5A
# and indexed pv_head_table 523,610 entries deep - the table holds 123,904 (0x1E400 pages of 484 MiB)
# - so the pv_h->pmap load landed on the unmapped VA 0xc1233b14 and the kernel aborted.
# Every other table in the boot path is zeroed: `l2_cache_to_range(zero=TRUE)` (whose own comment,
# arm_vm_init.c:250, warns about exactly this "may have been used by the bootloader" case) and
# `pmap_grab_page`'s bzero (pmap.c:2769) for `pmap_expand`.  The stolen vector table had no such
# treatment.  993 zeroes it and REPORTS the pre/post counts by value, and the alias block MEASURES
# (and fail-soft clears) the slot it is about to enter.
VMINIT_ANCHOR = "    va_vecpt = (vm_offset_t *)(phys_to_virt(vecpt_start) + pte_offset(VECTORS_BASE));"
VMINIT_DONE_MARK = "        uint32_t *vecl2 = (uint32_t *)phys_to_virt(vecpt_start);"
VMINIT_BLOCK = r'''#if defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS
    /* 993: the stolen page is RAW DRAM, not a zeroed table - only the VECTORS_BASE slot written
     * below is initialized; the other 255 slots of [0xFFFF0000+4K, 0xFFFFFFFF] keep whatever the
     * last writer of this physical page left behind. 992's press proved it by value: the first
     * pmap_enter into such a slot (the iOS 7 commpage alias at 0xFFFF4000 = slot 0xf4) read a
     * leftover 0xFFD5A000 as an "old mapping" and indexed pv_head_table 523,610 entries deep (the
     * table holds 123,904), faulting on the unmapped VA 0xc1233b14 in pmap_enter_options - kernel
     * data abort, panic (2026-10-12). Every other table in this boot path is zeroed -
     * l2_cache_to_range(zero=TRUE), whose own comment (arm_vm_init.c:250) warns about exactly this
     * "used by the bootloader" case, and pmap_grab_page's bzero for pmap_expand; this one was not.
     * The report below is the measurement; the bzero is the fix. */
    {
        uint32_t *vecl2 = (uint32_t *)phys_to_virt(vecpt_start);
        uint32_t nz_before = 0, nz_after = 0, shown = 0;

        for (uint32_t i = 0; i < (L2_SIZE / sizeof(uint32_t)); i++)
            if (vecl2[i] != 0)
                nz_before++;
        kdb_printf("IOS7LAB vecl2: %u nonzero slot(s) in the stolen high-vector L2 table before zeroing\n",
                   nz_before);
        for (uint32_t i = 0; i < (L2_SIZE / sizeof(uint32_t)); i++)
            if (vecl2[i] != 0 && shown++ < 16)
                kdb_printf("IOS7LAB vecl2: slot 0x%02x = 0x%08x\n", i, vecl2[i]);

        bzero((void *)vecl2, L2_SIZE);

        for (uint32_t i = 0; i < (L2_SIZE / sizeof(uint32_t)); i++)
            if (vecl2[i] != 0)
                nz_after++;
        kdb_printf("IOS7LAB vecl2: %u nonzero slot(s) after zeroing (VECTORS_BASE slot written next)\n",
                   nz_after);
    }
#endif'''
VMINIT_BLOCK_LINES = VMINIT_BLOCK.split("\n")

SHAREDPAGE_ANCHOR = "    assert(pmap_extract(kernel_pmap, IOS7LAB_COMMPAGE_ALIAS) == 0);"
SHAREDPAGE_DONE_MARK = "    uint32_t *lab_ptep = (uint32_t *)pmap_pte(kernel_pmap, IOS7LAB_COMMPAGE_ALIAS);"
SHAREDPAGE_PROBE = r'''    /* 993: MEASURE the alias va's L2 slot before the enter. The slot belongs to the page arm_vm_init
     * steals for the high-vector table; as far as that table is concerned it was raw DRAM (only the
     * vectors' own slot is written there). 992's press read a leftover 0xFFD5A000 from it as an "old
     * mapping" and died in pmap_enter_options (pai 0x7FD5A, 4.2x past the pv table's 123,904
     * entries: kernel data abort at 0xc1233b14). 993 zeroes the whole table in arm_vm_init; this
     * print is the reading that says so, and the clear turns a missing zero into a bad reading
     * instead of a second panic (fail-soft: the boot continues without the alias). */
    uint32_t *lab_ptep = (uint32_t *)pmap_pte(kernel_pmap, IOS7LAB_COMMPAGE_ALIAS);
    printf("IOS7LAB commpage alias: L2 slot pre = 0x%08x\n",
           lab_ptep ? *lab_ptep : 0xFFFFFFFFu);
    if (lab_ptep && *lab_ptep) {
        printf("IOS7LAB commpage alias: stale slot cleared (was 0x%08x)\n", *lab_ptep);
        *lab_ptep = 0;
    }'''
SHAREDPAGE_PROBE_LINES = SHAREDPAGE_PROBE.split("\n")

SHAREDPAGE_POST_ANCHOR = "    uint32_t lab_pte = *(uint32_t *)pmap_pte(kernel_pmap, IOS7LAB_COMMPAGE_ALIAS);"
SHAREDPAGE_POST_DONE_MARK = "    printf(\"IOS7LAB commpage alias: L2 slot post = 0x%08x (commpage pa 0x%08x)\\n\","
SHAREDPAGE_POST = r'''    printf("IOS7LAB commpage alias: L2 slot post = 0x%08x (commpage pa 0x%08x)\n",
           lab_pte, commpage->phys_page << PAGE_SHIFT);'''
SHAREDPAGE_POST_LINES = SHAREDPAGE_POST.split("\n")


def insert_lines(path: Path, anchor: str, block_lines, done_mark: str, what: str) -> bool:
    """Insert `block_lines` immediately AFTER the single anchor line.  True if a write happened.

    Idempotence is keyed on `done_mark` (one of the inserted lines), NOT on the anchor, which stays
    in the file below the insertion.  Any state other than (mark absent, one anchor) or (mark
    present, one anchor + one mark) is REFUSED, never re-guessed - the same discipline as the gates
    (`mi4-an-edit-on-a-prefix-deletes-the-suffix`).
    """
    text = path.read_text()
    lines = text.split("\n")
    at_anchor = [i for i, ln in enumerate(lines) if ln == anchor]
    at_done = [i for i, ln in enumerate(lines) if ln == done_mark]
    if len(at_done) == 1 and len(at_anchor) == 1:
        print(f"  {path}: {what} already staged (line {at_done[0] + 1})")
        return False
    if len(at_done) != 0 or len(at_anchor) != 1:
        print(f"patch_d13_commpage_alias: REFUSING: {path} has {len(at_anchor)} anchor + {len(at_done)} "
              f"{what} line(s), expected exactly one anchor and zero-or-one {what} - the region was "
              f"edited by hand; not repaired in place", file=sys.stderr)
        raise SystemExit(6)
    lines[at_anchor[0] + 1:at_anchor[0] + 1] = block_lines
    path.write_text("\n".join(lines))
    print(f"  {path}: {what} staged after line {at_anchor[0] + 1} ({len(block_lines)} lines)")
    return True


def patch_vm_init(path: Path) -> bool:
    return insert_lines(path, VMINIT_ANCHOR, VMINIT_BLOCK_LINES, VMINIT_DONE_MARK,
                        "993 vector-L2 zeroing block")


def patch_sharedpage_probe(path: Path) -> bool:
    return insert_lines(path, SHAREDPAGE_ANCHOR, SHAREDPAGE_PROBE_LINES, SHAREDPAGE_DONE_MARK,
                        "993 alias-slot probe")


def patch_sharedpage_post(path: Path) -> bool:
    return insert_lines(path, SHAREDPAGE_POST_ANCHOR, SHAREDPAGE_POST_LINES,
                        SHAREDPAGE_POST_DONE_MARK, "993 alias-slot post report")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_commpage_alias.py <xnu-tree-root>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    wrote = []
    for rel, (expect, anchor) in GATES.items():
        path = root / rel
        if not path.is_file():
            print(f"patch_d13_commpage_alias: no {rel} under {root} (D13 tree not provisioned?)",
                  file=sys.stderr)
            return 2
        if patch_one(path, expect, anchor):
            wrote.append(rel)
    if patch_marker(root / "osfmk/arm/pmap.c"):
        wrote.append("osfmk/arm/pmap.c (boot marker -> printf)")
    if patch_vm_init(root / "osfmk/arm/arm_vm_init.c"):
        wrote.append("osfmk/arm/arm_vm_init.c (993 vector-L2 zeroing)")
    if patch_sharedpage_probe(root / "osfmk/arm/pmap.c"):
        wrote.append("osfmk/arm/pmap.c (993 alias-slot probe)")
    if patch_sharedpage_post(root / "osfmk/arm/pmap.c"):
        wrote.append("osfmk/arm/pmap.c (993 alias-slot post report)")
    print(f"patch_d13_commpage_alias: {'staged ' + ', '.join(wrote) if wrote else 'nothing to do'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())