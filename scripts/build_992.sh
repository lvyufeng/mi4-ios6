#!/usr/bin/env bash
# Build 992: 991's switch set + STAGE90_XNU_COMMPAGE_ALIAS=1 - the six dormant IOS7LAB board gates in
# the D13 tree open: `pmap_create_sharedpage` maps the commpage page a second time at 0xFFFF4000 in
# kernel_pmap (user read-only via L2_ACCESS_USER|L2_ACCESS_APX, NX; its boot marker is staged as a LIVE
# `printf` - pmap.c:409's `#define kprintf(args...)` is a no-op - and its read-back asserts compile OUT
# under MACH_ASSERT=0, so the checker's r1 binding is the static read-back) and `commpage_populate`
# fills the iOS 7 count bytes at
# `_COMM_PAGE_BASE_ADDRESS+0x34..0x36` (libdispatch sizes its heap from +0x36) plus the cache-line byte
# at +0x26. TWO pool objects move: osfmk_arm_pmap.o and osfmk_arm_commpage_commpage.o - the assembly
# layer, pcb.c and vm_resident.c stay exactly 991's.
#
# WHY (991, pressed 2026-10-12, arm armed-d13-13355bf0, commit af099f4, evidence
# /tmp/cancro-last_kmsg-991.txt). A READ-CONFIRMED SUCCESS in the pre-registered sense: the VMHINT991
# guard fired exactly TWICE - `VMHINT991: obj=0xc0005b48 mem=0x1 lr=0x8003d8ec w00=8000 w04=baddecaf
# w18=1000 w1c=0 w24=0` (and obj=0xc0005ab0, same shape) - and the boot moved PAST the kernel-mode
# exec wall into USERSPACE: `/sbin/launchd` EXECUTED (dyld ran from the mapped shared cache:
# dyld_all_image_info set, pc/lr in the 0x2be5-0x2be7 region) and then aborted 231x:
#     launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0)
#     (Translation fault on page)
# Zero kernel fatal exceptions, zero panics in the whole 22,707-line log.
#
# THE w04 DECODE (991's pre-registered branch, ANSWERED). w04=0xbaddecaf is ZP_NOT_POISONED
# (osfmk/kern/zalloc.c:135-137), which `free_to_zone`'s default else-branch leaves in elem[1] when
# check-sampling is off; a real `_vm_object_allocate` copies the template over +4 and would have
# erased it. So the two reported "objects" are FREED zone-element memory being walked as a vm_object
# (their identity is a later rung - the guard's local shadow carried the boot past them), and the
# first abort MOVED to userspace, where the commpage wall stopped launchd.
#
# ROOT CAUSE (992), read BY VALUE. 0xffff4090 = the iOS 7 commpage + 0x90. This tree's control ABI is
# `_COMM_PAGE32_BASE_ADDRESS` = 0x40000000 (`osfmk/arm/cpu_capabilities.h:13`), and pmap.c's
# `leo_pte_build` grants exactly THAT va `L2_ACCESS_USER`; userspace reads the commpage at 0xffff4000,
# which is unmapped. The fix for this exact symptom already exists in the tree - the 4570/HD2 line's
# IOS7LAB alias (`pmap.c:205,2848,3242,3434` + `commpage.c:57,99`; its own comment: "the address used
# by the original iOS 7 dyld") - compiled out on D13 behind six `BOARD_CONFIG_ARMPBA8 ||
# BOARD_CONFIG_QSD8250_LEO` gates. The alias nests cleanly: `arm_vm_init.c:413-423` maps the high
# vectors at 0xffff0000 as an `L2_SMALL_PAGE`, so the region's L1 slot already points at an L2 table
# and slot 0xf4 (0xffff4000) is free - the same layout real iOS 7 hardware has.
#
# THE FIX: `tools/stage_d13_commpage_alias.sh` appends
# `|| (defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS)` to the six byte-identical
# gate lines (switch OFF compiles the tree's original text byte for byte; the `defined()` form never
# evaluates an undefined identifier - [[mi4-off-option-two-spellings]]). The linked-image property is
# asserted BY VALUE during the entry build by `tools/check_commpage_alias.py`, which REFUSES the 991
# image.
#
# PRE-REGISTERED READING for the press: the log carries BOTH markers - `IOS7LAB commpage alias
# 0xffff4000: same page, user read-only, XN` (pmap_create_sharedpage; the line is a printf BECAUSE
# kprintf is a no-op at pmap.c:409 - its three asserts compile out under MACH_ASSERT=0, so the checker's
# r1 binding is the static read-back that stands in for them) and `IOS7LAB commpage cpus:
# configured=... active=... physical=... logical=...`
# (commpage_populate) - and launchd's `usermode data abort ... 0xffff4090` does NOT recur; the boot
# moves further into userspace. Candidates for the NEXT wall, named in advance: (a) libdispatch heap
# sized from +0x36 (now 1); (b) the commpage's VERSION/timebase fields still ZERO (populate writes
# only CPUFAMILY/MYSTERY/CAPABILITIES/MEMORY_SIZE/NCPUS/count bytes/cache-line -
# `mach_absolute_time` takes the syscall fallback, which the 4570 comment predicts) -> a NEW abort or
# panic elsewhere; (c) a new launchd/dyld read at another address. FALSIFICATION: the SAME
# `0xffff4090 ... Translation fault` abort recurs => the alias did not reach the pool (the 992 clause
# should have refused such an image), the perms are wrong (then the SAME page faults with a PERMISSION
# fault instead of a translation fault - the compiled-out asserts cannot name it), or the alias landed
# in a pmap the user context does not consult (a 4570-assumption defect - then
# the pte attribute path is the suspect).
#
# The switch set is 991's + STAGE90_XNU_COMMPAGE_ALIAS=1 (window 0x1e400000 = 484 MiB, RESIDENT=1,
# USB ladder, MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0 = base never
# written, KEXTB_LOGIN=1, SP_ALIGN=1, VMHINT_GUARD=1). This script re-reads the current record and
# re-exports it verbatim, overriding nothing ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 992: open the IOS7LAB commpage alias (0xFFFF4000); rebuild the pool + entry =="

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# The record this build reads is 991's, which predates the COMMPAGE_ALIAS key (991 had no such switch),
# so pin it explicitly - the same value the ENV exports below carry
# ([[mi4-one-value-two-definitions]]). SP_ALIGN and VMHINT_GUARD are pinned the same way: 991's record
# carries both, but the pins keep the layers from ever disagreeing.
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all four idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly); 991's is the memq_hint guard in the POOL alone
#    `osfmk/vm/vm_resident.c`); 992's is the commpage alias in the POOL alone (`osfmk/arm/pmap.c` +
#    `osfmk/arm/commpage/commpage.c`, plus the alias block's marker line staged as printf).
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias (992) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
# MUST be exported BEFORE assemble_arm_layer.sh runs (it reads the environment, not the record).
export STAGE90_XNU_SP_ALIGN=1

# 991: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below - the guard
# lives in `osfmk/vm/vm_resident.c`, which the assembly pass never touches.
export STAGE90_XNU_VMHINT_GUARD=1

# 992: same rule for the commpage alias (`osfmk/arm/pmap.c` + `osfmk/arm/commpage/commpage.c`, both
# pool files).
export STAGE90_XNU_COMMPAGE_ALIAS=1

echo "-- arm asm layer (990: machine_routines_asm.o gets the astloop realignment; unchanged in 991/992) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS (pool DEFINES + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (pmap.c + commpage.c pick the alias up, vm_resident.c the hint guard, pcb.c the alignment) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** `hfs_vfsops.c` is `#if
# STAGE90_HFS_ROOT_RW`, set from the ENVIRONMENT variable `STAGE90_XNU_HFS_ROOT_RW`
# (build_xnu_arm_kernel.sh:720), NOT from `-DSTAGE90_XNU_HFS_ROOT_RW`. Same trap as 987
# ([[mi4-one-value-two-definitions]]): the record's key name and the tree's guard name differ by one
# word, and only the environment path bridges them.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

echo "-- entry image (991's switch set + COMMPAGE_ALIAS, re-exported verbatim; the 992 clause audits the link) --"
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
# The record (991's) does NOT carry COMMPAGE_ALIAS (991 predates the switch); pin the tree-shaping
# switches explicitly so build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the build
# layers compiled with ([[mi4-one-value-two-definitions]]).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_XNU_COMMPAGE_ALIAS=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   990 was 0d531453, 991 was 13355bf0 - a DIFFERENT value here means the alias moved the image."
echo "-- by value: the checker (PASSes only if the linked image carries the fix; it REFUSES 991's) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the 992 checker (both IOS7LAB anchors, 0xFFFF4000 + 0x40000000 in pmap_create_sharedpage, >=2 pmap_enter, the alias va live in r1 at the 2nd; the read-back asserts compile out under MACH_ASSERT=0) --"
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the POOL object FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
# NOTE (ruled by 991's own first run, honestly): under `set -o pipefail` each `| head -N` dump below
# returns the PRODUCER's SIGPIPE (141) once head closes early, so `set -e` aborts the script AFTER the
# dump's output has printed. `|| true` consumes only that status; every real check above has already
# run, so the script's exit code is honest.
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_pmap.o 2>/dev/null | grep -E "IOS7LAB|65535|16384" | head -20 || true
echo "-- by value: the commpage POOL object (the cpus marker + the count-byte writes) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_commpage_commpage.o 2>/dev/null | grep -E "IOS7LAB|commpage cpus|0x34|52" | head -20 || true
echo "-- by value: the 991 POOL object, still carrying the hint guard --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_resident.o 2>/dev/null | grep -E "VMHINT991|kprintf|tst" | head -20 || true
echo "-- by value: the 990 POOL object, still carrying the alignment fix --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_pcb.o 2>/dev/null | awk '
    /<machine_stack_attach>:/  { f="attach";  print; next }
    /<machine_stack_handoff>:/ { f="handoff"; print; next }
    f && /^[0-9a-f]+ </        { f="" }
    f                          { print }' | grep -E "<machine|bic|movw|add|sub|str" | head -20 || true
echo "-- by value: the ASSEMBLY object (astloop must carry sub sp / bl / add sp) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/machine_routines_asm.o 2>/dev/null | grep -B3 -A3 "ast_taken" | head -20 || true