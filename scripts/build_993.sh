#!/usr/bin/env bash
# Build 993: 992's switch set UNCHANGED (no new config key) + three staged tree edits that fix the 992
# press's regression: `arm_vm_init` ZEROES the stolen high-vector L2 table, and `pmap_create_sharedpage`
# MEASURES (and fail-soft clears) slot 0xf4 before the alias enter.
#
# WHY (992's press, 2026-10-12, arm armed-d13-fce6e8dd, commit d5facfc, evidence /tmp/cancro-last_kmsg.txt):
# the rung REGRESSED. 992 and 991's logs are identical through line 4043; 992 then panics at 4044, inside
# `vm_commpage_init` -> `pmap_create_sharedpage` (the alias `pmap_enter`):
#     panic(cpu 0 caller 0x8046b6b0): Kernel data abort. (faulting address: 0xc1233b14)
#     r4=0xc0837000 r6=0x80000000 r7=0xffff4000 r9=0x80b813d0 r10=0x003fffcc r11=0x000ffd5a
#     pc=0x80028b90 lr=0x80049abc fsr=0x7 (translation fault, page, READ); sleh seq1 user=0 recover=0.
#
# THE ROOT CAUSE, read BY VALUE (every number below is arithmetic against that log):
#   pc=0x80028b90 is `ldr r0, [r0, #12]` in pmap_enter_options+0x29c - the `pv_h->pmap` load that
#   precedes `panic("pmap_enter_options: null pv_list\n")` (pmap.c ~3312), taken because the slot held a
#   nonzero "old mapping". The chain: fp = old_pte>>12 = r11 = 0x000FFD5A (a leftover PA 0xFFD5A000);
#   pai = fp - (gPhysBase>>12) = 0xFFD5A - 0x80000 = 0x7FD5A (523,610); pv_h = r4 + pai*20 = 0xC1233B08;
#   the load of [pv_h+12] = 0xC1233B14 = the fault address exactly. The pv_head_table holds only 123,904
#   entries (484 MiB / 4 KiB), so the walk left the table and hit unmapped VA.
#   WHY the slot held garbage: `arm_vm_init` steals `vecpt_start = first_avail` as the high-vector L2
#   table and writes ONLY slot 0xF0 (VECTORS_BASE = 0xFFFF0000). boot_args->topOfKernel = 0x80a00000
#   (992 log line 3966) => vecpt_start = 0x80A00000 + 0x8000 + 0x79000 + 0x100000 = 0x80B81000, and
#   r9 = 0x80b813d0 = vecpt_start + 0xF4*4 = exactly the pmap_pte(kernel_pmap, 0xFFFF4000) pointer: the
#   992 alias enter was the FIRST EVER enter into that slot, whose 255 neighbors were raw DRAM. Every
#   OTHER table in the boot path is zeroed - l2_cache_to_range(zero=TRUE), whose own comment
#   (arm_vm_init.c:250) warns about exactly this "may have been used by the bootloader" case, and
#   pmap_grab_page's bzero (:2769) for pmap_expand; the stolen vector page was the sole exception.
#
# THE FIX (`tools/stage_d13_commpage_alias.sh` stages all three, gated on the SAME switch):
#   1. arm_vm_init.c: after `va_vecpt` is computed, count + print the stolen table's nonzero slots, bzero
#      the whole L2_SIZE page, count + print again ("IOS7LAB vecl2:").
#   2. pmap.c's alias block: print slot 0xf4 BEFORE the enter ("L2 slot pre = 0x%08x"), fail-soft clear it
#      if nonzero ("stale slot cleared"), print it after ("L2 slot post").
# The linked-image property is asserted BY VALUE by tools/check_commpage_alias.py, whose checks 6-7 now
# REFUSE a 992-shaped image (rehearsed: the frozen 992 elf exits 1 with exactly checks 1/6/7 failing).
#
# PRE-REGISTERED READING for the press: "IOS7LAB vecl2: N nonzero slot(s) ... before zeroing" (a small N;
# slot 0xf4's 0xFFD5A0xx region among them, the rest of the top lines shown), then "... 0 nonzero slot(s)
# after zeroing", then "IOS7LAB commpage alias: L2 slot pre = 0x00000000" (proving the arm_vm_init zero
# ran and REACHED this pmap), the 992 marker `IOS7LAB commpage alias 0xffff4000: same page, user
# read-only, XN` and `IOS7LAB commpage cpus: ...`; then bsd_init -> launchd, and the 231x
# `0xffff4090 ... Translation fault` burst MUST NOT recur.
# FALSIFICATION: the same 231x 0xffff4090 burst recurs (then the alias is in a pmap the user context does
# not consult - 994 = the user-pmap/context side), or a NEW kernel abort/panic appears (then its pc names
# the next wall - the checker's checks 6-7 still hold for the alias itself).
#
# The switch set is 992's, VERBATIM (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0, KEXTB_LOGIN=1, SP_ALIGN=1,
# VMHINT_GUARD=1, COMMPAGE_ALIAS=1). The record (992's own emission) now CARRIES
# STAGE90_XNU_COMMPAGE_ALIAS=1; this script re-reads the record and re-exports it verbatim, overriding
# nothing ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 993: zero the stolen high-vector L2 table (arm_vm_init) + probe slot 0xf4 (pmap) =="
echo "-- 992's arm was armed-d13-fce6e8dd (regressed: kernel data abort at 0xc1233b14); no switch changes --"

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
# The record (992's) carries all three of these keys; the pins keep the layers from ever disagreeing with
# each other or with the ENV exports below ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all four idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly); 991's is the memq_hint guard in the POOL alone
#    (`osfmk/vm/vm_resident.c`); 992+993's is the commpage alias in the POOL alone (`osfmk/arm/pmap.c` +
#    `osfmk/arm/commpage/commpage.c` + `osfmk/arm/arm_vm_init.c`, plus the alias block's marker line
#    staged as printf). A fresh checkout needs ALL FOUR stage scripts.
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) --"
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

# 992/993: same rule for the commpage alias (`osfmk/arm/pmap.c` + `osfmk/arm/commpage/commpage.c` +
# `osfmk/arm/arm_vm_init.c`, all pool files).
export STAGE90_XNU_COMMPAGE_ALIAS=1

echo "-- arm asm layer (990: machine_routines_asm.o gets the astloop realignment; unchanged in 991-993) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS (pool DEFINES + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (pmap.c + commpage.c + arm_vm_init.c pick the alias/zeroing up, vm_resident.c the hint guard, pcb.c the alignment) --"
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

echo "-- entry image (992's switch set re-exported verbatim; build_entry only LINKS the pool) --"
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
# The record (992's) carries COMMPAGE_ALIAS; the pins still name every tree-shaping switch explicitly so
# build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the build layers compiled with
# ([[mi4-one-value-two-definitions]]).
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
echo "   990 was 0d531453, 991 was 13355bf0, 992 was fce6e8dd - a DIFFERENT value here means the fix moved the image."
echo "-- by value: the checkers (PASS only if the linked image carries each fix) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the commpage-alias checker (its checks 6-7 now REFUSE a 992-shaped image: the vecl2 report + bzero in arm_vm_init, the slot probe loaded before the last pmap_enter) --"
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the POOL object FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
# NOTE (ruled by 991's own first run, honestly): under `set -o pipefail` each `| head -N` dump below
# returns the PRODUCER's SIGPIPE (141) once head closes early, so `set -e` aborts the script AFTER the
# dump's output has printed. `|| true` consumes only that status; every real check above has already
# run, so the script's exit code is honest.
echo "-- by value: the 993 arm_vm_init POOL object (the vecl2 report strings + the bzero call site) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_arm_vm_init.o 2>/dev/null | grep -E "IOS7LAB|bzero" | head -20 || true
echo "-- by value: the 992/993 pmap POOL object (the alias + both slot reports) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_pmap.o 2>/dev/null | grep -E "IOS7LAB|L2 slot|65535|16384" | head -24 || true
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