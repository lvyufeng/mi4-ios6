#!/usr/bin/env bash
# Build 997: 996's switch set VERBATIM (STAGE90_XNU_DYLD_WITNESS stays 1, STAGE90_XNU_SHREG_PROBE
# stays 1) + STAGE90_XNU_SHREG_WIRE=1. ONE pool file moves in this rung:
#   * `bsd/vm/vm_unix.c` - G3, the 32-byte shared-region wire (`tools/patch_d13_shreg_wire.py`):
#     three `#if IOS7_COMPAT_PLATFORM` lines gain the 994 spelling and the file-local
#     `ios7lab_cache_abi()` body is gated to `return 1;`, so the tree's OWN 32 B branch
#     (`shared_region_copyin_mappings` + its per-mapping `IOS7LAB cache mapping ...` printf,
#     vm_unix.c:1190-1220) compiles in. The MACRO itself is NOT touched (the 994 "do not widen the
#     macro" lesson: 13 files reference it).
#
# WHY (996's press, 2026-10-12, arm armed-d13-cea0c1c2, commit 821fb40, log
# out/stage90/captures/cancro-last_kmsg-996-armed-d13-cea0c1c2.txt:4101-4105): the probe MEASURED the
# §2 prediction by value - `IOS7SHREG check_np ret=12` (region exists+empty), `map_slide count=4
# slide=0x3811000 slide_start=0x3f9bd000 size=0x89000`, then `IOS7SHREG map_file kr=4` +
# `map_slide ret=22` (EINVAL) = `shared_region_copyin_mappings` computed `count * 5 << 2` against
# dyld 239's 32-byte-stride array (0x2be057ba `add.w r0, r4, r3, lsl #5`), m[0].size read 0 and
# `vm_map_enter_mem_object`'s `initial_size == 0` check (vm_map.c:2344) returned EINVAL before any
# slide work. Same press: the witness read the FULL 102-char halt message and `cacheBase=0` - the
# shared cache was never mapped BECAUSE 438 died, so this rung opens the wire the cache needs.
#
# THE SWITCH SET is the CURRENT record's 50 lines VERBATIM + STAGE90_XNU_SHREG_WIRE=1 (the record this
# script will write carries it from the next entry build on). The ENTRY BIN MUST MOVE (cea0c1c2 -> the
# new sha8) - the entry layer re-records the arm keys and relinks the pool objects.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 997: open the tree's 32 B shared-region wire (G3) - pool: bsd/vm/vm_unix.c =="
echo "-- 996's arm was armed-d13-cea0c1c2 (CONFIRMED: map_file kr=4 + map_slide ret=22 = the 20 B-vs-32 B misparse, by value; cacheBase=0; the full 102-char message read); +STAGE90_XNU_SHREG_WIRE=1, DYLD_WITNESS and SHREG_PROBE stay 1 --"

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS \
         STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# The record carries every key above now (996's own record carries SHREG_PROBE; 995's carried
# DYLD_WITNESS; 994's carried the rest). The pins keep the layers from ever disagreeing with each other
# or with the ENV exports below ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_PROBE=1"
# The new key: the record predates it, so it is ADDED here, not read back (997's own record carries it
# from the next entry build on).
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_WIRE=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all EIGHT idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly); 991's is the memq_hint guard in the POOL alone
#    (`osfmk/vm/vm_resident.c`); 992+993's is the commpage alias + vector-table zeroing in the POOL alone
#    (`osfmk/arm/pmap.c` + `osfmk/arm/commpage/commpage.c` + `osfmk/arm/arm_vm_init.c`); 994's is the
#    iOS 7 VM wire-width fix in the POOL (`osfmk/ipc/mach_kernelrpc.c` + `osfmk/arm/ios7_compat.h`)
#    and the -3 timebase in the ASSEMBLY layer (`osfmk/arm/traps_lo.s`); 995/996's is the dyld
#    halt-message witness in the POOL alone (`osfmk/arm/trap.c`); 996's is the 294/438 shared-region
#    probe in the POOL alone (`bsd/vm/vm_unix.c`); 997's is the 32 B shared-region wire in the POOL
#    alone (`bsd/vm/vm_unix.c`, the SAME file the probe rides). A fresh checkout needs ALL EIGHT stage
#    scripts.
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) + the iOS 7 VM wire + -3 SWI (994) + the dyld witness rev-3 (995/996) + the 294/438 shared-region probe (996) + the 32 B shared-region wire (997) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh
tools/stage_d13_shreg_probe.sh
tools/stage_d13_shreg_wire.sh

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
# environment variable) - `trap.c` rides the pool, so there is no ASFLAGS half. 997 keeps it at 1.
export STAGE90_XNU_DYLD_WITNESS=1

# 996: same rule for the shared-region probe (`bsd/vm/vm_unix.c`, a pool file). 997 keeps it at 1: the
# probe's IOS7SHREG lines stay readable next to the wire's own IOS7LAB cache mapping lines.
export STAGE90_XNU_SHREG_PROBE=1

# 997: same rule for the 32 B shared-region wire (`bsd/vm/vm_unix.c`, the SAME pool file). Exported
# BEFORE build_xnu_arm_kernel.sh runs below so the ENV spelling and the `-D` in EXTRA carry one value.
export STAGE90_XNU_SHREG_WIRE=1

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 990's astloop realignment is unchanged) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE (pool DEFINES + asm ASFLAGS + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (vm_unix.c carries the 32 B wire (997) + the 294/438 probe (996); trap.c the witness rev-3 + its call in sleh_undef's udf path; mach_kernelrpc.c routes traps 10/12/14 through the wire adapters; ios7_compat.h carries the ios7_traps_enabled selector; pmap.c + commpage.c + arm_vm_init.c the alias/zeroing; vm_resident.c the hint guard; pcb.c the alignment) --"
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

echo "-- entry image (996's switch set re-exported verbatim + SHREG_WIRE; build_entry only LINKS the pool) --"
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
# The record (996's) carries every tree-shaping switch EXCEPT the new SHREG_WIRE key; the pins name
# them explicitly (including the new key) so build_entry.sh's ENTRY_ARM_KEYS read-back records the
# SAME values the build layers compiled with ([[mi4-one-value-two-definitions]]).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_XNU_COMMPAGE_ALIAS=1")
EXPORTS+=("STAGE90_XNU_IOS7_TRAPS=1")
EXPORTS+=("STAGE90_XNU_DYLD_WITNESS=1")
EXPORTS+=("STAGE90_XNU_SHREG_PROBE=1")
EXPORTS+=("STAGE90_XNU_SHREG_WIRE=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   996 was cea0c1c2, 995 was 4efcabae - a DIFFERENT value here means the 32 B wire moved the image."
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
echo "-- by value: the 996 dyld-witness CHECKER (rev-3: copyin >=9 / copyinstr >=6, the #32/#48/#100/#80 immediates, the SAME 13 literals as rev-2 PLUS the six IOS7WIT_AI_* ones) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the same witness checker against the FROZEN 994 image as a NEGATIVE CONTROL (it must FAIL ON and PASS OFF - the checker tells the two shapes apart) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 996 SHREG checker ON the 997 image - its frozen 20 B clause ('996 measures the misparse, it does not fix it') is the ONE by-design FAIL, and NOTHING else may fail: the five IOS7SHREG literals in the image, the 294/438 sysent rows still name the real bodies, no IOS7SHREG reference outside the three probed functions, and the SOURCE-side gate check on the staged tree file --tree bsd/vm/vm_unix.c (7 guard regions; NOTE: --tree takes the FILE, not the tree root - measured) --"
# 997's whole point is the 20 B -> 32 B straddle moving, and the frozen 996 checker asserts the 20 B
# shape in BOTH modes, so on THIS image exactly that one clause must fail and nothing else. The same
# judgement is the link-time 996 clause in src/entry/build_entry.sh; the 32 B inversion is the wire
# checker's ON run below.
sp_rc=0
sp_out=$(python3 tools/check_d13_shreg_probe.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c 2>&1) || sp_rc=$?
printf '%s\n' "$sp_out"
sp_fails=$(printf '%s\n' "$sp_out" | grep -c '^  FAIL ' || true)
if [[ $sp_rc -eq 0 ]] || [[ $sp_fails -ne 1 ]] || ! printf '%s\n' "$sp_out" | grep -q '^  FAIL shared_region_copyin_mappings does not compute'; then
  echo "FAIL: the 996 checker did not fail on exactly its by-design 20 B clause (rc=$sp_rc, $sp_fails clause FAIL line(s)) - rc 0 means the 32 B wire did not reach the image; any other failing clause means the probe is damaged or the tree is not staged"
  exit 1
fi
echo "   (by design: the ONLY failing clause is the frozen 20 B straddle one)"
echo "-- by value: the SAME frozen checker against its OWN 996 image, with the FROZEN 994 image as a NEGATIVE CONTROL (on must FAIL, off must PASS) - the checker must still PASS its home image: 997 does not weaken it --"
python3 tools/check_d13_shreg_probe.py out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 997 shreg-WIRE CHECKER (switch ON: the IOS7LAB cache mapping literal in shared_region_copyin_mappings reaching a printf; count*32 (\`lsl #5\` + \`add rX,rX,#32\`); the 20 B \`count*5<<2\` pair ABSENT; 294's copyout r2=8 for the full uint64; no stray literal) + the SOURCE-side gate check --tree (3 gated #if lines + 1 body gate; the mapping printf INSIDE a gate region; NOTE: --tree takes the FILE, not the tree root - measured) --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c
echo "-- by value: the same wire checker against the FROZEN 996 image as a NEGATIVE CONTROL (it must FAIL ON and PASS OFF - the 996 elf carries the probe but the wire is still 20 B) and the 994 capture as a second --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --control out/stage90/captures/994-xnu_arm_entry.elf
# Rehearsal ONLY, same rule as the vmwire one above: a fresh build is ON, so the wire checker's OFF
# mode is EXPECTED to fail on this image (the 996 pre-image's shape is gone). The control runs above
# are the real discrimination proof.
echo "-- rehearsal only: the wire checker's OFF mode against this ON image (nonzero expected) --"
set +e
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect off
echo "   (--off exit $? - rehearsal, not a criterion; the ON run above is)"
set -e
echo "-- by value: the POOL objects FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
# NOTE (ruled by 991's own first run, honestly): under `set -o pipefail` each `| head -N` dump below
# returns the PRODUCER's SIGPIPE (141) once head closes early, so `set -e` aborts the script AFTER the
# dump's output has printed. `|| true` consumes only that status; every real check above has already
# run, so the script's exit code is honest.
echo "-- by value: the 997 WIRE object (bsd_vm_vm_unix.o must carry the IOS7LAB cache mapping literal + the count*32 shape and STILL carry the 996 IOS7SHREG probe) --"
# `objdump -dr` shows text/relocations only - the literals live in .rodata, so read them with
# `strings` (the CHECKED property is the checker's in-image clauses above; this dump is evidence).
strings -a out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "IOS7LAB cache mapping|IOS7SHREG" | head -10 || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "lsl.*#5|#32" | head -8 || true
echo "-- by value: the 995 PROBE object (trap.o must carry the witness REV-3: ios7wit_dyld_message + the IOS7WITNESS/dyld>/IOS7WIT_AI_ literals + the calls to copyin/copyinstr) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_trap.o 2>/dev/null | grep -E "ios7wit_dyld_message|IOS7WITNESS|IOS7WIT_AI_|copyin|copyinstr" | head -20 || true
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