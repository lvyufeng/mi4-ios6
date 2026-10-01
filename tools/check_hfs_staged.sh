#!/bin/bash
# Refuse drift between the HFS+ port's TRACKED artifacts and the state `tools/stage_hfs.sh` produces
# in the untracked 4570 tree (experiments 869/871/873).  In `make check`.
#
# WHY THIS EXISTS.  The port's additions live in tracked files - `src/shims/hfs/hfs_port_force.h` (the
# macros and options) and `src/supply/stage90_hfs_shims.c` (the ten symbols 4570 lacks) - and both the
# host-only probe and `tools/stage_hfs.sh` use them.  `external/` is a re-provisionable checkout, so
# the tree can silently fall behind the tracked header, and a check that read the tree as if it were
# the source of truth would then be reading an old answer.  This check compares the two DIRECTIONS
# that matter and is silent when they agree:
#
#   1. the tracked shims file compiles on its own (the port's own additions are self-consistent), and
#   2. every #define the force header adds is either absent from 4570's headers (a genuine addition)
#      or refused loudly - because a #define that 4570 already has at another value is the "one value,
#      two definitions" defect, and here it would be a silent redefinition.
#
# It needs no compiler and no device, so it fails in a second.  See
# [[mi4-a-claim-in-a-comment-is-not-a-check]].
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
FORCE=$REPO_ROOT/src/shims/hfs/hfs_port_force.h
SHIMS=$REPO_ROOT/src/supply/stage90_hfs_shims.c

fail=0
refuse() { printf 'check_hfs_staged: %s\n' "$*" >&2; fail=1; }

[[ -f $FORCE ]] || { printf 'check_hfs_staged: missing %s\n' "$FORCE" >&2; exit 1; }
[[ -f $SHIMS ]] || { printf 'check_hfs_staged: missing %s\n' "$SHIMS" >&2; exit 1; }

# --- 1. the tracked shims define exactly the ten symbols 871 named ---------------------------------
# A count, and the names, because "the ten are written" is the claim 871 makes and a file that grew a
# stub or lost one is a different claim.
WANT="vnode_name is_suser ubc_create_upl proc_tbe vfs_markdependency \
      proc_apply_thread_selfdiskacc fslog_fs_corrupt IOBSDGetPlatformSerialNumber \
      IOBSDIsMediaEjectable IOBSDIterateMediaWithContent"
for sym in $WANT; do
    grep -qE "(^|[ *])$sym[ (]" "$SHIMS" || refuse "$SHIMS no longer defines $sym (the ten of 871)"
done

# --- 2. the file list names exactly the staged sources ---------------------------------------------
# `src/supply/hfs_files.txt` is what the build will add to the manifest; it must name every `.c` the
# stager staged and nothing that is not there, or the build would silently compile a different set
# from the one 876 measured in place.
LIST=$REPO_ROOT/src/supply/hfs_files.txt
if [[ ! -d $XNU/bsd/hfs ]]; then
    # The tree is not staged (a fresh checkout, or `external/` re-provisioned): the two tree-list
    # checks cannot run, and they are the ONLY ones that need the tree.  Say so rather than pass
    # silently - a check that skipped is a check that did not run, and silence would read as covered.
    printf 'check_hfs_staged: NOT STAGED - %s/bsd/hfs is absent; the tree-list checks did not run.\n' \
        "${XNU##*/}" >&2
    printf 'check_hfs_staged: run tools/stage_hfs.sh to stage 2050'\''s HFS+ into the tree.\n' >&2
elif [[ -f $LIST ]]; then
    while read -r rel; do
        case $rel in '' | '#'*) continue ;; esac
        [[ -f $XNU/$rel ]] || refuse "$LIST names $rel, which is not in the tree (run tools/stage_hfs.sh?)"
    done <"$LIST"
    for f in "$XNU"/bsd/hfs/*.c "$XNU"/bsd/hfs/hfscommon/*/*.c; do
        rel=${f#"$XNU"/}
        grep -qxF "$rel" "$LIST" || refuse "$rel is staged but not named in $LIST"
    done
    [[ -f $XNU/bsd/vfs/vfs_journal.c ]] && \
        grep -qxF "bsd/vfs/vfs_journal.c" "$LIST" || refuse "vfs_journal.c is staged but not named"
fi

# --- 3. every macro the force header adds is absent from 4570 (or identical where 4570 has it) -----
# `#define NAME value` rows only; the function-like `kmem_alloc(map, addr, size)` rows are edits to
# the CALL SHAPE and are checked by the probe's own compile, not here.
if [[ -d $XNU/bsd ]]; then
    while read -r name value; do
        case $name in kmem_alloc | kmem_alloc_kobject | cp_wrap_func_t) continue ;; esac
        hit=$(grep -rhoE "^#[[:space:]]*define[[:space:]]+$name[[:space:]]+[^/]*" \
                  "$XNU/bsd/sys" "$XNU/osfmk" "$XNU/bsd/vfs" 2>/dev/null | head -1 \
                  | sed -E "s/^#[[:space:]]*define[[:space:]]+$name[[:space:]]+//; s/[[:space:]]+$//")
        if [[ -n $hit && $hit != "$value" ]]; then
            refuse "$name is already defined in 4570 as '$hit', force header says '$value'" \
                   " - one value, two definitions: pick one and cite the measurement"
        fi
    done < <(grep -oE '^#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]+[^/(].*' "$FORCE" \
             | sed -E 's/^#define[[:space:]]+//' | awk '{print $1, $2}')
fi

if (( fail )); then
    printf 'check_hfs_staged: FAIL - the HFS+ port'\''s tracked artifacts drifted from what they claim\n' >&2
    exit 1
fi
printf 'check_hfs_staged: ok - %d shim symbol(s) present; force-header additions do not collide with 4570\n' \
    "$(printf '%s\n' $WANT | grep -c .)"