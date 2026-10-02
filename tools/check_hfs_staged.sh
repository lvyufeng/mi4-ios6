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
#
# 874 ADDED A THIRD KIND OF PORT EDIT, so it gets a section here too: `bsd/vfs/vfs_conf.c` carries a
# patch (the static HFS root row before mockfs), applied by the stager and guarded by `STAGE90_HFS_ROOT`.
# It is the same "tracked edit to an untracked tree" as `hfs_macos_defs.h`, and the same drift applies:
# re-provisioning `external/` restores the row-less file, and a build would then silently mount mockfs
# with HFS wired but unreachable as root.  Section 4 refuses that, and refuses the two ways the row can
# be present yet wrong (after mockfs - so never tried - and a stray duplicate `FT_HFS`).
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
FORCE=$REPO_ROOT/src/shims/hfs/hfs_port_force.h
FORCE_CP=$REPO_ROOT/src/shims/hfs/hfs_cprotect_port.h
SHIMS=$REPO_ROOT/src/supply/stage90_hfs_shims.c

fail=0
refuse() { printf 'check_hfs_staged: %s\n' "$*" >&2; fail=1; }

[[ -f $FORCE ]] || { printf 'check_hfs_staged: missing %s\n' "$FORCE" >&2; exit 1; }
[[ -f $FORCE_CP ]] || { printf 'check_hfs_staged: missing %s\n' "$FORCE_CP" >&2; exit 1; }
[[ -f $SHIMS ]] || { printf 'check_hfs_staged: missing %s\n' "$SHIMS" >&2; exit 1; }

# --- 0. the second force header is force-included, and after the first ------------------------------
# The cprotect port header (877) renames `cp_is_valid_class` and defines structs that read
# `cp_wrap_func_t` and `aes_encrypt_ctx`; it MUST be -included second, after `hfs_port_force.h` and
# after `sys/cprotect.h` (which it pulls).  If the build ever includes it alone, or first, the build
# stops loudly - but this same check is the cheap statement of the order, in the one place a reader
# looks for "what does the HFS port add".
BUILD=$REPO_ROOT/tools/build_xnu_arm_kernel.sh
grep -q -- '-include "\$HFS_FORCE" -include "\$HFS_FORCE_CP"' "$BUILD" \
    || refuse "$BUILD no longer force-includes \$HFS_FORCE_CP second (877's cprotect port header)"

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
    # Every `#define NAME value` in BOTH force headers.  `cp_is_valid_class` is 877's deliberate
    # rename (a valueless macro, dropped by the `[^/(]` value guard); the three exclusions are above.
    done < <(grep -hoE '^#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]+[^/(].*' \
                 "$FORCE" "$FORCE_CP" \
             | sed -E 's/^#define[[:space:]]+//' | awk '{print $1, $2}')
fi

# --- 4. the HFS root row is present, guarded, and BEFORE mockfs in the static table ----------------
# 874: `vfs_mountroot` walks `vfstbllist[]`, takes the first row that mounts and `break`s, and mockfs
# always mounts - so the HFS row has to be a STATIC entry ahead of mockfs, not one registered at boot
# (`vfs_fsadd` appends after the last static row, `vfs_init.c:578`).  Three things are checked, in the
# direction that catches a re-provision (the row ABSENT), a mis-placement (the row AFTER mockfs), and
# the earlier hand-edit defect (a duplicate enumerator, which does not compile).
CONF=$XNU/bsd/vfs/vfs_conf.c
if [[ -d $XNU/bsd ]]; then
    if [[ ! -f $CONF ]]; then
        refuse "$CONF is absent although the tree is staged - the root row cannot be there"
    else
        # (a) the guard and the marker, i.e. the patch ran at all.
        grep -q "STAGE90_HFS_ROOT" "$CONF" \
            || refuse "$CONF carries no STAGE90_HFS_ROOT guard - the 874 root row is ABSENT (run tools/stage_hfs.sh)"
        grep -q "patch_vfs_conf_hfs_row.py" "$CONF" \
            || refuse "$CONF carries no 874 marker comment - it may have been hand-edited, not patched"
        # (b) the row must be BEFORE mockfs IN THE TABLE.  Anchor on mockfs's own table ROW, not on
        #     `#if MOCKFS`: that preprocessor line appears twice (the extern block first, the table
        #     block second), and the extern block is ABOVE the table - so anchoring on it compares
        #     against the wrong line and reports a correct row as mis-placed.  The row is compared by
        #     line number: the HFS row must sit above the mockfs row, because `vfs_mountroot` takes the
        #     first row that mounts and mockfs always does.
        hfs_line=$(grep -n '{ &hfs_vfsops, "hfs"' "$CONF" | head -1 | cut -d: -f1)
        mnt_line=$(grep -n '{ &mockfs_vfsops, "mockfs"' "$CONF" | head -1 | cut -d: -f1)
        if [[ -n $hfs_line && -n $mnt_line ]] && (( hfs_line > mnt_line )); then
            refuse "$CONF has the HFS row at line $hfs_line, AFTER the mockfs row (line $mnt_line) - vfs_mountroot" \
                   "takes the first row that mounts and mockfs always does, so it would never be tried as root"
        fi
        [[ -n $hfs_line ]] || refuse "$CONF has no '{ &hfs_vfsops, \"hfs\"' row in the static table"
        # (c) exactly one FT_HFS enumerator (a second is a redefinition the compiler catches, but this
        #     says WHICH patch produced it, and it is the shape a hand-edit produced once already).
        n=$(grep -c 'FT_HFS     = 17' "$CONF")
        [[ $n -eq 1 ]] || refuse "$CONF declares FT_HFS $n time(s), must be exactly one (874's row adds it)"
        # (d) 902: the HFS vnode-op descriptors must be in `vfs_opv_descs[]`, or `hfs_vnodeop_p` is
        #     never allocated and every HFS vnode's `v_op` is NULL.  The FIRST B-tree read dereferences
        #     it (`VNOP_STRATEGY`, `v_op[1]`) exactly as 901's press measured (`fault_addr=0x4`).  The
        #     root row alone links and mounts but faults on its first node read, so this is checked the
        #     same way: the marker, then the rows, each present and named in the port's own comment.
        grep -q "HFS vnode-op descriptors" "$CONF" \
            || refuse "$CONF carries no 902 marker comment for the HFS vnode-op descriptors (run tools/stage_hfs.sh)"
        for d in hfs_vnodeop_opv_desc hfs_std_vnodeop_opv_desc hfs_specop_opv_desc; do
            m=$(grep -c "&$d," "$CONF")
            [[ $m -eq 1 ]] || refuse "$CONF names &$d, in vfs_opv_descs[] $m time(s), must be exactly one - without it vfs_opv_init never allocates the vector and every HFS vnode's v_op is NULL (902)"
        done
        # The rows must sit inside the port's STAGE90_HFS_ROOT guard, i.e. between its two markers.
        awk '/#if STAGE90_HFS_ROOT/{g=1} /#endif \/\* STAGE90_HFS_ROOT \*\//{g=0}
             g && /&hfs_vnodeop_opv_desc,/{f=1} END{exit f?0:1}' "$CONF" \
            || refuse "$CONF carries &hfs_vnodeop_opv_desc, OUTSIDE a STAGE90_HFS_ROOT guard - the port's edits must be inert when the port is off"
    fi
fi

# --- 4c. the mount-path live step markers (899) are present, guarded, and match the tracked patch -----
# 899 adds a FOURTH tracked edit to the untracked tree: five `entry_live_write("xnu_live_hfs_stage", N)`
# markers, applied by `tools/hfs_patch_mount_markers.py` and guarded by `STAGE90_HFS_MOUNT_MARKERS` so
# they are inert unless a marker build asks for them.  897 served ONE strategy read and the mount never
# returned; 898 bounded the block to `hfs_MountHFSPlusVolume` but could not tell WHICH step blocked.  A
# re-provision drops all five and the next press would again read "one read, then silence", so the drift
# is refused in the same direction as the other three edits.  Two things are checked: the marker ids the
# patcher declares are the ones in the tree (a source rename cannot silently drop one), and each marker
# sits inside its `#if STAGE90_HFS_MOUNT_MARKERS` guard (an unguarded marker would emit a live record in
# a normal build).  The patcher is idempotent by construction (a site whose id is already present is
# left alone), and re-running `stage_hfs.sh` is the re-derivation - `make check` cannot re-run it without
# mutating the tree, so this reads the RESULT the stager was told to produce.
MARKERS=$REPO_ROOT/tools/hfs_patch_mount_markers.py
if [[ -f $MARKERS && -d $XNU/bsd/hfs ]]; then
    grep -q "M_HFSBITMAP" "$MARKERS" && refuse "$MARKERS still references the removed M_HFSBITMAP (898)"
    # read the declared (marker id, source file) pairs FROM the patcher's own `--list-sites`, not a
    # second hand-written list that could drift away from the one the stager applies.
    while read -r rel mid; do
        [[ -n $rel ]] || continue
        # The marker comment names it: `marker `<id>`.`  A tree that lost the patch, or a source
        # update that moved the anchor, both surface here - neither can silently drop a step.
        if grep -q "marker \`$mid\`" "$XNU/$rel"; then
            # The guard is what keeps a marker inert in a normal build; a marker without it would
            # emit a live record the moment the port is on, the one thing these must never do.
            grep -q "#if STAGE90_HFS_MOUNT_MARKERS" "$XNU/$rel" \
                || refuse "$XNU/$rel carries the \`$mid\` marker but no STAGE90_HFS_MOUNT_MARKERS guard"
        else
            refuse "$XNU/$rel carries no \`$mid\` step marker (run tools/stage_hfs.sh)"
        fi
    done < <(python3 "$MARKERS" --list-sites)
fi
# The stager must still call it, or a fresh stage silently loses the markers.
if [[ -f $MARKERS ]]; then
    grep -q "hfs_patch_mount_markers.py" "$REPO_ROOT/tools/stage_hfs.sh" \
        || refuse "tools/stage_hfs.sh no longer applies the 899 mount-path markers"
fi

# --- 4d. the read-write root clear (905 B2) is present, guarded, and the stager applies it ----------
# 905 adds a FIFTH tracked edit to the untracked tree: a guarded `vfs_clearflags(mp, MNT_RDONLY)` before
# `hfs_mountroot`'s `hfs_mountfs` call, applied by `tools/hfs_patch_root_rw.py` and guarded by
# `STAGE90_HFS_ROOT_RW` so it is inert unless a read-write-root build asks for it.  The root mount is
# read-only by construction (`vfs_subr.c` hard-codes MNT_RDONLY | MNT_ROOTFS), so a re-provision that
# drops the clear leaves the write path built but unreachable - the arm's whole claim, silently gone.
# The guard is what keeps it inert in a normal build; an unguarded clear would flip EVERY port build to
# read-write root.
RW=$REPO_ROOT/tools/hfs_patch_root_rw.py
if [[ -f $RW && -f $XNU/bsd/hfs/hfs_vfsops.c ]]; then
    grep -q "STAGE90_HFS_ROOT_RW" "$XNU/bsd/hfs/hfs_vfsops.c" \
        || refuse "bsd/hfs/hfs_vfsops.c carries no STAGE90_HFS_ROOT_RW clear (run tools/stage_hfs.sh)"
    grep -q "#if STAGE90_HFS_ROOT_RW" "$XNU/bsd/hfs/hfs_vfsops.c" \
        || refuse "bsd/hfs/hfs_vfsops.c carries the read-write clear but no STAGE90_HFS_ROOT_RW guard"
    grep -q "vfs_clearflags(mp, (u_int64_t)MNT_RDONLY)" "$XNU/bsd/hfs/hfs_vfsops.c" \
        || refuse "bsd/hfs/hfs_vfsops.c's STAGE90_HFS_ROOT_RW block carries no vfs_clearflags(mp, MNT_RDONLY)"
    grep -q "hfs_patch_root_rw.py" "$REPO_ROOT/tools/stage_hfs.sh" \
        || refuse "tools/stage_hfs.sh no longer applies the 905 read-write root clear"
fi

if (( fail )); then
    printf 'check_hfs_staged: FAIL - the HFS+ port'\''s tracked artifacts drifted from what they claim\n' >&2
    exit 1
fi
printf 'check_hfs_staged: ok - %d shim symbol(s) present; force-header additions do not collide with 4570; 874 root row guarded and before mockfs; 902 vnode-op descriptors in vfs_opv_descs; 899 mount markers present and guarded; 905 read-write root clear guarded\n' \
    "$(printf '%s\n' $WANT | grep -c .)"