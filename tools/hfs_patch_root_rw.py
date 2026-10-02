#!/usr/bin/env python3
"""Clear MNT_RDONLY in hfs_mountroot so the HFS+ root can be mounted read-write (experiment 905, B2).

WHY.  A root mount is read-only BY CONSTRUCTION: `vfs_rootmountalloc_internal` hard-codes
`mp->mnt_flag = MNT_RDONLY | MNT_ROOTFS` (`bsd/vfs/vfs_subr.c:989`), and `hfs_mountfs` latches the
read-only decision ONCE, early, into `HFS_READ_ONLY` when it reads `vfs_isrdonly(mp)` at
`bsd/hfs/hfs_vfsops.c:1312`.  So the write path 905 builds in the ladder and the strategy is never
reached: HFS refuses every write before it reaches `st_media_strategy`'s `B_WRITE` branch.  Nothing
else needs to change - the volume is NOT journaled and was unmounted cleanly (volume header
`attributes = 0x80000100`: `kHFSVolumeUnmountedBit` set, journaled bit clear), so rw is not blocked by
journal replay, and `DKIOCISWRITABLE` (honored at `hfs_vfsops.c:1558`) will be answered `1` by the
card unit once `STAGE90_XNU_HDD_WRITE=1`.

WHAT.  ONE statement, immediately BEFORE the `hfs_mountfs` call in `hfs_mountroot` (the call whose
`NULL` first argument is the root marker), guarded so the port-off image is byte-identical:

    #if STAGE90_HFS_ROOT_RW
        vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);
    #endif

`hfs_mountroot` is the ONLY mount entry for the root, and `vfs_clearflags` is declared by
`bsd/sys/mount.h` (already in the file's include set).  `STAGE90_HFS_ROOT_RW` is added to the build's
DEFINES array by `tools/build_xnu_arm_kernel.sh` only when the port is on AND
`STAGE90_XNU_HFS_ROOT_RW=1`, so a normal build sees the `#if` false and the object is byte-for-byte
the port-off one in every image that does not carry the arm.

WHY NOT `vfs_subr.c`.  Clearing the flag in `vfs_rootmountalloc_internal` would move EVERY mount in the
kernel to read-write, not just HFS's root - the general VFS layer is not this rung's to change.  The
clear is confined to the one function that mounts the one filesystem this rung writes.

Idempotent and re-appliable: a site already carrying the clear is left alone; a missing anchor is a
loud error, so a source update cannot silently drop the statement.  This mirrors
`tools/patch_vfs_conf_hfs_row.py` and `tools/hfs_patch_mount_markers.py`: one tracked definition of an
edit to the untracked, re-provisionable `external/` tree.  `tools/check_hfs_staged.sh` re-derives this
file's effect.
"""
import re
import sys

FILE = "bsd/hfs/hfs_vfsops.c"
# The anchor is the `hfs_mountroot` body's `hfs_mountfs(RVP, MP, NULL, 0, CONTEXT)` call, matched whole
# and end-trimmed.  `hfs_mountfs(` occurs at every other call site too; the root one is unique because
# of its `NULL` second argument (the root marker) and because it is the only one inside an `if ((error
# = hfs_mountfs(rvp, mp, NULL, 0, context)))`.  Anchoring on the FULL `if (` line keeps the clear
# BEFORE the call rather than between the call and the error test.
ANCHOR = "\tif ((error = hfs_mountfs(rvp, mp, NULL, 0, context))) {"
MARK = "STAGE90_HFS_ROOT_RW"
CLEAR = ("#if STAGE90_HFS_ROOT_RW\n"
         "\t/* 905: the root mount is read-only by construction (vfs_subr.c hard-codes MNT_RDONLY |\n"
         "\t * MNT_ROOTFS); clear it here, before hfs_mountfs latches HFS_READ_ONLY from it, so the\n"
         "\t * card unit's write path is reachable. Guarded: the port-off image is byte-identical. */\n"
         "\tvfs_clearflags(mp, (u_int64_t)MNT_RDONLY);\n"
         "#endif\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: hfs_patch_root_rw.py <external/xnu-4570.1.46>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/" + FILE
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()

    if ANCHOR not in src:
        print(f"hfs_patch_root_rw: anchor not found in {path}:\n  {ANCHOR!r}", file=sys.stderr)
        print("  The hfs_mountroot hfs_mountfs call has moved or changed shape; refusing rather than"
              " leaving the root read-only while a record claims it is writable.", file=sys.stderr)
        return 1

    if MARK in src:
        # Already patched.  Verify the clear is still adjacent to the anchor (a hand-edit that moved it
        # away would leave the marker present but the property false - the class the check re-derives).
        idx = src.index(MARK)
        anchor_idx = src.index(ANCHOR)
        if not (0 <= anchor_idx - idx < 400):
            print(f"hfs_patch_root_rw: {MARK} is present but NOT adjacent to the hfs_mountfs call in"
                  f" {path}; refusing so a moved clear is not read as a present one.", file=sys.stderr)
            return 1
        print(f"hfs_patch_root_rw: {FILE} already carries the clear (idempotent, left alone)")
        return 0

    patched = src.replace(ANCHOR, CLEAR + ANCHOR, 1)
    if patched == src:
        print("hfs_patch_root_rw: substitution made no change; refusing", file=sys.stderr)
        return 1
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(patched)
    print(f"hfs_patch_root_rw: cleared MNT_RDONLY before hfs_mountfs in {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())