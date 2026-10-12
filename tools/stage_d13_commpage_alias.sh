#!/bin/bash
# Stage 992+993's iOS 7 commpage alias into the D13 tree (experiments 992-993).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the edit that OPENS the six dormant IOS7LAB gates:
# `osfmk/arm/pmap.c` maps the commpage page a second time at `IOS7LAB_COMMPAGE_ALIAS` = 0xFFFF4000 in
# kernel_pmap (user read-only via L2_ACCESS_USER|L2_ACCESS_APX, NX); the block's read-back asserts
# compile OUT under MACH_ASSERT=0 (`assert(ex)` is `((void)0)`, osfmk/kern/assert.h:97) and its kprintf
# marker is a no-op in that file (`#define kprintf(args...)`, pmap.c:409), so the marker line is staged
# as a live `printf` (the same string) - the by-value evidence is the image-level checker's r1 binding;
# `osfmk/arm/commpage/commpage.c` writes the iOS 7 count bytes at
# `_COMM_PAGE_BASE_ADDRESS+0x34..0x36` (libdispatch sizes its heap from +0x36) and the cache-line byte
# at +0x26, with the `IOS7LAB commpage cpus:` marker.
#
# WHY (991's press, 2026-10-12, arm armed-d13-13355bf0, commit af099f4, evidence
# /tmp/cancro-last_kmsg-991.txt): a READ-CONFIRMED SUCCESS in the pre-registered sense - the
# VMHINT991 guard fired exactly twice (lr=0x8003d8ec = vm_map_pmap_enter's call) and the boot moved
# PAST the kernel-mode exec wall into userspace: `/sbin/launchd` EXECUTED (dyld ran from the mapped
# shared cache: dyld_all_image_info set, pc/lr in the 0x2be5-0x2be7 region) and then aborted 231x:
#     launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0)
#     (Translation fault on page)
# 0xffff4090 is `_COMM_PAGE_BASE_ADDRESS + 0x90` in the iOS 7 ABI. Our tree's control ABI sits at
# 0x40000000 (`cpu_capabilities.h:13`); the 4570/HD2 line solved this exact symptom with the alias
# this patch re-enables (its comment: "the address used by the original iOS 7 dyld").
#
# TWO build layers are touched: `osfmk/arm/pmap.c` -> pool object `osfmk_arm_pmap.o` and
# `osfmk/arm/commpage/commpage.c` -> pool object `osfmk_arm_commpage_commpage.o`, so re-run
# `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_COMMPAGE_ALIAS=1`
# (`build_xnu_arm_kernel.sh` also gates the same define on the same-named ENVIRONMENT variable; pass
# BOTH or the two spellings can diverge - `mi4-one-value-two-definitions`). `build_entry.sh` only
# LINKS the pool objects, so without the rebuild the linked image keeps the old code and the patch is
# invisible (933: the roots must be EXPORTED).
#
# Switch OFF compiles the tree's original text byte for byte: the appended clause is
# `|| (defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS)`, so an unset switch makes
# the `#if` false exactly as before (and an undefined identifier is never evaluated - the
# `mi4-off-option-two-spellings` guard).
#
# Idempotent: a second run re-verifies the six opened gates + the printf-staged marker line and prints
# the same summary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

for f in osfmk/arm/pmap.c osfmk/arm/commpage/commpage.c osfmk/arm/arm_vm_init.c; do
    [[ -f $XNU/$f ]] || {
        echo "stage_d13_commpage_alias: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }
done

python3 "$HERE/patch_d13_commpage_alias.py" "$XNU"

echo "stage_d13_commpage_alias: done (XNU=$XNU)"
echo "stage_d13_commpage_alias: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_COMMPAGE_ALIAS=1 + the same env var, for the pool's osfmk_arm_pmap.o + osfmk_arm_commpage_commpage.o + osfmk_arm_arm_vm_init.o)"