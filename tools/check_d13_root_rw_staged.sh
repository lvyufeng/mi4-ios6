#!/bin/bash
# Re-derive the staged D13 read-write-root edit (experiment 968).  Source-half, no compiler, no device.
#
# The 968 arm (the COW writable root) needs D13's `hfs_mountroot` to mount the root READ-WRITE: 905's
# `tools/hfs_patch_root_rw.py` inserts a guarded `vfs_clearflags(mp, MNT_RDONLY)` immediately before the
# function's `hfs_mountfs` call, and `tools/build_xnu_arm_kernel.sh` compiles it in only when the pool is
# built with `-DSTAGE90_HFS_ROOT_RW=1` (on D13 that define is passed by `build_entry.sh`'s arm, because
# D13 has native HFS and does NOT set `HFS_PORT` - so the guard is `#if STAGE90_HFS_ROOT_RW`, not the
# 4570 port's `HFS_PORT && ...` path).
#
# The property this check re-derives: the clear is PRESENT, GUARDED, and ADJACENT to the root `hfs_mountfs`
# call - because a `MNT_RDONLY`-latched root (hfs_mountfs reads it once, `hfs_vfsops.c:1313`) makes the
# card unit's write branch unreachable and the whole 968 mechanism inert, while a clear that is present
# but NOT guarded would move every D13 arm's HFS object off the port-off bytes.
#
# It refuses DRIFT: if `tools/stage_d13_root_rw.sh` is ever the source of truth for an edit no longer in
# the tree, or a hand edit removed it, this fails.  A re-provisioned `external/` must be re-staged
# (`tools/stage_d13_root_rw.sh`) for the 968 arm to be what its record says, and this is what says so.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
F=$XNU/bsd/hfs/hfs_vfsops.c

# If the D13 tree is not present at all, this check has nothing to say - `make check` also runs where the
# fork is not provisioned.  Absent tree => skip, explicitly, not silently.
if [[ ! -f $F ]]; then
    echo "check_d13_root_rw_staged: no $F - D13 tree not provisioned, skipping"
    exit 0
fi

fail=0

# (a) the guard is present AND wraps the clear, i.e. the object is byte-identical when the switch is off.
if ! grep -q '^#if STAGE90_HFS_ROOT_RW$' "$F"; then
    echo "check_d13_root_rw_staged: FAIL - hfs_vfsops.c carries no '#if STAGE90_HFS_ROOT_RW' guard" >&2
    fail=1
fi
if ! grep -q 'vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);' "$F"; then
    echo "check_d13_root_rw_staged: FAIL - hfs_vfsops.c carries no 'vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);'" >&2
    fail=1
fi

# (b) ADJACENCY, by line: the clear's line must sit between the guard and the root `hfs_mountfs` call, and
# within a few lines of the call.  A hand edit that moved it away would leave the marker present and the
# property false - the class a presence-only check cannot see ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
guard_ln=$(grep -n '^#if STAGE90_HFS_ROOT_RW$' "$F" | head -1 | cut -d: -f1)
clear_ln=$(grep -n 'vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);' "$F" | head -1 | cut -d: -f1)
call_ln=$(grep -n 'if ((error = hfs_mountfs(rvp, mp, NULL, 0, context))) {' "$F" | head -1 | cut -d: -f1)

if [[ -z $guard_ln || -z $clear_ln || -z $call_ln ]]; then
    echo "check_d13_root_rw_staged: FAIL - one of the guard/clear/root-call anchors is missing" >&2
    fail=1
else
    # guard < clear < call, and the clear within 16 lines of the call (the 905 insertion is 6).
    if (( guard_ln < clear_ln && clear_ln < call_ln && call_ln - clear_ln <= 16 )); then
        echo "check_d13_root_rw_staged: $F - the guarded MNT_RDONLY clear (line $clear_ln) is between its guard ($guard_ln) and the root hfs_mountfs call ($call_ln)"
    else
        echo "check_d13_root_rw_staged: FAIL - the clear is NOT adjacent to the root hfs_mountfs call" >&2
        echo "        guard=$guard_ln clear=$clear_ln call=$call_ln (want guard < clear < call and call-clear <= 16)" >&2
        fail=1
    fi
    # and it must be the ROOT call (journal_replay_only's 0 and args NULL), not the mountfs for a normal mount.
    if ! sed -n "$((clear_ln)),$((call_ln))p" "$F" | grep -q 'hfs_mountfs(rvp, mp, NULL, 0, context)'; then
        echo "check_d13_root_rw_staged: FAIL - the clear is not immediately before the ROOT hfs_mountfs (the NULL-args call)" >&2
        fail=1
    fi
fi

if (( fail )); then
    echo "check_d13_root_rw_staged: run tools/stage_d13_root_rw.sh to re-apply the edit" >&2
    exit 1
fi
echo "check_d13_root_rw_staged: ok - the D13 hfs_mountroot carries the guarded rw clear the 968 arm needs"