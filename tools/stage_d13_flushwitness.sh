#!/bin/bash
# Stage 1014's TLB-flush witness into the D13 tree (experiment 1014, branch V1).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the edit that makes `osfmk/arm/pmap.c`'s `pmap_flush_tlbs`
# publish WHICH TLB-flush arm ran for the faulting pmap - the ONE mechanism 1013's press left unread
# (1013 measured the leaf `pmap_enter_options` actually WROTE as user-writable, `_wwapx=0`, yet the USER
# store still permission-faults ~2.1 M times). See `tools/patch_d13_flushwitness.py` for the by-value
# reasoning and the five anchors.
#
# WHY A SOURCE EDIT AND NOT A `--wrap`.  `--wrap` rewrites only linker-visible CROSS-object calls; the
# call the storm's enter rides is INTRA-object. MEASURED on the built pool object:
#
#   arm-none-eabi-objdump -r out/xnu_kernel_obj_d13/osfmk_arm_pmap.o | grep -c pmap_flush_tlbs   -> 13
#   grep -rln pmap_flush_tlbs <tree>/osfmk/                                                      -> arm/pmap.c ONLY
#
# Eleven caller functions, thirteen call sites, EVERY one of them in the same object as the callee - so
# a `--wrap=pmap_flush_tlbs` would redirect zero of them, including `pmap.c:3473` (the flush the enter
# at `enter_options_done:` rides). That would be a hook that cannot fire (`mi4-hooks-live-at-calls`).
#
# ONE build layer is touched: `osfmk/arm/pmap.c` -> the POOL object
# `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`, so re-run `tools/build_xnu_arm_kernel.sh` with
# `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_FLUSH_WITNESS=1` (`build_xnu_arm_kernel.sh` also gates the
# same define on the same-named ENVIRONMENT variable; pass BOTH or the two spellings can diverge -
# `mi4-one-value-two-definitions`). `build_entry.sh` only LINKS the pool objects, so without the
# rebuild the linked image keeps the old code and the patch is invisible (933: the roots must be
# EXPORTED).
#
# OFF is the tree's ORIGINAL text: every inserted line - including the `liveseen` local and its two
# accesses - sits inside `#if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS`, so
# switch OFF removes all five insertions and no identifier `liveseen` is declared or used.
#
# Idempotent: a second run re-verifies the inserted forms and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

f=osfmk/arm/pmap.c
[[ -f $XNU/$f ]] || {
    echo "stage_d13_flushwitness: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_flushwitness.py" "$XNU"

echo "stage_d13_flushwitness: done (XNU=$XNU)"
echo "stage_d13_flushwitness: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_FLUSH_WITNESS=1 + the same env var, for the pool's osfmk_arm_pmap.o)"