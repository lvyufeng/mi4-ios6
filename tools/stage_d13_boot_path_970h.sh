#!/bin/bash
# Stage 970h's pre-switch TLB-flush removal into the D13 tree (experiment 970h).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the ONE edit that makes D13's `osfmk/arm/locore.s` boot path
# match 4570's `start.s` (which boots) on the one instruction 970g left behind:
#
#   remove `mcr p15, 0, r4, c8, c7, 0` at locore.s:85 - the whole-TLB invalidate that runs BEFORE the
#   TTBR0 write (:113) and before the zero/map loops build the boot table.  4570's `_start` has no
#   such pre-switch invalidate (its only one is at start.s:337, in `join_start`, after the table is
#   built).  The payload enters with the MMU ON and its L1 identity-maps [0x80000000,0x81000000), so
#   VA 0x80a00000 (the boot table) has a live identity TLB entry at the jump; the flush destroys it,
#   and the zero loop's first store then walks the just-switched stale table and faults - silently
#   (cpsid if is set at :55, VBAR is still the payload's).  That is the whole D13-line silence.
#
# **The object this touches is `out/xnu_asm_obj_d13/locore.o`, written by `tools/assemble_arm_layer.sh`
# and only CHECKED by `build_entry.sh`.** After this script, re-run `tools/assemble_arm_layer.sh` with
# `XNU_TREE` and `XNU_OBJ_SUFFIX` set, or the linked entry image keeps the old `locore.o` and the patch
# is invisible (933: the roots must be EXPORTED).
#
# Order-independent with `tools/stage_d13_boot_path.sh` (970g): the two anchors do not overlap.
#
# Idempotent: a second run re-applies the (idempotent) patch and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/locore.s ]] || {
    echo "stage_d13_boot_path_970h: no osfmk/arm/locore.s in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_boot_path_970h.py" "$XNU"

echo "stage_d13_boot_path_970h: done (XNU=$XNU)"
echo "stage_d13_boot_path_970h: NOW RE-RUN tools/assemble_arm_layer.sh so out/xnu_asm_obj_d13/locore.o picks it up"