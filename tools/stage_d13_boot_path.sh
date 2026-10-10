#!/bin/bash
# Stage 970g's boot-path restore into the D13 tree (experiment 970g).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the TWO edits that make D13's `osfmk/arm/locore.s` boot path
# behave like 4570's `start.s` (which boots):
#   edit 1 - delete the MMU fast path (`beq mmu_initialized` + its three-setup), so `__start` always runs
#            the boot path (builds the boot table at `topOfKernelData`, sets TTBR0 high) instead of
#            keeping the payload's LOW table (`0x6c4000`) - which is what makes the entry's console
#            window guard refuse (`why=1`) and the whole D13 entry line silent;
#   edit 2 - zero the boot translation table after the TTBR0 write (4570's `invalidate_tte:` loop, 10240
#            entries = `STAGE90_XNU_ENTRY_TABLE_BYTES`), which D13's boot path omitted, leaving the
#            console's own slot (index `0xde5`, above `memSize`) stale -> the entry refuses it as
#            occupied even with a HIGH table. 4570's working capture reads `xnu_live_slot_before=0`.
#
# **The object this touches is `out/xnu_asm_obj/locore.o`, which is written by
# `tools/assemble_arm_layer.sh` and only CHECKED by `build_entry.sh`.** After this script, re-run
# `tools/assemble_arm_layer.sh` with `XNU_TREE` and `XNU_OBJ_SUFFIX` set, or the linked entry image
# keeps the old `locore.o` and the patch is invisible (933: the roots must be EXPORTED).
#
# Idempotent: a second run re-applies the (idempotent) patch and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/locore.s ]] || {
    echo "stage_d13_boot_path: no osfmk/arm/locore.s in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_boot_path.py" "$XNU"

echo "stage_d13_boot_path: done (XNU=$XNU)"
echo "stage_d13_boot_path: NOW RE-RUN tools/assemble_arm_layer.sh so out/xnu_asm_obj/locore.o picks it up"