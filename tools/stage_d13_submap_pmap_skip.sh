#!/bin/bash
# Stage 1001's submap pmap-enter skip into the D13 tree (experiment 1001).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the edit that makes `osfmk/vm/vm_map.c`'s `vm_map_enter`
# tail SKIP `vm_map_pmap_enter` when `is_submap` is true - on a submap enter the `object` parameter IS
# the submap (`:2063`), a vm_map-zone element, and the unguarded call used to lock/probe it as if it
# were a vm_object (1000's press died there: `hw_lock_lock(): PANIC: Lock 0xc0005b50 = 0xffb5be0b` on
# a FREED vm_map-zone element's +8 word; 990 died at +0x18 of the same element, 999's VMHINT991
# guard caught it there). `pmap_empty` stays FALSE and a bounded kprintf beginning `SUBMAPSKIP:`
# records obj/start/end/size/lr in the skip branch. See `tools/patch_d13_submap_pmap_skip.py` for
# the by-value reasoning. The edit sits in
# `#if defined(STAGE90_XNU_SUBMAP_PMAP_SKIP) && STAGE90_XNU_SUBMAP_PMAP_SKIP ... #endif`, so switch
# OFF compiles the tree's original text byte for byte.
#
# ONE build layer is touched: `osfmk/vm/vm_map.c` -> the POOL object
# `out/xnu_kernel_obj_d13/osfmk_vm_vm_map.o`, so re-run `tools/build_xnu_arm_kernel.sh` with
# `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SUBMAP_PMAP_SKIP=1` (`build_xnu_arm_kernel.sh` also gates
# the same define on the same-named ENVIRONMENT variable; pass BOTH or the two spellings can diverge
# - `mi4-one-value-two-definitions`). `build_entry.sh` only LINKS the pool objects, so without the
# rebuild the linked image keeps the old code and the patch is invisible (933: the roots must be
# EXPORTED).
#
# Idempotent: a second run re-verifies the inserted form and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

f=osfmk/vm/vm_map.c
[[ -f $XNU/$f ]] || {
    echo "stage_d13_submap_pmap_skip: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_submap_pmap_skip.py" "$XNU"

echo "stage_d13_submap_pmap_skip: done (XNU=$XNU)"
echo "stage_d13_submap_pmap_skip: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SUBMAP_PMAP_SKIP=1 + the same env var, for the pool's osfmk_vm_vm_map.o)"