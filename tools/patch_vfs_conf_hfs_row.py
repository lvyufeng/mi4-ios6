#!/usr/bin/env python3
"""Put 2050's HFS row into 4570's STATIC `vfstbllist[]`, before `mockfs` - and the HFS vnode-op
descriptors into `vfs_opv_descs[]`.

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

WHY THE VNODE-OP DESCRIPTORS ARE PART OF THIS ROW (experiment 902).  The root row alone is not enough
to use the filesystem: `hfs_getnewvnode` builds each vnode with `vfsp.vnfs_vops = hfs_vnodeop_p`, and
`hfs_vnodeop_p` is a BSS global the ONLY writer of which is `vfs_opv_init()` walking the static
`vfs_opv_descs[]` table and allocating each descriptor's vector.  4570's table carries no HFS rows
(it has no HFS), so `hfs_vnodeop_p` stays NULL, the vnode's `v_op` is NULL, and the mount's first
B-tree node read (`BTOpenPath` -> the buffer cache -> `VNOP_STRATEGY`) dereferences `v_op[1]` at
address 4 - the 901 press's `fault_addr=0x4`, `pc` in `VNOP_STRATEGY+0x24`.  So the descriptors are
as load-bearing as the row, and they are patched HERE, in the one tracked place, for the same reason:
`external/` is re-provisionable.  2050's table has these four rows under `#if HFS`; the port takes
the three that are always present and the FIFO one under its own `#if FIFO`, both exactly as 2050
wrote them, guarded by the port's own `STAGE90_HFS_ROOT`.

Idempotent: a file that already carries the marker is left alone.  Refuses (exit 2) if neither the
marker nor the anchor `#if MOCKFS` block is found, because that means the tree moved under it.
"""
import sys

MARK = "vfs_conf.c HFS root row (tools/patch_vfs_conf_hfs_row.py)"
OPV_MARK = "vfs_conf.c HFS vnode-op descriptors (tools/patch_vfs_conf_hfs_row.py)"

# The anchor is the `mockfs` block's own preprocessor line, which is unique in the file and sits
# exactly where the new row must go (immediately before it).
ANCHOR = "#if MOCKFS\n\t/* If we are configured for it, mockfs should always be the last standard entry"

# The vnode-op descriptors: their `extern` block and their rows in `vfs_opv_descs[]`.  Both anchors are
# the existing MOCKFS sibling, which is unique in the file (measured).
OPV_ANCHOR = ("#if MOCKFS\n"
              "extern struct vnodeopv_desc mockfs_vnodeop_opv_desc;\n"
              "#endif /* MOCKFS */")
OPV_TBL_ANCHOR = ("#if MOCKFS\n"
                  "\t&mockfs_vnodeop_opv_desc,\n"
                  "#endif /* MOCKFS */")

OPV_ADD = """
#if STAGE90_HFS_ROOT
/* PORT SHIM (%s): 2050's HFS vnode-op descriptors (xnu-2050.18.24/bsd/vfs/vfs_conf.c, the `#if HFS`
 * block of vfs_opv_descs[]).  Without these rows `vfs_opv_init` never allocates hfs_vnodeop_p, so
 * `hfs_getnewvnode`'s `vnfs_vops = hfs_vnodeop_p` is NULL and the first B-tree read faults (902). */
extern struct vnodeopv_desc hfs_vnodeop_opv_desc;
extern struct vnodeopv_desc hfs_std_vnodeop_opv_desc;
extern struct vnodeopv_desc hfs_specop_opv_desc;
#if FIFO
extern struct vnodeopv_desc hfs_fifoop_opv_desc;
#endif /* FIFO */
#endif /* STAGE90_HFS_ROOT */
""" % OPV_MARK

OPV_TBL_ADD = """#if STAGE90_HFS_ROOT
	/* PORT SHIM (%s).  2050's own `#if HFS` rows, before mockfs like the root row.  Order among the
	 * op-vector descriptors does not affect the root choice (the root comes from vfstbllist[]); they
	 * are placed beside mockfs so the port's two additions read as one diff.  Without them
	 * `hfs_vnodeop_p` is never allocated and every HFS vnode's `v_op` is NULL (experiment 902). */
	&hfs_vnodeop_opv_desc,
	&hfs_std_vnodeop_opv_desc,
	&hfs_specop_opv_desc,
#if FIFO
	&hfs_fifoop_opv_desc,
#endif
#endif /* STAGE90_HFS_ROOT */
#if MOCKFS
	&mockfs_vnodeop_opv_desc,
#endif /* MOCKFS */""" % OPV_MARK

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

    changed = []

    # Group A - the root row (874): the FT_HFS number, its externs, and the row in vfstbllist[].
    # Applied only when its marker is absent, because a tree can carry this group and not the opv one.
    if MARK in text:
        print("patch_vfs_conf_hfs_row: %s already carries the HFS root row; left alone" % path)
    else:
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
        changed.append("FT_HFS and the HFS root row")
        print("patch_vfs_conf_hfs_row: put FT_HFS and the HFS root row into %s (marker: %s)" % (path, MARK))

    # Group B - the vnode-op descriptors (902): their externs and their rows in vfs_opv_descs[].
    # Separate marker so a tree patched by the root-row-only version (before 902) still receives it.
    if OPV_MARK in text:
        print("patch_vfs_conf_hfs_row: %s already carries the HFS vnode-op descriptors; left alone" % path)
    else:
        for anchor, what in ((OPV_ANCHOR, "the MOCKFS vnodeopv extern block"),
                             (OPV_TBL_ANCHOR, "the MOCKFS row in vfs_opv_descs[]")):
            if text.count(anchor) != 1:
                sys.stderr.write(
                    "patch_vfs_conf_hfs_row: %s does not contain exactly one %s (found %d) - the tree "
                    "moved under this patch; inspect it before re-running\n"
                    % (path, what, text.count(anchor)))
                return 2
        text = text.replace(OPV_ANCHOR, OPV_ANCHOR + OPV_ADD, 1)
        text = text.replace(OPV_TBL_ANCHOR, OPV_TBL_ADD, 1)
        changed.append("the HFS vnode-op descriptors")
        print("patch_vfs_conf_hfs_row: put the HFS vnode-op descriptors into %s (marker: %s)" % (path, OPV_MARK))

    if changed:
        open(path, "w", encoding="utf-8").write(text)
        print("patch_vfs_conf_hfs_row: %s now carries %s" % (path, " and ".join(changed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())