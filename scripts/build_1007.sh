#!/usr/bin/env bash
# Build 1007: 1006's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABT_SPAN - a pure MEASUREMENT
# rung (no kernel behaviour changes) that adds a SPAN WITNESS on the `sleh_abort` handler: it brackets
# each `__wrap_sleh_abort` call with the free-running generic timer and publishes, per abort, (a) the
# gap to the previous abort and (b) the handler's own duration - so a capture says whether the ~24.4 s
# freeze sits BETWEEN two aborts, INSIDE one abort, or AFTER the last abort. Design:
# docs/1007-design.md (§4 the change, §5 the A/B/C/D classifier, §6 the build refusals).
#
# 1006's press read (records/1006-press-read.txt) settled the frontier's PATH: the ~24.4 s
# `xnu_live_tick_sum_maxd` freeze (0x1be87a47 = 468,220,487 counts = 24.386 s at 19.2 MHz) does NOT
# overlap the idle path - `__wrap_machine_idle` is NEVER entered on the recent D13 tail (its
# entry_freeze_sample fired 0x, and the capture carries no xnu_live_freeze_* keys). The terminal frame
# (1005 pc=0x8001b48d lr=0x8046b95c cpsr=0x60000113) is `sleh_abort+0x468`, an unconditional forward
# branch - NOT a fault-return self-loop. So the freeze rides the NON-IDLE path, and 1004 saw that path
# as a WALK (~40 aborts). 1007 puts the clock on that path.
#
# A by-value defect in the INHERITED image is found and must be CLOSED by this rung (§2.3): the 1006
# sampler calls `entry_counter()` from `entry_stubs.c`, which in the linked image resolves to a
# terminal generated stand-in (`8049cce4 T entry_counter` -> `entry_stub_hit` -> `entry_epilogue`),
# NOT the timer. It was dormant only because the idle path it rides is never entered; on the abort
# path (which 1007 rides and which IS entered ~40x) it would end the run. 1007 routes the clock IN as
# an argument instead (§4.3) and the build refuses the stand-in (§6e).
#
# Originating rung (1006): 1005's switch set VERBATIM plus ONE new switch, STAGE90_XNU_FREEZE_WITNESS -
# the idle-path freeze witness (a sampler on `__wrap_machine_idle`). 1007 KEEPS it (the inheriting
# switch) and pins it alongside the new one.
#
# Originating rung (1005): 1004's switch set VERBATIM plus ONE new switch, STAGE90_XNU_CADENCE - the
# timer-independent syscall-path cadence witness + the all-interrupts counter.
#
# Originating rung (1004): 1003's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABORT_TRACE -
# a pure MEASUREMENT rung that widens the sleh (data/prefetch abort) detail caps so the TAIL of a
# fault run is published, and adds the returning frame's user pc beside each abort's fault pair.
#
# 1003's press read (records/1003-press-read.txt): pid 1's thread 0xc0bfa000 is NOT a user spin and
# NOT a passive sleep - the per-tick frame sampler showed the last samples are ONE kernel frame
# (pc=0x8001b1fd, sp=0xcfdbbf00, lr=sleh_abort+0x468), i.e. the CPU is on pid 1's kernel stack, in
# the data-abort RETURN path. Total abort entries that boot: `xnu_live_sleh_seen` reached 40 with
# `xnu_live_sleh_storm=9`, ALL on thread 0xc0bfa000. But the sleh hook's caps - SLEH_LIVE_MAX=8
# detailed entries, SLEH_LIVE_FRAMES=16 frames - publish only the FIRST 8, and those 8 show DIFFERENT
# `far` values (dyld walking FORWARD: 0x2be89fe0, 0x2be8c78f, 0x2be8a084, ...). THE HANG IS IN THE
# TAIL THE CAP HIDES.
#
# WHY THE INSTRUMENT, AND WHY *THIS* ONE. The 1003 read leaves two readings that fit its evidence:
# (a) the SAME page re-faults - each `vm_fault` returns and the user re-faults the same address (a
# validation / cs_validated-style LOOP); or (b) dyld WALKS FORWARD one page per fault (normal paging
# of the 301 MiB shared cache), which is merely slow, not stuck. The evidence leans to (a) at the very
# end (seven samples one frame) but the sleh records so far name DIFFERENT far addresses, so the early
# window may be (b) and only the tail (a). Two things separate them, and 1004 takes both:
#   * raise the caps under the NEW switch so EVERY abort of a run this size publishes its full tuple
#     (seq, type, fsr, far, thr) - the tail is no longer cut; and
#   * publish the frame's USER pc (`xnu_live_sleh_upc`) beside it - so consecutive `(far, upc)` PAIRS
#     answer loop-vs-walk directly: a `far` that repeats within a small window = LOOP (same page
#     re-faulting); a `far` that strictly advances = WALK (dyld progress, merely slow).
#
# THE SWITCH: STAGE90_XNU_ABORT_TRACE=1 (the one new switch). It reaches `entry_stubs.c`'s compile as
# `-DSTAGE90_XNU_ABORT_TRACE` via `STUB_DEFINES` (the file's whole sleh body is inside the
# `STAGE90_ENTRY_TRACE` region) - the same path the seam flags use. NO tree edit, NO stage driver and
# NO platform-object change: both halves are `src/entry/entry_stubs.c` edits, gated so
# ABORT_TRACE=0 is byte-identical. THE ENTRY BIN MUST MOVE (7669309b -> the new sha8) because the
# widened caps and the new `_upc` store change the linked entry payload.
#
# THE SWITCH SET is 1003's VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP and STALL stay 1) plus STAGE90_XNU_ABORT_TRACE=1 (the one new switch).
#
# ALL 1003 INSTRUMENTS STAY (the tick sampler/summaries, the cap-exempt pet/anomaly channels, the
# user-SVC frontier - USVC=1 - the 991 memq_hint guard, the 1001 submap pmap-enter skip, the 1002
# USVC-hook r3 save set, and 1003's per-tick stall sampler): the new expectation is that the boot
# SURVIVES to the same pid-1 fault-return frontier and the sleh tail is now PUBLISHED for every abort.
# ONE PAYLOAD edit (kept from 999/1000/1001/1002/1003): STAGE90_HW_WATCHDOG_TIMEOUT_S 29u - the loaded
# payload bytes are 999's; 1004 must not slip back to the 25 s net.
#
# 1003's headline: it sampled the interrupted frame on the tick's powers-of-two ladder and settled
# that pid 1 is in the kernel's fault-return path (KERNEL pc x14, USER pc x0), reproducing the 1002
# frontier byte for byte (last usvc pc 0x2be97328 = dyld's `_mach_absolute_time` return). 1002's
# headline (kept): the USVC hook's r3-save fix took BY VALUE (the 230x dyld abort at 0x4 is GONE).
# 1001's headline (kept): the is_submap guard in vm_map_enter's tail (SUBMAP_PMAP_SKIP). 1000's
# headline (kept): the USVC frontier hook; the tick sampler/summaries; the pet/anomaly keys.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 1007: the abort-span witness (brackets each sleh_abort call with the free-running timer -> per-abort gap + handler duration); 1006's set verbatim + ONE new switch =="
echo "-- 1006's arm was armed-d13-31bdebf3 (the ~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered - so the freeze rides the NON-IDLE abort path; see records/1006-press-read.txt); 1006's set verbatim --"

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
# 1005's NEW switch: STAGE90_XNU_CADENCE, the timer-independent cadence witness. It reaches the board
# PE's compile (`src/platform/darwin13/pe_msm8974.c`) as `-DSTAGE90_XNU_CADENCE` via
# `XNU_KERNEL_EXTRA_DEFINES` (== this `EXTRA`) - the SAME path STALL uses - because the all-interrupt
# counter and the two board accessors live in `pe_msm8974.o` (a platform object, NOT an entry
# toolchain object). The recorder + the atomic sleh `++` live in `src/entry/entry_stubs.c` and the pet
# call in `src/entry/entry_trace.c` (compiled by build_entry.sh, which reads `$CADENCE` - via its own
# `STAGE90_XNU_CADENCE` env var - for the RECORD and adds `-DSTAGE90_XNU_CADENCE` to BOTH
# `STUB_DEFINES` and `STUB_DEFINES_TRACE`). NO tree edit and NO stage driver: all halves are src/ files
# edited directly. It MUST be exported (not only in `EXTRA`) for the same reason STALL's export is -
# `build_xnu_arm_kernel.sh` gates the DEFINE of that name on the environment variable, and the platform
# block's compile (which carries the pe_msm8974.c accessors) reads it from there.
EXTRA="$EXTRA -DSTAGE90_XNU_CADENCE=1"
# 1006's NEW switch STAGE90_XNU_FREEZE_WITNESS is NOT here and is NOT exported for the pool/platform
# build: its ONLY two consumers are `entry_stubs.c` (the sampler) and `entry_trace.c` (the call site),
# both compiled by `build_entry.sh`, which reads `$FREEZE` for the RECORD and adds
# `-DSTAGE90_XNU_FREEZE_WITNESS` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. No pool object reads
# it and no platform object reads it, so unlike CADENCE it needs no `EXTRA` entry and no env export -
# the EXPORTS pin below is the only carrier. (Contrast the CADENCE comment just above, which DOES need
# both because pe_msm8974.o carries its counter.)
# 1007's NEW switch STAGE90_XNU_ABT_SPAN is NOT here and is NOT exported for the pool/platform build,
# for the SAME reason 1006's FREEZE_WITNESS is not: its ONLY two consumers are `entry_stubs.c` (the
# recorder, `entry_note_abt_span`) and `entry_trace.c` (the ONE bracket inside `__wrap_sleh_abort`),
# both compiled by `build_entry.sh`, which reads `$ABT_SPAN` for the RECORD and adds
# `-DSTAGE90_XNU_ABT_SPAN` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. No pool object reads it
# and no platform object reads it, so it needs no `EXTRA` entry and no env export - the EXPORTS pin
# below is the only carrier ([[mi4-one-value-two-definitions]]).
# 1004's NEW switch: STAGE90_XNU_ABORT_TRACE, the sleh detail-cap widening + the `xnu_live_sleh_upc`
# store. Its ONLY consumer is `src/entry/entry_stubs.c`, compiled by build_entry.sh (which reads
# `$ABORT_TRACE` for the RECORD and adds `-DSTAGE90_XNU_ABORT_TRACE` to `STUB_DEFINES`). It is NOT a
# pool switch and NOT a platform-object switch, so unlike SUBMAP_PMAP_SKIP it needs NO `EXTRA` entry
# and NO environment export here - the ENTRY build below carries it (the EXPORTS pin + the record).
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

# 1005: the NEW switch STAGE90_XNU_CADENCE. It must be exported (not only in `EXTRA`) for the same
# reason 991/1001's pool switches and 1003's STALL are: `build_xnu_arm_kernel.sh` gates the DEFINE of
# that name on the environment variable, and the platform block's compile (which carries the
# pe_msm8974.c all-interrupt counter and the two board accessors) reads it from there. Export it HERE,
# before the pool/platform build below - the SAME value reaches build_entry.sh through EXPORTS below
# ([[mi4-one-value-two-definitions]]). No assembly layer reads it, but the export keeps the three
# spellings one value.
export STAGE90_XNU_CADENCE=1

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
# 1004's NEW key STAGE90_XNU_ABORT_TRACE. The 1003 record does NOT carry it yet - the pin is the
# only carrier this run, and the record the entry build writes (asserted at the tail) is what parks
# the arm with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and
# adds it to `STUB_DEFINES` (the compile reads the SAME value).
EXPORTS+=("STAGE90_XNU_ABORT_TRACE=1")
# 1005's NEW key STAGE90_XNU_CADENCE. The 1004 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_CADENCE` to BOTH `STUB_DEFINES` (entry_stubs.c) and `STUB_DEFINES_TRACE`
# (entry_trace.c) - the same value the platform export above gave pe_msm8974.o.
EXPORTS+=("STAGE90_XNU_CADENCE=1")
# 1006's NEW key STAGE90_XNU_FREEZE_WITNESS. The 1005 record does NOT carry it yet - the pin is the
# only carrier this run, and the record the entry build writes (asserted at the tail) is what parks the
# arm with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_FREEZE_WITNESS` to BOTH `STUB_DEFINES` (entry_stubs.c, the sampler) and
# `STUB_DEFINES_TRACE` (entry_trace.c, the call site). No platform object and no pool object reads it.
EXPORTS+=("STAGE90_XNU_FREEZE_WITNESS=1")
# 1007's NEW key STAGE90_XNU_ABT_SPAN. The 1006 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_ABT_SPAN` to BOTH `STUB_DEFINES` (entry_stubs.c, the recorder `entry_note_abt_span`)
# and `STUB_DEFINES_TRACE` (entry_trace.c, the bracket inside `__wrap_sleh_abort`). No platform object
# and no pool object reads it. NOTE: 1007 KEEPS 1006's FREEZE_WITNESS=1 (the inheriting arm) - both
# are pinned.
EXPORTS+=("STAGE90_XNU_ABT_SPAN=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   1006 was 31bdebf3 - a DIFFERENT value here means the 1007 edits reached the image."
# The arm must MOVE off the PRESSED 1006 arm (the project rule: the arm IS the entry bin's sha8). A
# build that lands back on 31bdebf3 did not change the entry image - the 1007 edit did not reach the
# linked image (or one of the two entry objects was not rebuilt). This is the build-side counterpart of
# the arm-refusal guards the park/press scripts carry against the pressed arms.
_d6_arm=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $_d6_arm == armed-d13-31bdebf3 ]] && { echo "FAIL: the build produced 1006's PRESSED arm (31bdebf3) again - the entry bin did NOT move, so the 1007 edits did not reach the linked image (rebuild: build_entry.sh with STAGE90_XNU_ABT_SPAN=1 exported for BOTH STUB_DEFINES and STUB_DEFINES_TRACE); do not park a 1007 arm identical to the pressed 1006 one"; exit 1; }
[[ $_d6_arm == armed-d13-ff937225 ]] && { echo "FAIL: the build produced 1005's arm (ff937225) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-582a19c8 ]] && { echo "FAIL: the build produced 1004's arm (582a19c8) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-7669309b ]] && { echo "FAIL: the build produced 1003's arm (7669309b) - a stale layer"; exit 1; }
echo "   1007 arm: $_d6_arm (must differ from the pressed 1006 arm 31bdebf3)"
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
# 1003's delta, KEPT AS A REGRESSION (1004's set is 1003's verbatim): the per-tick stall sampler.
# ====================================================================================================
echo "-- by value: the 1003 STALL instrument (KEPT) - the recorder symbol + the 7 key literals in the image --"
pe_obj=out/xnu_platform_obj_d13/pe_msm8974.o
pe_u=$(arm-none-eabi-nm "$pe_obj" 2>/dev/null | awk '($1=="U" && $2=="entry_note_stall_sample") || ($2=="U" && $3=="entry_note_stall_sample"){print "1"}')
[[ "$pe_u" == "1" ]] || { echo "FAIL: $pe_obj does not reference entry_note_stall_sample - 1003's instrument was dropped"; exit 1; }
elf=out/stage90/xnu_arm_entry.elf
stall_sym=$(arm-none-eabi-nm "$elf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$stall_sym" == "1" ]] || { echo "FAIL: entry_note_stall_sample is not a defined text symbol in $elf (x$stall_sym, want 1) - 1003's instrument was dropped"; exit 1; }
stall_keys=$(strings "$elf" | grep -c 'xnu_live_stall_' || true)
[[ "$stall_keys" == "7" ]] || { echo "FAIL: the image carries $stall_keys xnu_live_stall_* literals (want 7)"; exit 1; }
echo "   entry_note_stall_sample (T) + xnu_live_stall_* x$stall_keys (1003 kept)"
[[ $(awk -F= '$1=="STAGE90_XNU_STALL"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_STALL=1 - 1003's switch must stay on"; exit 1; }

# ====================================================================================================
# THE 1004 DELTA: the abort trace (STAGE90_XNU_ABORT_TRACE), asserted BY VALUE, with the FROZEN 1003
# image (armed-d13-7669309b) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing.
#
# The change is ONE src/ file (NO tree edit, NO platform object): `src/entry/entry_stubs.c` widens the
# three sleh detail caps (SLEH_LIVE_MAX/_FRAMES/_SEEN 8/16/64 -> 512/512/512) and adds the frame's
# user pc as `xnu_live_sleh_upc`, both under `#if defined(STAGE90_XNU_ABORT_TRACE) &&
# STAGE90_XNU_ABORT_TRACE`. The by-value fingerprint is (i) the `xnu_live_sleh_upc` literal in the
# linked image, (ii) the widened `512` caps visible in `entry_note_sleh`'s own body, and (iii) the
# gated source shape. On the frozen 1003 elf the literal is ABSENT and the body carries the OLD 8/16/64
# caps - the negative control.
# ====================================================================================================
echo "-- by value: the 1004 DELTA - the abort trace (STAGE90_XNU_ABORT_TRACE) --"
# (a) the LINKED image carries the new `xnu_live_sleh_upc` literal - the one key a reader greps the
#     NEXT capture for, so its presence in THIS image is what makes that capture's `_upc` mean this arm.
upc_lit=$(strings "$elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$upc_lit" == "1" ]] || { echo "FAIL: the image carries $upc_lit \`xnu_live_sleh_upc\` literal(s) (want 1) - the STAGE90_XNU_ABORT_TRACE store did not reach the linked image"; exit 1; }
echo "   (a) the linked image carries the \`xnu_live_sleh_upc\` key literal (the new frame store)"

# (b) the CAPS moved: `entry_note_sleh`'s own body must compare against 512 - three times (MAX, FRAMES
#     and SEEN are all 512 with the switch on), and must NOT carry the OLD 8/16/64 comparisons that the
#     frozen 1003 body had. Read the disassembly of the function by value.
sleh_a=$(arm-none-eabi-nm "$elf" | awk '$3=="entry_note_sleh"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_note_sleh is not in the image - the abort record's body is gone"; exit 1; }
sleh_x=$(arm-none-eabi-nm -n "$elf" | awk -v s="${sleh_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
sleh_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$sleh_a" --stop-address="${sleh_x:-0xffffffff}")
cap512=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(512|0x200)|movw[[:space:]]+r[0-9]+, #(512|0x200)\b' <<<"$sleh_body" || true)
cap_old=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(8|16|64)\b' <<<"$sleh_body" || true)
echo "   (b) entry_note_sleh ($sleh_a..${sleh_x:-end}): #512 comparisons x$cap512 (want >= 3: MAX x2 (entry+back) + FRAMES + SEEN), legacy #8/#16/#64 comparisons x$cap_old (want 0)"
[[ "$cap512" -ge 3 && "$cap_old" == "0" ]] || { echo "FAIL: the caps did not move (512 x$cap512 want >= 3, legacy x$cap_old want 0) - the STAGE90_XNU_ABORT_TRACE #if did not compile the widened constants in"; exit 1; }

# (c) the SOURCE-side shape: the defined()-first guard once, the three widened definitions inside it,
#     and the gated `_upc` store - each so ABORT_TRACE=0 (the #else branch: the exact old tokens)
#     changes neither file's compiled bytes.
stub_src=src/entry/entry_stubs.c
guard_n=$(grep -cE '^#if defined\(STAGE90_XNU_ABORT_TRACE\) && STAGE90_XNU_ABORT_TRACE$' "$stub_src" || true)
echo "   (c) $stub_src: the defined()-first guard x$guard_n (want 2 - one for the caps, one for the _upc store)"
[[ "$guard_n" == "2" ]] || { echo "FAIL: $stub_src does not carry the two defined()-first \`#if defined(STAGE90_XNU_ABORT_TRACE) && STAGE90_XNU_ABORT_TRACE\` guards (a hand edit moved one, or one is missing)"; exit 1; }
[[ $(grep -cE '^#define SLEH_LIVE_MAX 512u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_FRAMES 512u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_SEEN 512u$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the three widened caps (512u) are not each defined once in $stub_src"; exit 1; }
# The OFF branch must STILL be the exact old tokens - a widened constant would break this.
[[ $(grep -cE '^#define SLEH_LIVE_MAX 8u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_FRAMES 16u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_SEEN 64u$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the ABORT_TRACE=0 branch is not the old 8/16/64 tokens - the off-arm is not byte-identical"; exit 1; }
[[ $(grep -cE 'entry_live_write\("xnu_live_sleh_upc", pc\);' "$stub_src" || true) == 1 ]] || { echo "FAIL: the \`xnu_live_sleh_upc\` store is not present exactly once in $stub_src"; exit 1; }
echo "   (c) the source shape: 2 guards + the 512u caps + the old 8u/16u/64u off-branch + the one _upc store"

# (d) the fresh record carries the new arm key.
[[ $(awk -F= '$1=="STAGE90_XNU_ABORT_TRACE"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABORT_TRACE=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (d) the fresh record carries STAGE90_XNU_ABORT_TRACE=1 (the new arm key this run)"

echo "-- by value: the FROZEN 1003 elf as the NEGATIVE CONTROL (it must NOT carry the 1004 change; a reader that cannot fail certifies nothing) --"
d4_ctrl=out/stage90/frozen/armed-d13-7669309b/xnu_arm_entry.elf
if [[ -f $d4_ctrl ]]; then
  ctl_upc=$(strings "$d4_ctrl" | grep -c '^xnu_live_sleh_upc$' || true)
  ctl_a=$(arm-none-eabi-nm "$d4_ctrl" | awk '$3=="entry_note_sleh"{print "0x" $1; found=1} END{exit(found?0:1)}')
  ctl_x=$(arm-none-eabi-nm -n "$d4_ctrl" | awk -v s="${ctl_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  ctl_body=$(arm-none-eabi-objdump -d "$d4_ctrl" --start-address="$ctl_a" --stop-address="${ctl_x:-0xffffffff}")
  ctl_512=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #512|movw[[:space:]]+r[0-9]+, #512' <<<"$ctl_body" || true)
  [[ "$ctl_upc" == "0" && "$ctl_512" == "0" ]] || { echo "FAIL: the frozen 1003 elf already carries the 1004 change (upc x$ctl_upc, #512 x$ctl_512) - the negative control is not a control"; exit 1; }
  echo "   frozen 1003 elf ($d4_ctrl): xnu_live_sleh_upc x0, #512 caps x0 - the change is NOT in it (control holds)"
else
  echo "   (frozen 1003 elf not present at $d4_ctrl - the negative control is unavailable this run)"
fi
echo "-- 1004 build done: park with scripts/park_1004.sh --"

# ====================================================================================================
# THE 1005 DELTA: the cadence witness (STAGE90_XNU_CADENCE), asserted BY VALUE, with the FROZEN 1004
# elf (armed-d13-582a19c8, the PRESSED 1004 arm) as the NEGATIVE CONTROL - a reader that cannot fail
# certifies nothing. Design: docs/1005-stall-cadence-design.md, §6.
#
# The change is THREE src/ files (NO tree edit, NO stage driver): `src/platform/darwin13/pe_msm8974.c`
# adds the all-interrupt counter + the two board accessors; `src/entry/entry_stubs.c` adds the
# syscall-path sample + the late summary + the atomic sleh `++`; `src/entry/entry_trace.c`'s pet calls
# the summary. All gated `#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE`; OFF is byte-identical.
# ====================================================================================================
echo "-- by value: the 1005 DELTA - the cadence witness (STAGE90_XNU_CADENCE) --"
pe_cad=src/platform/darwin13/pe_msm8974.c

# (a) SOURCE side, per-file (a hand edit that moved a guard would drift the count). The guard is
#     spelled defined()-first in all three files.
cad_p=$(grep -cE '^#if defined\(STAGE90_XNU_CADENCE\) && STAGE90_XNU_CADENCE$' "$pe_cad" || true)
cad_s=$(grep -cE '^#if defined\(STAGE90_XNU_CADENCE\) && STAGE90_XNU_CADENCE$' "$stub_src" || true)
cad_t=$(grep -cE '^#if defined\(STAGE90_XNU_CADENCE\) && STAGE90_XNU_CADENCE$' src/entry/entry_trace.c || true)
echo "   (a) guards: pe_msm8974.c x$cad_p (want 2 - accessors+irq-counter, the handler incr), entry_stubs.c x$cad_s (want 6 - decl, block, USVC call, exec-done call, 2 sleh), entry_trace.c x$cad_t (want 2 - decl, pet call)"
[[ "$cad_p" == "2" && "$cad_s" == "6" && "$cad_t" == "2" ]] || { echo "FAIL: the cadence guards are not one-per-region (pe x$cad_p want 2, stubs x$cad_s want 6, trace x$cad_t want 2) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -c 'entry_cadence_sample()' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_cadence_sample() is not present exactly once in $stub_src (definition + USVC call; the def carries no ())"; exit 1; }
[[ $(grep -c 'entry_cadence_summary()' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_cadence_summary() is not present exactly once in $stub_src"; exit 1; }
# The board half and the two accessors are in the platform PE source.
[[ $(grep -c 'g_msm8974_irq_count' "$pe_cad" || true) == 4 ]] || { echo "FAIL: g_msm8974_irq_count is not the 4 expected spellings (comment, decl, increment, accessor) in $pe_cad"; exit 1; }
[[ $(grep -cF 'uint32_t entry_board_tick_count(void)' "$pe_cad" || true) == 1 && $(grep -cF 'uint32_t entry_board_irq_count(void)' "$pe_cad" || true) == 1 ]] || { echo "FAIL: the two board accessors are not each defined once in $pe_cad"; exit 1; }
# The OFF branch of the sleh edits must still be the OLD token set (byte-identical arm).
[[ $(grep -cE '^[[:space:]]*g_sleh_seq\+\+;$' "$stub_src" || true) == 1 && $(grep -cE '^[[:space:]]*g_sleh_back\+\+;$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the two non-atomic \`g_sleh_seq++;\`/\`g_sleh_back++;\` off-branch tokens are not present - the CADENCE=0 arm is not byte-identical"; exit 1; }
echo "   (a) the source shape: guards one-per-region + 1 sample()/1 summary() call + the board counter + the old \`++\` off-branch"

# (b) IMAGE side (by value). Four unique cadence literals, the four symbols DEFINED (a generated
#     stand-in would show 0), and the one call site each.
cad_lit=$(strings "$elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$cad_lit" == "4" ]] || { echo "FAIL: the image carries $cad_lit \`xnu_live_cadence_\` literals (want 4: t/sync/tick/irq) - the CADENCE store did not reach the linked image"; exit 1; }
echo "   (b) the linked image carries the 4 \`xnu_live_cadence_\` key literals"
for cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $cad_sym\$" || { echo "FAIL: $cad_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) the four symbols are DEFINED T/t in the image"

# Call-site clause: entry_note_user_svc's body carries exactly one entry_cadence_sample call;
# entry_note_exec_returned and entry_wdt_pet each carry exactly one entry_cadence_summary call (the
# exec-done site is a tail call - a `b` is a valid spelling, the same reader fix as build_1004.sh:500).
cad_sa=$(arm-none-eabi-nm "$elf" | awk '$3=="entry_note_user_svc"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_note_user_svc is not in the image"; exit 1; }
cad_sx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${cad_sa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
cad_sbody=$(arm-none-eabi-objdump -d "$elf" --start-address="$cad_sa" --stop-address="${cad_sx:-0xffffffff}")
cad_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_cadence_sample>' <<<"$cad_sbody" || true)
echo "   (b) entry_note_user_svc calls entry_cadence_sample: $cad_sc (want 1)"
[[ "$cad_sc" == "1" ]] || { echo "FAIL: entry_note_user_svc does not call entry_cadence_sample exactly once (0 = the call site moved; >1 = it runs twice)"; exit 1; }
for cad_pop in entry_note_exec_returned entry_wdt_pet; do
  cad_pa=$(arm-none-eabi-nm "$elf" | awk -v s="$cad_pop" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $cad_pop not in the image"; exit 1; }
  cad_px=$(arm-none-eabi-nm -n "$elf" | awk -v s="${cad_pa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  cad_pb=$(arm-none-eabi-objdump -d "$elf" --start-address="$cad_pa" --stop-address="${cad_px:-0xffffffff}")
  cad_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_cadence_summary>' <<<"$cad_pb" || true)
  echo "   (b) $cad_pop calls entry_cadence_summary: $cad_pc (want 1)"
  [[ "$cad_pc" == "1" ]] || { echo "FAIL: $cad_pop does not call the cadence summary exactly once (0 = the call site moved; >1 = it runs twice)"; exit 1; }
done

# (b3) ATOMICITY: the sleh increments in the stub object must be INLINE ldrex/strex, NOT a
#      `bl __atomic_fetch_add_4` libcall the freestanding link may not resolve. Design §6(b3).
cad_stubobj=out/stage90/xnu_arm_entry_stubs.o
if [[ -f $cad_stubobj ]]; then
  cad_libcall=$(arm-none-eabi-objdump -d "$cad_stubobj" 2>/dev/null | grep -cE 'bl[[:space:]]+[0-9a-f]+ <__atomic_fetch_add' || true)
  cad_ldrex=$(arm-none-eabi-objdump -d "$cad_stubobj" 2>/dev/null | grep -cE '(ldrex|strex)' || true)
  echo "   (b3) the stub object: __atomic_fetch_add libcall(s) x$cad_libcall (want 0), inline ldrex/strex x$cad_ldrex (want >= 1)"
  [[ "$cad_libcall" == "0" && "$cad_ldrex" -ge 1 ]] || { echo "FAIL: the atomic sleh increments are not inline ldrex/strex (libcall x$cad_libcall, ldrex/strex x$cad_ldrex) - the §4 fallback (store the seq into the _back record) is required"; exit 1; }
else
  echo "   (b3) (stub object not present at $cad_stubobj - the atomicity reader is unavailable this run)"
fi

# (b3b) entry_trace.o must itself carry the CADENCE define (the pet's call), so the summary call is
#       not compiled out of the trace object - the §5.3 STUB_DEFINES_TRACE half.
cad_traceobj=out/stage90/xnu_arm_entry_trace.o
if [[ -f $cad_traceobj ]]; then
  cad_tu=$(arm-none-eabi-nm "$cad_traceobj" 2>/dev/null | grep -cE '(U|w) entry_cadence_summary' || true)
  echo "   (b3b) the trace object references entry_cadence_summary: $cad_tu (want 1)"
  [[ "$cad_tu" == "1" ]] || { echo "FAIL: $cad_traceobj does not reference entry_cadence_summary - the STUB_DEFINES_TRACE half did not reach entry_trace.c"; exit 1; }
else
  echo "   (b3b) (trace object not present at $cad_traceobj - the reader is unavailable this run)"
fi

# (d) the POOL/platform object: pe_msm8974.o must define the two accessors and reference nothing new
#     that did not resolve (a stand-in would not be a `T`).
if [[ -f $pe_obj ]]; then
  for cad_bsym in entry_board_tick_count entry_board_irq_count; do
    arm-none-eabi-nm "$pe_obj" | grep -E "^[0-9a-f]+ [Tt] $cad_bsym\$" || { echo "FAIL: $pe_obj does not DEFINE $cad_bsym - the platform block was not rebuilt with -DSTAGE90_XNU_CADENCE (export the switch before build_xnu_arm_kernel.sh)"; exit 1; }
  done
  echo "   (d) $pe_obj defines entry_board_tick_count + entry_board_irq_count"
else
  echo "   (d) (platform object not present at $pe_obj - the reader is unavailable this run)"
fi

# (e) the fresh record carries the new arm key, and the preflight gate names it (else the converse
#     check refuses the press - exactly how 1004's first press preflight failed).
[[ $(awk -F= '$1=="STAGE90_XNU_CADENCE"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_CADENCE=1 - the new arm key was not recorded this run"; exit 1; }
echo "   (e) the fresh record carries STAGE90_XNU_CADENCE=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_CADENCE' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_CADENCE is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (e) scripts/preflight_boot_check.sh names STAGE90_XNU_CADENCE (the gate will not refuse the press)"

echo "-- by value: the FROZEN 1004 elf (armed-d13-582a19c8, the PRESSED 1004 arm) as the NEGATIVE CONTROL (it must NOT carry the 1005 change; a reader that cannot fail certifies nothing) --"
d5_ctrl=out/stage90/frozen/armed-d13-582a19c8/xnu_arm_entry.elf
if [[ -f $d5_ctrl ]]; then
  ctl_cad=$(strings "$d5_ctrl" | grep -c 'xnu_live_cadence_' || true)
  ctl_cadsym=$(arm-none-eabi-nm "$d5_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_cadence_(sample|summary)$' || true)
  ctl_ldrex=$(arm-none-eabi-objdump -d "$d5_ctrl" 2>/dev/null | grep -cE '(ldrex|strex)' || true)
  [[ "$ctl_cad" == "0" && "$ctl_cadsym" == "0" ]] || { echo "FAIL: the frozen 1004 elf already carries the 1005 cadence change (literals x$ctl_cad, symbols x$ctl_cadsym) - the negative control is not a control"; exit 1; }
  echo "   frozen 1004 elf ($d5_ctrl): xnu_live_cadence_ x0, entry_cadence_{sample,summary} x0 (ldrex/strex x$ctl_ldrex) - the change is NOT in it (control holds)"
else
  echo "   (frozen 1004 elf not present at $d5_ctrl - the negative control is unavailable this run)"
fi

# ====================================================================================================
# THE 1006 DELTA: the idle-path freeze witness (STAGE90_XNU_FREEZE_WITNESS), asserted BY VALUE, with
# the FROZEN 1005 elf (armed-d13-ff937225, the PRESSED 1005 arm) as the NEGATIVE CONTROL - a reader
# that cannot fail certifies nothing. Design: docs/1006-interrupt-freeze-witness-design.md, §4/§8.
#
# The change is FOUR src/ files (NO tree edit, NO stage driver, NO platform object): `entry_stubs.c`
# adds the sampler `entry_freeze_sample` (+ externs entry_counter/entry_gic_freeze_state); `entry_trace.c`
# declares it and calls it at `__wrap_machine_idle`'s head; `entry_irq.c` defines the GIC-state helper
# `entry_gic_freeze_state`; `entry_gic.h` declares it. All gated `#if defined(STAGE90_XNU_FREEZE_WITNESS)
# && STAGE90_XNU_FREEZE_WITNESS`; OFF is byte-identical.
# ====================================================================================================
echo "-- by value: the 1006 DELTA - the idle-path freeze witness (STAGE90_XNU_FREEZE_WITNESS) --"

# (a) SOURCE side, per-file: the defined()-first guard, once per gated region.
fw_stub=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' "$stub_src" || true)
fw_trace=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' src/entry/entry_trace.c || true)
fw_irq=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' src/entry/entry_irq.c || true)
fw_gich=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' src/entry/entry_gic.h || true)
echo "   (a) guards: entry_stubs.c x$fw_stub (want 1), entry_trace.c x$fw_trace (want 2 - decl, call site), entry_irq.c x$fw_irq (want 1 - the helper), entry_gic.h x$fw_gich (want 1 - the decl)"
[[ "$fw_stub" == "1" && "$fw_trace" == "2" && "$fw_irq" == "1" && "$fw_gich" == "1" ]] || { echo "FAIL: the freeze guards are not one-per-region (stubs x$fw_stub want 1, trace x$fw_trace want 2, irq x$fw_irq want 1, gic.h x$fw_gich want 1) - a hand edit moved or dropped one"; exit 1; }
# The sampler is defined once; the helper is declared once in the header; the call site is one line.
# **1007 §4.3 changed the sampler's SIGNATURE**: it now TAKES the clock by value
# (`entry_freeze_sample(uint32_t now)`), because a call to `entry_counter()` from `entry_stubs.c`
# resolves to the terminal stand-in (§2.3). The caller passes it: `entry_freeze_sample(entry_counter())`.
[[ $(grep -cF 'entry_freeze_sample(uint32_t now)' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_freeze_sample(uint32_t now) is not present exactly once in $stub_src (the §4.3 definition taking the clock by value)"; exit 1; }
[[ $(grep -cF 'entry_freeze_sample(entry_counter());' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the entry_freeze_sample(entry_counter()) CALL is not present exactly once in entry_trace.c (the §4.3 caller must pass the clock)"; exit 1; }
[[ $(grep -c 'void entry_gic_freeze_state(uint32_t \*out);' src/entry/entry_gic.h || true) == 1 ]] || { echo "FAIL: entry_gic_freeze_state is not declared once in entry_gic.h"; exit 1; }
[[ $(grep -c 'entry_gic_freeze_state(uint32_t \*out)' src/entry/entry_irq.c || true) == 1 ]] || { echo "FAIL: entry_gic_freeze_state is not defined once in entry_irq.c"; exit 1; }
# The threshold constant is the published ordinal (design §7) - a drift here moves the reading.
[[ $(grep -c '^#define MSM8974_FREEZE_MIN 0x00004b00u$' "$stub_src" || true) == 1 ]] || { echo "FAIL: MSM8974_FREEZE_MIN is not the published 0x00004b00u in $stub_src"; exit 1; }
echo "   (a) the source shape: guards one-per-region + 1 sample def / 1 call / 1 helper decl+def + the threshold constant"

# (b) IMAGE side (by value). Eight literals: six freeze (n/d/cpsr/ien/pend/ctlr) + two idle baseline;
#     the two symbols DEFINED (a generated stand-in would show 0); the call site inside __wrap_machine_idle.
fw_lit=$(strings "$elf" | grep -c 'xnu_live_freeze_' || true)
[[ "$fw_lit" == "6" ]] || { echo "FAIL: the image carries $fw_lit \`xnu_live_freeze_\` literals (want 6: n/d/cpsr/ien/pend/ctlr) - the store did not reach the linked image"; exit 1; }
fw_idle=$(strings "$elf" | grep -c 'xnu_live_frzbase_' || true)
[[ "$fw_idle" == "2" ]] || { echo "FAIL: the image carries $fw_idle \`xnu_live_frzbase_\` literals (want 2: n/d, the unfiltered baseline)"; exit 1; }
echo "   (b) the linked image carries the 6 \`xnu_live_freeze_\` + 2 \`xnu_live_frzbase_\` key literals"
for fw_sym in entry_freeze_sample entry_gic_freeze_state; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $fw_sym\$" || { echo "FAIL: $fw_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) the two symbols are DEFINED T/t in the image"

# (c) the call site is inside __wrap_machine_idle, exactly once image-wide.
fw_mi=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_machine_idle"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_machine_idle is not in the image"; exit 1; }
fw_mix=$(arm-none-eabi-nm -n "$elf" | awk -v s="${fw_mi#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
fw_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$fw_mi" --stop-address="${fw_mix:-0xffffffff}")
fw_calls=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' <<<"$fw_body" || true)
fw_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' || true)
echo "   (c) __wrap_machine_idle calls entry_freeze_sample: $fw_calls (want 1); image-wide: $fw_all (want 1)"
[[ "$fw_calls" == "1" && "$fw_all" == "1" ]] || { echo "FAIL: entry_freeze_sample is not called exactly once, inside __wrap_machine_idle (in-wrapper x$fw_calls, image-wide x$fw_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }

# (d) the OBJECTS reference what the design says, and the platform object is UNCHANGED by 1006/1007.
#     **CHANGED AT 1007 (§4.3).**  1006's sampler called `entry_counter()` from entry_stubs.c, which the
#     standalone stub generator had emitted as a TERMINAL stand-in (the image's `8049cce4 T
#     entry_counter` -> `entry_stub_hit` -> `entry_epilogue`), so entry_stubs.o carried `U
#     entry_counter`.  1007 gives the sampler the clock as an ARGUMENT, so entry_stubs.o must now have
#     NO `entry_counter` reference at all (the §6e landmine clause asserts this by value below); the GIC
#     helper extern stays.
fw_so=out/stage90/xnu_arm_entry_stubs.o
if [[ -f $fw_so ]]; then
  fw_o_ec=$(arm-none-eabi-nm "$fw_so" 2>/dev/null | grep -cE '(U|w) entry_counter' || true)
  fw_o_gic=$(arm-none-eabi-nm "$fw_so" 2>/dev/null | grep -cE '(U|w) entry_gic_freeze_state' || true)
  echo "   (d) entry_stubs.o references entry_counter: $fw_o_ec (want 0 - 1007's §4.3 argument-passing fix), entry_gic_freeze_state: $fw_o_gic (want 1)"
  [[ "$fw_o_ec" == "0" && "$fw_o_gic" == "1" ]] || { echo "FAIL: entry_stubs.o still references entry_counter (x$fw_o_ec, want 0) - the §4.3 argument-passing fix did not reach the sampler, and the terminal stand-in would end the run on the abort path; or entry_gic_freeze_state (x$fw_o_gic, want 1) is gone - a stale object"; exit 1; }
else
  echo "   (d) (entry_stubs.o not present at $fw_so - the reader is unavailable this run)"
fi

# (e) the fresh record carries the new arm key, and the preflight gate names it (else the converse
#     check refuses the press - exactly how 1004's/1005's first press preflight failed).
[[ $(awk -F= '$1=="STAGE90_XNU_FREEZE_WITNESS"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - the new arm key was not recorded this run"; exit 1; }
echo "   (e) the fresh record carries STAGE90_XNU_FREEZE_WITNESS=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_FREEZE_WITNESS' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_FREEZE_WITNESS is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (e) scripts/preflight_boot_check.sh names STAGE90_XNU_FREEZE_WITNESS (the gate will not refuse the press)"

echo "-- by value: the FROZEN 1005 elf (armed-d13-ff937225, the PRESSED 1005 arm) as the NEGATIVE CONTROL (it must NOT carry the 1006 change; a reader that cannot fail certifies nothing) --"
d6_ctrl=out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf
if [[ -f $d6_ctrl ]]; then
  ctl_fw=$(strings "$d6_ctrl" | grep -c 'xnu_live_freeze_' || true)
  ctl_fwsym=$(arm-none-eabi-nm "$d6_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_freeze_sample|entry_gic_freeze_state)$' || true)
  [[ "$ctl_fw" == "0" && "$ctl_fwsym" == "0" ]] || { echo "FAIL: the frozen 1005 elf already carries the 1006 freeze change (literals x$ctl_fw, symbols x$ctl_fwsym) - the negative control is not a control"; exit 1; }
  echo "   frozen 1005 elf ($d6_ctrl): xnu_live_freeze_ x0, entry_freeze_sample/entry_gic_freeze_state x0 - the change is NOT in it (control holds)"
else
  echo "   (frozen 1005 elf not present at $d6_ctrl - the negative control is unavailable this run)"
fi

# ====================================================================================================
# THE 1007 DELTA: the abort-span witness (STAGE90_XNU_ABT_SPAN), asserted BY VALUE, with the FROZEN
# 1006 elf (armed-d13-31bdebf3, the PRESSED 1006 arm) as the NEGATIVE CONTROL - a reader that cannot
# fail certifies nothing. Design: docs/1007-design.md (§4 the change, §5 the A/B/C/D classifier, §6 the
# refusals (a)-(h)).
#
# The change is TWO src/ files (NO tree edit, NO stage driver, NO platform object): `entry_stubs.c`
# adds the recorder `entry_note_abt_span(uint32_t t0, uint32_t call)` (it takes the two clock VALUES as
# arguments - it never calls `entry_counter()`); `entry_trace.c` DECLARES it and BRACKETS the
# `__real_sleh_abort` call inside `__wrap_sleh_abort` with the file's own `static inline
# entry_counter()`. All gated `#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN`; OFF is
# byte-identical. 1007 ALSO carries §4.3 (the `entry_freeze_sample(uint32_t now)` argument-passing fix),
# so the OFF image is NOT any pressed arm - the honest OFF control is the symbol-level one (§6d).
# ====================================================================================================
echo "-- by value: the 1007 DELTA - the abort-span witness (STAGE90_XNU_ABT_SPAN) --"

# (a) SOURCE SHAPE, PER FILE - the `defined()`-first guard count, the recorder defined once, the call
#     one line.  A hand edit that moved a guard would drift the count.
ab_sp=$(grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$' "$stub_src" || true)
ab_tr=$(grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$' src/entry/entry_trace.c || true)
# entry_trace.c carries FOUR guards, not the design's two: the `t0` DECLARATION and its ASSIGNMENT sit
# in different places, and each must be guarded on its own (an unguarded `uint32_t t0;` is an unused
# local to `-Wall -Wextra -Werror` in the OFF arm, `build_entry.sh:2089`). The measured count is 4.
echo "   (a) guards: entry_stubs.c x$ab_sp (want 1 - the recorder), entry_trace.c x$ab_tr (want 4 - decl, t0 decl, clock read, note call)"
[[ "$ab_sp" == "1" && "$ab_tr" == "4" ]] || { echo "FAIL: the ABT_SPAN guards are not one-per-region (stubs x$ab_sp want 1, trace x$ab_tr want 4: decl + t0 decl + clock read + note call) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -cF 'entry_note_abt_span(uint32_t t0, uint32_t call)' "$stub_src" || true) == 1 ]] || { echo "FAIL: the recorder `entry_note_abt_span(uint32_t t0, uint32_t call)` is not DEFINED exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_span(t0, entry_counter() - t0);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the ONE bracket call `entry_note_abt_span(t0, entry_counter() - t0);` is not present exactly once in entry_trace.c"; exit 1; }
# §4.3 (the companion fix that closes the §2.3 defect): the 1006 sampler must take the clock as an
# argument, NOT call the stubbed symbol.
[[ $(grep -cF 'entry_freeze_sample(uint32_t now)' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_freeze_sample is not the §4.3 argument-passing shape `entry_freeze_sample(uint32_t now)` in $stub_src - the sampler still calls entry_counter and the terminal stand-in would end the run"; exit 1; }
[[ $(grep -cF 'entry_freeze_sample(entry_counter());' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: `entry_freeze_sample(entry_counter());` is not present exactly once in entry_trace.c - the §4.3 caller does not pass the clock"; exit 1; }
echo "   (a) the source shape: 1+4 guards + 1 recorder def + 1 bracket call + the §4.3 argument-passing fix"

# (b) IMAGE KEYS - the six `xnu_live_abt_` literals (n/seq/gap/call/maxcall/maxgap), and the recorder a
#     DEFINED T/t (0 => an object did not rebuild or a stand-in was used).
ab_lit=$(strings "$elf" | grep -c 'xnu_live_abt_' || true)
[[ "$ab_lit" == "6" ]] || { echo "FAIL: the image carries $ab_lit \`xnu_live_abt_\` literals (want 6: n/seq/gap/call/maxcall/maxgap) - the ABT_SPAN store did not reach the linked image"; exit 1; }
echo "   (b) the linked image carries the 6 \`xnu_live_abt_\` key literals"
arm-none-eabi-nm "$elf" | grep -E '^[0-9a-f]+ [Tt] entry_note_abt_span$' || { echo "FAIL: entry_note_abt_span is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
echo "   (b) entry_note_abt_span is DEFINED T/t in the image"

# (c) CALL SITE - `__wrap_sleh_abort`'s own body carries exactly one branch to `entry_note_abt_span`,
#     and image-wide exactly one ([[mi4-hooks-live-at-calls]]).
#     **MEASURED, not assumed (1007 first build):** the compiler turns the last statement into a TAIL
#     CALL - `b <entry_note_abt_span>`, a plain `b`, not a `bl` - because the recorder is `void` and the
#     wrapper returns its value. The clause must accept `b` OR `bl`; a `bl`-only reader false-refuses a
#     correct image (the 1004 tail-call reader fix, `build_1004.sh:500`, again).
ab_wa=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_sleh_abort is not in the image - the abort handler the witness rides is gone"; exit 1; }
# NO early `exit` in the next-symbol awk (998's measured SIGPIPE/141 defect under `set -o pipefail`).
ab_wx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${ab_wa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
ab_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$ab_wa" --stop-address="${ab_wx:-0xffffffff}")
ab_in=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$ab_body" || true)
ab_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' || true)
echo "   (c) __wrap_sleh_abort ($ab_wa..${ab_wx:-end}) calls entry_note_abt_span: $ab_in (want 1); image-wide: $ab_all (want 1)"
[[ "$ab_in" == "1" && "$ab_all" == "1" ]] || { echo "FAIL: entry_note_abt_span is not called exactly once, inside __wrap_sleh_abort (in-wrapper x$ab_in, image-wide x$ab_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }
echo "   the bracket, by value:"
grep -B4 -A2 -E '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$ab_body" | sed 's/^/       /'

# (d) OFF CONTROL, BY VALUE - a rebuild with STAGE90_XNU_ABT_SPAN=0 carries `xnu_live_abt_` x0 and no
#     `entry_note_abt_span` symbol (the switch truly compiles out).  NOTE (design §6d): 1007 also
#     carries the §4.3 fix, so the OFF image is NOT any pressed arm; the honest OFF control is this
#     SYMBOL-LEVEL one.
#
#     **SKIPPED BY DEFAULT** - it costs a second full entry build.  Set
#     `STAGE90_1007_OFF_CONTROL=1` to run it: rebuild into a scratch OUT dir with ABT_SPAN=0 (the OFF
#     arm) and assert the two absences on THAT image, WITHOUT disturbing out/stage90.
if [[ "${STAGE90_1007_OFF_CONTROL:-0}" == "1" ]]; then
  echo "-- by value: the OFF CONTROL (STAGE90_XNU_ABT_SPAN=0 twin rebuild; the switch must compile out) --"
  # The switch's ONLY consumers are the two entry-toolchain objects (entry_stubs.c, entry_trace.c);
  # no pool/platform object reads it, so the OFF image is the SAME (already built) pool linked with the
  # entry objects rebuilt OFF.  build_entry.sh hardcodes OUT to REPO_ROOT/out/stage90, so the twin build
  # SWAPS that dir for an empty scratch, runs the entry build with ABT_SPAN=0, inspects the scratch elf,
  # then restores the ON out/stage90 in ALL cases (EXIT/INT trap).  **This touches live shared state
  # ([[mi4-out-stage90-is-shared-live-state]]): get a window first; do NOT run it while a press preflight
  # is reading out/stage90.**  The OFF arm is the operator's call (the switch is OFF by default).
  if [[ -e out/stage90-off-save ]]; then echo "FAIL: out/stage90-off-save already exists - a prior OFF run did not clean up"; exit 1; fi
  mv out/stage90 out/stage90-off-save
  mkdir -p out/stage90
  ab_restore() { rm -rf out/stage90; mv out/stage90-off-save out/stage90; }
  trap ab_restore EXIT INT TERM
  _ab_ok=0
  ( cd src/entry && env -i PATH="$PATH" HOME="$HOME" \
      STAGE90_XNU_ABT_SPAN=0 STAGE90_XNU_FREEZE_WITNESS=1 \
      "${EXPORTS[@]}" ./build_entry.sh ) && _ab_ok=1 || _ab_ok=0
  _ab_offelf=out/stage90/xnu_arm_entry.elf
  if [[ "$_ab_ok" == "1" && -f "$_ab_offelf" ]]; then
    off_lit=$(strings "$_ab_offelf" | grep -c 'xnu_live_abt_' || true)
    off_sym=$(arm-none-eabi-nm "$_ab_offelf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_span$' || true)
    echo "   (d) OFF image: xnu_live_abt_ x$off_lit (want 0), entry_note_abt_span x$off_sym (want 0)"
    ab_restore; trap - EXIT INT TERM
    [[ "$off_lit" == "0" && "$off_sym" == "0" ]] || { echo "FAIL: the OFF image still carries the ABT_SPAN change (literals x$off_lit, symbol x$off_sym) - the switch did NOT truly compile out"; exit 1; }
    echo "   (d) the switch COMPILES OUT (OFF image carries no abt keys and no recorder symbol)"
  else
    ab_restore; trap - EXIT INT TERM
    echo "   (d) OFF control: the twin build was requested but FAILED (rc=$_ab_ok) - out/stage90 was restored; investigate before trusting the OFF clause"
    [[ "${STAGE90_1007_OFF_CONTROL_HARD:-0}" == "1" ]] && exit 1
  fi
else
  echo "   (d) OFF control: SKIPPED (set STAGE90_1007_OFF_CONTROL=1 to run the twin build) - the OFF"
  echo "       shape is asserted by the SOURCE guard (a) and demonstrated by the frozen 1006 elf (f)"
fi

# (e) THE LANDMINE CLAUSE - the terminal `entry_counter` stand-in must be GONE, forcing §4.3.  The
#     standalone stub generator emits `void entry_counter(void) { entry_stub_hit(...) }` into
#     realstubs.c ONLY when entry_stubs.o has an undefined `entry_counter`; so BOTH must be absent.
if [[ -f out/stage90/xnu_arm_entry_realstubs.c ]]; then
  lm_gen=$(grep -cE '^void entry_counter\(void\)' out/stage90/xnu_arm_entry_realstubs.c || true)
  echo "   (e) realstubs.c: \`^void entry_counter(void)\` lines: $lm_gen (want 0)"
  [[ "$lm_gen" == "0" ]] || { echo "FAIL: the terminal entry_counter STAND-IN is still generated in realstubs.c - entry_stubs.c still references entry_counter; apply the §4.3 argument-passing fix"; exit 1; }
else
  echo "   (e) (realstubs.c not present at out/stage90/xnu_arm_entry_realstubs.c - the landmine reader is unavailable this run)"
fi
if [[ -f out/stage90/xnu_arm_entry_stubs.o ]]; then
  lm_u=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o 2>/dev/null | grep -cE '^[[:space:]]*U entry_counter$' || true)
  echo "   (e) out/stage90/xnu_arm_entry_stubs.o: \`U entry_counter\` lines: $lm_u (want 0)"
  [[ "$lm_u" == "0" ]] || { echo "FAIL: entry_stubs.o still carries an undefined \`U entry_counter\` - the §2.3 landmine: on the abort path (which 1007 rides ~40x) the terminal stand-in would end the run at entry_stub_hit -> entry_epilogue"; exit 1; }
else
  echo "   (e) (entry_stubs.o not present - the landmine reader is unavailable this run)"
fi

# (f) NEGATIVE CONTROL against the FROZEN 1006 elf (the PRESSED 1006 arm): `xnu_live_abt_` x0 AND no
#     `entry_note_abt_span` symbol.  By value the 1006 elf DOES contain the `entry_counter` stand-in at
#     8049cce4 - this control also demonstrates the §2.3 defect 1007 closes.
d7_ctrl=out/stage90/frozen/armed-d13-31bdebf3/xnu_arm_entry.elf
if [[ -f $d7_ctrl ]]; then
  ctl_ab=$(strings "$d7_ctrl" | grep -c 'xnu_live_abt_' || true)
  ctl_absym=$(arm-none-eabi-nm "$d7_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_span$' || true)
  [[ "$ctl_ab" == "0" && "$ctl_absym" == "0" ]] || { echo "FAIL: the frozen 1006 elf already carries the 1007 change (xnu_live_abt_ x$ctl_ab, entry_note_abt_span x$ctl_absym) - the negative control is not a control"; exit 1; }
  _ab_ctr_ec=$(arm-none-eabi-nm "$d7_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_counter$' || true)
  echo "   frozen 1006 elf ($d7_ctrl): xnu_live_abt_ x$ctl_ab, entry_note_abt_span x$ctl_absym - the change is NOT in it (control holds)"
  echo "   frozen 1006 elf carries the §2.3 defect BY VALUE: entry_counter stand-in x$_ab_ctr_ec (want >= 1 - the terminal stand-in 1007 closes)"
else
  echo "   (frozen 1006 elf not present at $d7_ctrl - the negative control is unavailable this run)"
fi

# (g) RECORD + GATE - the fresh record carries the new arm key once, and the preflight gate names it
#     (else the converse check at preflight_boot_check.sh:1172 refuses the press).
[[ $(awk -F= '$1=="STAGE90_XNU_ABT_SPAN"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABT_SPAN=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (g) the fresh record carries STAGE90_XNU_ABT_SPAN=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_ABT_SPAN' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_ABT_SPAN is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (g) scripts/preflight_boot_check.sh names STAGE90_XNU_ABT_SPAN (the gate will not refuse the press)"

echo "-- 1007 build done: park with scripts/park_1007.sh --"