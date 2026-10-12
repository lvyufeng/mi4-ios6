#!/usr/bin/env bash
# Park 992 and append its block to records/revert-set.txt.
#
# 992 = 991's switch set + STAGE90_XNU_COMMPAGE_ALIAS=1: the six dormant IOS7LAB board gates in the D13
# tree open, and `pmap_create_sharedpage` maps the commpage page a second time at 0xFFFF4000 in
# kernel_pmap (user read-only via L2_ACCESS_USER|L2_ACCESS_APX, NX; its boot marker is staged as a LIVE
# `printf` - pmap.c:409's kprintf is a no-op - and its read-back asserts compile out under MACH_ASSERT=0,
# so the checker's r1 binding is the static read-back) while `commpage_populate` fills the iOS 7 count
# bytes at `_COMM_PAGE_BASE_ADDRESS+0x34..0x36`
# (libdispatch sizes its heap from +0x36). TWO tree objects move: the pool's osfmk_arm_pmap.o and
# osfmk_arm_commpage_commpage.o - this rung has NO assembly-layer, NO pcb.c and NO vm_resident.c change.
# The ENTRY BIN moves (13355bf0 -> ?) and build_entry.sh gains the ENTRY_ARM_KEY, the record emission and
# a by-value refusing clause (tools/check_commpage_alias.py). A fresh checkout needs ALL FOUR stage
# scripts: tools/stage_d13_kextb_login.sh (988's), tools/stage_d13_sp_align.sh (990's),
# tools/stage_d13_vmhint_guard.sh (991's) and tools/stage_d13_commpage_alias.sh (992's).
#
# WHY (991's press, 2026-10-12, arm armed-d13-13355bf0, commit af099f4, evidence
# /tmp/cancro-last_kmsg-991.txt): a READ-CONFIRMED SUCCESS in the pre-registered sense - the VMHINT991
# guard fired exactly TWICE (`VMHINT991: obj=0xc0005b48 mem=0x1 lr=0x8003d8ec w00=8000 w04=baddecaf
# w18=1000 w1c=0 w24=0`, and obj=0xc0005ab0, same shape) and the boot moved PAST the kernel-mode exec
# wall into USERSPACE: /sbin/launchd EXECUTED - dyld ran from the mapped shared cache (dyld_all_image_info
# set, pc/lr in the 0x2be5-0x2be7 region) - and then aborted 231x:
#     launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0)
#     (Translation fault on page)
# Zero kernel fatal exceptions, zero panics in the whole 22,707-line log.
#
# THE w04 DECODE (991's pre-registered branch, ANSWERED). w04=0xbaddecaf is ZP_NOT_POISONED
# (osfmk/kern/zalloc.c:135-137), which free_to_zone's default else-branch leaves in elem[1] when
# check-sampling is off; a real _vm_object_allocate copies the template over +4 and would have erased
# it. So the two reported "objects" are FREED zone-element memory, and the first abort MOVED to
# userspace, where the commpage wall stopped launchd.
#
# ROOT CAUSE (992), read BY VALUE. 0xffff4090 = the iOS 7 commpage + 0x90. This tree's control ABI is
# `_COMM_PAGE32_BASE_ADDRESS` = 0x40000000 (osfmk/arm/cpu_capabilities.h:13) and pmap.c's leo_pte_build
# grants exactly THAT va L2_ACCESS_USER; userspace reads the commpage at 0xffff4000, which is unmapped.
# The fix for exactly this symptom already exists in the tree - the 4570/HD2 line's IOS7LAB alias
# (pmap.c:205,2848,3242,3434 + commpage.c:57,99; its comment: "the address used by the original iOS 7
# dyld") - compiled out on D13 behind six `BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` gates. The
# alias nests cleanly: arm_vm_init.c:413-423 maps the high vectors at 0xffff0000 as an L2_SMALL_PAGE, so
# the region's L1 slot already points at an L2 table and slot 0xf4 (0xffff4000) is free.
#
# THE FIX (`tools/stage_d13_commpage_alias.sh`): appends
# `|| (defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS)` to the six byte-identical gate
# lines (switch OFF compiles the tree's original text byte for byte; the defined() form never evaluates
# an undefined identifier). The linked-image property is asserted BY VALUE during the entry build by
# tools/check_commpage_alias.py, which REFUSES the 991 image.
#
# READ: the log carries BOTH markers - `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN`
# (staged as printf because kprintf is a no-op at pmap.c:409; the three asserts compile out under
# MACH_ASSERT=0, so the checker's r1 binding is the static read-back) and `IOS7LAB commpage cpus:
# configured=... active=... physical=... logical=...` - and launchd's `usermode data abort ...
# 0xffff4090` does NOT recur; the boot moves
# further into userspace (candidates: libdispatch heap from +0x36=1; the commpage VERSION/timebase fields
# still ZERO; a new dyld read elsewhere).
# FALSIFICATION: the SAME `0xffff4090 ... Translation fault` abort recurs => the alias did not reach the
# pool (the 992 clause should have refused such an image), the perms are wrong (then the SAME page faults
# with a PERMISSION fault - the compiled-out asserts cannot name it), or the alias landed in a pmap the
# user context does not consult.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to park the same
# bytes twice or to re-park 991's arm under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-13355bf0 ]] && { echo "REFUSING: $LIVE holds 991's arm (13355bf0); build 992 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  992: THE COMMPAGE IS MAPPED AT 0xFFFF4000 FOR iOS 7 USERSPACE (IOS7LAB ALIAS OPENED).\n' "$ARM"
  printf '# Switch set is 991-s + STAGE90_XNU_COMMPAGE_ALIAS=1; the ENTRY BIN moved (13355bf0 -> %s) and TWO tree\n' "${ARM#armed-d13-}"
  printf '# objects move: the pool-s osfmk_arm_pmap.o and osfmk_arm_commpage_commpage.o - this rung has NO\n'
  printf '# assembly-layer, NO pcb.c and NO vm_resident.c change (a fresh checkout needs ALL FOUR\n'
  printf '# tools/stage_d13_kextb_login.sh, tools/stage_d13_sp_align.sh, tools/stage_d13_vmhint_guard.sh and\n'
  printf '# tools/stage_d13_commpage_alias.sh).\n#\n'
  printf '# 991 (pressed 2026-10-12, arm armed-d13-13355bf0, commit af099f4, evidence\n'
  printf '# /tmp/cancro-last_kmsg-991.txt) is a READ-CONFIRMED SUCCESS in the pre-registered sense: the VMHINT991\n'
  printf '# guard fired exactly TWICE (obj=0xc0005b48 mem=0x1 lr=0x8003d8ec w00=8000 w04=baddecaf w18=1000 w1c=0\n'
  printf '# w24=0; and obj=0xc0005ab0, same shape) and the boot moved PAST the kernel-mode exec wall into\n'
  printf '# USERSPACE: /sbin/launchd EXECUTED - dyld ran from the mapped shared cache (dyld_all_image_info set,\n'
  printf '# pc/lr in the 0x2be5-0x2be7 region) - and then aborted 231x: launchd[1]: usermode data abort,\n'
  printf '# EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0) (Translation fault on page). Zero\n'
  printf '# kernel fatal exceptions, zero panics in the whole 22,707-line log.\n#\n'
  printf '# THE w04 DECODE (991-s pre-registered branch, ANSWERED): w04=0xbaddecaf is ZP_NOT_POISONED\n'
  printf '# (osfmk/kern/zalloc.c:135-137), which free_to_zone-s default else-branch leaves in elem[1] when\n'
  printf '# check-sampling is off; a real _vm_object_allocate copies the template over +4 and would have erased\n'
  printf '# it. The two reported "objects" are FREED zone-element memory; the first abort MOVED to userspace,\n'
  printf '# where the commpage wall stopped launchd.\n#\n'
  printf '# ROOT CAUSE (992), read BY VALUE: 0xffff4090 = the iOS 7 commpage + 0x90. This tree-s control ABI is\n'
  printf '# _COMM_PAGE32_BASE_ADDRESS = 0x40000000 (osfmk/arm/cpu_capabilities.h:13) and pmap.c-s leo_pte_build\n'
  printf '# grants exactly THAT va L2_ACCESS_USER; userspace reads the commpage at 0xffff4000, unmapped. The fix\n'
  printf '# for exactly this symptom already exists in the tree - the 4570/HD2 line-s IOS7LAB alias\n'
  printf '# (pmap.c:205,2848,3242,3434 + commpage.c:57,99; its comment: "the address used by the original iOS 7\n'
  printf '# dyld") - compiled out on D13 behind six BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO gates. The\n'
  printf '# alias nests cleanly: arm_vm_init.c:413-423 maps the high vectors at 0xffff0000 as an L2_SMALL_PAGE,\n'
  printf '# so the region-s L1 slot already points at an L2 table and slot 0xf4 (0xffff4000) is free.\n#\n'
  printf '# 992: tools/stage_d13_commpage_alias.sh appends `|| (defined(STAGE90_XNU_COMMPAGE_ALIAS) &&\n'
  printf '# STAGE90_XNU_COMMPAGE_ALIAS)` to the six byte-identical gate lines (switch OFF compiles the tree-s\n'
  printf '# original text byte for byte; the defined() form never evaluates an undefined identifier).\n'
  printf '# tools/check_commpage_alias.py walks the LINKED elf (both IOS7LAB anchors, the alias va AND the\n'
  printf '# control-ABI va materialized inside pmap_create_sharedpage, >=2 pmap_enter calls with the alias va\n'
  printf '# live in r1 at the second - the static read-back, since the asserts compile out under MACH_ASSERT=0 -\n'
  printf '# and the alias va in >=2 bodies) and REFUSES the 991 image.\n#\n'
  printf '# READ: the log carries BOTH markers - IOS7LAB commpage alias 0xffff4000: same page, user read-only,\n'
  printf '# XN (staged as printf: kprintf is a no-op at pmap.c:409; the three asserts compile out under\n'
  printf '# MACH_ASSERT=0) and IOS7LAB commpage cpus: configured=... active=... physical=...\n'
  printf '# logical=... - and launchd-s usermode data abort ... 0xffff4090 does NOT recur; the boot moves further\n'
  printf '# into userspace (candidates: libdispatch heap from +0x36=1; the commpage VERSION/timebase fields still\n'
  printf '# ZERO; a new dyld read elsewhere).\n'
  printf '# FALSIFICATION: the SAME 0xffff4090 Translation fault abort recurs => the alias did not reach the pool\n'
  printf '# (the 992 clause should have refused such an image), the perms are wrong (then the SAME page faults\n'
  printf '# with a PERMISSION fault - the compiled-out asserts cannot name it), or the alias landed in a pmap the\n'
  printf '# user context does not consult.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never written -\n'
  printf '# fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=992-commpage-alias-0xffff4000;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3