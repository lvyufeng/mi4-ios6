#!/bin/bash
# Stage 998's iOS 7 shared-region GEOMETRY + stack top into the D13 tree (experiment 998 = G1).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single entry point for 998's tree edit - THREE files - and calls the patcher that owns
# them; it edits nothing itself:
#   * `osfmk/vm/vm_shared_region.c` - `patch_d13_shreg_geom.py` (vmabi-recon lane's): the ONE
#     `#if IOS7_COMPAT_PLATFORM` line at the geometry block (:626) gains the 994 spelling
#     `|| (defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM)` and its runtime selector line
#     (:629) becomes `ios7_shreg_geom_enabled()`, so the tree's OWN iOS 7 window compiles in:
#     `base_address = 0x2C000000U;`, `size = 0x14000000U;` and the
#     `IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000` printf (all INSIDE the
#     gate). Dark on CANCRO today: `IOS7_COMPAT_PLATFORM` is 0 (only `BOARD_CONFIG_MSM8974_CANCRO`
#     is defined) and the runtime selector returns 0.
#   * `osfmk/arm/status.c` - the STACK HALF of the same ABI, and a HARD requirement of the SAME rung:
#     the stack `#if` (:62) gains the same spelling and :66 becomes the same helper, so
#     `LAB_USER_STACK` = `ios7lab_user_stack()` -> 0x2BE00000 instead of USRSTACK 0x2FE00000. Without
#     it the new window covers the stack top, `create_unix_stack`'s FIXED allocation at
#     trunc(user_stack - MAXSSIZ) targets 0x2FDF0000 INSIDE `[0x2C000000, 0x40000000)` and EVERY
#     process's stack relays out (audit §2). With it the target 0x2BD00000 falls below the window.
#   * `osfmk/arm/ios7_compat.h` - `ios7_shreg_geom_enabled()` added beside `ios7_traps_enabled()`:
#     `#if defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM` -> 1, `#else` ->
#     `ios7_compat_vmabi_enabled()`.
#
# WHY. 997's press (arm armed-d13-518b6a94, commit 9e6307a, log
# out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt:4096-4110) MEASURED the geometry
# wall by value: the four `IOS7LAB cache mapping` lines print (the 32 B wire is open) with ALREADY SLID
# addresses - m0 va=0x2cd9c000 = 0x2C000000 + slide 0xd9c000, m1 carries VM_PROT_SLIDE (init=0x23) -
# then `map_file kr=1` (KERN_INVALID_ADDRESS, the FIXED-path wrap in vm_map.c:1846) + `map_slide
# ret=14`: the tree's default 0x30000000/0x10000000 window cannot hold dyld's slid shared cache. See
# docs/reference/d13-shreg-frontier.md and the G1 audit docs/reference/d13-shreg-g1-audit.md (the
# by-value record of the block, its blast radius and the off-identity proof).
#
# WHY 1:1 SUBSTITUTIONS AND NOT AN INLINE `#if` WRAP (MEASURED - the audit's v1 rehearsal): this
# tree's `panic` embeds `__FILE__:__LINE__` (`osfmk/kern/debug.h:264`), so a wrap that adds lines to a
# .c file shifts every later panic string (three changed in the rehearsal) while `.text` stays
# identical. The 1:1 shape moves no .c line at all; the helper lives in a header, whose line count is
# never a .c's `__LINE__`.
#
# TWO build layers are touched, BOTH pool objects (`osfmk_vm_vm_shared_region.o`,
# `osfmk_arm_status.o`), so re-run `tools/build_xnu_arm_kernel.sh` with
# `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SHREG_GEOM=1` (it also gates the same define on the same-named
# ENVIRONMENT variable; pass BOTH or the two spellings can diverge - `mi4-one-value-two-definitions`).
# 998 does NOT touch the assembly layer and `build_entry.sh` only LINKS the objects, so without the
# rebuild the linked image keeps the default geometry and the fix is invisible (its 998 clause refuses
# such a link by value).
#
# Switch OFF compiles the tree's original text: the gate spelling is
# `#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM)`
# (`defined()` checked FIRST - the `mi4-off-option-two-spellings` guard) and the helper half is
# `#if defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM` / `#else` original, so with the macro
# undefined the preprocessor condition is the original one and the body is the original text. The
# patcher PROVES the removability with `--verify-off` (strip(staged) == pre-image AND re-plan(pre) ==
# staged, all three files).
#
# Idempotent: the patcher decides all edits before writing, asserts each anchor's occurrence count
# exactly, REFUSES (files untouched) on a partial/double/foreign edit, and a second run prints
# "nothing to do". `tools/check_d13_shreg_geom.py --tree <tree> [--expect pre|staged]` reads the state
# by value (and 998's link clause runs it with `--expect staged`).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/vm/vm_shared_region.c ]] || {
    echo "stage_d13_shreg_geom: no osfmk/vm/vm_shared_region.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 -I "$HERE/patch_d13_shreg_geom.py" "$XNU"

echo "stage_d13_shreg_geom: done (XNU=$XNU)"
echo "stage_d13_shreg_geom: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SHREG_GEOM=1 + the same env var, for the pool's osfmk_vm_vm_shared_region.o + osfmk_arm_status.o - the linked image only LINKS those objects; 998 does NOT touch the assembly layer)"