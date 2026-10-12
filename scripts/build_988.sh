#!/usr/bin/env bash
# Build 988: 987's switch set UNCHANGED, rebuilt with THE __LINKEDIT KEXT-BOOTSTRAP TEARDOWN COMPILED
# OUT (a new switch, STAGE90_XNU_KEXTB_LOGIN), so the boot can survive the first statement after
# `bsd_init()` returns.
#
# WHY 987 IS NOT THE ONE. 987's press (2026-10-12, commit cb2dd66, arm armed-d13-2df6333a) is a
# DECISIVE SUCCESS: it retired the __MALLOC stub and the boot went THROUGH the entire HFS mount and ALL
# of BSD init -- `Added memory device md0/rmd0` -> `BSD root: md0` -> `bsd_init: vfs_mountroot` ->
# `bsd_init: done` (55 bsd_init: lines). `cal_enter/exit=1`, `iokitinit_exit=1` -- 984's wall retired.
# Then it panicked at the FIRST thing after bsd_init returns.
#
# ROOT CAUSE (988). `osfmk/kern/startup.c:408` calls `OSKextRemoveKextBootstrap()` right after
# `bsd_init()` (:403). Inside `OSKext::removeKextBootstrap()` (`libkern/c++/OSKext.cpp:746`) there is an
# UNGUARDED lookup + dereference inside a LIVE `#if CONFIG_KXLD` block:
#     seg_to_remove = (kernel_segment_command_t *)getsegbyname("__LINKEDIT");   // :843
#     ...
#     seg_data = (void *) seg_to_remove->vmaddr;                                // :859  (r0 == NULL)
# Our kernel is a synthetic Mach-O (`_mh_execute_header`=0x804e54e4) whose header declares ncmds=3
# with segments only `__TEXT`/`__DATA`/`__PRELINK_TEXT` -- there is NO `__LINKEDIT`, so
# getsegbyname("__LINKEDIT") returns NULL and :859 dereferences it. The fault, BY VALUE:
# pc=0x800b2c90 = removeKextBootstrap+0x198, `e590501c ldr r5,[r0,#28]`, r0=0, far=0x1c, fsr=0x5.
# The sibling calls at :805 (`__KLD`) and :838 (`__symtab`) ARE guarded (`if (seg_to_remove)`), which is
# why they do not fault -- :843/:859 is the unguarded one. Apple and the reference repo carry no guard
# here; this is our ELF image's shape meeting Apple's Mach-O assumption
# ([[mi4-macho-must-describe-the-segments-boot-code-does-arithmetic-with]]).
#
# THE FIX. `tools/stage_d13_kextb_login.sh` (idempotent, text, in the gitignored tree) wraps the whole
# `#if CONFIG_KXLD ... #else ... #endif` block in `#ifndef STAGE90_XNU_KEXTB_LOGIN ... #endif`. With
# `-DSTAGE90_XNU_KEXTB_LOGIN=1` the teardown is compiled out on D13; with the switch off the text is
# byte-identical to Apple, so 4570's object does not move. `build_entry.sh` gains a by-value clause
# that REFUSES a linked image still carrying the faulting dereference (a claim in a comment is not a
# check).
#
# The switch set is 987's UNCHANGED (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0 = base never written). This
# script re-reads the current record and re-exports it verbatim, overriding nothing
# ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 988: compile the __LINKEDIT kext-bootstrap teardown out of D13; rebuild the pool + entry =="

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
EXTRA="$EXTRA -DSTAGE90_XNU_KEXTB_LOGIN=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edit (idempotent). This is a POOL object edit (`libkern/c++/OSKext.cpp`), so unlike
#    986 there is no `assemble_arm_layer.sh` step; the pool rebuild below is the load-bearing step.
echo "-- stage the __LINKEDIT teardown guard (libkern/c++/OSKext.cpp) --"
tools/stage_d13_kextb_login.sh

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

echo "-- arm asm layer (unchanged, still reassembled so the pool is self-consistent) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh:761` gates
# the define on the environment variable of that name: two spellings of one value would be exactly the
# [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN (pool DEFINES + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (OSKext.o must drop the __LINKEDIT dereference) --"
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

echo "-- entry image (987's switch set, re-exported verbatim) --"
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
# The record (987's) predates this key, so the value must arrive by ENVIRONMENT: export it explicitly
# so build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME value the pool was compiled with
# ([[mi4-one-value-two-definitions]] - the record's key and the -D must not diverge).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   987 was 2df6333a - a DIFFERENT value here means the guard moved the image."
echo "-- by value: does the linked removeKextBootstrap still carry the faulting dereference? (must be GONE) --"
KEXTB_SYM=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf 2>/dev/null | awk '/removeKextBootstrap/{print $3; exit}')
echo "   symbol: ${KEXTB_SYM:-<not found>}"
if [[ -n $KEXTB_SYM ]]; then
  arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf 2>/dev/null |
    awk -v s="<$KEXTB_SYM>:" '$0 ~ s {f=1} f && /ldr[[:space:]]+r[0-9]+, \[r[0-9]+, #28\]/ {print "   STILL PRESENT: "$0; n++} END{ if(!n) print "   (no ldr rN,[rM,#28] in removeKextBootstrap - the __LINKEDIT dereference is GONE)" }'
fi