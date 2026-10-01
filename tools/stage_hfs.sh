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

N=$(find "$DST/bsd/hfs" -name '*.c' | wc -l)
say "stage_hfs: staged $N .c file(s) under $DST/bsd/hfs (plus vfs_journal.c, spl.h)"
say ""
say "REMAINING for a real HFS root (each its own step, none done here):"
say "  * wire the HFS sources + src/shims/hfs/hfs_port_force.h into the kernel build"
say "  * link src/supply/stage90_hfs_shims.c (the ten symbols 4570 lacks, 871)"
say "  * the ROOT ROW: an HFS row BEFORE mockfs in bsd/vfs/vfs_conf.c - it must be STATIC, because"
say "    874 measured that a vfs_fsadd registration lands after mockfs and is never tried as a root"
say "  * CONFIG_PROTECT=0 in the build, and the eMMC driver behind the medium (867)"