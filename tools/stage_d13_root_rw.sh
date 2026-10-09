#!/bin/bash
# Stage 905's read-write-root clear into the Darwin-13 tree the 968 arm links (experiment 968).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit to it must be a script a
# re-checkout can re-apply, not a hand edit it would lose.  The 968 arm (the COW writable root) mounts
# the iOS root read-WRITE, and the ONE host-side edit that makes that possible is `tools/hfs_patch_root_rw.py`'s
# guarded `vfs_clearflags(mp, MNT_RDONLY)` immediately before `hfs_mountroot`'s `hfs_mountfs` call.
#
# Until now that patcher had only ever been run against 4570 (the pre-913 line).  Post-913 the working
# tree is Darwin 13 (`external/xnu-hd2-darwin13/xnu`), and its `hfs_mountroot` carries the IDENTICAL
# anchor (the `if ((error = hfs_mountfs(rvp, mp, NULL, 0, context)))` line), so the same patcher applies
# unchanged - this script is the thin, named way to apply it to the D13 tree, beside `stage_d13_board.sh`
# and `tools/patch_d13_memory_total.py`, so "the D13 tree needs N edits and here they all are" is one
# list rather than a fact living in one experiment's shell history.
#
# WHAT IT DOES: run `tools/hfs_patch_root_rw.py <D13 tree>`, which is idempotent (a tree already carrying
# the clear is left alone) and loud (a moved anchor is refused, never silently skipped).
#
# `tools/check_d13_root_rw_staged.sh` (in `make check`) re-derives this script's effect and refuses drift.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

if [[ ! -f $XNU/osfmk/sys/types.h ]]; then
    echo "stage_d13_root_rw: $XNU is not the Darwin-13 tree (no osfmk/sys/types.h); nothing to stage" >&2
    exit 2
fi

exec "$HERE/hfs_patch_root_rw.py" "$XNU"