#!/usr/bin/env python3
"""Insert the HFS+ mount-path live step markers (experiment 899).

WHY.  897's press served exactly ONE strategy read (the HFS+ volume header at offset 1024) and then
the mount never returned: 898 localized the block to `hfs_MountHFSPlusVolume` between the VCB fill and
the extents `BTOpenPath`, but reading cannot tell a block in `hfs_getnewvnode` from one in `BTOpenPath`
from one in the prologue.  The only way to tell them apart on the next press is a live record at each
step, and `entry_live_write` APPENDS, so the LAST `xnu_live_hfs_stage=` record names the farthest step
reached.

  1  hfs_mountfs: after the volume-header bcopy
  2  hfs_MountHFSPlusVolume: after the VCB fill (the last statement before the extents vnode)
  3  hfs_getnewvnode: about to return the new vnode (the extents vnode is built)
  4  BTOpenPath: entered (just before its header-node read)
  5  hfs_MountHFSPlusVolume: after the extents BTOpenPath RETURNED (so 4 then 5 == the extents btree
     was opened; 4 alone == BTOpenPath entered but never returned)

The markers compile to nothing unless `STAGE90_HFS_MOUNT_MARKERS` is defined, so this patch is inert in
a normal build and cannot change any arm's behaviour.  `entry_live_write` is declared by the force
header (`src/shims/hfs/hfs_port_force.h`, force-included into every HFS translation unit), so every
site here resolves it; the marker re-declares it locally too, so it stands alone.

Idempotent and re-appliable: a site already marked is left alone; a missing anchor is a loud error, so
a source update cannot silently drop a marker.  `tools/check_hfs_staged.sh` re-derives this file's
effect.  This mirrors `tools/patch_vfs_conf_hfs_row.py`: one tracked definition of an edit to the
untracked, re-provisionable `external/` tree.
"""
import re
import sys

# (file, anchor line, marker id, step) - the anchor is matched as a whole, end-trimmed line.
SITES = [
    ("bsd/hfs/hfs_vfsops.c",
     "\tbcopy((char *)buf_dataptr(bp) + HFS_PRI_OFFSET(phys_blksize), mdbp, kMDBSize);",
     "hfs_mountfs_hdr", 1),
    ("bsd/hfs/hfs_vfsutils.c",
     "\tvcb->hfsPlusIOPosOffset\t= embeddedOffset;",
     "hfsplus_vcb", 2),
    ("bsd/hfs/hfs_cnode.c",
     "\t*vpp = vp;",
     "getnewvnode_ret", 3),
    ("bsd/hfs/hfscommon/BTree/BTree.c",
     "NodeRec\t\t\t\t\tnodeRec;",
     "btopenpath_entered", 4),
    ("bsd/hfs/hfs_vfsutils.c",
     # The anchor is the STATEMENT'S LAST LINE, not its first: the call spans two lines
     # (`BTOpenPath(VTOF(...),` / `(KeyCompareProcPtr) CompareExtentKeysPlus));`), and an anchor on
     # the first line puts the marker in the MIDDLE of the call - `expected expression` at compile.
     # Anchoring on the closing line puts the marker AFTER the call, so step 5 reads as "the extents
     # BTOpenPath RETURNED".  `CompareExtentKeysPlus` occurs once, on that line only.
     "\t                                  (KeyCompareProcPtr) CompareExtentKeysPlus));",
     "extents_btopenpath", 5),
]

PROV = "PORT SHIM (hfs_patch_mount_markers.py, exp 899)"


def marker(mid, step, indent):
    return (
        '%s/* %s: step %d, marker `%s`.  Compiled only under STAGE90_HFS_MOUNT_MARKERS. */\n'
        '%s#if STAGE90_HFS_MOUNT_MARKERS\n'
        '%s{ extern void entry_live_write(const char *, unsigned); '
        'entry_live_write("xnu_live_hfs_stage", %du); }\n'
        '%s#endif /* STAGE90_HFS_MOUNT_MARKERS */\n'
        % (indent, PROV, step, mid, indent, indent, step, indent)
    )


def main():
    # `--list-sites` prints `file marker` per site, so a reader (tools/check_hfs_staged.sh) derives
    # what the tree must carry FROM this file rather than from a second hand-written list that can drift.
    if len(sys.argv) > 1 and sys.argv[1] == "--list-sites":
        for rel, _anchor, mid, _step in SITES:
            print("%s %s" % (rel, mid))
        return
    root = sys.argv[1] if len(sys.argv) > 1 else "external/xnu-4570.1.46"
    for rel, anchor, mid, step in SITES:
        # Every anchor MUST be a whole statement ending in `;`, because the marker is inserted
        # immediately after the anchor line - an anchor on the first line of a multi-line call puts
        # the marker in the MIDDLE of the call, which is `expected expression` at compile (step 5
        # did exactly this once).  A per-function-body anchor cannot be checked this way, so it is
        # the one exception, named here rather than left implicit.
        if mid != "btopenpath_entered" and not anchor.strip().endswith(";"):
            sys.exit("hfs_patch_mount_markers: anchor for %s is not a whole statement (no `;`): %r"
                     % (mid, anchor.strip()))
        path = "%s/%s" % (root, rel)
        text = open(path, encoding="latin-1").read()
        if mid in text:
            print("hfs_patch_mount_markers: %s already carries %s" % (rel, mid))
            continue
        # Whitespace-flexible: an anchor's internal runs of spaces/tabs match any run, so a
        # re-indent of the source cannot silently drop a marker (the fields still must match).
        _fields = [re.escape(x) for x in anchor.strip().split()]
        pat = re.compile(r"(?m)^([ \t]*)" + r"[ \t]+".join(_fields) + r"[ \t]*$")
        start = 0
        if mid == "btopenpath_entered":
            # The local-declaration anchor is not unique in BTree.c (many functions declare
            # `btreePtr`): scope the search to after the BTOpenPath signature, and insert after the
            # LAST of the function's leading declarations is not needed - the first one inside the
            # function body is already after the `{`.
            sig = re.compile(r"(?m)^OSStatus BTOpenPath\(FCB \*filePtr, KeyCompareProcPtr keyCompareProc\)")
            sm = sig.search(text)
            if not sm:
                sys.exit("hfs_patch_mount_markers: BTOpenPath signature not found in %s" % rel)
            start = sm.end()
        m = pat.search(text, start)
        if not m:
            sys.exit("hfs_patch_mount_markers: anchor not found in %s: %r" % (rel, anchor.strip()))
        indent = m.group(1)
        eol = text.find("\n", m.end())
        eol = len(text) if eol < 0 else eol
        ins = text[:eol + 1] + marker(mid, step, indent) + text[eol + 1:]
        open(path, "w", encoding="latin-1").write(ins)
        print("hfs_patch_mount_markers: %s + %s (step %d)" % (rel, mid, step))


if __name__ == "__main__":
    main()