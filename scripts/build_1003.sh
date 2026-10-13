#!/usr/bin/env bash
# Build 1003: 1002's switch set VERBATIM plus ONE new switch, STAGE90_XNU_STALL - a pure MEASUREMENT
# rung (no kernel behaviour changes) that samples pid 1's user pc on the timer tick. It answers the
# one question the 1002 press read (2026-10-13, arm armed-d13-b04976fe) could NOT answer by value.
#
# 1002's press advanced the boot INTO pid-1 user mode, FIXED the 1001 abort (230x `usermode data abort
# at 0x4` -> 0) and moved the frontier: after `IOS7SHREG check_np ret=12` the boot prints `map_slide`,
# runs 22 user SVCs, then the user side makes NO further syscall while the kernel stays alive ~23 min
# (43,179 WDT pets, tick summaries climbing) and the storage door serves 537 reads. See
# records/1002-press-read.txt and docs/1003-dyld-stall-design-draft.md.
#
# WHY THE INSTRUMENT, AND WHY *THIS* ONE. The capture cannot distinguish (i) pid 1 ASLEEP in the
# kernel (blocked on a page-in or IPC that never completes) from (ii) pid 1 SPINNING in user mode
# (the last three SVCs are num=-3 = `mach_absolute_time`, same pc 0x2be97328, `lr` marching - a
# monotonic-clock poll). An SVC is a syscall, and a spin makes none, so "no more SVCs" fits both. The
# kernel-side suspects are exonerated BY VALUE: `rtclock_bus_configure` ran ([RTCLOCK] 19.2 MHz, so
# `rtclock_sec_divisor != 0` and `mach_absolute_time()` advances), `cs_validate_page`'s SHA1 is
# compile-stubbed to ACCEPT (`ubc_subr.c` `_NOTYET`), the read door completes every op
# (`rd_complete=1`/`rd_err=0`). So the question is a MEASUREMENT, not an inference - and 1003 takes it:
# the board PE's timer IRQ already holds the interrupted frame (`context`), and sampling its pc/cpsr on
# the tick's powers-of-two ladder gives a short autonomous history that separates the two cases.
#
#   * the sampled pcs ADVANCE or REPEAT as a USER pc (cpsr low bits 0x10) => pid 1 is SPINNING in user
#     mode (a user-space wait loop);
#   * every sample is a KERNEL pc (0x8000xxxx) => pid 1 is ASLEEP, and the last syscall is its sleep
#     site.
#
# THE SWITCH: STAGE90_XNU_STALL=1 (the one new switch). It reaches `pe_msm8974.c`'s compile as
# `-DSTAGE90_XNU_STALL` via `XNU_KERNEL_EXTRA_DEFINES` (== `$EXTRA`) - the platform block path - and is
# recorded in the arm. NO tree edit, NO stage driver: the recorder is in `src/entry/entry_stubs.c` and
# the call site in `src/platform/darwin13/pe_msm8974.c`, both gated so STALL=0 is byte-identical.
# THE ENTRY BIN MUST MOVE (b04976fe -> the new sha8) because the call site reaches `pe_msm8974.o`,
# which the entry link carries.
#
# THE SWITCH SET is 1002's VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC and
# SUBMAP_PMAP_SKIP stay 1) plus STAGE90_XNU_STALL=1 (the one new switch).
#
# ALL 1002 INSTRUMENTS STAY (the tick sampler/summaries, the cap-exempt pet/anomaly channels, the
# user-SVC frontier - USVC=1 - the 991 memq_hint guard, the 1001 submap pmap-enter skip, and the
# 1002 USVC-hook r3 save set): the new expectation is that the boot SURVIVES the code-signing
# page-validation wall and advances past `cs_invalid_page` for launchd (or the wall moves again,
# record by value).
# ONE PAYLOAD edit (kept from 999/1000/1001/1002): STAGE90_HW_WATCHDOG_TIMEOUT_S 29u - the loaded
# payload bytes are 999's; 1003 must not slip back to the 25 s net.
#
# 1001's headline: the 1000 death (hw_lock_lock on a FREED vm_map-zone element) was FIXED BY VALUE
# (SUBMAPSKIP x2 == 999's two objects; VMHINT991 inverted to 0) and the boot ADVANCED into pid-1 user
# mode - then faulted 230x, byte-identical, at 0x4: `launchd[1]: usermode data abort, EXC_BAD_ACCESS
# at 0x00000004 ... (Translation fault on page)`, right after `IOS7SHREG check_np ret=12`. BY VALUE
# (slide 0xE7000): the fault was dyld's OWN errno store - `_cerror_nocancel` (vmaddr 0x2be1b8d4 =
# pc 0x2bf028d4) reads TPIDRURO (`mrc p15,0,r1,c13,c0,3`), masks it, and `ldr r1,[r1,#4]` faults
# because the register read 0. The 999 CONTRAST: SAME binary, SAME syscall, SAME ret=12 - and 999
# (which lacked only 1001's changes) prints `IOS7SHREG map_slide` on the NEXT line with ZERO `usermode
# data abort`. The root cause was `traps_lo.s`'s 1000 hook saving the wrong register set; 1002 fixed it.
#
# THE SWITCH SET is the CURRENT record's lines VERBATIM. The ENTRY BIN MUST MOVE (b04976fe -> the new
# sha8) and the layer carrying the change (POOL or ASSEMBLY) must be rebuilt (build_entry.sh only
# LINKS the objects).
#
# 1000's headline: the boot died at XNU time 10.31 s, immediately after `attempting to start init of
# /sbin/launchd` - `panic(cpu 0 caller 0x80498058): hw_lock_lock(): PANIC: Lock 0xc0005b50 =
# 0xffb5be0b`. BY VALUE: `hw_lock_lock` = 0x80498020 (the caller is +0x38), `vm_map_pmap_enter` =
# 0x8003d894, and the "object" it locked = 0xc0005b48 = a FREED vm_map-zone element (w04 =
# 0xbaddecaf = zalloc's ZP_NOT_POISONED; 999's VMHINT991 records named the same two elements, and
# 990 died on the same one at +0x18). The DELTA 999->1000 was DRAM pre-state, NOT code: 999
# survived the same call because that element's +8 word was then benign.
#
# THE ROOT CAUSE, BY VALUE: `vm_map_enter`'s pmap-enter tail (`vm_map.c:~2149`) is conditioned
# `object != VM_OBJECT_NULL && vm_map_pmap_enter_enable && !anywhere && !needs_copy && size < 128K`
# - with NO `is_submap` guard - and on a SUBMAP enter the `object` parameter IS the submap itself
# (`:2063 submap = (vm_map_t) object;`, a vm_map-zone element). The call handed a vm_map to
# `vm_map_pmap_enter`, which locked `object + 8` (a struct vm_object's lock). The SAME function
# guards the SAME parameter 30 lines later (`:2192 if (!is_submap && ...)`) - the missing guard is
# that idiom.
#
# THE FIX (this rung): `STAGE90_XNU_SUBMAP_PMAP_SKIP=1` makes the tail SKIP the pmap-enter work when
# `is_submap`, keeping `pmap_empty = FALSE`; a bounded (max 8) kprintf beginning `SUBMAPSKIP:`
# records obj/start/end/size/lr in the skip branch. Staged by
# `tools/stage_d13_submap_pmap_skip.sh` (defined()-first gate; switch OFF = the tree's text byte for
# byte); `vm_map.c` is a POOL file, so the switch rides BOTH `EXTRA` and the environment export into
# `tools/build_xnu_arm_kernel.sh`'s DEFINES, and the linked-image property is asserted BY VALUE by
# `tools/check_submap_pmap_skip.py` (the SUBMAPSKIP string referenced from vm_map_enter's own body
# or one branch of it, a kprintf call in the same body, and the still-present `vm_map_pmap_enter`
# call for the non-submap path), with the frozen 1000 elf as the NEGATIVE CONTROL (must FAIL
# check 1).
# ALL 1000 INSTRUMENTS STAY (the tick sampler/summaries, the cap-exempt pet/anomaly channels, the
# user-SVC frontier - USVC=1 - and the 991 memq_hint guard): the new expectations are that the boot
# SURVIVES past the exec (the exec-done trio appears), the SUBMAPSKIP probe records its submap
# callers, USVC records the first user-mode SVCs, and VMHINT991 stays at 0 while the boot survives.
# ONE PAYLOAD edit (kept from 999/1000): `STAGE90_HW_WATCHDOG_TIMEOUT_S` 29u - the loaded payload
# bytes are 999's; 1001 must not slip back to the 25 s net.
#
# THE SWITCH SET is the CURRENT record's lines VERBATIM (all checker/witness/USB-ladder switches
# stay 1; RESIDENT=1 carries the pet, the WDT feed, the tick probe and - via the ASFLAGS half - the
# timer-edge hooks) plus STAGE90_XNU_SUBMAP_PMAP_SKIP=1. The ENTRY BIN MUST MOVE (bbdf41bc -> the
# new sha8) AND the pool's `osfmk_vm_vm_map.o` must be rebuilt - build_entry.sh only LINKS it.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 1003: the STALL INSTRUMENT - a per-tick pid-1 user-pc sampler (STAGE90_XNU_STALL), to settle BLOCKED vs SPINNING; 1002's set verbatim + ONE new switch =="
echo "-- 1002's arm was armed-d13-b04976fe (PRESSED 2026-10-13: the USVC-hook r3-save fix took BY VALUE - the 230x dyld abort at 0x4 is GONE and the frontier MOVED to a user-side stall after 22 SVCs; adjudication in records/1002-press-read.txt, reframe in docs/1003-dyld-stall-design-draft.md); 1002's set verbatim --"

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
# The record carries every key above (998's own record carries SHREG_GEOM and the rest). The pins
# keep the layers from ever disagreeing with each other or with the ENV exports below
# ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_PROBE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_WIRE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_GEOM=1"
# 1001's switch: `vm_map.c` IS a pool file (out/xnu_kernel_obj_d13/osfmk_vm_vm_map.o), so unlike
# 1000's USVC this one rides BOTH `EXTRA` and the environment export below into
# `build_xnu_arm_kernel.sh`'s DEFINES ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SUBMAP_PMAP_SKIP=1"
# 1003's NEW switch: STAGE90_XNU_STALL, the per-tick pid-1 pc sampler. It reaches the board PE's
# compile (`src/platform/darwin13/pe_msm8974.c`) as `-DSTAGE90_XNU_STALL` via `XNU_KERNEL_EXTRA_DEFINES`
# (== this `EXTRA`), the same path HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD use - `pe_msm8974.o` is
# compiled by `build_xnu_arm_kernel.sh`'s platform block with `EXTRA_DEFINES`, NOT by the entry
# toolchain. The recorder half lives in `src/entry/entry_stubs.c` (compiled by build_entry.sh, which
# reads `$STALL` for the RECORD only; its body is ungated). NO tree edit and NO stage driver: both
# halves are src/ files edited directly.
EXTRA="$EXTRA -DSTAGE90_XNU_STALL=1"
# 1000's USVC adds NO key HERE: it is read by the ASSEMBLY layer (ASFLAGS via assemble_arm_layer.sh)
# and by build_entry.sh (the env EXPORTS below), and NO pool C file reads it - the same shape
# RESIDENT had at 999. 1001's SUBMAP_PMAP_SKIP DOES (pinned just above): vm_map.c rides the pool.
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all TWELVE idempotent; the shreg-probe driver stages TEN edits - the
#    three 999 slide prints ride the SAME file and gate as the five existing IOS7SHREG ones - the
#    timer-edge driver stages the 999 amendment's TWO assembly hooks, the usvc driver stages 1000's
#    ONE hook in traps_lo.s, and the submap-pmap-skip driver stages 1001's ONE edit in vm_map.c).
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) + the iOS 7 VM wire + -3 SWI (994) + the dyld witness rev-3 (995/996) + the 294/438 shared-region probe + the 999 slide ladder (996/999) + the 32 B shared-region wire (997) + the shared-region geometry + stack top (998) + the timer-discriminator edge hooks (999 am2) + the user-SVC frontier hook (1000) + the submap pmap-enter skip (1001) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh
tools/stage_d13_shreg_probe.sh
tools/stage_d13_shreg_wire.sh
tools/stage_d13_shreg_geom.sh
tools/stage_d13_timer_edge.sh
tools/stage_d13_usvc.sh
tools/stage_d13_submap_pmap_skip.sh
# 1003 adds NO stage driver. The two halves of the stall instrument are src/ files edited directly:
# the recorder in `src/entry/entry_stubs.c` (compiled by build_entry.sh) and the ONE call site in
# `src/platform/darwin13/pe_msm8974.c` (compiled by the platform block under -DSTAGE90_XNU_STALL). No
# tree file outside src/ is touched - this is a pure MEASUREMENT rung, and the gated `#if` in
# pe_msm8974.c keeps the tree's own objects byte-identical when STALL=0.

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
# MUST be exported BEFORE assemble_arm_layer.sh runs (it reads the environment, not the record).
export STAGE90_XNU_SP_ALIGN=1

# 991: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below.
export STAGE90_XNU_VMHINT_GUARD=1

# 1001: the POOL reads this one the same way (vm_map.c is a pool file; the DEFINE is gated on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below - the same
# rule as 991's VMHINT_GUARD ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_SUBMAP_PMAP_SKIP=1

# 992/993: same rule for the commpage alias (all pool files).
export STAGE90_XNU_COMMPAGE_ALIAS=1

# 994: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_IOS7_TRAPS=$IOS7_TRAPS into traps_lo.o, and the
# POOL reads the same variable. MUST be exported BEFORE assemble_arm_layer.sh runs.
export STAGE90_XNU_IOS7_TRAPS=1

# 995: the POOL reads this one - `trap.c` rides the pool, so there is no ASFLAGS half. 999 keeps it at 1.
export STAGE90_XNU_DYLD_WITNESS=1

# 996/999: same rule for the shared-region probe (`bsd/vm/vm_unix.c`, a pool file). 999 keeps it at 1:
# the probe's IOS7SHREG lines stay readable next to the wire's own IOS7LAB cache mapping lines, and the
# three 999 slide prints ride the same gate.
export STAGE90_XNU_SHREG_PROBE=1

# 997: same rule for the 32 B shared-region wire (the SAME pool file). 999 keeps it at 1.
export STAGE90_XNU_SHREG_WIRE=1

# 998: same rule for the shared-region geometry + stack top. 999 keeps it at 1 and touches neither.
export STAGE90_XNU_SHREG_GEOM=1

# 999 (amendment #2): the ASSEMBLY layer reads this to put -DSTAGE90_XNU_RESIDENT=$RESIDENT into
# machine_routines_asm.o and cswitch.o (the timer-discriminator edge hooks). MUST be exported BEFORE
# assemble_arm_layer.sh runs; the SAME value reaches build_entry.sh through EXPORTS below
# ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_RESIDENT=1

# 1000: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_USVC=$USVC into traps_lo.o (the user-SVC
# frontier hook in fleh_swi). MUST be exported BEFORE assemble_arm_layer.sh runs; the SAME value
# reaches build_entry.sh through EXPORTS below ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_USVC=1

# 1003: the NEW switch STAGE90_XNU_STALL. It must be exported (not only in `EXTRA`) for the same
# reason 991/1001's pool switches are: `build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable, and the platform block's compile (which carries the pe_msm8974.c call site)
# reads it from there. Export it HERE, before the pool/platform build below - the SAME value reaches
# build_entry.sh through EXPORTS below ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_STALL=1

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 999+am2: machine_routines_asm.o and cswitch.o get the timer-discriminator edge hooks; 1000: traps_lo.o ALSO gets the USVC hook - both switches exported before this line) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_SUBMAP_PMAP_SKIP=$STAGE90_XNU_SUBMAP_PMAP_SKIP STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM (pool DEFINES + asm ASFLAGS + EXTRA agree) STAGE90_XNU_RESIDENT=$STAGE90_XNU_RESIDENT (asm ASFLAGS + build_entry EXPORTS + the ungated helper's runtime gate; no pool object reads it) STAGE90_XNU_USVC=$STAGE90_XNU_USVC (asm ASFLAGS + build_entry EXPORTS; no pool object reads it - the C half is the entry image's own) --"
echo "-- the WHOLE D13 kernel pool + platform block (vm_unix.c carries the 32 B wire (997) + the 294/438 probe + the 999 slide ladder; vm_shared_region.c the iOS 7 geometry (998); status.c the 0x2BE00000 stack top (998); trap.c the witness rev-3; mach_kernelrpc.c the wire adapters; ios7_compat.h the two selectors; pmap.c + commpage.c + arm_vm_init.c the alias/zeroing; vm_resident.c the hint guard; pcb.c the alignment) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** Same trap as 987
# ([[mi4-one-value-two-definitions]]): the record's key name and the tree's guard name differ by one
# word, and only the environment path bridges them.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

echo "-- entry image (1000's switch set re-exported verbatim + SUBMAP_PMAP_SKIP=1; build_entry only LINKS the pool) --"
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
# The record (999's own) carries every tree-shaping switch including SHREG_GEOM and RESIDENT; the
# pins name them explicitly so build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the
# build layers compiled with ([[mi4-one-value-two-definitions]]). STAGE90_XNU_USVC is 1000's NEW key
# and the 999 record does NOT carry it yet - the pin is the only carrier this run, and the record the
# entry build writes (asserted at the tail) is what parks the arm with it.
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_XNU_COMMPAGE_ALIAS=1")
EXPORTS+=("STAGE90_XNU_IOS7_TRAPS=1")
EXPORTS+=("STAGE90_XNU_DYLD_WITNESS=1")
EXPORTS+=("STAGE90_XNU_SHREG_PROBE=1")
EXPORTS+=("STAGE90_XNU_SHREG_WIRE=1")
EXPORTS+=("STAGE90_XNU_SHREG_GEOM=1")
EXPORTS+=("STAGE90_XNU_RESIDENT=1")
EXPORTS+=("STAGE90_XNU_USVC=1")
# 1001's NEW key: the 1000 record does NOT carry it yet - the pin is the only carrier this run, and
# the record the entry build writes (asserted at the tail) is what parks the arm with it.
EXPORTS+=("STAGE90_XNU_SUBMAP_PMAP_SKIP=1")
# 1003's NEW key STAGE90_XNU_STALL. The 1002 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD only; the PE's
# call site was compiled from the environment export above (== the same value).
EXPORTS+=("STAGE90_XNU_STALL=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   1002 was b04976fe - a DIFFERENT value here means the 1003 edits reached the image."
# The arm must MOVE off the PRESSED 1002 arm (the project rule: the arm IS the entry bin's sha8). A
# build that lands back on b04976fe did not change the entry image - the 1003 edit did not reach the
# linked image (or the layer was not rebuilt). This is the build-side counterpart of the arm-refusal
# guards the park/press scripts carry against the pressed arms.
_d3_arm=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $_d3_arm == armed-d13-b04976fe ]] && { echo "FAIL: the build produced 1002's arm (b04976fe) again - the entry bin did NOT move, so the 1003 edit did not reach the linked image (rebuild the layer that carries the change: tools/assemble_arm_layer.sh with the switch exported, or the pool); do not park a 1003 arm identical to the pressed 1002 one"; exit 1; }
[[ $_d3_arm == armed-d13-6ab42af7 ]] && { echo "FAIL: the build produced 1001's arm (6ab42af7) - a stale layer"; exit 1; }
echo "   1003 arm: $_d3_arm (must differ from the pressed 1002 arm b04976fe)"
echo "-- 1000's own constants/keys BY VALUE (source side, what the image check cannot split) --"
printf '   ENTRY_LIVE_CAP:  '; grep -m1 '^#define ENTRY_LIVE_CAP' src/entry/entry_stubs.c || true
printf '   WDT timeout:     '; grep -m1 '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S' src/stage90.h || true
[[ $(grep -c '^#define ENTRY_LIVE_CAP 24576u$' src/entry/entry_stubs.c) == 1 ]] || { echo "FAIL: the cap constant is not 24576u - the rebuild would re-hit the 998 wall"; exit 1; }
[[ $(grep -c '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S 29u$' src/stage90.h) == 1 ]] || { echo "FAIL: the watchdog timeout is not 29u - the outer net is not the arithmetic ceiling"; exit 1; }
# The robust use-count: a bare `grep 'ST_LIVE_RD('` also matches the two `#define ST_LIVE_RD(...)`
# lines themselves (and any prose naming the macro), measured 72 - statement USES start after
# whitespace with the macro name, measured 70.
rd_thin=$(grep -cE '^\s*ST_LIVE_RD\(' src/entry/entry_storage.c || true)
rd_full=$(sed -n '/^static __attribute__((noinline, noclone)) void st_read_single_block/,/^}/p' src/entry/entry_storage.c | grep -cE '(^|[^_])ST_LIVE\(' || true)
echo "   read body: $rd_thin thinned cell site(s) (want 70), $rd_full locating cell site(s) that never thin (want 8)"
[[ "$rd_thin" == "70" && "$rd_full" == "8" ]] || { echo "FAIL: the thinning is not 70/8 - either the six locating cells were gated (a record whose absence is evidence would move) or the thinning did not reach the reader"; exit 1; }
probe_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_exec_done_probe_' || true)
echo "   exec-done probe keys in the image: $probe_keys (want 5)"
[[ "$probe_keys" == "5" ]] || { echo "FAIL: the five probe keys are not in the image - the sig/size read-back did not reach it"; exit 1; }
done_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -cE 'xnu_live_exec_done_(seq|who|current)' || true)
echo "   exec-done record keys in the image: $done_keys (want 3, still the SAME names)"
[[ "$done_keys" == "3" ]] || { echo "FAIL: the done trio's names moved - the pre-registration greps for them"; exit 1; }
echo "-- by value: the DOOR'S PET (entry_storage_driver_read references entry_wdt_pet WEAKLY, null-guarded; the trace object defines it as a global T) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_wdt_pet$' || { echo "FAIL: entry_wdt_pet is not a defined text symbol in the image (RESIDENT=1 carries the pet; 977)"; exit 1; }
# The reference class is part of the contract: `w` (weak undefined), NOT `U`. The first 999 build died on
# the strong form - pass 1 stubs the pet and the final link refuses with `multiple definition` (the trap
# at build_entry.sh:29101-29112); measured on the third run, this exact line REJECTED the weak object
# (the check had been written for `U` before the 2026-10-13 ruling). A revert to strong here means the
# ruling was undone, and the name count is still 1.
[[ $(arm-none-eabi-nm out/stage90/xnu_arm_entry_storage.o | grep -c '^[[:space:]]*w entry_wdt_pet$' || true) == 1 ]] || { echo "FAIL: the storage object does not carry exactly one WEAK 'w entry_wdt_pet' reference - either the read-door feed did not compile in, or the reference went back to the strong 'U' shape (the pass-1 stub collision of 2026-10-13)"; exit 1; }
door_a=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="entry_storage_driver_read"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_storage_driver_read is not in the image (the probe>=60 door is compiled out - the whole resident pet hangs off it)"; exit 1; }
# Same introduced-defect guard as 998's reader below: NO early `exit` in the next-symbol awk (an early
# exit SIGPIPEs `nm`, and under `set -o pipefail` the 141 is a silent death); read the stream, keep the
# first match.
door_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${door_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
door_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$door_a" --stop-address="${door_x:-0xffffffff}")
door_pets=$(grep -c 'bl.*<entry_wdt_pet>' <<<"$door_body" || true)
echo "   the door body calls entry_wdt_pet $door_pets time(s) (want 1)"
[[ "$door_pets" == "1" ]] || { echo "FAIL: entry_storage_driver_read does not call the pet exactly once (0 = the feed is not at the door; >1 = the body moved)"; exit 1; }
echo "-- by value: the 999 SLIDE LADDER in the POOL object (bsd_vm_vm_unix.o must carry the three new printf literals) --"
slide_lits=$(strings out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -c 'IOS7SHREG slide_' || true)
echo "   IOS7SHREG slide_* literals: $slide_lits (want 3)"
[[ "$slide_lits" == "3" ]] || { echo "FAIL: the three slide prints are not in the pool object - stage_d13_shreg_probe.sh did not put them in bsd/vm/vm_unix.c or the pool was not rebuilt"; exit 1; }
echo "-- by value: the TIMER-DISCRIMINATOR hooks (amendment #2) - the C helper in the image, BOTH asm objects referencing it (the ASFLAGS half of the switch, the 933/990 trap), exactly ONE \`bl\` inside each staged site's own body, and the six direct record keys --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_timer_edge_probe$' || { echo "FAIL: entry_timer_edge_probe is not a defined text symbol in the image - the pe_msm8974.c helper did not link (the bl would be undefined)"; exit 1; }
for eobj in machine_routines_asm cswitch; do
  e_u=$(arm-none-eabi-nm out/xnu_asm_obj_d13/$eobj.o 2>/dev/null | grep -c '^[[:space:]]*U entry_timer_edge_probe$' || true)
  echo "   out/xnu_asm_obj_d13/$eobj.o references entry_timer_edge_probe: $e_u (want 1)"
  [[ "$e_u" == "1" ]] || { echo "FAIL: $eobj.o does not reference the helper - the -DSTAGE90_XNU_RESIDENT=\$RESIDENT ASFLAGS line did not reach this object (export the switch before tools/assemble_arm_layer.sh and rebuild the assembly layer)"; exit 1; }
done
for esym in ml_set_interrupts_enabled thread_exception_return; do
  ea=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$esym" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $esym not in the image - the site the edge probe rides is gone"; exit 1; }
  # The range ends at the next GLOBAL `T`, NOT the next `T/t` the door reader uses: both hook sites
  # sit BEHIND an internal label the assembler gave a local symbol (`astloop` at mlie+0xc,
  # `return_to_user` at texr+0x28), so a first-T/t reader would stop 4 bytes into the function and
  # report 0 calls on a CORRECT image (measured 2026-10-13 on the 998 elf: mlie 12 bytes, texr 4).
  # The next global is the function's true end here - `ml_get_interrupts_enabled` at mlie+0x80,
  # `L_CurrentThread`'s successor T at texr+0x4c - and it contains the site. Same no-early-exit awk
  # as the door reader above (998's measured SIGPIPE/141 defect).
  ex=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${ea#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 == "T" { if (!d) { print "0x" $1; d = 1 } }')
  ebody=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$ea" --stop-address="${ex:-0xffffffff}")
  ecalls=$(grep -c 'bl.*<entry_timer_edge_probe>' <<<"$ebody" || true)
  echo "   $esym ($ea..${ex:-end}) calls entry_timer_edge_probe: $ecalls (want 1)"
  [[ "$ecalls" == "1" ]] || { echo "FAIL: $esym does not carry exactly one edge-probe call site (0 = the hook did not reach the assembly layer; >1 = the body moved)"; exit 1; }
done
edge_keys=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cE 'xnu_live_edge_(mlie|texr)_(seq|cpsr|tick)' || true)
echo "   timer-discriminator record keys in the image: $edge_keys (want 6)"
[[ "$edge_keys" == "6" ]] || { echo "FAIL: the six edge keys are not in the image - the pre-registration greps for xnu_live_edge_mlie_seq/_cpsr/_tick and xnu_live_edge_texr_seq/_cpsr/_tick verbatim"; exit 1; }
echo "-- by value: the checkers (PASS only if the linked image carries each fix) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_ios7_traps.py out/stage90/xnu_arm_entry.elf --expect on
python3 tools/check_d13_ios7_vmwire.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the same witness checker against the FROZEN 994 image as a NEGATIVE CONTROL (must FAIL ON and PASS OFF) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 996 SHREG checker ON the 999 image (via --ladder; the frozen 996/998 runs below stay base-5) - its frozen 20 B clause ('996 measures the misparse, it does not fix it') is the ONE by-design FAIL, and NOTHING else may fail: the EIGHT IOS7SHREG literals in the image (5 x 996 + 3 x 999), the 294/438 sysent rows still name the real bodies, no IOS7SHREG reference outside the three probed functions, and the SOURCE-side gate check on the staged tree file --tree bsd/vm/vm_unix.c (10 guard regions; NOTE: --tree takes the FILE, not the tree root - measured) --"
# NOTE (coordination, 2026-10-13): the three 999 slide literals are enforced ONLY under the new
# `--ladder` flag (recommended to the vmabi-recon lane: base PROBE_LITERALS stay 5 and the frozen
# 996/998 home-image runs above/below keep passing; `--ladder` adds the 3 image clauses AND makes
# --tree expect 10 guard regions instead of 7). If the lane lands a different spelling, this one
# line and the echo above change - nothing else here depends on it.
sp_rc=0
sp_out=$(python3 tools/check_d13_shreg_probe.py out/stage90/xnu_arm_entry.elf --expect on --ladder --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c 2>&1) || sp_rc=$?
printf '%s\n' "$sp_out"
sp_fails=$(printf '%s\n' "$sp_out" | grep -c '^  FAIL ' || true)
if [[ $sp_rc -eq 0 ]] || [[ $sp_fails -ne 1 ]] || ! grep -q '^  FAIL shared_region_copyin_mappings does not compute' <<<"$sp_out"; then
  echo "FAIL: the 996 checker did not fail on exactly its by-design 20 B clause (rc=$sp_rc, $sp_fails clause FAIL line(s)) - rc 0 means the 32 B wire did not reach the image; any other failing clause means the probe/ladder is damaged or the tree is not staged"
  exit 1
fi
echo "   (by design: the ONLY failing clause is the frozen 20 B straddle one)"
echo "-- by value: the SAME frozen checker against its OWN 996 image, with the FROZEN 994 image as a NEGATIVE CONTROL (on must FAIL, off must PASS) - 997/998/999 do not weaken it --"
python3 tools/check_d13_shreg_probe.py out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 997 shreg-WIRE CHECKER (switch ON: the IOS7LAB cache mapping literal in shared_region_copyin_mappings reaching a printf; count*32 (\`lsl #5\` + \`add rX,rX,#32\`); the 20 B \`count*5<<2\` pair ABSENT; 294's copyout r2=8 for the full uint64; no stray literal) + the SOURCE-side gate check --tree --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c
echo "-- by value: the same wire checker against the FROZEN 996 image as a NEGATIVE CONTROL (it must FAIL ON and PASS OFF) and the 994 capture as a second --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 998 shreg-GEOM CHECKER, SOURCE side (NOTE: --tree takes the TREE ROOT here, unlike the wire/probe checkers' FILE argument - measured): the three files all in the staged shape, the ABI constants intact in BOTH states, no v1 inline-#if wrap --"
python3 tools/check_d13_shreg_geom.py --tree external/xnu-hd2-darwin13/xnu --expect staged
echo "-- by value: the 998 IMAGE-side clauses, standalone (the same three properties build_entry.sh's 998 clause asserts at the link site): (b) the IOS7LAB shared region printf literal is in the image (it sits INSIDE the opened gate), (c) thread_userstack + thread_userstackdefault materialize the 0xBE0 stack top and NOT the 0xFE0 default --"
gl_lit='IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000'
gl_n=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   (b) IOS7LAB shared region literal copies: $gl_n (want 1)"
[[ $gl_n -eq 1 ]] || { echo "FAIL: the image-side literal reader found $gl_n copies of the geometry printf literal (want exactly 1)"; exit 1; }
for gfn in thread_userstack thread_userstackdefault; do
  gfa=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$gfn" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $gfn not in the image"; exit 1; }
  # NO early `exit` in this awk (998's measured defect: an early exit SIGPIPEs `nm` and under
  # `set -o pipefail` the 141 becomes a silent death). Read the whole stream, keep the first match.
  gfnx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${gfa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  echo "   (c) $gfn ($gfa..${gfnx:-end}):"
  arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$gfa" --stop-address="${gfnx:-0xffffffff}" | grep -E 'movt.*; 0x2be0|movt.*; 0x2fe0' | sed 's/^/       /' || true
done
echo "-- by value: the SAME image-side reader against the FROZEN 998 elf as a NEGATIVE CONTROL (both readers must still work on it; 999 moves neither constant) --"
gc_frz=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-dee822b0/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   frozen 998 elf: IOS7LAB shared region literal copies: $gc_frz (want 1)"
[[ $gc_frz -eq 1 ]] || { echo "FAIL: the frozen 998 elf does not carry the geometry literal the reader looks for - the reader is stale"; exit 1; }
echo "-- by value: the POOL objects FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
echo "-- by value: the 997/999 WIRE+PROBE object (bsd_vm_vm_unix.o must carry the IOS7LAB cache mapping literal, the count*32 shape, the 996 IOS7SHREG probe AND the 999 slide ladder) --"
strings -a out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "IOS7LAB cache mapping|IOS7SHREG" | head -14 || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "lsl.*#5|#32" | head -8 || true
echo "-- by value: the 998 GEOMETRY object (osfmk_vm_vm_shared_region.o must carry the IOS7LAB shared region literal + 0x2C000000/0x14000000) --"
strings -a out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -F 'IOS7LAB shared region' || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -E '#0x2c00|#0x1400' | head -6 || true
echo "-- by value: the 998 STACK object (osfmk_arm_status.o must carry the 0x2BE00000 stack top) --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_status.o 2>/dev/null | grep -B1 -A1 '; 0x2be0' | head -12 || true
echo "-- by value: the 995 PROBE object (trap.o must carry the witness REV-3) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_trap.o 2>/dev/null | grep -E "ios7wit_dyld_message|IOS7WITNESS|IOS7WIT_AI_|copyin|copyinstr" | head -20 || true
echo "-- by value: the 994 POOL object (mach_kernelrpc.o must carry the wire adapters) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o 2>/dev/null | grep -E "IOS7LAB|vm_allocate64|vm_deallocate64|vm_protect64|ios7_traps_enabled" | head -20 || true
echo "-- by value: the ASSEMBLY object (traps_lo.o must carry the mach_absolute_time call, NOT the cmn r12,#3 fast path) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -B2 -A2 -E "mach_absolute_time|swi_exit64" | head -20 || true
echo "-- by value: the 999 PAYLOAD constant AT THE ARM SITE (stage90_main.o's call must pass 29 - the register carries SECONDS; measured 998: 'mov r0, #25' immediately before 'bl stage90_hw_watchdog_arm') --"
# The 0xE7FA9 (29*32765) / 0xFFFA0 (32*32765) tick values are RUNTIME arithmetic
# (hw_watchdog.c:223 `bark_ticks = timeout_s * MSM8974_WDT_HZ` + BITE_EXTRA_TICKS), so NO object
# contains them - the ARMED log's `hw_watchdog_timeout_s=0x0000001d` and
# `hw_watchdog_bite_ticks_written=0x000FFFA0` are the press-side observables, and this is the
# build-side one that makes them arithmetically inevitable.
arm_site=$(arm-none-eabi-objdump -dr out/stage90/stage90_main.o 2>/dev/null | grep -B3 'bl.*stage90_hw_watchdog_arm' | grep -E 'mov[a-z]*[[:space:]]+r0, #(25|29)' || true)
printf '%s\n' "${arm_site:-   (no mov r0,#25|#29 found near the arm call - the reader is stale)}"
grep -qE 'mov[a-z]*[[:space:]]+r0, #29' <<<"$arm_site" || { echo "FAIL: the payload's arm site does not pass 29 - the loaded payload would arm the OLD 25 s net and the run would die at 998's wall"; exit 1; }
echo "-- by value: the 1000 read-dump thinning ordinal AT THE SOURCE (999 built 256 and it NEVER FIRED - 218 reads in the whole boot - so the full-width dump spent the cap before exec; 1000 moves it to 64 and the tool PARSES it, never restates it) --"
printf '   thinning ordinal:  '; grep -m1 'const uint32_t rd_full_' src/entry/entry_storage.c || true
[[ $(grep -cF 'rd_full_ = (st_read_count < 64u);' src/entry/entry_storage.c) == 1 ]] || { echo "FAIL: the thinning ordinal is not 64 - the rebuild would spend the cap on full-width dumps before exec again"; exit 1; }
[[ $(grep -cF 'st_read_count < 256u' src/entry/entry_storage.c) == 0 ]] || { echo "FAIL: a '< 256u' comparison survives - the moved ordinal is defined twice"; exit 1; }
python3 tools/read_storage_key_order.py --selftest >/dev/null || { echo "FAIL: the key-order tool's selftest is red - the ordinal this build just moved is parsed by that tool, not restated"; exit 1; }
printf '   USVC window:       '; grep -m1 '^#define USVC_FULL_N' src/entry/entry_stubs.c || true
[[ $(grep -c '^#define USVC_FULL_N 64u' src/entry/entry_stubs.c) == 1 ]] || { echo "FAIL: the SVC full-field window is not 64u - the press-side pre-registration greps the first 64 SVCs in full"; exit 1; }
python3 tools/check_saved_state_offsets.py --selftest >/dev/null || { echo "FAIL: the saved-state checker's selftest is red"; exit 1; }
python3 tools/check_saved_state_offsets.py || { echo "FAIL: the saved-state offsets disagree across the trees - the USVC C half reads R0/R12 by these very numbers, and a wrong one prints a field of the wrong frame word"; exit 1; }
[[ $(awk -F= '$1=="STAGE90_XNU_USVC"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_USVC=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "-- by value: the 1000 USER-SVC hook IN THE IMAGE (the C half defined, the ASFLAGS half in traps_lo.o, ONE hook call inside locore_fleh_swi - the link's rename of traps_lo.o's fleh_swi, NOT the low-address stand-in - the hook IS the body's first branch, the writes all DIRECT, and the eight keys x1) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_note_user_svc$' || { echo "FAIL: entry_note_user_svc is not a defined text symbol in the image - the C half (entry_stubs.c) did not compile in or link"; exit 1; }
u10_u=$(arm-none-eabi-nm out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -c '^[[:space:]]*U entry_note_user_svc$' || true)
echo "   out/xnu_asm_obj_d13/traps_lo.o references entry_note_user_svc: $u10_u (want 1)"
[[ "$u10_u" == "1" ]] || { echo "FAIL: traps_lo.o does not reference the helper - the -DSTAGE90_XNU_USVC=\$USVC ASFLAGS line did not reach this object (export the switch before tools/assemble_arm_layer.sh and rebuild the assembly layer)"; exit 1; }
u10_ls=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: locore_fleh_swi is not in the image - it is traps_lo.o's fleh_swi under the link's own rename of the six colliding names, and it is the ONE body the hook is inserted at"; exit 1; }
# The range ends at the next GLOBAL `T`: this body is ASSEMBLY with internal named labels
# (swi_trap_ret at +0x3c and the rest), so the first-T/t reader the C bodies above use would stop
# early. Same no-early-exit awk as the edge-probe reader (999's measured SIGPIPE/141 defect).
u10_lsx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_ls#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 == "T" { if (!d) { print "0x" $1; d = 1 } }')
u10_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_ls" --stop-address="${u10_lsx:-0xffffffff}")
u10_c=$(grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_user_svc>' <<<"$u10_body" || true)
echo "   locore_fleh_swi ($u10_ls..${u10_lsx:-end}) calls entry_note_user_svc: $u10_c (want 1)"
[[ "$u10_c" == "1" ]] || { echo "FAIL: locore_fleh_swi does not carry exactly one hook call (0 = the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported; >1 = the body moved)"; exit 1; }
u10_all=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_user_svc>' || true)
echo "   image-wide call sites: $u10_all (want 1 - the SVC entry's own)"
[[ "$u10_all" == "1" ]] || { echo "FAIL: the image carries $u10_all call site(s) of the hook - a second site would count calls the frontier is not about"; exit 1; }
# The same reader as build_entry.sh's live clause, and it must be spelled the same way for the same
# two measured reasons (2026-10-13: run 3 of build_1000.sh died SILENTLY at the twin - rc 1, no FAIL
# line - because `\t` inside an ERE is NOT a tab to GNU grep 3.7, which is what runs here under
# `env -i`/child processes, while the interactive shell's ugrep-backed `grep` function matched the
# same pattern - a reader "verified" through the wrong grep is m997's class, the measurement being
# the thing that was wrong; and `| head -1 |` can race SIGPIPE under `set -o pipefail`, 998's 141
# defect - `-m1 ... || true` lets a genuine no-match reach the check below instead of a bare exit 1).
u10_hook=$(grep -n -m1 '<entry_note_user_svc>' <<<"$u10_body" | cut -d: -f1 || true)
u10_br=$(grep -nE -m1 '[[:space:]](b|bl|bx|blx|b(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al))(\.[a-z]+)?[[:space:]]' <<<"$u10_body" | cut -d: -f1 || true)
echo "   the hook is the body's FIRST branch: hook at disasm line ${u10_hook:-none}, first branch at ${u10_br:-none}"
[[ -n "$u10_hook" && "$u10_hook" == "$u10_br" ]] || { echo "FAIL: the hook is not the body's first branch - it must sit right after \`cpsie i\`, BEFORE the -3 timebase fast path and every dispatch branch, or the frontier reading is about a subset of the calls"; exit 1; }
echo "   the hook window, by value:"
grep -B2 -A2 'bl.*<entry_note_user_svc>' <<<"$u10_body" | sed 's/^/       /'
u10_svc=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="entry_note_user_svc"{print "0x" $1; found=1} END{exit(found?0:1)}')
u10_svcx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_svc#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
u10_svc_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_svc" --stop-address="${u10_svcx:-0xffffffff}")
u10_kv=$(grep -c '<entry_write_kv>' <<<"$u10_svc_body" || true)
u10_lw=$(grep -c '<entry_live_write>' <<<"$u10_svc_body" || true)
echo "   entry_note_user_svc ($u10_svc..${u10_svcx:-end}): entry_write_kv x$u10_kv (want >= 8), entry_live_write x$u10_lw (want 0)"
[[ "$u10_kv" -ge 8 && "$u10_lw" -eq 0 ]] || { echo "FAIL: the C half's writes are not all DIRECT (>= 8 sites for the full window's eight fields, 0 counted) - a record whose ABSENCE is the reading may not ride the cap"; exit 1; }
grep -qE 'cmp[[:space:]]+r[0-9]+, #(64|0x40)' <<<"$u10_svc_body" || { echo "FAIL: entry_note_user_svc does not compare against 64 - USVC_FULL_N is the full-field window"; exit 1; }
u10_keys=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_usvc_' || true)
echo "   xnu_live_usvc_* keys in the image: $u10_keys (want 8)"
[[ "$u10_keys" == "8" ]] || { echo "FAIL: the eight field keys are not in the image once each - the press-side pre-registration greps them verbatim"; exit 1; }
echo "-- by value: the FROZEN 999 elf as the NEGATIVE CONTROL for the new readers (the hook and the keys are NEW - a reader that cannot fail certifies nothing) --"
u10_frz_u=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-467d59e4/xnu_arm_entry.elf | grep -c 'xnu_live_usvc_' || true)
echo "   frozen 999 elf: xnu_live_usvc_* keys: $u10_frz_u (want 0)"
[[ "$u10_frz_u" == "0" ]] || { echo "FAIL: the frozen 999 elf carries usvc keys - the reader cannot discriminate the two arms"; exit 1; }
u10_frz_t=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-467d59e4/xnu_arm_entry.elf | grep -cE 'xnu_live_tick_(sum_|anom_)' || true)
echo "   frozen 999 elf: xnu_live_tick_{sum_,anom_}* keys: $u10_frz_t (want 0)"
[[ "$u10_frz_t" == "0" ]] || { echo "FAIL: the frozen 999 elf carries 1000's tick keys - the reader cannot discriminate the two instruments"; exit 1; }
echo "-- by value: the 1000 TICK probe IN THE IMAGE (the sample's ONE call site inside the handler, the summaries' two call sites, the ten keys) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_tick_summary$' || { echo "FAIL: entry_tick_summary is not in the image - the pe_msm8974.c aggregate dump did not link"; exit 1; }
u10_tss=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="msm8974_tick_sample"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: msm8974_tick_sample is not in the image - the tick's own sampler did not link"; exit 1; }
u10_tss_all=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE 'bl[[:space:]]+[0-9a-f]+ <msm8974_tick_sample>' || true)
echo "   image-wide msm8974_tick_sample call sites: $u10_tss_all (want 1)"
[[ "$u10_tss_all" == "1" ]] || { echo "FAIL: the sample does not have exactly one call site - a second site changes what the delta the records carry means"; exit 1; }
u10_hi=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="msm8974_handle_interrupt"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: msm8974_handle_interrupt is not in the image - the function whose timer branch owns the one sample call"; exit 1; }
u10_hix=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_hi#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
u10_hi_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_hi" --stop-address="${u10_hix:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <msm8974_tick_sample>' <<<"$u10_hi_body" || { echo "FAIL: the one sample call is not inside msm8974_handle_interrupt ($u10_hi..${u10_hix:-end}) - the reading would be taken by code the timer path does not run"; exit 1; }
echo "   the sample's site inside the handler, by value:"
grep -B3 -A1 'bl.*<msm8974_tick_sample>' <<<"$u10_hi_body" | sed 's/^/       /'
for u10_pop in entry_note_exec_returned entry_wdt_pet; do
  u10_pa=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$u10_pop" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $u10_pop not in the image - it is one of the two summary call sites"; exit 1; }
  u10_px=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_pa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  u10_pb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_pa" --stop-address="${u10_px:-0xffffffff}")
  # `b` OR `bl`: the exec-done site's call is a TAIL call (measured: `b 801ae2ec
  # <entry_tick_summary>` at the body's last instruction - the same reader fix as build_entry.sh's
  # 1000 clause; the structure to certify is one direct branch, not a spelling).
  u10_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_tick_summary>' <<<"$u10_pb" || true)
  echo "   $u10_pop ($u10_pa..${u10_px:-end}) calls entry_tick_summary: $u10_pc (want 1)"
  [[ "$u10_pc" == "1" ]] || { echo "FAIL: $u10_pop does not call the summary exactly once - the aggregate must be asked for at this moment (0 = the call site moved; >1 = it runs twice)"; exit 1; }
done
u10_tk=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_tick_sum_' || true)
u10_ta=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_tick_anom_' || true)
u10_pk=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_wdt_pet_' || true)
echo "   tick summary keys: $u10_tk (want 3); tick anomaly keys: $u10_ta (want 4); pet feed keys: $u10_pk (want 3)"
[[ "$u10_tk" == "3" && "$u10_ta" == "4" && "$u10_pk" == "3" ]] || { echo "FAIL: the ten new direct keys (3 summary + 4 anomaly + 3 pet) are not each in the image once - the press-side pre-registration greps them verbatim"; exit 1; }
echo "-- by value: the 1001 SUBMAP-SKIP in the POOL object and the LINKED image (switch ON) --"
ss_obj=$(strings -a out/xnu_kernel_obj_d13/osfmk_vm_vm_map.o 2>/dev/null | grep -c 'SUBMAPSKIP:' || true)
echo "   osfmk_vm_vm_map.o SUBMAPSKIP literals: $ss_obj (want 1)"
[[ "$ss_obj" == "1" ]] || { echo "FAIL: the SUBMAPSKIP literal is not in the pool object - tools/stage_d13_submap_pmap_skip.sh did not put the skip in osfmk/vm/vm_map.c, or the pool was not rebuilt with STAGE90_XNU_SUBMAP_PMAP_SKIP=1 exported (build_entry.sh only LINKS this object)"; exit 1; }
python3 tools/check_submap_pmap_skip.py out/stage90/xnu_arm_entry.elf || { echo "FAIL: the 1001 checker did not PASS on the fresh image (see its clauses above)"; exit 1; }
echo "-- by value: the FROZEN 1000 elf as the NEGATIVE CONTROL for the 1001 checker (must FAIL check 1 - the skip did not exist when it was built) --"
ss_ctrl=0
ss_ctrl_out=$(python3 tools/check_submap_pmap_skip.py out/stage90/frozen/armed-d13-bbdf41bc/xnu_arm_entry.elf 2>&1) || ss_ctrl=$?
printf '%s\n' "$ss_ctrl_out" | sed 's/^/   /'
if [[ $ss_ctrl -ne 1 ]] || ! grep -q 'not in the image at all' <<<"$ss_ctrl_out"; then
  echo "FAIL: the 1001 checker did not refuse the frozen 1000 elf by its check-1 clause (rc=$ss_ctrl) - a checker that cannot fail certifies nothing"; exit 1
fi
echo "-- by value: the 1001 SOURCE edit in the staged tree (the kprintf literal once; the defined()-first #if twice; the is_submap guard once) --"
ss_src=external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_map.c
[[ $(grep -cF 'kprintf("SUBMAPSKIP:' "$ss_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one kprintf(\"SUBMAPSKIP: ...) - the 1001 edit is not the staged shape (re-run tools/stage_d13_submap_pmap_skip.sh)"; exit 1; }
[[ $(grep -cE '^#if defined\(STAGE90_XNU_SUBMAP_PMAP_SKIP\) && STAGE90_XNU_SUBMAP_PMAP_SKIP$' "$ss_src" || true) == 2 ]] || { echo "FAIL: the tree does not carry the two defined()-first #if wraps - half the edit is missing or a hand edit moved it"; exit 1; }
[[ $(grep -cF 'if (!is_submap) {' "$ss_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one \`if (!is_submap) {\` guard in vm_map_enter's tail"; exit 1; }
[[ $(awk -F= '$1=="STAGE90_XNU_SUBMAP_PMAP_SKIP"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "-- by value: the 1002 FIX AS A REGRESSION (the USVC hook's r3 save set must STILL be the six-word {r0-r3,r8,lr}; it is the baseline 1003 builds on, not 1003's delta) --"
echo "   (a) traps_lo.o carries the SIX-word save/restore {r0-r3,r8,lr} and NOT the four-word {r0,r1,r8,lr}"
t10_pw=$(arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' || true)
t10_lw=$(arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' || true)
echo "   traps_lo.o: six-word push {r0-r3,r8,lr} x$t10_pw (want 1), four-word {r0,r1,r8,lr} x$t10_lw (want 0)"
[[ "$t10_pw" == "1" && "$t10_lw" == "0" ]] || { echo "FAIL: traps_lo.o does not carry the 1002 six-word save set (and only it) - the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported, OR a revert put the 1001 four-word form back (reintroducing the r3 clobber)"; exit 1; }
echo "   (b) the LINKED image's locore_fleh_swi carries the SAME six-word save, with the four-word form gone"
t10_iw=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$u10_body" || true)
t10_il=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$u10_body" || true)
echo "   locore_fleh_swi: six-word push {r0-r3,r8,lr} x$t10_iw (want 1), four-word {r0,r1,r8,lr} x$t10_il (want 0)"
[[ "$t10_iw" == "1" && "$t10_il" == "0" ]] || { echo "FAIL: the linked locore_fleh_swi does not carry the 1002 six-word save set (and only it) - build_entry.sh links traps_lo.o, so a stale object would keep the r3 clobber the 1001 press read diagnosed"; exit 1; }
echo "   (c) the six-word save is immediately followed by the hook call, and the restore brackets it"
grep -A3 'push.*{r0, r1, r2, r3, r8, lr}' <<<"$u10_body" | sed 's/^/       /'
echo "   (d) the SOURCE edit in the staged tree (the six-word stmfd/ldmfd once each; the four-word form absent)"
t10_src=external/xnu-hd2-darwin13/xnu/osfmk/arm/traps_lo.s
[[ $(grep -cE '^[[:space:]]*stmfd[[:space:]]+sp!,[[:space:]]*\{r0-r3,r8,lr\}$' "$t10_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one six-word \`stmfd sp!, {r0-r3,r8,lr}\` - re-run tools/patch_d13_usvc.py"; exit 1; }
[[ $(grep -cE '^[[:space:]]*ldmfd[[:space:]]+sp!,[[:space:]]*\{r0-r3,r8,lr\}$' "$t10_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one matching six-word \`ldmfd\`"; exit 1; }
[[ $(grep -cE '^[[:space:]]*stmfd[[:space:]]+sp!,[[:space:]]*\{r0,r1,r8,lr\}' "$t10_src" || true) == 0 ]] || { echo "FAIL: the four-word \`stmfd {r0,r1,r8,lr}\` survives in the tree - the r3 clobber is back"; exit 1; }
echo "   (e) the payload constant is unchanged (29 s) and the watchdog arm site still passes 29"
grep -qE 'mov[a-z]*[[:space:]]+r0, #29' <<<"$arm_site" || { echo "FAIL: the arm site no longer passes 29"; exit 1; }
# ====================================================================================================
# THE 1003 DELTA: the stall instrument, asserted BY VALUE, with the FROZEN 1002 image
# (armed-d13-b04976fe) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing.
#
# The instrument is two src/ files (NO tree edit): the recorder `entry_note_stall_sample` in
# `src/entry/entry_stubs.c` (compiled by build_entry.sh into the payload's stub object) and ONE call
# site in `src/platform/darwin13/pe_msm8974.c` (compiled by the platform block into
# `$XNU_PLATFORM_OBJ_OUT/pe_msm8974.o`, which the entry link carries). The by-value fingerprint is
# the FIVE key-name literals `xnu_live_stall_*` (strings) plus the `entry_note_stall_sample` symbol
# in the linked image. On the frozen 1002 elf all six are ABSENT - the negative control.
# ====================================================================================================
echo "-- by value: the 1003 DELTA - the per-tick pid-1 pc sampler (STAGE90_XNU_STALL) --"
pe_obj=out/xnu_platform_obj_d13/pe_msm8974.o
# (a) the PE OBJECT carries the call site: it references the recorder by name. Read the object's own
#     undefined-symbol table (the call is to the entry image's symbol), which is what proves the
#     gated `#if STAGE90_XNU_STALL` block compiled in and the call did not optimize out.
pe_u=$(arm-none-eabi-nm "$pe_obj" 2>/dev/null | awk '($1=="U" && $2=="entry_note_stall_sample") || ($2=="U" && $3=="entry_note_stall_sample"){print "1"}')
[[ "$pe_u" == "1" ]] || { echo "FAIL: $pe_obj does not reference entry_note_stall_sample - the -DSTAGE90_XNU_STALL call site did not reach the platform compile (was STAGE90_XNU_STALL exported before build_xnu_arm_kernel.sh, and is -DSTAGE90_XNU_STALL in \$EXTRA?)"; exit 1; }
echo "   (a) the PE object ($pe_obj) references entry_note_stall_sample (U) - the gated call site compiled in"

# (b) the LINKED image carries the recorder AND the five key literals. The symbol is defined in the
#     payload; the literals are the evidence a reader will grep the NEXT capture for, so their presence
#     in THIS image is what makes that capture's keys mean this arm.
elf=out/stage90/xnu_arm_entry.elf
stall_sym=$(arm-none-eabi-nm "$elf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$stall_sym" == "1" ]] || { echo "FAIL: entry_note_stall_sample is not a defined text symbol in $elf (x$stall_sym, want 1; elf mtime $(stat -c %y "$elf" 2>/dev/null)) - the recorder did not reach the linked image"; exit 1; }
stall_keys=$(strings "$elf" | grep -c 'xnu_live_stall_' || true)
[[ "$stall_keys" == "7" ]] || { echo "FAIL: the image carries $stall_keys xnu_live_stall_* literals (want 7: _n/_t/_pc/_cpsr/_sp/_lr/_nz)"; exit 1; }
echo "   (b) the linked image defines entry_note_stall_sample (T) and carries the 7 stall key literals"

# (c) the SOURCE-side shape (there is no stage driver - both halves are src/): exactly ONE gated call
#     site in pe_msm8974.c and the recorder body present, each gated so STALL=0 changes neither file's
#     compiled bytes.
pe_src=src/platform/darwin13/pe_msm8974.c
call_sites=$(grep -c 'entry_note_stall_sample((const uint32_t \*)context);' "$pe_src" || true)
[[ "$call_sites" == "1" ]] || { echo "FAIL: $pe_src carries $call_sites call site(s) (want exactly 1)"; exit 1; }
grep -q '#if STAGE90_XNU_STALL' "$pe_src" || { echo "FAIL: $pe_src has no \`#if STAGE90_XNU_STALL\` guard - the call site is ungated"; exit 1; }
grep -qE '^entry_note_stall_sample\(const uint32_t \*frame\)' src/entry/entry_stubs.c || { echo "FAIL: the recorder body entry_note_stall_sample is missing from src/entry/entry_stubs.c"; exit 1; }
echo "   (c) the source shape: 1 gated call site in pe_msm8974.c + the recorder in entry_stubs.c"

# (d) the fresh record carries the new arm key.
[[ $(awk -F= '$1=="STAGE90_XNU_STALL"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_STALL=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (d) the fresh record carries STAGE90_XNU_STALL=1 (the new arm key this run)"

echo "-- by value: the FROZEN 1002 elf as the NEGATIVE CONTROL (it must NOT carry the 1003 change; a reader that cannot fail certifies nothing) --"
d3_ctrl=out/stage90/frozen/armed-d13-b04976fe/xnu_arm_entry.elf
if [[ -f $d3_ctrl ]]; then
  ctl_sym=$(arm-none-eabi-nm "$d3_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
  ctl_keys=$(strings "$d3_ctrl" | grep -c 'xnu_live_stall_' || true)
  [[ "$ctl_sym" == "0" && "$ctl_keys" == "0" ]] || { echo "FAIL: the frozen 1002 elf already carries the stall instrument (sym x$ctl_sym, keys x$ctl_keys) - the negative control is not a control"; exit 1; }
  echo "   frozen 1002 elf ($d3_ctrl): entry_note_stall_sample x0, xnu_live_stall_* x0 - the change is NOT in it (control holds)"
else
  echo "   (frozen 1002 elf not present at $d3_ctrl - the negative control is unavailable this run)"
fi
echo "-- 1003 build done: park with scripts/park_1003.sh --"