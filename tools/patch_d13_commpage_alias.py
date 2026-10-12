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
    print(f"patch_d13_commpage_alias: {'staged ' + ', '.join(wrote) if wrote else 'nothing to do'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())