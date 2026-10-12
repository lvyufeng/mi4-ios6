#!/bin/bash
# Stage 986's mlckwait WAITERS-bit fix into the D13 tree (experiment 986).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the ONE edit that makes the D13 ARM mutex wait wake its
# waiters. See `tools/patch_d13_hw_lock_mlckwait.py` for the defect and the fixed/source evidence.
#
# The object this touches is `out/xnu_asm_obj_d13/hw_lock.o`, written by `tools/assemble_arm_layer.sh`
# and only CHECKED by `build_entry.sh`. After this script, RE-RUN `tools/assemble_arm_layer.sh` with
# `XNU_TREE`/`XNU_OBJ_SUFFIX` set, or the linked entry image keeps the old `hw_lock.o` and the patch is
# invisible (933: the roots must be EXPORTED) - the same rule 970h carries.
#
# Idempotent: a second run re-applies the (idempotent) patch and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/hw_lock.s ]] || {
    echo "stage_d13_hw_lock_986: no osfmk/arm/hw_lock.s in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_hw_lock_mlckwait.py" "$XNU"

echo "stage_d13_hw_lock_986: done (XNU=$XNU)"
echo "stage_d13_hw_lock_986: NOW RE-RUN tools/assemble_arm_layer.sh so out/xnu_asm_obj_d13/hw_lock.o picks it up"