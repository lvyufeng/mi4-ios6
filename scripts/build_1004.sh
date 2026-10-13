#!/usr/bin/env bash
# ====================================================================================================
# SKELETON - build_1004.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/build_1003.sh.
#
# 1004 = 1003's switch set VERBATIM plus ONE new switch, STAGE90_XNU_USER_PC - a pure MEASUREMENT
# rung (no kernel behaviour change) that GENERALISES the 1003 per-tick sampler from a fixed
# interrupted-frame sample into a per-tick pid-1 FRAME instrument: it samples the interrupted pid-1
# frame's user pc AND cpsr on the tick's powers-of-two ladder, so the SPIN-vs-ASLEEP question 1003
# asked can be read as a CHANGING pc (spinning in a named function) vs a KERNEL cpsr (asleep/blocked).
#
# The switch reaches the tree exactly as 1003's STALL did: -DSTAGE90_XNU_USER_PC via
# XNU_KERNEL_EXTRA_DEFINES (== $EXTRA) for the platform candidate (B), and via the build_entry EXPORTS
# for the entry-image candidate (D).  It is exported in BOTH places so the two candidate call sites
# disagree with neither layer ([[mi4-one-value-two-definitions]]).
#
# ----------------------------------------------------------------------------------------------------
# PLACEHOLDER / TODO TOKENS used below (replace each when the 1004 design lands).  Search for
# "1004-TODO".  NOTHING below is a committed arm string:
#   * __1004_RECORDER__  = `entry_note_user_pc`  - the PROPOSED recorder symbol (mirrors 1003's
#     `entry_note_stall_sample`).  The real name is chosen by the 1004 implementer.
#   * __1004_KEYS__      = xnu_live_user_pc / _n / _nz / _cpsr (4 keys; given by the task).  The
#     reader counts 4 (the bare `xnu_live_user_pc` is a prefix of all four).
#   * __1004_SITE_D__    = src/entry/entry_trace.c, near the Idle_load_context wrapper (~l.3040-3080),
#     on the idle pass-count ladder, reading the raw interrupted frame (safe, always-run).  UNWRITTEN.
#   * __1004_SITE_B__    = src/platform/darwin13/pe_msm8974.c, in the tick handler, gated on the
#     thread being pid 1, reading the syscall frame's user pc/cpsr.  UNWRITTEN.
#   * __1004_PRIOR_ARM__ = armed-d13-7669309b  - the 1003 arm (parked: records/revert-set.txt), the
#     FROZEN negative control and the arm this build must MOVE off.  Real value, read from the record.
# ----------------------------------------------------------------------------------------------------
#
# 1003's headline (the baseline this rung builds on): 1003's stall sampler published
# `xnu_live_stall_*` on the tick's powers-of-two ladder to settle BLOCKED vs SPINNING after the
# frontier the 1002 press read reached (a user-side STALL after 22 SVCs, arm armed-d13-b04976fe).
# 1004 keeps EVERY 1003 instrument (its switch set verbatim, STAGE90_XNU_STALL=1 included) and ADDS
# the generalised pid-1 frame instrument under STAGE90_XNU_USER_PC.
#
# 1002's headline (kept as a regression): the USVC hook's save set gained r3 (`{r0,r1,r8,lr}` ->
# `{r0-r3,r8,lr}`); the 230x dyld `usermode data abort at 0x4` is GONE.
# 1001's headline (kept): the is_submap guard in vm_map_enter's pmap-enter tail (SUBMAP_PMAP_SKIP).
# 1000's headline (kept): the USVC frontier hook; the tick sampler/summaries; the pet/anomaly keys.
#
# THE SWITCH SET is 1003's VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP and STALL stay 1) plus STAGE90_XNU_USER_PC=1 (the one new switch).  ONE PAYLOAD
# edit is kept from 999..1003: STAGE90_HW_WATCHDOG_TIMEOUT_S 29u.
# THE ENTRY BIN MUST MOVE (7669309b -> the new sha8): the call site reaches a src/ file the entry
# link carries (candidate (D) is compiled INTO the entry image; candidate (B)'s pe_msm8974.o is
# carried by the entry link).  build_entry.sh only LINKS the pool, so the layer carrying the change
# must be rebuilt.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 1004: the PER-TICK PID-1 FRAME INSTRUMENT - a generalised sampler of the interrupted pid-1 frame (user pc + cpsr), switch STAGE90_XNU_USER_PC; 1003's set verbatim + ONE new switch =="
echo "-- 1003's arm was armed-d13-7669309b (the per-tick stall sampler, STAGE90_XNU_STALL); 1004 keeps it and adds the pid-1 frame instrument --"

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.  (Unchanged from 1003.)
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS \
         STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# The pins keep the layers from ever disagreeing with each other or with the ENV exports below
# ([[mi4-one-value-two-definitions]]).  These are 1003's pins, KEPT VERBATIM.
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_PROBE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_WIRE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_GEOM=1"
# 1001's switch: `vm_map.c` IS a pool file, so it rides BOTH `EXTRA` and the environment export.
EXTRA="$EXTRA -DSTAGE90_XNU_SUBMAP_PMAP_SKIP=1"
# 1003's switch STAGE90_XNU_STALL, KEPT (1004's set is 1003's verbatim).  It reaches the board PE's
# compile (`src/platform/darwin13/pe_msm8974.c`) via XNU_KERNEL_EXTRA_DEFINES.
EXTRA="$EXTRA -DSTAGE90_XNU_STALL=1"
# ----------------------------------------------------------------------------------------------------
# 1004-TODO: 1004's NEW switch STAGE90_XNU_USER_PC, the generalised pid-1 frame instrument.  It rides
# XNU_KERNEL_EXTRA_DEFINES for the platform candidate (B) exactly as STALL did; the entry candidate (D)
# reads it from the build_entry EXPORTS below.  The recorder half is PROPOSED to live in
# `src/entry/entry_stubs.c` (__1004_RECORDER__ = `entry_note_user_pc`), mirroring 1003's
# `entry_note_stall_sample`.  NO tree edit and NO stage driver: both halves are src/ files.
EXTRA="$EXTRA -DSTAGE90_XNU_USER_PC=1"
# ----------------------------------------------------------------------------------------------------
# 1000's USVC adds NO key HERE: it is read by the ASSEMBLY layer (ASFLAGS) and by build_entry.sh (the
# env EXPORTS below), and NO pool C file reads it.
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all TWELVE idempotent - IDENTICAL to 1003's list; 1004 adds NONE).
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
# 1004 adds NO stage driver, exactly like 1003.  The two halves of the pid-1 frame instrument are
# src/ files edited directly (the recorder in `src/entry/entry_stubs.c` and the ONE call site at
# whichever of candidate (D)/(B) the design picks).  No tree file outside src/ is touched.

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
export STAGE90_XNU_SP_ALIGN=1
# 991: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE on the environment variable).
export STAGE90_XNU_VMHINT_GUARD=1
# 1001: the POOL reads this one the same way (vm_map.c is a pool file).
export STAGE90_XNU_SUBMAP_PMAP_SKIP=1
# 992/993: same rule for the commpage alias (all pool files).
export STAGE90_XNU_COMMPAGE_ALIAS=1
# 994: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_IOS7_TRAPS=$IOS7_TRAPS into traps_lo.o.
export STAGE90_XNU_IOS7_TRAPS=1
# 995: the POOL reads this one (`trap.c` rides the pool).
export STAGE90_XNU_DYLD_WITNESS=1
# 996/999: same rule for the shared-region probe (`bsd/vm/vm_unix.c`, a pool file).
export STAGE90_XNU_SHREG_PROBE=1
# 997: same rule for the 32 B shared-region wire (the SAME pool file).
export STAGE90_XNU_SHREG_WIRE=1
# 998: same rule for the shared-region geometry + stack top.
export STAGE90_XNU_SHREG_GEOM=1
# 999 (amendment #2): the ASSEMBLY layer reads this (the timer-discriminator edge hooks); the SAME
# value reaches build_entry.sh through EXPORTS below.
export STAGE90_XNU_RESIDENT=1
# 1000: the ASSEMBLY layer reads this (the user-SVC frontier hook in fleh_swi); the SAME value reaches
# build_entry.sh through EXPORTS below.
export STAGE90_XNU_USVC=1
# 1003: the switch STAGE90_XNU_STALL, KEPT.  Exported (not only in `EXTRA`) because
# `build_xnu_arm_kernel.sh` gates the platform define on the environment variable.
export STAGE90_XNU_STALL=1
# ----------------------------------------------------------------------------------------------------
# 1004-TODO: 1004's NEW switch STAGE90_XNU_USER_PC.  MUST be exported BEFORE build_xnu_arm_kernel.sh
# runs (the platform block's compile, candidate (B), reads it from the environment) AND before
# assemble_arm_layer.sh only if an ASFLAGS half is used (none is expected).  The SAME value reaches
# build_entry.sh through the EXPORTS below - the entry-image candidate (D) is compiled in there
# ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_USER_PC=1
# ----------------------------------------------------------------------------------------------------

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 999+am2: machine_routines_asm.o and cswitch.o get the timer-discriminator edge hooks; 1000: traps_lo.o ALSO gets the USVC hook) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_SUBMAP_PMAP_SKIP=$STAGE90_XNU_SUBMAP_PMAP_SKIP STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM (pool DEFINES + asm ASFLAGS + EXTRA agree) STAGE90_XNU_RESIDENT=$STAGE90_XNU_RESIDENT STAGE90_XNU_USVC=$STAGE90_XNU_USVC STAGE90_XNU_STALL=$STAGE90_XNU_STALL STAGE90_XNU_USER_PC=$STAGE90_XNU_USER_PC (1004's new switch: asm ASFLAGS n/a + build_entry EXPORTS + the platform compile; no pool object reads it) --"
echo "-- the WHOLE D13 kernel pool + platform block (vm_unix.c the wire+probe+ladder; vm_shared_region.c the geometry; status.c the stack top; trap.c the witness; mach_kernelrpc.c the wire adapters; ios7_compat.h the selectors; pmap.c + commpage.c + arm_vm_init.c the alias/zeroing; vm_resident.c the hint guard; pcb.c the alignment) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** (Unchanged from 1003.)
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

echo "-- entry image (1003's switch set re-exported verbatim + STAGE90_XNU_USER_PC=1; build_entry only LINKS the pool) --"
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
# The record (1003's own) carries every tree-shaping switch including SHREG_GEOM, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP and STALL; the pins name them explicitly so build_entry.sh's ENTRY_ARM_KEYS
# read-back records the SAME values the build layers compiled with ([[mi4-one-value-two-definitions]]).
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
EXPORTS+=("STAGE90_XNU_SUBMAP_PMAP_SKIP=1")
EXPORTS+=("STAGE90_XNU_STALL=1")
# ----------------------------------------------------------------------------------------------------
# 1004-TODO: 1004's NEW key STAGE90_XNU_USER_PC.  The 1003 record does NOT carry it yet - the pin is
# the only carrier this run, and the record the entry build writes (asserted at the tail) is what
# parks the arm with it ([[mi4-one-value-two-definitions]]).  build_entry.sh reads it for the RECORD
# (and, for candidate (D), for the entry-image compile).
EXPORTS+=("STAGE90_XNU_USER_PC=1")
# ----------------------------------------------------------------------------------------------------
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   1003 was 7669309b - a DIFFERENT value here means the 1004 edits reached the image."
# The arm must MOVE off the 1003 arm (the project rule: the arm IS the entry bin's sha8).
_d4_arm=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $_d4_arm == armed-d13-7669309b ]] && { echo "FAIL: the build produced 1003's arm (7669309b) again - the entry bin did NOT move, so the 1004 edit did not reach the linked image (rebuild the layer that carries the change); do not park a 1004 arm identical to the 1003 one"; exit 1; }
[[ $_d4_arm == armed-d13-b04976fe ]] && { echo "FAIL: the build produced 1002's arm (b04976fe) - a stale layer"; exit 1; }
echo "   1004 arm: $_d4_arm (must differ from the 1003 arm 7669309b)"
echo "-- 1003's own constants/keys BY VALUE (source side, what the image check cannot split) --"
printf '   ENTRY_LIVE_CAP:  '; grep -m1 '^#define ENTRY_LIVE_CAP' src/entry/entry_stubs.c || true
printf '   WDT timeout:     '; grep -m1 '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S' src/stage90.h || true
[[ $(grep -c '^#define ENTRY_LIVE_CAP 24576u$' src/entry/entry_stubs.c) == 1 ]] || { echo "FAIL: the cap constant is not 24576u - the rebuild would re-hit the 998 wall"; exit 1; }
[[ $(grep -c '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S 29u$' src/stage90.h) == 1 ]] || { echo "FAIL: the watchdog timeout is not 29u - the outer net is not the arithmetic ceiling"; exit 1; }
rd_thin=$(grep -cE '^\s*ST_LIVE_RD\(' src/entry/entry_storage.c || true)
rd_full=$(sed -n '/^static __attribute__((noinline, noclone)) void st_read_single_block/,/^}/p' src/entry/entry_storage.c | grep -cE '(^|[^_])ST_LIVE\(' || true)
echo "   read body: $rd_thin thinned cell site(s) (want 70), $rd_full locating cell site(s) that never thin (want 8)"
[[ "$rd_thin" == "70" && "$rd_full" == "8" ]] || { echo "FAIL: the thinning is not 70/8 - either the six locating cells were gated or the thinning did not reach the reader"; exit 1; }

echo "-- by value: the checkers (PASS only if the linked image carries each kept fix) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_ios7_traps.py out/stage90/xnu_arm_entry.elf --expect on
python3 tools/check_d13_ios7_vmwire.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the same witness checker against the FROZEN 994 image as a NEGATIVE CONTROL (must FAIL ON and PASS OFF) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
# (The 996/997/998/999 SHREG checkers below are KEPT VERBATIM from 1003 - their one by-design frozen
#  20 B clause is the only allowed FAIL.  Re-run them ON the 1004 image.)
echo "-- by value: the 996 SHREG checker ON the 1004 image (via --ladder); the ONE by-design FAIL is the frozen 20 B clause, and NOTHING else may fail --"
sp_rc=0
sp_out=$(python3 tools/check_d13_shreg_probe.py out/stage90/xnu_arm_entry.elf --expect on --ladder --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c 2>&1) || sp_rc=$?
printf '%s\n' "$sp_out"
sp_fails=$(printf '%s\n' "$sp_out" | grep -c '^  FAIL ' || true)
if [[ $sp_rc -eq 0 ]] || [[ $sp_fails -ne 1 ]] || ! grep -q '^  FAIL shared_region_copyin_mappings does not compute' <<<"$sp_out"; then
  echo "FAIL: the 996 checker did not fail on exactly its by-design 20 B clause (rc=$sp_rc, $sp_fails clause FAIL line(s))"
  exit 1
fi
echo "   (by design: the ONLY failing clause is the frozen 20 B straddle one)"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --control out/stage90/captures/994-xnu_arm_entry.elf
python3 tools/check_d13_shreg_geom.py --tree external/xnu-hd2-darwin13/xnu --expect staged
python3 tools/check_submap_pmap_skip.py out/stage90/xnu_arm_entry.elf || { echo "FAIL: the 1001 checker did not PASS on the fresh image"; exit 1; }
python3 tools/read_storage_key_order.py --selftest >/dev/null || { echo "FAIL: the key-order tool's selftest is red"; exit 1; }
python3 tools/check_saved_state_offsets.py --selftest >/dev/null || { echo "FAIL: the saved-state checker's selftest is red"; exit 1; }
python3 tools/check_saved_state_offsets.py || { echo "FAIL: the saved-state offsets disagree across the trees"; exit 1; }

# ====================================================================================================
# 1003's delta, KEPT AS A REGRESSION (1004's set is 1003's verbatim): the per-tick stall sampler.
# ====================================================================================================
echo "-- by value: the 1003 STALL instrument (kept) - the recorder symbol + the 7 key literals in the image --"
elf=out/stage90/xnu_arm_entry.elf
stall_sym=$(arm-none-eabi-nm "$elf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$stall_sym" == "1" ]] || { echo "FAIL: entry_note_stall_sample is not in $elf (x$stall_sym, want 1) - 1003's instrument was dropped"; exit 1; }
stall_keys=$(strings "$elf" | grep -c 'xnu_live_stall_' || true)
[[ "$stall_keys" == "7" ]] || { echo "FAIL: the image carries $stall_keys xnu_live_stall_* literals (want 7)"; exit 1; }
echo "   entry_note_stall_sample (T) + xnu_live_stall_* x$stall_keys (kept)"
# (1002 r3-save regression, kept verbatim from 1003: the six-word window must still be present.)
_u10_ls=$(arm-none-eabi-nm "$elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: locore_fleh_swi is not in the image"; exit 1; }
_u10_lsx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${_u10_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_u10_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$_u10_ls" --stop-address="${_u10_lsx:-0xffffffff}")
t10_iw=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_u10_body" || true)
t10_il=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_u10_body" || true)
echo "   locore_fleh_swi: six-word push x$t10_iw (want 1), four-word x$t10_il (want 0)"
[[ "$t10_iw" == "1" && "$t10_il" == "0" ]] || { echo "FAIL: the 1002 six-word save set regressed - the 1001 r3 clobber is back"; exit 1; }

# ====================================================================================================
# THE 1004 DELTA: the per-tick PID-1 FRAME instrument (STAGE90_XNU_USER_PC), asserted BY VALUE, with
# the FROZEN 1003 image (armed-d13-7669309b) as the NEGATIVE CONTROL - a reader that cannot fail
# certifies nothing.
#
# 1004-TODO: the by-value fingerprint below is PROVISIONAL.  It assumes the design mirrors 1003: a
# recorder __1004_RECORDER__ (`entry_note_user_pc`, PROPOSED) in `src/entry/entry_stubs.c` and ONE
# gated call site at candidate (D) `src/entry/entry_trace.c` or candidate (B)
# `src/platform/darwin13/pe_msm8974.c`.  When the implementer fixes the symbol name and the call
# site, update the two `entry_note_user_pc` spellings, the 4-key count, and the source-shape clause
# (c) below.  The KEYS (xnu_live_user_pc / _n / _nz / _cpsr) are given by the task.
# ====================================================================================================
echo "-- by value: the 1004 DELTA - the per-tick pid-1 frame sampler (STAGE90_XNU_USER_PC) --"
# (a) 1004-TODO: the CALLER half.
#     If candidate (B) is chosen, the platform object references the recorder by name (U):
#        pe_obj=out/xnu_platform_obj_d13/pe_msm8974.o
#        pe_u=$(arm-none-eabi-nm "$pe_obj" 2>/dev/null | awk '($1=="U" && $2=="entry_note_user_pc") || ($2=="U" && $3=="entry_note_user_pc"){print "1"}')
#        [[ "$pe_u" == "1" ]] || { echo "FAIL: $pe_obj does not reference entry_note_user_pc (U) - the -DSTAGE90_XNU_USER_PC call site did not reach the platform compile"; exit 1; }
#     If candidate (D) is chosen, the caller is compiled INTO the entry image, so there is no separate
#     object to read U from - clause (b) on the linked image is then the whole caller-half evidence.
echo "   1004-TODO(a): the caller-half clause is UNWRITTEN - it depends on candidate (D) vs (B) (see the TODO block above)."

# (b) the LINKED image carries the recorder (T) and the FOUR key literals.  The literals are the
#     evidence a reader greps the NEXT capture for, so their presence in THIS image is what makes that
#     capture's keys mean this arm.
upc_sym=$(arm-none-eabi-nm "$elf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_user_pc$' || true)
[[ "$upc_sym" == "1" ]] || { echo "FAIL: entry_note_user_pc is not a defined text symbol in $elf (x$upc_sym, want 1) - the recorder did not reach the linked image (1004-TODO: confirm the symbol name)"; exit 1; }
upc_keys=$(strings "$elf" | grep -c 'xnu_live_user_pc' || true)
[[ "$upc_keys" == "4" ]] || { echo "FAIL: the image carries $upc_keys xnu_live_user_pc* literals (want 4: _pc/_n/_nz/_cpsr)"; exit 1; }
echo "   (b) the linked image defines entry_note_user_pc (T) and carries the 4 user_pc key literals"

# (c) the SOURCE-side shape.
#     1004-TODO: exactly ONE gated call site, in EITHER candidate (D) or (B), each gated so
#     STAGE90_XNU_USER_PC=0 changes neither file's compiled bytes; and the recorder body present in
#     src/entry/entry_stubs.c.  Uncomment and point at the file the implementer picks:
#        _d_src=src/entry/entry_trace.c            # candidate (D)
#        _d_calls=$(grep -c 'entry_note_user_pc(' "$_d_src" || true)
#        [[ "$_d_calls" == "1" ]] || { echo "FAIL: $_d_src carries $_d_calls call site(s) (want exactly 1)"; exit 1; }
#        grep -q '#if STAGE90_XNU_USER_PC' "$_d_src" || { echo "FAIL: $_d_src has no gated call site"; exit 1; }
#        grep -qE '^entry_note_user_pc\(' src/entry/entry_stubs.c || { echo "FAIL: the recorder body is missing"; exit 1; }
echo "   1004-TODO(c): the source-shape clause is UNWRITTEN - point $_d_src at candidate (D) or (B)."

# (d) the fresh record carries the new arm key.
[[ $(awk -F= '$1=="STAGE90_XNU_USER_PC"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_USER_PC=1 - the new arm key was not recorded this run"; exit 1; }
echo "   (d) the fresh record carries STAGE90_XNU_USER_PC=1 (the new arm key this run)"

echo "-- by value: the FROZEN 1003 elf as the NEGATIVE CONTROL (it must NOT carry the 1004 change) --"
d4_ctrl=out/stage90/frozen/armed-d13-7669309b/xnu_arm_entry.elf
if [[ -f $d4_ctrl ]]; then
  ctl_sym=$(arm-none-eabi-nm "$d4_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_user_pc$' || true)
  ctl_keys=$(strings "$d4_ctrl" | grep -c 'xnu_live_user_pc' || true)
  [[ "$ctl_sym" == "0" && "$ctl_keys" == "0" ]] || { echo "FAIL: the frozen 1003 elf already carries the 1004 instrument (sym x$ctl_sym, keys x$ctl_keys) - the negative control is not a control"; exit 1; }
  echo "   frozen 1003 elf ($d4_ctrl): entry_note_user_pc x0, xnu_live_user_pc* x0 - the change is NOT in it (control holds)"
else
  echo "   (frozen 1003 elf not present at $d4_ctrl - the negative control is unavailable this run)"
fi
echo "-- 1004 build done: park with scripts/park_1004.sh --"