#!/bin/bash
# Stage 991's memq_hint guard into the D13 tree (experiment 991).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the edit that makes `osfmk/vm/vm_resident.c`'s
# `vm_page_lookup` treat an implausible `object->memq_hint` (`(vm_offset_t)mem < 0x80000000u` or
# `mem & 3u` - not a kernel address, or not word-aligned) as absent FOR THAT CALL ONLY: a local
# shadow lets the hash search below run as if there were no hint, and a bounded kprintf beginning
# `VMHINT991:` records the object's words and the caller. THE FIELD IS NEVER WRITTEN. The edit sits in
# `#if STAGE90_XNU_VMHINT_GUARD ... #endif`, so switch OFF compiles the tree's original text byte for
# byte. See `tools/patch_d13_vmhint_guard.py` for 990's fault, by value, and the reasoning.
#
# ONE build layer is touched: `osfmk/vm/vm_resident.c` -> the POOL object
# `out/xnu_kernel_obj_d13/osfmk_vm_vm_resident.o`, so re-run `tools/build_xnu_arm_kernel.sh` with
# `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_VMHINT_GUARD=1` (`build_xnu_arm_kernel.sh` also gates the
# same define on the same-named ENVIRONMENT variable; pass BOTH or the two spellings can diverge -
# `mi4-one-value-two-definitions`). `build_entry.sh` only LINKS the pool objects, so without the
# rebuild the linked image keeps the old code and the patch is invisible (933: the roots must be
# EXPORTED).
#
# Idempotent: a second run re-verifies the inserted form and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

f=osfmk/vm/vm_resident.c
[[ -f $XNU/$f ]] || {
    echo "stage_d13_vmhint_guard: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_vmhint_guard.py" "$XNU"

echo "stage_d13_vmhint_guard: done (XNU=$XNU)"
echo "stage_d13_vmhint_guard: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_VMHINT_GUARD=1 + the same env var, for the pool's osfmk_vm_vm_resident.o)"