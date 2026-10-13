#!/bin/bash
# Stage 1000's user-SVC frontier hook into the D13 tree (experiment 1000, given (c) of the 999 read).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the rung's ONE assembly edit:
#
#   * `osfmk/arm/traps_lo.s` - a `bl _entry_note_user_svc` in `fleh_swi` (the file's one SVC entry;
#     `EnterARM(fleh_swi)` :115), right after `cpsie i` (:136) and before the LEO `#if` (:137).
#     r0 = r8 = the saved frame - the same pointer every path below dispatches on.
#
# It sits in `#if defined(STAGE90_XNU_USVC) && STAGE90_XNU_USVC ... #endif`, so switch OFF compiles
# the tree's original text byte for byte (the `#if`/`#endif` delimiters are dropped by cpp). See
# `tools/patch_d13_usvc.py` for the site's register liveness, by value.
#
# ONE build layer is touched, and it is the ASSEMBLY layer:
#   * the file -> `out/xnu_asm_obj_d13/traps_lo.o`, an ASSEMBLY-layer object: re-run
#     `tools/assemble_arm_layer.sh` with `STAGE90_XNU_USVC=1` EXPORTED - it appends
#     `-DSTAGE90_XNU_USVC=$USVC` to the per-file ASFLAGS, and without the rebuild the linked image
#     keeps the old code and the hook is invisible (933: the roots must be EXPORTED).
#     `build_entry.sh` only LINKS those objects.
#   * NO pool object changes: the C half (`entry_note_user_svc`) is entry-side
#     (`src/entry/entry_stubs.c`), which is in `LINK_OBJS` before pass 1 runs.
#
# Idempotent: a second run re-verifies the inserted form and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

f=osfmk/arm/traps_lo.s
[[ -f $XNU/$f ]] || {
    echo "stage_d13_usvc: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_usvc.py" "$XNU"

echo "stage_d13_usvc: done (XNU=$XNU)"
echo "stage_d13_usvc: NOW RE-RUN tools/assemble_arm_layer.sh (STAGE90_XNU_USVC=1 exported, so traps_lo.o carries the \`bl _entry_note_user_svc\`), THEN RELINK"