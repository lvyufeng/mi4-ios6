#!/bin/bash
# Stage 2050's HFS+ filesystem into the 4570 tree (experiments 868-874).
#
# Idempotent and re-appliable: `external/` is a re-provisionable checkout (README: "keep large source
# checkouts under the ignored external/"), so every edit this makes to it is a copy plus one marked
# in-place edit, never a hand edit that a re-checkout would lose.  It mirrors EXACTLY what
# `tools/hfs_port_probe.sh` copies and edits and measured at 38/38 compile and a zero link gap
# (experiments 871/873); it does not add anything the probe did not.
#
# WHAT IT DOES  (source staging only)
#   1. copies 2050's `bsd/hfs/` into 4570's `bsd/hfs/` (minus Apple's Makefile);
#   2. copies the three HFS dependencies that live OUTSIDE `bsd/hfs/`: `vfs_journal.c/.h` (the
#      `#if JOURNALING` source HFS calls into) and `machine/spl.h` (included by hfs_vnops.c and
#      absent from 4570);
#   3. applies the ONE in-place substitution the probe needed: `hfs_macos_defs.h`'s `false`/`true`
#      enum, which cannot be declared because 4570 reaches `<stdbool.h>` (which #defines both).
#      Marked with a sentinel, so a second run is a no-op rather than a double edit.
#
# WHAT IT DELIBERATELY DOES NOT DO  - and says so, because each is a real decision, not an omission:
#   * no `conf/files` rows and no `MASTER` option.  The force-header `src/shims/hfs/hfs_port_force.h`
#     carries the port's own options (`HFS`, `HFS_COMPRESSION`, `CONFIG_HFS_STD`, `JOURNALING`) and
#     its macro additions, so the port's additions live in ONE TRACKED file rather than in rows of an
#     untracked one.  Wiring that header and the HFS sources into the kernel build is the next step.
#   * no build, no arm, no park, no press, no device.
#
# `tools/check_hfs_staged.sh` (in `make check`) re-derives this script's effect and refuses drift.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
SRC=${HFS_2050_TREE:-$REPO_ROOT/external/xnu-2050.18.24}
DST=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
MARK="tools/stage_hfs.sh"

[[ -d $SRC/bsd/hfs ]] || { echo "stage_hfs: no HFS sources at $SRC/bsd/hfs" >&2; exit 2; }
[[ -d $DST/bsd ]]     || { echo "stage_hfs: no 4570 tree at $DST" >&2; exit 2; }

say() { printf '%s\n' "$*"; }

# --- 1/2. the copies ------------------------------------------------------------------------------
say "stage_hfs: copying $SRC/bsd/hfs -> $DST/bsd/hfs"
rm -rf "$DST/bsd/hfs"
cp -r "$SRC/bsd/hfs" "$DST/bsd/hfs"
rm -f "$DST/bsd/hfs/Makefile"          # Apple's own makefile; 4570 builds HFS through this project

mkdir -p "$DST/bsd/machine"
cp "$SRC/bsd/vfs/vfs_journal.c" "$DST/bsd/vfs/vfs_journal.c"
cp "$SRC/bsd/vfs/vfs_journal.h" "$DST/bsd/vfs/vfs_journal.h"
cp "$SRC/bsd/machine/spl.h"     "$DST/bsd/machine/spl.h"

# --- 3. the one in-place substitution -------------------------------------------------------------
# ONE definition of the edit, shared with the host-only probe (tools/hfs_port_probe.sh), so the probe
# measures the file this script produces rather than a second copy.
say "stage_hfs: applying the hfs_macos_defs.h substitution"
"$HERE/hfs_patch_macos_defs.py" "$DST/bsd/hfs/hfs_macos_defs.h"

# --- 4. the root row, in the STATIC table ---------------------------------------------------------
# 874: the row vfs_mountroot walks must be BEFORE mockfs in vfstbllist[], and vfs_fsadd appends after
# it, so the row is a STATIC entry - a tracked patch to the untracked tree, guarded by STAGE90_HFS_ROOT
# so it is inert when the port is off. One definition of the edit, like the substitution above.
say "stage_hfs: applying the vfs_conf.c HFS root row"
"$HERE/patch_vfs_conf_hfs_row.py" "$DST/bsd/vfs/vfs_conf.c"

# --- 4b. the mount-path live step markers (899) ---------------------------------------------------
# Like the row above: a tracked patch to the untracked tree, guarded by `STAGE90_HFS_MOUNT_MARKERS`
# so it is inert unless a marker build asks for it. 897 served one read and the mount never
# returned; these markers name the last step the mount reached.
say "stage_hfs: applying the HFS+ mount-path live step markers"
"$HERE/hfs_patch_mount_markers.py" "$DST"

# --- 4c. the read-write root clear (905 B2) ------------------------------------------------------
# Like the row and the markers above: a tracked patch to the untracked tree, guarded by
# `STAGE90_HFS_ROOT_RW` so it is inert unless a build asks for the read-write root.  The root mount is
# read-only by construction (`vfs_subr.c` hard-codes MNT_RDONLY | MNT_ROOTFS); this clears it in the
# one place before `hfs_mountfs` latches HFS_READ_ONLY, so the card unit's write path is reachable.
say "stage_hfs: applying the HFS+ read-write root clear"
"$HERE/hfs_patch_root_rw.py" "$DST"

N=$(find "$DST/bsd/hfs" -name '*.c' | wc -l)
say "stage_hfs: staged $N .c file(s) under $DST/bsd/hfs (plus vfs_journal.c, spl.h, the root row)"
# --- 5. the record, for the one gate that cannot see the enumerated files -------------------------
#
# **THIS IS NOT A SECOND DEFINITION OF THE FOOTPRINT; IT IS A RECORD OF A STATE THAT HAS NO OTHER
# WITNESS.** `tools/check_hfs_staged.sh` re-derives what was staged from the file list and the tree,
# and `scripts/xnu_compile_graph_scan.py` reads that same list - so both know the enumerated members.
# What neither can know is that an UNENUMERATED file is not also sitting in the tree: `git status
# --short` collapses 36 of the 37 files to the single line `?? bsd/hfs/`, so a stray edit inside that
# directory is invisible to a membership test over the list.
#
# A FRESH WRITE IS WHAT MAKES THE STRING TRUSTWORTHY. This assignment is unconditional, so if someone
# hand-edits the staged tree after a run, `git status` differs from this record and the gate that
# compares them refuses - the property "written by the stager" is preserved rather than the property
# "looks clean". The comparison itself lives in `scripts/xnu_compile_graph_scan.py`, which is where the
# gate that needs it is; what belongs here is the measurement.
if [[ -n ${STAGE_HFS_RECORD:-} ]]; then
    # `--untracked-files=all` and not the default: the default collapses the 36 files under `bsd/hfs/` to
    # the single line `?? bsd/hfs/`, which is a record that cannot distinguish the stager's own output
    # from that plus a stray file inside the same directory. The reader uses the same two flags.
    git -C "$DST" status --short --untracked-files=all > "$STAGE_HFS_RECORD"
    say "stage_hfs: recorded the staged tree's git status in $STAGE_HFS_RECORD"
fi

say ""
say "REMAINING for a real HFS root (each its own step, none done here):"
say "  * the ROOT ROW is staged but INERT until the port is on: build with STAGE90_XNU_HFS=1, which"
say "    defines STAGE90_HFS_ROOT for vfs_conf.c and puts the row in vfstbllist[] before mockfs"
say "  * src/shims/hfs/hfs_cprotect_port.h closes the cprotect gap (877/878): 37/37 compile with it"
say "  * CONFIG_PROTECT=1 keeps the struct bufattr layout the rest of the kernel has - do NOT flip it"
say "    per-file; the port's cprotect layer is declarations, and the ENGINE (cp_register_wraps has no"
say "    caller) is still owed"
say "  * the eMMC driver behind the medium (867): HFS mounts nothing until a byte moves"