#!/usr/bin/env bash
# Build 998: 997's switch set VERBATIM (STAGE90_XNU_DYLD_WITNESS / STAGE90_XNU_SHREG_PROBE /
# STAGE90_XNU_SHREG_WIRE all stay 1) + STAGE90_XNU_SHREG_GEOM=1. THREE pool files move in this rung:
#   * `osfmk/vm/vm_shared_region.c` - G1, the tree's OWN iOS 7 shared-region geometry
#     (`tools/patch_d13_shreg_geom.py`): the `#if IOS7_COMPAT_PLATFORM` line at the geometry block
#     (:626) gains the 994 spelling and its runtime selector (:629) becomes `ios7_shreg_geom_enabled()`,
#     so `base_address = 0x2C000000U;` / `size = 0x14000000U;` and the `IOS7LAB shared region ...`
#     printf compile in (all inside the gate).
#   * `osfmk/arm/status.c` - the STACK HALF, a hard requirement of the SAME rung: the same gate spelling
#     at :62 + the same helper at :66, so `LAB_USER_STACK` = `ios7lab_user_stack()` -> 0x2BE00000
#     instead of USRSTACK 0x2FE00000. Without it the new window covers the stack top and every
#     process's FIXED stack allocation relays out (audit section 2).
#   * `osfmk/arm/ios7_compat.h` - `ios7_shreg_geom_enabled()` beside `ios7_traps_enabled()`.
# The MACRO itself is NOT touched (the 994 "do not widen the macro" lesson).
#
# WHY (997's press, 2026-10-12, arm armed-d13-518b6a94, commit 9e6307a, log
# out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt:4096-4110): the 32 B wire's mappings
# now arrive and the failure moved to the WINDOW - four `IOS7LAB cache mapping` lines print with
# ALREADY SLID addresses (m0 va=0x2cd9c000 = 0x2C000000 + slide 0xd9c000; m1 carries VM_PROT_SLIDE,
# init=0x23), then `map_file kr=1` (KERN_INVALID_ADDRESS) + `map_slide ret=14`: the default
# 0x30000000/0x10000000 window cannot hold dyld's slid shared cache. This rung opens the geometry the
# tree already carries, in ONE switch with its stack half.
#
# THE SWITCH SET is the CURRENT record's lines VERBATIM + STAGE90_XNU_SHREG_GEOM=1 (the record this
# script will write carries it from the next entry build on). The ENTRY BIN MUST MOVE (518b6a94 -> the
# new sha8) - the entry layer re-records the arm keys and relinks the pool objects.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 998: open the tree's iOS 7 shared-region geometry + stack top (G1) - pool: osfmk/vm/vm_shared_region.c + osfmk/arm/status.c (+ header helper) =="
echo "-- 997's arm was armed-d13-518b6a94 (CONFIRMED: the four IOS7LAB cache mapping lines print with already-slid addresses; map_file kr=1 + map_slide ret=14 = the default window cannot hold dyld's slid cache); +STAGE90_XNU_SHREG_GEOM=1, DYLD_WITNESS and SHREG_PROBE and SHREG_WIRE stay 1 --"

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
# The record carries every key above now (997's own record carries SHREG_WIRE; 996's carried
# SHREG_PROBE; 995's carried DYLD_WITNESS; 994's carried the rest). The pins keep the layers from ever
# disagreeing with each other or with the ENV exports below ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_PROBE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_WIRE=1"
# The new key: the record predates it, so it is ADDED here, not read back (998's own record carries it
# from the next entry build on).
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_GEOM=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all NINE idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`); 990's is the SP alignment in BOTH layers (`osfmk/arm/pcb.c` pool +
#    `osfmk/arm/machine_routines_asm.s` assembly); 991's is the memq_hint guard in the POOL alone
#    (`osfmk/vm/vm_resident.c`); 992+993's is the commpage alias + vector-table zeroing in the POOL alone
#    (`osfmk/arm/pmap.c` + `osfmk/arm/commpage/commpage.c` + `osfmk/arm/arm_vm_init.c`); 994's is the
#    iOS 7 VM wire-width fix in the POOL (`osfmk/ipc/mach_kernelrpc.c` + `osfmk/arm/ios7_compat.h`)
#    and the -3 timebase in the ASSEMBLY layer (`osfmk/arm/traps_lo.s`); 995/996's is the dyld
#    halt-message witness in the POOL alone (`osfmk/arm/trap.c`); 996's is the 294/438 shared-region
#    probe in the POOL alone (`bsd/vm/vm_unix.c`); 997's is the 32 B shared-region wire in the POOL
#    alone (`bsd/vm/vm_unix.c`, the SAME file the probe rides); 998's is the iOS 7 shared-region
#    geometry + stack top in the POOL alone (`osfmk/vm/vm_shared_region.c` + `osfmk/arm/status.c`,
#    with the helper in `osfmk/arm/ios7_compat.h`). A fresh checkout needs ALL NINE stage scripts.
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) + the iOS 7 VM wire + -3 SWI (994) + the dyld witness rev-3 (995/996) + the 294/438 shared-region probe (996) + the 32 B shared-region wire (997) + the shared-region geometry + stack top (998) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh
tools/stage_d13_shreg_probe.sh
tools/stage_d13_shreg_wire.sh
tools/stage_d13_shreg_geom.sh

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
# environment variable) - `trap.c` rides the pool, so there is no ASFLAGS half. 998 keeps it at 1.
export STAGE90_XNU_DYLD_WITNESS=1

# 996: same rule for the shared-region probe (`bsd/vm/vm_unix.c`, a pool file). 998 keeps it at 1: the
# probe's IOS7SHREG lines stay readable next to the wire's own IOS7LAB cache mapping lines.
export STAGE90_XNU_SHREG_PROBE=1

# 997: same rule for the 32 B shared-region wire (`bsd/vm/vm_unix.c`, the SAME pool file). 998 keeps it
# at 1 - the geometry rung sits ON TOP of the open wire, not instead of it.
export STAGE90_XNU_SHREG_WIRE=1

# 998: same rule for the shared-region geometry + stack top (`osfmk/vm/vm_shared_region.c` +
# `osfmk/arm/status.c`, both pool files). Exported BEFORE build_xnu_arm_kernel.sh runs below so the ENV
# spelling and the `-D` in EXTRA carry one value.
export STAGE90_XNU_SHREG_GEOM=1

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 990's astloop realignment is unchanged; 998 touches no assembly) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name: two spellings of one value would be exactly
# the [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM (pool DEFINES + asm ASFLAGS + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (vm_shared_region.c carries the iOS 7 geometry (998); status.c the 0x2BE00000 stack top (998); vm_unix.c the 32 B wire (997) + the 294/438 probe (996); trap.c the witness rev-3 + its call in sleh_undef's udf path; mach_kernelrpc.c routes traps 10/12/14 through the wire adapters; ios7_compat.h carries the ios7_traps_enabled selector + ios7_shreg_geom_enabled; pmap.c + commpage.c + arm_vm_init.c the alias/zeroing; vm_resident.c the hint guard; pcb.c the alignment) --"
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

echo "-- entry image (997's switch set re-exported verbatim + SHREG_GEOM; build_entry only LINKS the pool) --"
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
# The record (997's) carries every tree-shaping switch EXCEPT the new SHREG_GEOM key; the pins name
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
EXPORTS+=("STAGE90_XNU_SHREG_GEOM=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   997 was 518b6a94, 996 was cea0c1c2 - a DIFFERENT value here means the geometry/stack moved the image."
echo "-- the record's three SHREG lines (one value, two definitions) --"
grep -E '^STAGE90_XNU_SHREG_(PROBE|WIRE|GEOM)=' out/stage90/xnu_arm_entry-config.txt
grep -q '^STAGE90_XNU_SHREG_GEOM=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "FAIL: the fresh record carries no STAGE90_XNU_SHREG_GEOM=1 - the arm this image IS would not be readable by the gate"; exit 1; }
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
echo "-- by value: the 996 SHREG checker ON the 998 image - its frozen 20 B clause ('996 measures the misparse, it does not fix it') is the ONE by-design FAIL, and NOTHING else may fail: the five IOS7SHREG literals in the image, the 294/438 sysent rows still name the real bodies, no IOS7SHREG reference outside the three probed functions, and the SOURCE-side gate check on the staged tree file --tree bsd/vm/vm_unix.c (7 guard regions; NOTE: --tree takes the FILE, not the tree root - measured) --"
# 997's whole point was the 20 B -> 32 B straddle moving, and the frozen 996 checker asserts the 20 B
# shape in BOTH modes, so on this image exactly that one clause must fail and nothing else. The same
# judgement is the link-time 996 clause in src/entry/build_entry.sh; the 32 B inversion is the wire
# checker's ON run below.
sp_rc=0
sp_out=$(python3 tools/check_d13_shreg_probe.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c 2>&1) || sp_rc=$?
printf '%s\n' "$sp_out"
sp_fails=$(printf '%s\n' "$sp_out" | grep -c '^  FAIL ' || true)
if [[ $sp_rc -eq 0 ]] || [[ $sp_fails -ne 1 ]] || ! grep -q '^  FAIL shared_region_copyin_mappings does not compute' <<<"$sp_out"; then
  echo "FAIL: the 996 checker did not fail on exactly its by-design 20 B clause (rc=$sp_rc, $sp_fails clause FAIL line(s)) - rc 0 means the 32 B wire did not reach the image; any other failing clause means the probe is damaged or the tree is not staged"
  exit 1
fi
echo "   (by design: the ONLY failing clause is the frozen 20 B straddle one)"
echo "-- by value: the SAME frozen checker against its OWN 996 image, with the FROZEN 994 image as a NEGATIVE CONTROL (on must FAIL, off must PASS) - the checker must still PASS its home image: 997/998 do not weaken it --"
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
echo "-- by value: the 998 shreg-GEOM CHECKER, SOURCE side (the vmabi-recon lane's; NOTE: --tree takes the TREE ROOT here, unlike the wire/probe checkers' FILE argument - measured): the three files all in the staged shape (0 bare \`#if IOS7_COMPAT_PLATFORM\` in both .c files, 1 gated each, the helper called 2x / defined 1x), the ABI constants intact in BOTH states (base_address = 0x2C000000U;, size = 0x14000000U;, the IOS7LAB printf, status.c's 0x2BE00000U + USRSTACK 0x2FE00000), no v1 inline-#if wrap --"
python3 tools/check_d13_shreg_geom.py --tree external/xnu-hd2-darwin13/xnu --expect staged
# Rehearsal ONLY: the tree IS staged, so `--expect pre` is EXPECTED to be refused (rc 1). The scratch
# rehearsal already proved the checker's pre/staged/mixed tri-state; this is the on-tree reminder.
echo "-- rehearsal only: the geom checker's WRONG-expect run against this staged tree (rc 1 expected) --"
set +e
python3 tools/check_d13_shreg_geom.py --tree external/xnu-hd2-darwin13/xnu --expect pre
echo "   (--expect pre exit $? - rehearsal, not a criterion; the staged run above is)"
set -e
echo "-- by value: the 998 IMAGE-side clauses, standalone (the same three properties build_entry.sh's 998 clause asserts at the link site): (b) the IOS7LAB shared region printf literal is in the image (it sits INSIDE the opened gate), (c) thread_userstack + thread_userstackdefault materialize the 0xBE0 stack top and NOT the 0xFE0 default --"
gl_lit='IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000'
gl_n=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   (b) IOS7LAB shared region literal copies: $gl_n (want 1)"
[[ $gl_n -eq 1 ]] || { echo "FAIL: the image-side literal reader found $gl_n copies of the geometry printf literal (want exactly 1)"; exit 1; }
for gfn in thread_userstack thread_userstackdefault; do
  gfa=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$gfn" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $gfn not in the image"; exit 1; }
  # NO early `exit` in this awk: the reader that stops first sends SIGPIPE to `nm`, and under
  # `set -o pipefail` the pipeline then returns 141 - which `set -e` turns into a SILENT death (measured:
  # the first full 998 build died exactly here, 12 lines of output lost). Read the whole stream and keep
  # the FIRST match instead - same answer, deterministic status. (strtonum is the shape build_entry.sh's
  # own `sym_next` uses, so it is known to work in this shell's awk.)
  gfnx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${gfa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  echo "   (c) $gfn ($gfa..${gfnx:-end}):"
  arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$gfa" --stop-address="${gfnx:-0xffffffff}" | grep -E 'movt.*; 0x2be0|movt.*; 0x2fe0' | sed 's/^/       /'
done
echo "-- by value: the SAME image-side reader against the FROZEN 997 elf as a NEGATIVE CONTROL (the literal count there must be 0 and BOTH functions must still show the 0x2FE00000 default - the reader tells the two geometries apart) --"
gc_997=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-518b6a94/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   frozen 997 elf: IOS7LAB shared region literal copies: $gc_997 (want 0)"
[[ $gc_997 -eq 0 ]] || { echo "FAIL: the frozen 997 elf carries the 998 literal - the reader does not discriminate"; exit 1; }
for gfn in thread_userstack thread_userstackdefault; do
  gfa=$(arm-none-eabi-nm out/stage90/frozen/armed-d13-518b6a94/xnu_arm_entry.elf | awk -v s="$gfn" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $gfn not in the frozen 997 elf"; exit 1; }
  gfnx=$(arm-none-eabi-nm -n out/stage90/frozen/armed-d13-518b6a94/xnu_arm_entry.elf | awk -v s="${gfa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  arm-none-eabi-objdump -d out/stage90/frozen/armed-d13-518b6a94/xnu_arm_entry.elf --start-address="$gfa" --stop-address="${gfnx:-0xffffffff}" | grep -E 'movt.*; 0x2fe0' | sed "s/^/       frozen 997 $gfn: /" | head -4 || true
done
echo "-- by value: the POOL objects FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
echo "-- by value: the 998 GEOMETRY object (osfmk_vm_vm_shared_region.o must carry the IOS7LAB shared region literal + 0x2C000000/0x14000000) --"
strings -a out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -F 'IOS7LAB shared region' || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -E '#0x2c00|#0x1400' | head -6 || true
echo "-- by value: the 998 STACK object (osfmk_arm_status.o must carry the 0x2BE00000 stack top; the selector call is inlined by -O2, so the constant is the marker) --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_status.o 2>/dev/null | grep -B1 -A1 '; 0x2be0' | head -12 || true
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