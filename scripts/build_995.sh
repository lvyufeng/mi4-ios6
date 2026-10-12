#!/usr/bin/env bash
# Build 995: 994's switch set VERBATIM + STAGE90_XNU_DYLD_WITNESS=1 - the dyld halt-message witness
# (`osfmk/arm/trap.c`: `ios7wit_dyld_message` + its call in `sleh_undef`'s `udf #0xdefe` path).
#
# WHY (994's press, 2026-10-12, arm armed-d13-8b4c9a3e, commit a4a2532, evidence
# out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt): 994 CONFIRMED the trap-10 wire
# fix - `IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000` (log:4101);
# `flags=1` is `VM_FLAGS_ANYWHERE`, where the pre-994 read produced 0 - and moved the frontier one
# step further: dyld still halts, but now at its REAL error.
#     :4103 onward, 235x: `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2beb90c4`, slide
#     0xB8000, i.e. pc 0x2be010c4 = `_dyld_fatal_error` = `fe de ff e7` = `udf #0xdefe`.
# The register block's r5 = 0x2bedc390 = a HEAP STRING in dyld's malloc pool - a message dyld
# formatted (`throwf` -> `__simple_salloc` succeeds -> `vsprintf` -> `strdup` -> `_main`'s
# `catch (const char*)` fatal pad -> `halt(r5)`), and NOTHING on CANCRO can print it: the tree's own
# reader (the LEO witness, `trap.c:71-126`) is compiled out (`#if defined(BOARD_CONFIG_QSD8250_LEO)`).
#
# THE FIX (`tools/stage_d13_dyld_witness.sh`, the rung's driver - it calls the udf-retry lane's
# `tools/patch_d13_dyld_witness.py` and edits nothing itself): `osfmk/arm/trap.c` (ONE file, a POOL
# object) grows `ios7wit_dyld_message`, called in `sleh_undef` immediately after the
# `dyld_all_image_info_addr` printf - the SAME path that prints A, inside the `udf #0xdefe` branch
# (`trap.c:877` returns early for every other undefined instruction). The reader takes the message
# pointer from the word at `task->all_image_info_addr + 0x20` - the field `setAlImageInfosHalt`
# (dyld 0x2be08d04 = `str r0, [r3, #32]`) writes, `dyld::halt` `strlcpy`s `_error_string` into it -
# with `copyin` + `copyinstr`, printing `IOS7WITNESS dyld msg=... copyinstr=... len=...` then
# `dyld> <text>` in 48-byte lines, two print slots at most (claimed BEFORE any I/O: the halt
# re-executes forever and the console block drops silently past 128 KiB). Fail-soft, absolutely:
# every user read is a copyin, no branch panics, allocates, sleeps or locks. 995 does NOT touch the
# assembly layer (`traps_lo.s` is 994's, unchanged).
#
# PRE-REGISTERED READING for the press: `IOS7WITNESS dyld msg=... copyinstr=0 len=...` then the
# `dyld> <text>` lines in the first two `udf #0xdefe` dumps, and the text must be a REAL dyld error
# (the candidates by value are `recursiveLoadLibraries`'s `Library not loaded: %s`, dyld 0x2be0a35e,
# or a `mach_error_string`, 0x2be09220, produced from the IOS7LAB results).
# FALSIFICATION: no `IOS7WITNESS` header at all => the witness was never called - the switch did not
# reach `osfmk_arm_trap.o` (check with `tools/check_d13_dyld_witness.py`), or the trap is not on the
# path this reader hangs off; a header with `copyinstr=<nonzero>` => `A + 0x20` is not the message
# pointer in this dyld (fall back to reading the pool string at r5, or to a frame walk of the halted
# thread).
#
# The switch set is 994's, VERBATIM, PLUS STAGE90_XNU_DYLD_WITNESS=1 (window 0x1e400000 = 484 MiB,
# RESIDENT=1, USB ladder, MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0,
# KEXTB_LOGIN=1, SP_ALIGN=1, VMHINT_GUARD=1, COMMPAGE_ALIAS=1, IOS7_TRAPS=1). The record (994's own
# emission) carries every one of those keys; this script re-reads that record and re-exports it
# verbatim, overriding nothing except adding the new key ([[mi4-build-variant-comes-from-an-env-default]]
# - never use a default).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 995: read dyld's halt message out of the halting task (pool: osfmk/arm/trap.c) =="
echo "-- 994's arm was armed-d13-8b4c9a3e (CONFIRMED: the trap-10 wire fix HELD; the frontier moved to dyld's real, unreadable error); +STAGE90_XNU_DYLD_WITNESS=1 --"

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS \
         STAGE90_XNU_IOS7_TRAPS; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# 994's record carries every key above (including IOS7_TRAPS); 995's record (which this build will
# write) additionally carries DYLD_WITNESS. The pins keep the layers from ever disagreeing with each
# other or with the ENV exports below ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
# The new key: 994's record predates it, so it is ADDED here, not read back (995's own record carries it
# from the next entry build on).
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all six idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly); 991's is the memq_hint guard in the POOL alone
#    (`osfmk/vm/vm_resident.c`); 992+993's is the commpage alias + vector-table zeroing in the POOL alone
#    (`osfmk/arm/pmap.c` + `osfmk/arm/commpage/commpage.c` + `osfmk/arm/arm_vm_init.c`); 994's is the
#    iOS 7 VM wire-width fix in the POOL (`osfmk/ipc/mach_kernelrpc.c` + `osfmk/arm/ios7_compat.h`)
#    and the -3 timebase in the ASSEMBLY layer (`osfmk/arm/traps_lo.s`); 995's is the dyld
#    halt-message witness in the POOL alone (`osfmk/arm/trap.c`). A fresh checkout needs ALL SIX stage
#    scripts.
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) + the iOS 7 VM wire + -3 SWI (994) + the dyld halt-message witness (995) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh

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

# 994: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_IOS7_TRAPS=$IOS7_TRAPS into traps_lo.o, and the
# POOL reads the same variable (`build_xnu_arm_kernel.sh` gates its DEFINE on it). MUST be exported
# BEFORE assemble_arm_layer.sh runs - and before build_xnu_arm_kernel.sh - so the two spellings carry one
# value ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_IOS7_TRAPS=1

# 995: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable) - `trap.c` rides the pool, so there is no ASFLAGS half this time. Exported
# BEFORE build_xnu_arm_kernel.sh runs below.
export STAGE90_XNU_DYLD_WITNESS=1

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 990's astloop realignment is unchanged) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS (pool DEFINES + asm ASFLAGS + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (trap.c carries the witness + its call in sleh_undef's udf path, mach_kernelrpc.c routes traps 10/12/14 through the wire adapters, ios7_compat.h carries the ios7_traps_enabled selector, pmap.c + commpage.c + arm_vm_init.c the alias/zeroing, vm_resident.c the hint guard, pcb.c the alignment) --"
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

echo "-- entry image (994's switch set re-exported verbatim + DYLD_WITNESS; build_entry only LINKS the pool) --"
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
# The record (994's) carries every tree-shaping switch EXCEPT the new DYLD_WITNESS key; the pins name
# them explicitly (including the new key) so build_entry.sh's ENTRY_ARM_KEYS read-back records the
# SAME values the build layers compiled with ([[mi4-one-value-two-definitions]]).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_XNU_COMMPAGE_ALIAS=1")
EXPORTS+=("STAGE90_XNU_IOS7_TRAPS=1")
EXPORTS+=("STAGE90_XNU_DYLD_WITNESS=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   993 was 1d5cf20e, 994 was 8b4c9a3e - a DIFFERENT value here means the witness moved the image."
echo "-- by value: the checkers (PASS only if the linked image carries each fix) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the commpage-alias checker (993's vecl2 report + bzero in arm_vm_init, the slot probe loaded before the last pmap_enter) --"
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
echo "-- by value: the 994 trap-wire checker (the cmn r12,#3 / beq swi_trap_tb fast path is GONE; a cmn r11,#3 branches to the mach_absolute_time call; that call is followed by b swi_exit64) --"
python3 tools/check_d13_ios7_traps.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the 994 vmwire checker - traps 10/12/14 still {5,5,7} in mach_trap_table and pointing at the real fn; each trap routes through its adapter; the adapter reads the wire's EXTRA word (#16 / #16 / #24, offsets the native struct cannot have); the three IOS7LAB vm_*64 markers --"
python3 tools/check_d13_ios7_vmwire.py out/stage90/xnu_arm_entry.elf
# Rehearsal ONLY, never a criterion here: a fresh build is ON, and the two modes assert DISJOINT
# offsets, so --off is EXPECTED to fail on this image. Run it against a pre-994/993 elf to prove the
# checker tells the two shapes apart. Guarded with `set +e` so a rehearsal can never abort the script
# after the real checks have passed.
echo "-- rehearsal only: the vmwire checker's OFF mode against this ON image (nonzero expected) --"
set +e
python3 tools/check_d13_ios7_vmwire.py out/stage90/xnu_arm_entry.elf --off
echo "   (--off exit $? - rehearsal, not a criterion; the ON run above is)"
set -e
echo "-- by value: the 995 dyld-witness checker (ios7wit_dyld_message exists; sleh_undef's region calls it after a bl printf; the witness's region calls copyin/copyinstr/printf and names the #32 field offset; the IOS7WITNESS / dyld> %s / dyld> <copyinstr failed> literals) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the same checker against the FROZEN 994 image as a NEGATIVE CONTROL (it must FAIL ON and PASS OFF - the checker tells the two shapes apart) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the POOL object FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
# NOTE (ruled by 991's own first run, honestly): under `set -o pipefail` each `| head -N` dump below
# returns the PRODUCER's SIGPIPE (141) once head closes early, so `set -e` aborts the script AFTER the
# dump's output has printed. `|| true` consumes only that status; every real check above has already
# run, so the script's exit code is honest.
echo "-- by value: the 995 POOL object (trap.o must carry the witness: ios7wit_dyld_message + the IOS7WITNESS/dyld> literals + the call to copyin/copyinstr) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_trap.o 2>/dev/null | grep -E "ios7wit_dyld_message|IOS7WITNESS|copyin|copyinstr" | head -20 || true
echo "-- by value: the 994 POOL object (mach_kernelrpc.o must carry the wire adapters: the IOS7LAB markers + ios7_traps_enabled) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o 2>/dev/null | grep -E "IOS7LAB|vm_allocate64|vm_deallocate64|vm_protect64|ios7_traps_enabled" | head -20 || true
# (994's ruling: osfmk_kern_syscall_sw.o is NOT part of the trap-wire fix - traps 10/12/14 already
# carry arg counts 5/5/7 and on __arm__ the mungers are ignored, so the syscall_sw.o nm dump an
# earlier draft of the 994 rung printed would be silent by design and was deliberately dropped.)
echo "-- by value: the ASSEMBLY object (traps_lo.o must carry the mach_absolute_time call, NOT the cmn r12,#3 fast path) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -B2 -A2 -E "mach_absolute_time|swi_exit64" | head -20 || true
echo "-- by value: the 993 POOL objects, still carrying the alias + zeroing --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_arm_vm_init.o 2>/dev/null | grep -E "IOS7LAB|bzero" | head -20 || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_pmap.o 2>/dev/null | grep -E "IOS7LAB|L2 slot" | head -12 || true
echo "-- by value: the 991/990 POOL objects, still carrying their fixes --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_resident.o 2>/dev/null | grep -E "VMHINT991|kprintf|tst" | head -20 || true
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/machine_routines_asm.o 2>/dev/null | grep -B3 -A3 "ast_taken" | head -20 || true