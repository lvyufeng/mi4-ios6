#!/bin/bash
# Stage 971's arm_vm_init boot-table copy into the D13 tree (experiment 971).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the ONE edit that makes D13's `arm_vm_init` carry the entry's
# boot-table descriptors (the console) into the system table the MMU walks afterwards:
#
#   arm_vm_init.c:353  `bzero(phys_to_virt(cpu_ttb), L1_SIZE)`  ->  `bcopy(phys_to_virt(gTopOfKernel),
#                       phys_to_virt(cpu_ttb), L1_SIZE)`, which is 4570's `bcopy(boot_tte, cpu_tte, ...)`.
#
# It is the SECOND of the two D13 defects 970g's press would expose: 970g (locore.s) makes the console
# reachable through the `_start`->`arm_init` window; this makes it survive `arm_vm_init`'s table switch,
# which is what the entry's own source (`src/entry/entry_stubs.c:2210-2214`) depends on.  4570 has the
# copy; D13 bzeroes a fresh table and so loses every key after `arm_vm_init`.
#
# **The object this touches is `out/xnu_asm_obj_d13/arm_vm_init.o`, written by the kernel build
# (`tools/build_xnu_arm_kernel.sh` / `tools/assemble_arm_layer.sh`).** After this script, re-run that
# build with `XNU_TREE` and `XNU_OBJ_SUFFIX` set, or the linked image keeps the old object and the patch
# is invisible (933: the roots must be EXPORTED).  The proof is by VALUE - the rebuilt object must call
# `_bcopy` where it used to call `_bzero`.
#
# Idempotent: a second run re-applies the (idempotent) patch and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/arm_vm_init.c ]] || {
    echo "stage_d13_vm_init: no osfmk/arm/arm_vm_init.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_vm_init.py" "$XNU"

echo "stage_d13_vm_init: done (XNU=$XNU)"
echo "stage_d13_vm_init: NOW RE-RUN the kernel build so out/xnu_asm_obj_d13/arm_vm_init.o picks it up"