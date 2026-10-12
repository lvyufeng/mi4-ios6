#!/usr/bin/env bash
# Build 990: 989's switch set + STAGE90_XNU_SP_ALIGN=1 - the D13 tree hands C 8-byte-aligned stacks at
# its two measured gates (B1: `pcb.c`'s saved-state block rounded down to 8; A: astloop's `sub sp,#4`
# around its `blx _ast_taken`). TWO build layers move: the assembly layer (machine_routines_asm.s) and
# the kernel pool (pcb.c).
#
# WHY (989, pressed 2026-10-12, arm armed-d13-2e0eebf5, commit 1bc3fdb, evidence /tmp/press989.log).
# A DECISIVE SUCCESS in the pre-registered sense - the abort MOVED again. 988's dt-census guard HELD
# (`xnu_live_dtref_ok=0`), the mount stayed up, and the boot ran to the pid-1 launchd exec:
# `bsd_init: done` -> `attempting to start init of /sbin/launchd` -> NEW first abort:
#     Fatal exception: sleh_abort: alignment fault; pc=0x8021b6a8 = parse_machfile+0x60c,
#     instruction `f461279f vld1.32 {d18}, [r1 :64]`, r1 = sp+168 = 0xcfdbbb24, sp = 0xcfdbba7c.
# 168 is 8-aligned, so the 8-byte-qualified NEON pair load faults iff the running sp is 4 (mod 8) -
# and 0xcfdbba7c is 4 (mod 8). Three further aborts on the way (copyout 0x80494970, copyinstr
# 0x80494d9c, bcopy) all decode to the same 4-mod-8 C island.
#
# ROOT CAUSE (990). The D13 tree has TWO places that hand C a stack at 4 (mod 8); 4570 has neither:
#   1. `osfmk/arm/pcb.c`: `iss = STACK_IKS(stack) = stack_top - 68` and
#      `sizeof(arm_saved_state_t) = 68 = 4 (mod 8)`. `Call_continuation` (cswitch.s:46) loads the
#      continuation's sp straight from `machine.iss` and `fleh_swi` runs syscall handlers on the same
#      value - so every continuation and every syscall C function starts 4 (mod 8). 4570's
#      `thread_kernel_state` was 16-aligned (80): one value, two definitions.
#   2. `osfmk/arm/machine_routines_asm.s:133`: astloop's `stmfd sp!,{r0-r7,lr}` = 9 words = 36 = 4
#      (mod 8) immediately before `blx _ast_taken` (C) - and bsdinit's thread's first C function was
#      exactly `ast_taken`, reached from `thread_return_join` (no push of its own), whose sp came from
#      gate 1. The exec C island that died is that chain: ast_taken -> bsd_ast -> bsdinit_task ->
#      load_init_program -> execve -> __mac_execve -> exec_activate_image -> exec_mach_imgact ->
#      load_machfile -> parse_machfile (its own entry frame, 36+268=304, preserves the residue).
#
# THE FIX: `tools/stage_d13_sp_align.sh` (idempotent; `#if STAGE90_XNU_SP_ALIGN` keeps switch-OFF text
# byte-identical). The linked-image property is REFUSED by value when wrong: `build_entry.sh`'s 990
# clause runs `tools/check_sp_alignment.py` (the same walker that REFUSES the 989 image - astloop's
# delta -36 = 4 mod 8) whenever STAGE90_XNU_SP_ALIGN=1.
#
# The switch set is 989's + STAGE90_XNU_SP_ALIGN=1 (window 0x1e400000 = 484 MiB, RESIDENT=1, USB
# ladder, MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0 = base never written,
# KEXTB_LOGIN=1). This script re-reads the current record and re-exports it verbatim, overriding
# nothing ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 990: 8-byte-align the D13 tree's C stack gates; rebuild the asm layer + pool + entry =="

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# 989's record predates the SP_ALIGN key (989 had no such switch), so pin it explicitly - the same
# value the two ENV exports below carry ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (both idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly).
echo "-- stage the __LINKEDIT teardown guard (988's edit, still needed) + the SP-alignment edit (990) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
# MUST be exported BEFORE assemble_arm_layer.sh runs (it reads the environment, not the record).
export STAGE90_XNU_SP_ALIGN=1

echo "-- arm asm layer (990: machine_routines_asm.o gets the astloop realignment) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh:772` gates
# the pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN (pool DEFINES + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (pcb.c picks the alignment up) --"
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

echo "-- entry image (989's switch set + SP_ALIGN, re-exported verbatim; the 990 clause audits the link) --"
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
# The record (989's) does NOT carry SP_ALIGN (989 predates the switch); pin both explicitly so
# build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the two build layers compiled with
# ([[mi4-one-value-two-definitions]]).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   989 was 2e0eebf5 - a DIFFERENT value here means the alignment fix moved the image."
echo "-- by value: the checker (PASSes only if the linked image carries the fix; it REFUSES 989's) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the POOL object FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_pcb.o 2>/dev/null | awk '
    /<machine_stack_attach>:/  { f="attach";  print; next }
    /<machine_stack_handoff>:/ { f="handoff"; print; next }
    f && /^[0-9a-f]+ </        { f="" }
    f                          { print }' | grep -E "<machine|bic|movw|add|sub|str" | head -30
echo "-- by value: the ASSEMBLY object (astloop must carry sub sp / bl / add sp) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/machine_routines_asm.o 2>/dev/null | grep -B3 -A3 "ast_taken" | head -20