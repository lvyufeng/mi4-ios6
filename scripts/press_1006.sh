#!/usr/bin/env bash
# ====================================================================================================
# press_1006.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1005.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1006 = 1005's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC, SUBMAP_PMAP_SKIP, CADENCE, STALL and ABORT_TRACE stay 1) plus ONE
# new switch STAGE90_XNU_FREEZE_WITNESS.  1006 is a MEASUREMENT rung (no kernel behaviour change): it
# adds an IDLE-PATH FREEZE WITNESS so the CAUSE of the ~24.4 s whole-interrupt-path freeze is a reading.
# A sampler runs once per pass of `__wrap_machine_idle`, times the pass with `entry_counter()`, and on
# the powers-of-two ladder publishes the dwell and - when the dwell exceeds a threshold - a GIC/CPSR
# triple read AT THAT MOMENT (keys `xnu_live_freeze_{n,d,cpsr,ien,pend,ctlr}`; the FIRST FOUR passes are
# published UNFILTERED as `xnu_live_frzbase_{n,d}`, so the run measures its OWN idle period as a baseline).
#
# WHY (the 1005 rung, the baseline, and the frontier 1005 named).  1005's press read
# (records/1005-press-read.txt) settled the frontier: the STALL is (b2) a WHOLE-INTERRUPT-PATH FREEZE.
# `tick = irq - 3` is CONSTANT across the whole run (every interrupt IS a timer tick), the pid-1 SVC
# path froze with it, for a single repeatable ~24.4 s interval (`xnu_live_tick_sum_maxd = 0x1be8bc01`),
# after which the tick BURSTS (135 -> 479).  The machine stays ALIVE (`No errors detected`).  1005 named
# the bespoken frontier itself: 1006 asks THE CAUSE - (i) an IRQ-masked window, (ii) a GIC distributor
# re-mask, (iii) an ISR that never EOI'd, (iv) a non-idle fault loop.
# The 1006 change is SOURCE-ONLY, across FOUR files (NO tree edit, NO stage driver): the sampler in
# src/entry/entry_stubs.c; the ONE idle-path call in src/entry/entry_trace.c (inside
# `__wrap_machine_idle`); the one GIC helper `entry_gic_freeze_state` in src/entry/entry_irq.c (the GIC
# stays addressed from ONE function); and its declaration in src/entry/entry_gic.h - all gated
# `#if defined(STAGE90_XNU_FREEZE_WITNESS) && STAGE90_XNU_FREEZE_WITNESS` so OFF is byte-identical.
# STAGE90_XNU_FREEZE_WITNESS is in NEITHER $EXTRA NOR the exported pool defines: only the TWO entry-
# toolchain objects read it.  Design: docs/1006-interrupt-freeze-witness-design.md.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible.  RESIDENT rung
# (POST_END_TICKS=0): the primary channel is /proc/last_kmsg, read by scripts/recover_last_kmsg.sh
# after the device comes back (step 4 - a plain boot of the FLASHED ROM is the carrier, NEVER
# `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1006 PRESS PRE-REGISTRATION
# ====================================================================================================
# SWITCH SET.  1005's verbatim (STAGE90_XNU_CADENCE=1, STAGE90_XNU_STALL=1, STAGE90_XNU_DYLD_WITNESS=1,
# STAGE90_XNU_SHREG_PROBE=1, STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1,
# STAGE90_XNU_USVC=1, STAGE90_XNU_SUBMAP_PMAP_SKIP=1, STAGE90_XNU_ABORT_TRACE=1, USB ladder
# PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0).  NEW switch
# STAGE90_XNU_FREEZE_WITNESS=1 (the idle-path freeze witness).  CADENCE=1, STALL=1, USVC=1,
# SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1 AND STAGE90_XNU_FREEZE_WITNESS=1 must ALL be in the record.
#
# (0) THE 1006 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the SIX
#     `xnu_live_freeze_` key literals (n/d/cpsr/ien/pend/ctlr) and the TWO `xnu_live_frzbase_` key literals
#     (n/d) embedded in out/stage90/xnu_arm_entry.elf, AND the two DEFINED text symbols
#     `entry_freeze_sample` / `entry_gic_freeze_state` (a generated stand-in would show 0), AND exactly
#     ONE `bl <entry_freeze_sample>` INSIDE `__wrap_machine_idle` ([[mi4-hooks-live-at-calls]] - inlined
#     code between calls is unreachable), exactly as build_1006.sh's delta clause and scripts/park_1006.sh
#     assert.  If the change is NOT in the image, NOTHING below may be read as a test - rebuild.
# (0b) THE 1005 CADENCE WITNESS IS A REGRESSION: the four `xnu_live_cadence_` key literals (t/sync/tick/
#     irq) AND the four symbols `entry_board_tick_count` / `entry_board_irq_count` / `entry_cadence_sample`
#     / `entry_cadence_summary` must STILL be in the image (1006's set is 1005's verbatim).  A x0 means
#     the cadence witness was dropped - rebuild.
# (0c) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be in the
#     image x1.
# (0d) THE 1002 r3-SAVE FIX IS A REGRESSION: locore_fleh_swi's prologue must STILL be the six-word window
#     `push {r0, r1, r2, r3, r8, lr}` / `bl <entry_note_user_svc>` / `pop {r0, r1, r2, r3, r8, lr}`.  A
#     FOUR-word window means the fix regressed - rebuild.
# (F) THE FRONTIER UNDER TEST - THE CAUSE OF THE ~24.4 s WHOLE-INTERRUPT-PATH FREEZE, AND THE GIC/CPSR
#     TRIPLE AT THE LARGEST DWELL IS THE READING.  records/1005-press-read.txt: the STALL is (b2) - the
#     tick AND the whole IRQ path AND the pid-1 SVC path all froze together for one repeatable ~24.4 s
#     interval while the machine stayed alive.  THE READING (keys xnu_live_freeze_n / _d / _cpsr / _ien /
#     _pend / _ctlr, plus the unfiltered early baseline xnu_live_frzbase_n / _d): for the record with the
#     LARGEST `_d` (the dwell in entry_counter() counts), decode the IRQ-mask bit i = (_cpsr >> 7) & 1
#     (1 = IRQs MASKED, 0 = ENABLED) and read the GIC triple (ien = ISENABLER0 bit 20 = the timer line
#     enabled; pend = ISPENDR0 bit 20 = the line pending; ctlr = GICD_CTLR = the whole distributor):
#       * _d large, i == 0, ien != 0, pend != 0 => a long NON-IDLE stretch (a fault loop, or a wfi that
#         did not wake) - the CPU had IRQs enabled and the line enabled+pending yet did not take it, so
#         it was NOT in the idle path and NOT masked.
#       * _d large, i == 1 => (i) an IRQ-MASKED window held across the gap.
#       * _d large, i == 0, ien == 0 => (ii) a GIC distributor re-mask (the timer line was DISABLED
#         across the gap).
#       * _d large, ctlr == 0 => a global sub-case of (ii) (the whole distributor was disabled).
#     This is a pure MEASUREMENT: the run is EXPECTED to still stall (1006 changes no kernel behaviour).
#     SILENCE IS A READING: if there are ZERO `xnu_live_freeze_` records but the `xnu_live_frzbase_` baseline
#     IS present, the witness saw NO idle pass dwell longer than the threshold - either the ~24.4 s freeze
#     did not overlap the idle path (it was spent in non-idle kernel code) or the machine never re-entered
#     idle after it.  That is NOT a pass and NOT an instrument failure - it is a READING (the classifier
#     says so, and check_1006_capture.py's [W] section reports it as a valid classification, not a HARD).
#     FALSIFIERS: (a) no `xnu_live_freeze_` keys at all => the switch did not reach the image (rebuild);
#     (b) a NON-MONOTONE `xnu_live_freeze_n` => a MEASUREMENT defect (adjudicate against entry_stubs.c's
#     recorder); (c) a record with `_d <= 0x00004b00` => a recorder inconsistency (it filters those).
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np ret=12`,
#     the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the whole capture
#     must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr>
#     size=<ptr> lr=<ptr>`, BOUNDED (max 8).  EXPECTED >= 1; most likely EXACTLY 2, obj = 999's two
#     VMHINT991 objects 0xc0005b48 / 0xc0005ab0.  ABSENCE while the (4) gate holds = rebuild.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1006_capture.py carries the gate: its [0] DEATH GATE + the 1004/1005
#     clauses): no panic + the exec-done trio + the five probe keys + records after the exec-returned
#     point.  A freeze-witness verdict is a READING taken from a live image, so it counts only if the
#     boot reached the print site (the [W] section never certifies on a dead run).
# (5) THE 1005 CADENCE WITNESS (kept a regression AND a second reading).  Keys xnu_live_cadence_t / _sync
#     / _tick / _irq plus the entry_cadence_summary() triples.  The ratio ds/max(dt,1) is the (b1)/(b2)/
#     full-stop partition; 1006 expects the SAME (b2) shape (a pure measurement changes nothing).
# (6) THE ABORT TRACE (1004, kept).  The full ordered sleh sequence (keys xnu_live_sleh_seq / _type /
#     _fsr / _far / _thr / _upc): a `far` that REPEATS = LOOP; a `far` that strictly ADVANCES = WALK.
# (7) THE USER-SVC FRONTIER (kept; keys xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr / _r0).
#     `_pid` = 1; `_pc` a user VA; `_cpsr` a USER-mode CPSR.
# (8) THE PET (kept).  The feed gate is `count >= bark/2` = 14.5 s.  If 1006 lives past ~14.5 s with the
#     read door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` MUST appear.
# (9) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump thinning
#     ordinal stays 64.  The sleh caps stay RAISED to 512 under 1004's switch.
# (10) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) the (0) change is NOT in the image =>
#     REBUILD; (b) the 230x `usermode data abort at 0x4` recurs past `check_np ret=12` => the 1002
#     r3-save fix REGRESSED - rebuild; (c) `xnu_live_freeze_` x0 in the capture => the switch did not
#     reach the image (the (0) rebuild signal); (d) a NON-MONOTONE `xnu_live_freeze_n` => a MEASUREMENT
#     defect; (e) death ELSEWHERE, later => the frontier ADVANCED - record by value.  The FROZEN
#     REGRESSION GUARDS - the geometry line exactly once, `IOS7LAB cache mapping` x4, the shared region
#     x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0` - are re-verified
#     only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent - read the
# skipped-block shape IN THE IMAGE before any judgement; (c) the log ends with a capped key and no
# direct key after it = rebuild; (d) the log ends in a direct record = that record's moment IS the
# frontier finding this rung was built to produce - read the last direct keys (freeze last, then
# cadence, then sleh, then usvc, then pet) as the frontier, and classify the freeze by the GIC/CPSR
# triple at the largest dwell; (e) the (0) change is not in the image = the witness is not in the
# image - rebuild, nothing else is a test.
# ====================================================================================================
if ! grep -q '^# 1006 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1006 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003/1004/1005 pattern: switch"
  echo "          set / THE POINTS / failure signatures / falsification / regression guards). The build"
  echo "          side: scripts/build_1006.sh + scripts/park_1006.sh + the 1006 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-ff937225 ]] && {
  echo "REFUSING: out/stage90 holds 1005's arm (ff937225) - the PRESSED predecessor (frontier: the STALL"
  echo "is (b2) a whole-interrupt-path freeze ~24.4 s; adjudicated in records/1005-press-read.txt)."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-582a19c8 ]] && {
  echo "REFUSING: out/stage90 holds 1004's arm (582a19c8) - the PRESSED predecessor (frontier: the"
  echo "STALL after dyld first touches a shared-cache page; adjudicated in records/1004-press-read.txt)."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - already pressed 2026-10-13 (pid 1 in the"
  echo "kernel's data-abort return path; adjudicated in records/1003-press-read.txt)."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt)."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1006 (scripts/build_1006.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1006 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1006 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip, 1003's stall + 1004's abort-trace instrument + 1005's cadence witness AND 1006's freeze witness =="
while read -r key why; do
  grep -q "^${key}=1" out/stage90/xnu_arm_entry-config.txt || {
    echo "REFUSING: record has no ${key}=1 - ${why}"; exit 1; }
done <<'EOF'
STAGE90_XNU_USB_PROBE the USB ladder stage 1 (probe)
STAGE90_XNU_USB_DEV the USB device stage (the reader below would capture an empty file)
STAGE90_XNU_USB_ENUM the USB enumeration stage
STAGE90_XNU_USB_STREAM the USB streaming stage
STAGE90_XNU_DYLD_WITNESS the message/cacheBase readings
STAGE90_XNU_SHREG_PROBE the IOS7SHREG ladder lines
STAGE90_XNU_SHREG_WIRE the IOS7LAB cache mapping lines (997's reading)
STAGE90_XNU_SHREG_GEOM the geometry/stack half (kept from 998/999; G1 held)
STAGE90_XNU_RESIDENT the WDT door feed's gate AND the tick probes (999's, kept)
STAGE90_XNU_USVC the SVC frontier hook - 1002's r3-save fix lives INSIDE it (must stay on)
STAGE90_XNU_SUBMAP_PMAP_SKIP the is_submap guard in vm_map_enter's tail (1001's; kept)
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004/1005/1006)
STAGE90_XNU_ABORT_TRACE the abort trace - raised sleh caps + the frame's user pc (1004's; kept verbatim)
STAGE90_XNU_CADENCE the cadence witness - a timer-independent (syscall) sample + the all-interrupt counter (1005's; kept verbatim)
STAGE90_XNU_FREEZE_WITNESS the idle-path freeze witness - the dwell sampler + the GIC/CPSR triple (1006's; a pure measurement rung)
EOF
echo "   (record carries the 1004 verbatim switch set PLUS STAGE90_XNU_CADENCE PLUS the new STAGE90_XNU_FREEZE_WITNESS; USVC, SUBMAP_PMAP_SKIP, STALL, ABORT_TRACE and CADENCE all on)"

echo "== 2-0. the 1006 freeze witness in the image (read by value BEFORE the press) + the 1005 cadence witness + the 1004 abort trace + the 1002 r3-save fix as regressions =="
# (0) THE 1006 CHANGE IS IN THE IMAGE: the six `xnu_live_freeze_` key literals + the two `xnu_live_frzbase_`
#     key literals embedded AND the two DEFINED text symbols (a generated stand-in would show 0).
_frz_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_freeze_' || true)
echo "   (0) xnu_live_freeze_* literals x$_frz_lit (want 6: n/d/cpsr/ien/pend/ctlr)"
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the 1006 freeze store is NOT in the image (literals x$_frz_lit want 6) - rebuild"; exit 1; }
_idl_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_frzbase_' || true)
echo "   (0) xnu_live_frzbase_* literals x$_idl_lit (want 2: n/d)"
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the 1006 idle-baseline store is NOT in the image (literals x$_idl_lit want 2) - rebuild"; exit 1; }
# NOTE: `nm | grep -q` under `set -o pipefail` is a trap - `grep -q` exits on the first match, SIGPIPEs
# `nm`, and the pipeline's status becomes 141, vetoing a match that exists (the 487/998 SIGPIPE lesson).
# Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_frz_nm=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf)
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the image - an object did not rebuild or a stand-in was linked - rebuild"; exit 1; }
done
echo "   (0) the two freeze symbols are DEFINED T/t in the image"
# The sampler's ONE call site: `__wrap_machine_idle` carries exactly one entry_freeze_sample call (the
# idle path IS the witness; a second site would time a pass the classifier is not about).
_idl_a=$(awk '$3=="__wrap_machine_idle"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_frz_nm") || {
  echo "REFUSING: __wrap_machine_idle is not in the image - the idle path the freeze sample rides is gone"; exit 1; }
_idl_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_idl_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_idl_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_idl_a" --stop-address="${_idl_x:-0xffffffff}")
_frz_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' <<<"$_idl_body" || true)
echo "   (0) __wrap_machine_idle calls entry_freeze_sample: $_frz_sc (want 1)"
[[ "$_frz_sc" == 1 ]] || {
  echo "REFUSING: __wrap_machine_idle does not call entry_freeze_sample exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# (0b) THE 1005 CADENCE WITNESS IS A REGRESSION: the four key literals AND the four DEFINED symbols.
_cad_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_cadence_' || true)
echo "   (0b) xnu_live_cadence_* literals x$_cad_lit (want 4: t/sync/tick/irq)"
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the 1005 cadence store is NOT in the image (literals x$_cad_lit want 4) - 1006's set is 1005's verbatim, so it must be present - rebuild"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the image - the 1005 cadence witness regressed - rebuild"; exit 1; }
done
echo "   (0b) the four cadence symbols are DEFINED T/t in the image"
# (0c) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal x1.
_d4_upc=$(strings out/stage90/xnu_arm_entry.elf | grep -c '^xnu_live_sleh_upc$' || true)
echo "   (0c) xnu_live_sleh_upc literal x$_d4_upc (want 1)"
[[ "$_d4_upc" == 1 ]] || {
  echo "REFUSING: the 1004 abort trace is NOT in the image (upc x$_d4_upc want 1) - 1006's set is 1004's verbatim, so it must be present - rebuild"; exit 1; }
# (0d) THE 1002 r3-SAVE FIX IS A REGRESSION. The prologue of locore_fleh_swi must STILL be six-word.
_ls=$(awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_frz_nm") || {
  echo "REFUSING: locore_fleh_swi is not in the image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
echo "   locore_fleh_swi ($_ls..${_lsx:-end}): six-word window x$_w6 (want 1), four-word x$_w4 (want 0)"
grep -B1 -A2 'bl.*<entry_note_user_svc>' <<<"$_lb" | sed 's/^/       /'
[[ "$_w6" == 1 && "$_w4" == 0 ]] || {
  echo "REFUSING: the 1002 r3-save fix REGRESSED (six-word window x$_w6 want 1, four-word x$_w4 want 0) -"
  echo "the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported, or the tree is not at the 1002 form."
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1006.sh."
  exit 1; }
echo "   (0d) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1006 freeze witness"

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.** /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 1006 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1006-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is a normal outcome here, not a
# failure, so do not let `set -e` abort before the reader is stopped and its capture is reported.
set +e
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"
RUN_RC=$?
set -e
echo "   (runner exit $RUN_RC; a resident rung not returning shows as 2)"

kill "$USB_PID" 2>/dev/null || true
echo "== USB console capture: $USB_OUT =="
if [ -s "$USB_OUT" ]; then echo "   bytes read: $(wc -c < "$USB_OUT")"; else
  echo "   (empty - the ladder did not reach the idle path, or the payload stopped before it)"
fi

echo "== 4. recovery: the run's own log via /proc/last_kmsg (the carrier is the FLASHED ROM) =="
# The recovery is the same either way: the device must come back ONCE (on its own, or by the
# operator's power press) and the NEXT boot's /proc/last_kmsg holds the run as its previous.
# --read-only when Android is already up (do NOT reboot it); no flags when it sits in fastboot (the
# script reboots the installed ROM - NEVER `fastboot boot`).
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet; if it is still dark, the operator power-press is the next step, then:"
  echo "    scripts/recover_last_kmsg.sh --read-only while Android is up; do not boot anything else first)"
fi
echo "   (adjudicate the recovered capture with: python3 tools/check_1006_capture.py <capture>"
echo "    - the [0] DEATH GATE and the 1004/1005 clauses are kept, and the [W] FREEZE section is the"
echo "    1006 rung's own reading: the GIC/CPSR triple at the largest idle-pass dwell partitions the"
echo "    ~24.4 s freeze into (i) IRQ-masked / (ii) GIC re-mask / non-idle stretch / a valid silence)"
echo "== DONE =="