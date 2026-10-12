#!/bin/bash
# Stage 990's SP-alignment fix into the D13 tree (experiment 990).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the edit that makes the D13 tree hand C code 8-byte-aligned
# stacks at its two measured gates: `osfmk/arm/pcb.c`'s `machine_stack_attach`/`machine_stack_handoff`
# round the saved-state block (and the frame below it) down to 8 (`& ~7u`), and
# `osfmk/arm/machine_routines_asm.s`'s `astloop` carries `sub sp, sp, #4` / `add sp, sp, #4` around its
# `blx _ast_taken` (the 9-word frame was 36 = 4 mod 8). Both edits sit in
# `#if STAGE90_XNU_SP_ALIGN ... #else <original> #endif`, so switch OFF compiles the tree's original
# text byte for byte. See `tools/patch_d13_sp_align.py` for the 989 fault, by value, and the fix.
#
# TWO build layers are touched (unlike 988/989, which were one each):
#   * `osfmk/arm/pcb.c`            -> `out/xnu_kernel_obj_d13/osfmk_arm_pcb.o`, a POOL object: re-run
#     `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SP_ALIGN=1`
#     (`build_xnu_arm_kernel.sh:763` also gates the same define on the same-named ENVIRONMENT variable;
#     pass BOTH or the two spellings can diverge - `mi4-one-value-two-definitions`).
#   * `osfmk/arm/machine_routines_asm.s` -> `out/xnu_asm_obj_d13/machine_routines_asm.o`, an
#     ASSEMBLY-layer object: re-run `tools/assemble_arm_layer.sh` with `STAGE90_XNU_SP_ALIGN=1`
#     EXPORTED (it appends `-DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN` to the per-file ASFLAGS).
# `build_entry.sh` only LINKS those objects, so without both rebuilds the linked image keeps the old
# code and the patch is invisible (933: the roots must be EXPORTED).
#
# Idempotent: a second run re-verifies the six inserted forms and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

for f in osfmk/arm/pcb.c osfmk/arm/machine_routines_asm.s; do
    [[ -f $XNU/$f ]] || {
        echo "stage_d13_sp_align: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }
done

python3 "$HERE/patch_d13_sp_align.py" "$XNU"

echo "stage_d13_sp_align: done (XNU=$XNU)"
echo "stage_d13_sp_align: NOW RE-RUN tools/assemble_arm_layer.sh (STAGE90_XNU_SP_ALIGN=1 exported, for the asm-layer machine_routines_asm.o) AND tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SP_ALIGN=1 + the same env var, for the pool's osfmk_arm_pcb.o)"