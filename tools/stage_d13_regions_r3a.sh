#!/bin/bash
# Stage 915-B R3a (the COUNT half of the second allocator interval) into the D13 tree.
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule).
# This script is the single source of truth for the edit that makes the DT `/memory/reg` region list
# the pmap's region table (`pmap_mem_regions[]`), makes `pmap_valid_page` region-aware, and makes
# `avail_remaining` carry the low bank with a matching drain.  See `tools/patch_d13_regions_r3a.py`
# for the by-value reasoning, the anchor set and the refuse-not-half-apply rules.
#
# **WHAT R3a IS, AND WHY IT IS ONLY HALF OF THE DESIGN'S "R3".**  R3 is "the kernel acts on the DT
# region list".  Its emit half - draining low pages INTO CONSUMERS - is gated on R5's bounce window:
# `arm_vm_init` sets `TTBCR.N = 2` (`:475`), so TTBR0 owns [0, 0x40000000) and the kernel's
# identity/managed tables are built into the ONE `cpu_ttb` TTBR1 points at, covering
# [gPhysBase, gPhysBase + gMemSize) = 0x80000000-and-up only.  A general low DRAM page below the
# split has NO mapped kernel VA, so a consumer that dereferences one needs R5.  R3a is the BOOKING
# half: register the list, make the predicate a span-set, and make the free-page COUNT (the deciding
# instrument) include the low bank.  `tools/patch_d13_regions_r3a.py`'s module docstring records the
# one correction this rung makes to the design text: the count is NOT separable from the drain,
# because `pmap_startup` (`vm_resident.c:799-821`) sizes `vm_pages[]` from `pmap_free_pages()` and
# then steals that many pages through the same `pmap_next_page` - so a count with no matching drain
# overspends the boot region.  The drain rides in the same switch; the R5 emit-consumer half stays
# behind `STAGE90_XNU_BOUNCE_WINDOW`, which this rung does NOT define.
#
# **WHY THE SWITCH MUST REACH THE KERNEL CORPUS, AND WHY IT ALREADY DOES.**  Every reader of
# `STAGE90_XNU_REGIONS` this rung adds is in the KERNEL corpus - `osfmk/arm/pmap.c` and
# `osfmk/arm/arm_vm_init.c` - which `tools/build_xnu_arm_kernel.sh` compiles.  That script ALREADY
# threads the switch (`:243-259`, 915-B rung 2.5), always defining it 0 or 1, and refuses any other
# value.  So the ONLY channel for this rung is `STAGE90_XNU_REGIONS=1` in the environment of
# `tools/build_xnu_arm_kernel.sh` (the payload half is `scripts/build.sh:182-187`, which defines the
# same macro for the DT shape).  **MEASURED: `grep -c STAGE90_XNU_REGIONS tools/build_xnu_arm_kernel.sh`
# -> nonzero, the switch is threaded there; there is NO `XNU_KERNEL_EXTRA_DEFINES` path needed** -
# unlike 1015b's `STAGE90_XNU_GLOBAL_FIX`, which has no environment channel at all.  Passing the
# value twice (both the environment variable and `XNU_KERNEL_EXTRA_DEFINES`) is the second-spelling
# defect ([[mi4-one-value-two-definitions]]); export the one variable.
#
# OFF is the tree's ORIGINAL bytes: every insertion sits inside `#if STAGE90_XNU_REGIONS` (never
# `#ifdef` - the macro is always defined 0 or 1, so a 0 must read as off), including the `#else`
# arms of `pmap_valid_page`/`pmap_next_page`, which are Apple's text verbatim.  So a build with the
# switch 0 compiles the tree's bytes and the arms the ladder has already run do not move.
#
# Idempotent: a second run re-verifies the inserted forms and prints the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

for f in osfmk/arm/pmap.c osfmk/arm/arm_vm_init.c; do
    [[ -f $XNU/$f ]] || {
        echo "stage_d13_regions_r3a: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }
done

# Refuse the second spelling BEFORE staging: this switch is threaded by the kernel build script, so
# an `XNU_KERNEL_EXTRA_DEFINES` carrying it would be a second channel for one value.
case "${XNU_KERNEL_EXTRA_DEFINES:-}" in
    *STAGE90_XNU_REGIONS*)
        echo "stage_d13_regions_r3a: XNU_KERNEL_EXTRA_DEFINES carries STAGE90_XNU_REGIONS - the" >&2
        echo "stage_d13_regions_r3a: switch is ALREADY threaded by tools/build_xnu_arm_kernel.sh:243-259." >&2
        echo "stage_d13_regions_r3a: export STAGE90_XNU_REGIONS=1 and pass nothing else (one value, one channel)." >&2
        exit 2 ;;
esac

python3 "$HERE/patch_d13_regions_r3a.py" "$XNU"

echo "stage_d13_regions_r3a: done (XNU=$XNU)"
echo "stage_d13_regions_r3a: NOW re-run tools/build_xnu_arm_kernel.sh with STAGE90_XNU_REGIONS=1"
echo "stage_d13_regions_r3a: exported (the kernel corpus half) AND scripts/build.sh with the same value"
echo "stage_d13_regions_r3a: (the payload DT half) - it is ONE variable, threaded by both; build_entry.sh"
echo "stage_d13_regions_r3a: only LINKS the pool objects, so the switch must be set when the kernel is built."
echo "stage_d13_regions_r3a: The arm is BUILT and PRESS-GATED: docs/915b-next-rung.md section 3 gate G1"
echo "stage_d13_regions_r3a: (must ride a PRESSED 958) is still closed - the 958 arm was parked, not pressed."