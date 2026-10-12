#!/usr/bin/env bash
# Build 991: 990's switch set + STAGE90_XNU_VMHINT_GUARD=1 - `vm_page_lookup` treats an implausible
# `object->memq_hint` as absent FOR THAT CALL ONLY (a local shadow; the field is never written). ONE
# build layer moves: the kernel pool (osfmk/vm/vm_resident.c, pool object osfmk_vm_vm_resident.o) - the
# assembly layer and pcb.c stay exactly 990's.
#
# WHY (990, pressed 2026-10-12, arm armed-d13-0d531453, commit ce0195e, evidence /tmp/cancro-last_kmsg.txt).
# A READ-CONFIRMED SUCCESS in the pre-registered sense: the SP-alignment fix HELD (sp = 0xcfdbba88,
# 8-aligned; 989's `vld1.32 {d18}` alignment fault did NOT recur), the boot ran the WHOLE mount +
# bsd_init and reached `attempting to start init of /sbin/launchd`, then took a NEW first abort INSIDE
# `vm_page_lookup`:
#     pc = 0x80020838, instruction `ldrd r0,[r5,#24]`, r5 = object->memq_hint = 0x1,
#     object = r0 = r4 = 0xc0005b48, lr = 0x8003d8ec = vm_map_pmap_enter's vm_page_lookup call (offset 0),
#     far = 0x19 (= 1 + 24), fsr = 1 (alignment fault).
# Then 214x `recursive abort` at pc = 0x8046a464 (Debugger trap) and a watchdog reset ~46 s later. The USB
# console capture was EMPTY; the log came from /proc/last_kmsg.
#
# ROOT CAUSE (991), read BY VALUE. `memq_hint` sits at vm_object+0x20 (`ldr r5,[r0,#32]`), and NO
# legitimate writer of `memq_hint` can produce 0x1: `vm_object_template` copies in NULL via
# `_vm_object_allocate`, VM_PAGE_INSERT/VM_PAGE_REMOVE write a valid page or NULL, and `vm_page_lookup`'s
# own hint paths write a real page. So either the field was clobbered after allocation, or `object` is
# not a vm_object - it sits ~11 MB below every other heap pointer in the log, consistent with an early
# zalloc allocation.
#
# THE FIX: `tools/stage_d13_vmhint_guard.sh` (idempotent; `#if STAGE90_XNU_VMHINT_GUARD` keeps switch-OFF
# text byte-identical). `vm_page_lookup` treats a hint with `(vm_offset_t)mem < 0x80000000u || (mem & 3u)`
# as absent FOR THAT CALL ONLY (local shadow; the field is never written), after a bounded (max 8)
# kprintf beginning `VMHINT991: ` that records obj/mem/lr and the object's words w00/w04/w18/w1c/w24. The
# linked-image property is asserted BY VALUE during the entry build by `tools/check_vmhint_guard.py`,
# which REFUSES the 990 image.
#
# PRE-REGISTERED READING for the press: the log carries `VMHINT991:` lines (at most 8) naming the
# object's words and lr, and the boot moves PAST the exec wall. If w00 (`memq.next`) equals obj and the
# other words look like a live vm_object, the next rung hunts the clobberer; if the words are nonsense,
# the object identity chain (ubc_getobject -> moc_object) is the bug and lr names the path.
# FALSIFICATION: the SAME fault recurs (`ldrd r0,[r5,#24]` at pc=0x80020838 with r5=1) => the guard did
# not reach the pool (the 991 clause should have refused such an image) or an object's hint was
# plausible-range-but-bad (the guard models only <0x80000000 | &3).
#
# The switch set is 990's + STAGE90_XNU_VMHINT_GUARD=1 (window 0x1e400000 = 484 MiB, RESIDENT=1, USB
# ladder, MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0 = base never written,
# KEXTB_LOGIN=1, SP_ALIGN=1). This script re-reads the current record and re-exports it verbatim,
# overriding nothing ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 991: guard vm_page_lookup's memq_hint; rebuild the pool + entry =="

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# The record this build reads is 990's, which predates the VMHINT_GUARD key (990 had no such switch), so
# pin it explicitly - the same value the ENV exports below carry ([[mi4-one-value-two-definitions]]).
# SP_ALIGN is pinned the same way: 990's record does carry it, but the pin keeps the layers from ever
# disagreeing.
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all three idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly); 991's is the memq_hint guard in the POOL alone
#    (`osfmk/vm/vm_resident.c`).
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
# MUST be exported BEFORE assemble_arm_layer.sh runs (it reads the environment, not the record).
export STAGE90_XNU_SP_ALIGN=1

# 991: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below - the guard
# lives in `osfmk/vm/vm_resident.c`, which the assembly pass never touches.
export STAGE90_XNU_VMHINT_GUARD=1

echo "-- arm asm layer (990: machine_routines_asm.o gets the astloop realignment; unchanged in 991) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh:772` gates
# the pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD (pool DEFINES + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (pcb.c picks the alignment up, vm_resident.c the hint guard) --"
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

echo "-- entry image (990's switch set + VMHINT_GUARD, re-exported verbatim; the 991 clause audits the link) --"
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
# The record (990's) does NOT carry VMHINT_GUARD (990 predates the switch); pin both tree-shaping
# switches explicitly so build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the build
# layers compiled with ([[mi4-one-value-two-definitions]]).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   990 was 0d531453 - a DIFFERENT value here means the guard moved the image."
echo "-- by value: the checker (PASSes only if the linked image carries the fix; it REFUSES 989's) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the 991 checker (PASSes only if the linked pool carries the guard; it REFUSES 990's image) --"
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the POOL object FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
# NOTE (ruled by 991's own first run, honestly): under `set -o pipefail` each `| head -N` dump below
# returns the PRODUCER's SIGPIPE (141) once head closes early, so `set -e` aborts the script AFTER the
# dump's output has printed. 991's first run died exactly there - exit 141 with both checkers PASS -
# and the last two dumps never printed. `|| true` consumes only that status; every real check above
# has already run, so the script's exit code is honest again.
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_resident.o 2>/dev/null | grep -E "VMHINT991|kprintf|tst" | head -30 || true
echo "-- by value: the 990 POOL object, still carrying the alignment fix --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_pcb.o 2>/dev/null | awk '
    /<machine_stack_attach>:/  { f="attach";  print; next }
    /<machine_stack_handoff>:/ { f="handoff"; print; next }
    f && /^[0-9a-f]+ </        { f="" }
    f                          { print }' | grep -E "<machine|bic|movw|add|sub|str" | head -30 || true
echo "-- by value: the ASSEMBLY object (astloop must carry sub sp / bl / add sp) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/machine_routines_asm.o 2>/dev/null | grep -B3 -A3 "ast_taken" | head -20 || true