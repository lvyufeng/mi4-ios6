#!/bin/bash
# Stage 988's __LINKEDIT kext-bootstrap guard into the D13 tree (experiment 988).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the ONE edit that makes D13's
# `OSKext::removeKextBootstrap()` stop dereferencing a NULL `seg_to_remove`: it wraps the whole
# `#if CONFIG_KXLD ... #else ... #endif` block in `libkern/c++/OSKext.cpp` in
# `#ifndef STAGE90_XNU_KEXTB_LOGIN`, so a pool built with `-DSTAGE90_XNU_KEXTB_LOGIN=1` compiles the
# teardown out. See `tools/patch_d13_kextb_login.py` for the fault, by value, and the fixed/source
# evidence.
#
# The object this touches is `out/xnu_kernel_obj_d13/bsd_...OSKext.o` (the pool spelling
# `build_xnu_arm_kernel.sh` gives the C++ half) - a POOL object, built by
# `tools/build_xnu_arm_kernel.sh`, NOT by the assembly layer. Unlike 986
# (`stage_d13_hw_lock_986.sh`, whose object is `out/xnu_asm_obj_d13/hw_lock.o` and which needs
# `tools/assemble_arm_layer.sh` re-run afterwards) there is NO extra assemble step here: the POOL
# rebuild is the load-bearing step, because `build_entry.sh` only LINKS the pool's `.o`s. Re-run
# `tools/build_xnu_arm_kernel.sh` with `XNU_TREE`/`XNU_OBJ_SUFFIX` set and
# `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_KEXTB_LOGIN=1` (933: the roots must be EXPORTED), or the
# linked image keeps the old `OSKext.o` and the patch is invisible.
#
# Idempotent: a second run re-applies the (idempotent) patch and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/libkern/c++/OSKext.cpp ]] || {
    echo "stage_d13_kextb_login: no libkern/c++/OSKext.cpp in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_kextb_login.py" "$XNU"

echo "stage_d13_kextb_login: done (XNU=$XNU)"
echo "stage_d13_kextb_login: NOW RE-RUN tools/build_xnu_arm_kernel.sh with -DSTAGE90_XNU_KEXTB_LOGIN=1 so the pool's OSKext.o picks it up (no assemble_arm_layer.sh step here - the pool rebuild IS the step)"