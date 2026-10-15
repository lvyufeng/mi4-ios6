#!/bin/bash
# Stage 1015b's GLOBAL-FIX arm into the D13 tree (rung 1015b, the behaviour half of 1015).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule).
# This script is the single source of truth for the edit that makes `osfmk/arm/pmap.c`'s
# `pmap_flush_tlbs` take the FULL TLBIALL for a non-kernel pmap instead of the ASID-scoped TLBIASID.
# See `tools/patch_d13_globalfix.py` for the by-value reasoning, the anchor set and the refuse-not-
# half-apply rules.
#
# **THIS IS A BEHAVIOUR CHANGE - THE FIRST SINCE 986 - AND THE WHOLE POINT OF THE RUNG.**  1014 left
# every INPUT read: the leaf `pmap_enter_options` writes is USER-WRITABLE (`_wwapx=0`) and the flush
# RAN under the matching ASID (`_wfkind=2`, `_wfasid == _wctx`), yet the USER store still takes
# FS=15 permission faults at a CONSTANT `far`/`pc`/`fsr`.  H-G (docs/1015-design.md; H-G, records/
# 1014-press-read.txt §7) is that the CPU is using a STALE GLOBAL TLB ENTRY: every user leaf this
# port emits is GLOBAL (`L2_NG_BIT 0x800`, pmap.h:231, is never ORed tree-wide), and TLBIASID cannot
# evict a GLOBAL entry.  This script stages the one-line cure whose EFFECT is pre-registered in
# `tools/check_1015b_capture.py`: storm STOPPED => G-CONFIRMED; storm PERSISTS => G-REFUTED.
#
# **WHY A SOURCE EDIT AND NOT A `--wrap`.**  `--wrap` rewrites only linker-visible CROSS-object
# calls; `pmap_flush_tlbs` is called 13 times from 11 functions and EVERY call site is in the same
# object as the callee, so a wrapper would redirect zero of them - a hook that cannot fire
# (`mi4-hooks-live-at-calls`).  The 1014 pool object, MEASURED this session:
#
#   arm-none-eabi-objdump -r out/xnu_kernel_obj_d13/osfmk_arm_pmap.o | grep -E 'arm_tlb_flushID'
#     00000560 R_ARM_CALL  arm_tlb_flushID        (pmap_flush_tlbs, the :1347 arm)
#     0000058c R_ARM_CALL  arm_tlb_flushID_ASID   (pmap_flush_tlbs, the :1349 arm)
#
# ONE build layer is touched: `osfmk/arm/pmap.c` -> the POOL object
# `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`, so re-run `tools/build_xnu_arm_kernel.sh` with
# `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_GLOBAL_FIX=1`.  **THAT ONE CHANNEL IS THE ONLY ONE, AND IT
# IS MEASURED RATHER THAN ASSUMED** (`grep -c STAGE90_XNU_GLOBAL_FIX tools/build_xnu_arm_kernel.sh`
# -> 0): unlike 1014's `STAGE90_XNU_FLUSH_WITNESS` (`:819`, which carries its own `DEFINES+=` clause
# gated on the environment variable), this switch has **no environment path at all**.  Exporting it
# changes nothing, and `scripts/build_1015b.sh` refuses if it is exported, because a second spelling
# of one value is how the two drift apart (`mi4-one-value-two-definitions`) - pass `EXTRA` and only
# `EXTRA`.  `build_entry.sh` only LINKS the pool objects, so without that rebuild the linked image
# keeps the old code and the patch is invisible - and the arm sha8 would not move either, which
# `scripts/build_1015b.sh` refuses on.
#
# OFF is the tree's ORIGINAL text: the three inserted lines sit inside
# `#if defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX`, so switch OFF removes the arm
# entirely and Apple's `if (pmap_is_shared) / arm_tlb_flushID(); / else / arm_tlb_flushID_ASID(...)`
# is byte-for-byte what the compiler sees.
#
# **THE SHARED-OBJECT CAVEAT, STATED BECAUSE IT LIMITS WHAT "OFF IS BYTE-IDENTICAL" CAN MEAN HERE.**
# `arm_tlb_flushID` and `arm_tlb_flushID_ASID` are defined in ONE SHARED source,
# `osfmk/arm/armv/cpufunc-v7.s` (`:66-84`), compiled into its own object.  This switch changes which
# of the two symbols `pmap.c` CALLS; it never changes either body.  So the OFF proof is taken on the
# `pmap.c`-born object's call/relocation census, and it is exactly: ON moves one `R_ARM_CALL` from
# `arm_tlb_flushID_ASID` (@0x58c) to `arm_tlb_flushID`.  The instruction count of the arm does not
# change either way - a 2-instruction `if/else` becomes a 3-instruction `if/else if/else` whose extra
# `cmp` is a NET ZERO with the `beq` the compiler was already emitting - which is why the D13
# delta assertion in `scripts/build_1015b.sh` is an ORDERING test and not a size test.
#
# Idempotent: a second run re-verifies the inserted form and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

f=osfmk/arm/pmap.c
[[ -f $XNU/$f ]] || {
    echo "stage_d13_globalfix: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_globalfix.py" "$XNU"

echo "stage_d13_globalfix: done (XNU=$XNU)"
echo "stage_d13_globalfix: NOW RE-RUN tools/build_xnu_arm_kernel.sh with"
echo "stage_d13_globalfix: XNU_KERNEL_EXTRA_DEFINES carrying -DSTAGE90_XNU_GLOBAL_FIX=1 - that is the"
echo "stage_d13_globalfix: ONLY channel (no DEFINES+= clause, no environment path: MEASURED 0 mentions)"
echo "stage_d13_globalfix: for the pool's osfmk_arm_pmap.o - build_entry.sh only LINKS it"
echo "stage_d13_globalfix: this stage runs AFTER tools/stage_d13_flushwitness.sh (it anchors on the"
echo "stage_d13_globalfix: text that payload leaves in place) - see scripts/build_1015b.sh's order."