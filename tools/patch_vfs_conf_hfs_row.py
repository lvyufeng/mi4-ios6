#!/usr/bin/env python3
"""Put 2050's HFS row into 4570's STATIC `vfstbllist[]`, before `mockfs`.

    tools/patch_vfs_conf_hfs_row.py PATH/TO/bsd/vfs/vfs_conf.c

WHY THE ROW MUST BE STATIC AND BEFORE MOCKFS (experiment 874).  The root filesystem is chosen by
`vfs_mountroot`, which walks `vfstbllist[]` and takes the FIRST row that mounts without error, then
`break`s.  A row registered at boot (`vfs_fsadd`) is APPENDED after the last static row
(`vfs_init.c:578` fills the first empty slot), so it lands *after* `mockfs` - and `mockfs` always
mounts, so a registered HFS row would be recorded, printed and never tried as root.  The row the boot
actually reaches must therefore be a static entry ahead of `mockfs`.

WHY IT IS A TRACKED PATCH AND NOT A HAND EDIT.  `external/` is a re-provisionable checkout, so an edit
made in the tree is lost on the next provision; the row is applied by `tools/stage_hfs.sh` beside the
other two port edits, from this one file.  It is guarded by `STAGE90_HFS_ROOT`, which
`tools/build_xnu_arm_kernel.sh` defines for every compilation exactly when the port is on
(`STAGE90_XNU_HFS=1`), so the patch is INERT when the port is off: the `#if` is false, nothing between
it and its `#endif` is compiled, and `vfs_conf.c` is byte-for-byte what 4570 shipped.

THE ROW.  2050's own row (`xnu-2050.18.24/bsd/vfs/vfs_conf.c:119`) is copied field-for-field, then
adapted to 4570's table, which is the SAME `struct vfstable` but with one extra trailing field
`vfc_sysctl` (`mount_internal.h:316`) - so the initialiser gains a trailing `, NULL` (a positional
initialiser against the bigger struct would zero-fill silently, which is right only by coincidence).
The type number is `FT_HFS = 17` (2050's own; 4570's highest is `FT_MOCKFS = 0x6D6F636B`, so 17 is
free).  `VFS_THREAD_SAFE_FLAG` is DROPPED: it is a 2050-local macro (`VFC_VFSTHREADSAFE`, 0x200) that
4570's flag set does not carry, and 874's instruction is not to invent a bit.  Every remaining flag
name - `MNT_LOCAL`, `MNT_DOVOLFS`, `VFC_VFSLOCALARGS`, `VFC_VFSREADDIR_EXTENDED`, `VFC_VFS64BITREADY`,
`VFC_VFSVNOP_PAGEINV2`, `VFC_VFSVNOP_PAGEOUTV2` - is present in 4570 with the same value.

Idempotent: a file that already carries the marker is left alone.  Refuses (exit 2) if neither the
marker nor the anchor `#if MOCKFS` block is found, because that means the tree moved under it.
"""
import sys

MARK = "vfs_conf.c HFS root row (tools/patch_vfs_conf_hfs_row.py)"

# The anchor is the `mockfs` block's own preprocessor line, which is unique in the file and sits
# exactly where the new row must go (immediately before it).
ANCHOR = "#if MOCKFS\n\t/* If we are configured for it, mockfs should always be the last standard entry"

# 1. The `extern` declarations, inserted after the MOCKFS extern block (so `mockfs_vfsops` still
#    reads first).  Anchored on that block's full text: `#endif /* MOCKFS */` alone appears FOUR times
#    in the file (measured), so a shorter anchor would be ambiguous.
EXTERN_ANCHOR = """#if MOCKFS
extern\tstruct vfsops mockfs_vfsops;
extern\tint mockfs_mountroot(mount_t, vnode_t, vfs_context_t);
#endif /* MOCKFS */
"""
EXTERN_ADD = """
#if STAGE90_HFS_ROOT
/* PORT SHIM (%s): 2050's HFS vector and root-mount entry
 * (xnu-2050.18.24/bsd/hfs/hfs.h:719).  The port brings both. */
extern\tstruct vfsops hfs_vfsops;
extern\tint hfs_mountroot(mount_t, vnode_t, vfs_context_t);
#endif /* STAGE90_HFS_ROOT */
""" % MARK

# 2. The type number, beside the others in `enum fs_type_num`.  Anchored on the FT_MOCKFS row, which is
#    the LAST enumerator and carries no trailing comma - so the replacement adds one before FT_HFS.
FT_ANCHOR = "\tFT_MOCKFS  = 0x6D6F636B\n"
FT_REPLACE = "\tFT_MOCKFS  = 0x6D6F636B,\n" \
             "\tFT_HFS     = 17\t\t/* 2050 xnu-2050.18.24/bsd/vfs/vfs_conf.c:119; free in 4570 */\n"

# 3. The row itself, before the mockfs block.
ROW_ADD = """#if STAGE90_HFS_ROOT
	/* PORT SHIM (%s).  2050's HFS row (xnu-2050.18.24/bsd/vfs/vfs_conf.c:119), adapted to 4570's
	 * table.  It is BEFORE
	 * mockfs on purpose: vfs_mountroot takes the first row that mounts and breaks, and mockfs always
	 * mounts, so a row after it is never tried as root (experiment 874).  VFS_THREAD_SAFE_FLAG is
	 * dropped - 4570's flag set has no such bit.  The trailing NULL is 4570's extra `vfc_sysctl`. */
	{ &hfs_vfsops, "hfs", FT_HFS, 0, (MNT_LOCAL | MNT_DOVOLFS), hfs_mountroot, NULL, 0, 0, VFC_VFSLOCALARGS | VFC_VFSREADDIR_EXTENDED | VFC_VFS64BITREADY | VFC_VFSVNOP_PAGEINV2 | VFC_VFSVNOP_PAGEOUTV2, NULL, 0, NULL},
#endif /* STAGE90_HFS_ROOT */
#if MOCKFS
	/* If we are configured for it, mockfs should always be the last standard entry""" % MARK


def main():
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    path = sys.argv[1]
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        sys.stderr.write("patch_vfs_conf_hfs_row: cannot read %s: %s\n" % (path, e))
        return 2

    if MARK in text:
        print("patch_vfs_conf_hfs_row: %s already carries the HFS root row; left alone" % path)
        return 0

    for anchor, what in ((ANCHOR, "the mockfs table block"),
                         (EXTERN_ANCHOR, "the MOCKFS extern block"),
                         (FT_ANCHOR, "the FT_MOCKFS type number")):
        if text.count(anchor) != 1:
            sys.stderr.write(
                "patch_vfs_conf_hfs_row: %s does not contain exactly one %s (found %d) - the tree "
                "moved under this patch; inspect it before re-running\n"
                % (path, what, text.count(anchor)))
            return 2

    text = text.replace(EXTERN_ANCHOR, EXTERN_ANCHOR + EXTERN_ADD, 1)
    text = text.replace(FT_ANCHOR, FT_REPLACE, 1)
    text = text.replace(ANCHOR, ROW_ADD, 1)

    open(path, "w", encoding="utf-8").write(text)
    print("patch_vfs_conf_hfs_row: put FT_HFS and the HFS root row into %s (marker: %s)" % (path, MARK))
    return 0


if __name__ == "__main__":
    sys.exit(main())