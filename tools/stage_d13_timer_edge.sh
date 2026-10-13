#!/bin/bash
# Stage 999's timer-discriminator edge hooks into the D13 tree (experiment 999, amendment #2).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the amendment's TWO assembly edits:
#
#   * `osfmk/arm/machine_routines_asm.s` - a `bl _entry_timer_edge_probe` (site 1, r1 = `mrs cpsr`
#     read back) immediately after `enable_interrupts`'s `cpsie i` (:151-153).
#   * `osfmk/arm/cswitch.s` - the same call (site 2, r1 = the CPSR the return edge is about to
#     install, `[uss + 0x40]`) in `thread_exception_return`'s `return_to_user` tail (:167-170).
#
# Both sit in `#if STAGE90_XNU_RESIDENT ... #endif`, so switch OFF compiles the tree's original text
# byte for byte (the `#if`/`#endif` delimiters are dropped by cpp). NO new switch: RESIDENT is the
# 999 rung's own switch, the same one the resident pet and the WDT feed ride. See
# `tools/patch_d13_timer_edge.py` for the citation corrections and the site readings, by value.
#
# ONE build layer is touched, and it is the ASSEMBLY layer (unlike 990's two):
#   * both files -> `out/xnu_asm_obj_d13/machine_routines_asm.o` and `.../cswitch.o`, ASSEMBLY-layer
#     objects: re-run `tools/assemble_arm_layer.sh` with `STAGE90_XNU_RESIDENT=1` EXPORTED - it
#     appends `-DSTAGE90_XNU_RESIDENT=$RESIDENT` to the per-file ASFLAGS, and without the rebuild the
#     linked image keeps the old code and the hooks are invisible (933: the roots must be EXPORTED).
#     `build_entry.sh` only LINKS those objects.
#   * NO pool object changes: the C half of the discriminator (`entry_timer_edge_probe`) is
#     entry-side (`src/platform/darwin13/pe_msm8974.c`), part of the entry image itself.
#
# Idempotent: a second run re-verifies the two inserted forms and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

for f in osfmk/arm/machine_routines_asm.s osfmk/arm/cswitch.s; do
    [[ -f $XNU/$f ]] || {
        echo "stage_d13_timer_edge: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }
done

python3 "$HERE/patch_d13_timer_edge.py" "$XNU"

echo "stage_d13_timer_edge: done (XNU=$XNU)"
echo "stage_d13_timer_edge: NOW RE-RUN tools/assemble_arm_layer.sh (STAGE90_XNU_RESIDENT=1 exported, so machine_routines_asm.o and cswitch.o carry the two \`bl _entry_timer_edge_probe\` sites), THEN RELINK"