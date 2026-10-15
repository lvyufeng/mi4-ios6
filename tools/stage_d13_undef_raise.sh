#!/bin/bash
# Stage 1020's `udf`-raise fix into the D13 tree (experiment 1020).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single entry point for 1020's ONE tree edit - `osfmk/arm/trap.c` - and calls the
# patcher that owns it; it edits nothing itself:
#   * `osfmk/arm/trap.c` - `patch_d13_undef_raise.py` (udf-retry lane's): in `sleh_undef`'s user-mode
#     VFP/NEON arm it moves the `return;` INSIDE the `if (!thread->machine.vfp_enable)` block, so an
#     undefined instruction arriving with VFP ALREADY enabled falls through to the existing `printf` +
#     `EXC_BAD_INSTRUCTION` (the raise this arm never reached) instead of returning and re-faulting.
#     The `#else` branch is the tree's ORIGINAL block, verbatim, so the switch OFF is byte-identical.
#
# WHY (1019, commit d8c350b): the resident no-return is pid 1's `udf #254` re-executing forever. The
# user-mode arm's VFP/NEON false-positive path takes an unconditional `return` for EVERY undefined
# instruction whose encoding is not dyld's own `0xfedeffe7` (trap.c:1108) - the VFP left arm re-enables
# VFP once and returns without advancing the PC, so the same `udf` faults again. Moving the `return`
# inside the `if (!vfp_enable)` block makes the already-enabled case fall through to `sleh_undef`'s
# printf + EXC_BAD_INSTRUCTION below it, which raises instead of looping.  See docs/1020-design.md and
# `records/1020-ref-sleh-undef.txt`.
#
# ONE build layer is touched: `trap.c` is a POOL object (`out/xnu_kernel_obj_d13/osfmk_arm_trap.o`), so
# re-run `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_UNDEF_RAISE=1`
# (it also gates the same define on the same-named ENVIRONMENT variable; pass BOTH or the two
# spellings can diverge - `mi4-one-value-two-definitions`).  1020 does NOT touch the assembly layer,
# and `build_entry.sh` only LINKS the objects, so without the rebuild the linked image keeps the old
# code and the patch is invisible.
#
# Switch OFF compiles the tree's original text byte for byte: the gate is
# `#if defined(STAGE90_XNU_UNDEF_RAISE) && STAGE90_XNU_UNDEF_RAISE`, so `defined()` is checked FIRST
# and an unset switch leaves the file's preprocessed text identical to HEAD - the
# `mi4-off-option-two-spellings` guard. The patcher PROVES that with the real preprocessor over the
# whole file (four `clang -E -P` runs) BEFORE it writes a single byte, and proves the ON state moved
# the `return` inside the block by string position.
#
# Idempotent: the patcher decides before writing, asserts the anchor line's occurrence count exactly,
# REFUSES (file untouched) on a partial/double/foreign edit, and a second run prints "nothing to do".
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/trap.c ]] || {
    echo "stage_d13_undef_raise: no osfmk/arm/trap.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_undef_raise.py" "$XNU"

echo "stage_d13_undef_raise: done (XNU=$XNU)"
echo "stage_d13_undef_raise: NOW RE-RUN tools/build_xnu_arm_kernel.sh with XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_UNDEF_RAISE=1 AND STAGE90_XNU_UNDEF_RAISE=1 exported - osfmk/arm/trap.c is a POOL object (out/xnu_kernel_obj_d13/osfmk_arm_trap.o), and build_entry.sh only LINKS that object, so without the rebuild the linked image keeps the old code"