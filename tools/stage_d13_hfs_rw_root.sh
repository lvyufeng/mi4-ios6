#!/bin/bash
# Stage 905 B2's read-write-root clear into the Darwin-13 tree (the tree the D13 arms link).
#
# The clear (`vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);`) is inserted into `hfs_mountroot` immediately
# before its root `hfs_mountfs` call, so the HFS+ root mounts READ-WRITE and the card unit's write path
# is reachable.  `external/` is a re-provisionable, gitignored checkout, so every edit to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose - this is the D13, marker-bounded,
# OFF-byte-proven form of `tools/hfs_patch_root_rw.py` (which staged the same clear marker-lessly).
#
# The D13 tree is `external/xnu-hd2-darwin13/xnu` (it ships the legacy `osfmk/sys/types.h` that is this
# project's Darwin-13 discriminator, `tools/xnu_tree_roots.sh:33`).  `hfs_mountroot` there carries the
# IDENTICAL anchor (`if ((error = hfs_mountfs(rvp, mp, NULL, 0, context)))`), so the same edit applies.
#
# WHAT IT DOES: run `tools/patch_d13_hfs_rw_root.py <D13 tree>`, which refuses (never silently skips) a
# moved anchor or a foreign edit, normalises the prior marker-less staged form into the marker-bounded
# one, and proves the switch-OFF preprocessed text is byte-identical before writing.
#
# `tools/check_d13_hfs_rw_root.py` re-derives this script's effect by value.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

if [[ ! -f $XNU/osfmk/sys/types.h ]]; then
    echo "stage_d13_hfs_rw_root: $XNU is not the Darwin-13 tree (no osfmk/sys/types.h); nothing to stage" >&2
    exit 2
fi

exec "$HERE/patch_d13_hfs_rw_root.py" "$XNU"