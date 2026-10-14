#!/usr/bin/env bash
# ====================================================================================================
# press_1007.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1006.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1007 = 1006's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC, SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE and FREEZE_WITNESS
# stay 1) plus ONE new switch STAGE90_XNU_ABT_SPAN.  1007 is a MEASUREMENT rung (no kernel behaviour
# change): it adds an ABORT-SPAN WITNESS so the LOCATION of the ~24.4 s freeze is a reading.
# `__wrap_sleh_abort` brackets each `__real_sleh_abort` call with the free-running generic timer and
# calls `entry_note_abt_span(t0, call)`, publishing per abort the GAP to the previous abort and the
# handler's DURATION (keys `xnu_live_abt_{n,seq,gap,call,maxcall,maxgap}`).
#
# WHY (the 1006 rung, the baseline, and the frontier 1006 named).  1006's press read
# (records/1006-press-read.txt) settled the frontier's PATH: the ~24.4 s `xnu_live_tick_sum_maxd` freeze
# (0x1be87a47 = 468,220,487 counts = 24.386 s at 19.2 MHz) does NOT overlap the idle path -
# `__wrap_machine_idle` is NEVER entered (its sampler fired 0x).  The terminal frame (1005) is
# `sleh_abort+0x468`, an unconditional forward branch, NOT a fault-return self-loop - so the freeze
# rides the NON-IDLE abort path (1004 saw ~40 aborts walking forward).  1007 brackets that path.
# The 1007 change is SOURCE-ONLY, across TWO files (NO tree edit, NO stage driver): the recorder
# `entry_note_abt_span` in src/entry/entry_stubs.c (it takes the clock VALUES as arguments - it NEVER
# calls `entry_counter()`); and the declaration + the ONE bracket inside `__wrap_sleh_abort` in
# src/entry/entry_trace.c (using the file's own `static inline entry_counter()` - no new extern, no new
# link edge).  All gated `#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN` so OFF is
# byte-identical.  1007 ALSO carries §4.3 (the MANDATORY fix): the 1006 sampler's `entry_counter()`
# CALL is converted to an argument (`void entry_freeze_sample(uint32_t now)`), closing a by-value
# DEFECT in the inherited image - `entry_stubs.c`'s `entry_counter` resolved to a TERMINAL generated
# stand-in (8049cce4 -> entry_stub_hit -> entry_epilogue) that would END THE RUN on the abort path.
# STAGE90_XNU_ABT_SPAN is in NEITHER $EXTRA NOR the exported pool defines: only the TWO entry-toolchain
# objects read it.  Design: docs/1007-design.md.
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
# 1007 PRESS PRE-REGISTRATION
# ====================================================================================================
# SWITCH SET.  1006's verbatim (STAGE90_XNU_FREEZE_WITNESS=1, STAGE90_XNU_CADENCE=1, STAGE90_XNU_STALL=1,
# STAGE90_XNU_DYLD_WITNESS=1, STAGE90_XNU_SHREG_PROBE=1, STAGE90_XNU_SHREG_WIRE=1,
# STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1, STAGE90_XNU_USVC=1, STAGE90_XNU_SUBMAP_PMAP_SKIP=1,
# STAGE90_XNU_ABORT_TRACE=1, USB ladder PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung
# (POST_END_TICKS=0).  NEW switch STAGE90_XNU_ABT_SPAN=1 (the abort-span witness).  CADENCE=1, STALL=1,
# USVC=1, SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1, FREEZE_WITNESS=1 AND STAGE90_XNU_ABT_SPAN=1 must ALL be in
# the record.
#
# (0) THE 1007 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the SIX `xnu_live_abt_`
#     key literals (n/seq/gap/call/maxcall/maxgap) embedded in out/stage90/xnu_arm_entry.elf, AND
#     `entry_note_abt_span` a DEFINED text symbol (a generated stand-in would show 0), AND exactly ONE
#     `bl <entry_note_abt_span>` INSIDE `__wrap_sleh_abort` ([[mi4-hooks-live-at-calls]] - inlined code
#     between calls is unreachable), exactly as build_1007.sh's delta clause and scripts/park_1007.sh
#     assert.  PLUS the §4.3 fix: out/stage90/xnu_arm_entry_stubs.o must have NO `U entry_counter`
#     (the §2.3 terminal stand-in is gone).  If the change is NOT in the image, NOTHING below may be
#     read as a test - rebuild.
# (0b) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals (n/d/cpsr/ien/
#     pend/ctlr) and the two `xnu_live_frzbase_` key literals (n/d) AND the two symbols
#     `entry_freeze_sample` / `entry_gic_freeze_state` must STILL be in the image x0 each (1007's set is
#     1006's verbatim).  A x0 means the freeze witness was dropped - rebuild.
# (0c) THE 1005 CADENCE WITNESS IS A REGRESSION: the four `xnu_live_cadence_` key literals (t/sync/tick/
#     irq) AND the four symbols `entry_board_tick_count` / `entry_board_irq_count` / `entry_cadence_sample`
#     / `entry_cadence_summary` must STILL be in the image.  A x0 means the cadence witness was dropped.
# (0d) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be in the
#     image x1.
# (0e) THE 1002 r3-SAVE FIX IS A REGRESSION: locore_fleh_swi's prologue must STILL be the six-word window
#     `push {r0, r1, r2, r3, r8, lr}` / `bl <entry_note_user_svc>` / `pop {r0, r1, r2, r3, r8, lr}`.  A
#     FOUR-word window means the fix regressed - rebuild.
# (F) THE FRONTIER UNDER TEST - WHERE THE ~24.4 s FREEZE IS SPENT ON THE NON-IDLE ABORT PATH.
#     records/1006-press-read.txt: the tick AND the whole IRQ path froze for one repeatable ~24.4 s
#     interval while the machine stayed alive, and 1006 proved the CPU was NOT idle through it (the
#     `__wrap_machine_idle` witness never fired).  1004 saw the abort handler run ~40 times, walking
#     forward.  THE READING (keys xnu_live_abt_n / _seq / _gap / _call / _maxcall / _maxgap): compare
#     `xnu_live_abt_maxcall` and `xnu_live_abt_maxgap` against the SAME capture's
#     `xnu_live_tick_sum_maxd` (M ~ 0x1be87a47 ~ 468.2e6 counts): the abort-path classifier:
#       (A) BOTH maxcall and maxgap << M (e.g. <= a few hundred-thousand counts), several aborts, no
#           single spike => the abort path is honest but FAST - the 24.4 s is NOT here; it is in a
#           NON-abort stretch (the copyin/SVC path or a non-fault kernel stretch past the frame).  A
#           NEGATIVE result for this rung, and a positive pointer elsewhere.
#       (B) maxgap ~ M with maxcall small => the freeze is BETWEEN two aborts - one abort returned and
#           the next was not entered for 24.4 s; something OUTSIDE sleh_abort blocked the thread
#           between faults.
#       (C) maxcall ~ M => a SINGLE sleh_abort (its arm_fast_fault + vm_fault body) took 24.4 s.  This
#           is the STRONGEST positive: the frontier is inside one fault.
#       (D) xnu_live_abt_n / _seq PLATEAU (the last aborts are the same _seq as the sleh tail; `n`
#           stops while the log runs on) and NO maxcall/maxgap approaches M => the freeze is AFTER the
#           last abort - the abort path ended before it, so the frame's return-to-user stretch (or the
#           SVC path) is where the CPU sat.
#     Pair `xnu_live_abt_seq` with the sleh entry record's `xnu_live_sleh_seq`/`_far` to name the fault
#     address of the pegging sample.  This is a pure MEASUREMENT: the run is EXPECTED to still freeze
#     (1007 changes no kernel behaviour) - the LOCATION of the freeze is the finding.
#     FALSIFIERS: (a) NO `xnu_live_abt_` keys at all => the switch did not reach the image (rebuild);
#     (b) a NON-MONOTONE `xnu_live_abt_n` => a MEASUREMENT defect (adjudicate against entry_stubs.c's
#     recorder); (c) `xnu_live_stub_hit_seq` != 0 => the §2.3 terminal stand-in FIRED (the argument-
#     passing fix did not take, or entry_counter escaped) - the run ended inside the abort path and the
#     span keys are the last thing before it; (d) a `_gap`/`_call` delta > 2^31 => an entry_counter WRAP
#     (2^32 / 19.2 MHz = 223.7 s), NOT a reading - the freeze (24.4 s) is 9x smaller than a wrap.
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np ret=12`,
#     the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the whole capture
#     must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr>
#     size=<ptr> lr=<ptr>`, BOUNDED (max 8).  EXPECTED >= 1; most likely EXACTLY 2, obj = 999's two
#     VMHINT991 objects 0xc0005b48 / 0xc0005ab0.  ABSENCE while the (4) gate holds = rebuild.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1007_capture.py carries the gate: its [0] DEATH GATE + the kept
#     1004/1005/1006 clauses): no panic + the exec-done trio + the five probe keys + records after the
#     exec-returned point.  An abort-span verdict is a READING taken from a live image, so it counts
#     only if the boot reached the print site (the [ABT] section never certifies on a dead run).
# (5) THE 1006 FREEZE WITNESS (kept a regression; keys xnu_live_freeze_* / xnu_live_frzbase_*).  1006
#     read it as a VALID SILENCE (zero freeze records, the idle path never entered) - 1007 expects the
#     SAME silence (a pure measurement changes nothing).
# (6) THE 1005 CADENCE WITNESS (kept a regression AND a second reading).  Keys xnu_live_cadence_t / _sync
#     / _tick / _irq plus the entry_cadence_summary() triples.  The ratio ds/max(dt,1) is the (b1)/(b2)/
#     full-stop partition; 1007 expects the SAME (b2) shape (a pure measurement changes nothing).
# (7) THE ABORT TRACE (1004, kept).  The full ordered sleh sequence (keys xnu_live_sleh_seq / _type /
#     _fsr / _far / _thr / _upc): a `far` that REPEATS = LOOP; a `far` that strictly ADVANCES = WALK.
#     This names the fault address of the abort the span witness pegs.
# (8) THE USER-SVC FRONTIER (kept; keys xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr / _r0).
#     `_pid` = 1; `_pc` a user VA; `_cpsr` a USER-mode CPSR.
# (9) THE PET (kept).  The feed gate is `count >= bark/2` = 14.5 s.  If 1007 lives past ~14.5 s with the
#     read door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` MUST appear.
# (10) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
#     thinning ordinal stays 64.  The sleh caps stay RAISED to 512 under 1004's switch.
# (11) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) the (0) change is NOT in the image =>
#     REBUILD; (b) the 230x `usermode data abort at 0x4` recurs past `check_np ret=12` => the 1002
#     r3-save fix REGRESSED - rebuild; (c) `xnu_live_abt_` x0 in the capture => the switch did not reach
#     the image (the (0) rebuild signal); (d) `xnu_live_stub_hit_seq` != 0 => the §2.3 terminal stand-in
#     fired - the run ended inside the abort path; (e) a NON-MONOTONE `xnu_live_abt_n` => a MEASUREMENT
#     defect; (f) death ELSEWHERE, later => the frontier ADVANCED - record by value.  The FROZEN
#     REGRESSION GUARDS - the geometry line exactly once, `IOS7LAB cache mapping` x4, the shared region
#     x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0` - are re-verified
#     only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent - read the
# skipped-block shape IN THE IMAGE before any judgement; (c) the log ends with a capped key and no
# direct key after it = rebuild; (d) the log ends in a direct record = that record's moment IS the
# frontier finding this rung was built to produce - read the last direct keys (abt-span last, then
# freeze, then cadence, then sleh, then usvc, then pet) as the frontier, and classify the freeze's
# LOCATION by maxcall/maxgap against xnu_live_tick_sum_maxd; (e) the (0) change is not in the image =
# the witness is not in the image - rebuild, nothing else is a test.
# ====================================================================================================
if ! grep -q '^# 1007 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1007 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003/1004/1005/1006 pattern: switch"
  echo "          set / THE POINTS / failure signatures / falsification / regression guards). The build"
  echo "          side: scripts/build_1007.sh + scripts/park_1007.sh + the 1007 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-31bdebf3 ]] && {
  echo "REFUSING: out/stage90 holds 1006's arm (31bdebf3) - the PRESSED predecessor (frontier: the"
  echo "~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered;"
  echo "adjudicated in records/1006-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-ff937225 ]] && {
  echo "REFUSING: out/stage90 holds 1005's arm (ff937225) - the PRESSED predecessor (frontier: the STALL"
  echo "is (b2) a whole-interrupt-path freeze ~24.4 s; adjudicated in records/1005-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-582a19c8 ]] && {
  echo "REFUSING: out/stage90 holds 1004's arm (582a19c8) - the PRESSED predecessor (frontier: the"
  echo "STALL after dyld first touches a shared-cache page; adjudicated in records/1004-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - already pressed 2026-10-13 (pid 1 in the"
  echo "kernel's data-abort return path; adjudicated in records/1003-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1007 (scripts/build_1007.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1007 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1007 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip, 1003's stall + 1004's abort-trace instrument + 1005's cadence witness + 1006's freeze witness AND 1007's abort-span witness =="
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
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004/1005/1006/1007)
STAGE90_XNU_ABORT_TRACE the abort trace - raised sleh caps + the frame's user pc (1004's; kept verbatim)
STAGE90_XNU_CADENCE the cadence witness - a timer-independent (syscall) sample + the all-interrupt counter (1005's; kept verbatim)
STAGE90_XNU_FREEZE_WITNESS the idle-path freeze witness - the dwell sampler + the GIC/CPSR triple (1006's; kept verbatim)
STAGE90_XNU_ABT_SPAN the abort-span witness - the gap + handler duration per sleh_abort (1007's; a pure measurement rung)
EOF
echo "   (record carries the 1006 verbatim switch set PLUS the new STAGE90_XNU_ABT_SPAN; USVC, SUBMAP_PMAP_SKIP, STALL, ABORT_TRACE, CADENCE and FREEZE_WITNESS all on)"

echo "== 2-0. the 1007 abort-span witness in the image (read by value BEFORE the press) + the 1006 freeze witness + the 1005 cadence witness + the 1004 abort trace + the 1002 r3-save fix as regressions =="
# (0) THE 1007 CHANGE IS IN THE IMAGE: the six `xnu_live_abt_` key literals embedded AND the
#     `entry_note_abt_span` DEFINED text symbol (a generated stand-in would show 0) AND exactly ONE
#     `bl <entry_note_abt_span>` INSIDE `__wrap_sleh_abort` ([[mi4-hooks-live-at-calls]]).
_abt_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_abt_' || true)
echo "   (0) xnu_live_abt_* literals x$_abt_lit (want 6: n/seq/gap/call/maxcall/maxgap)"
[[ "$_abt_lit" == 6 ]] || {
  echo "REFUSING: the 1007 abort-span store is NOT in the image (literals x$_abt_lit want 6) - rebuild"; exit 1; }
# NOTE: `nm | grep -q` under `set -o pipefail` is a trap - `grep -q` exits on the first match, SIGPIPEs
# `nm`, and the pipeline's status becomes 141, vetoing a match that exists (the 487/998 SIGPIPE lesson).
# Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_abt_nm=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf)
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the image - an object did not rebuild or a stand-in was linked - rebuild"; exit 1; }
echo "   (0) entry_note_abt_span is DEFINED T/t in the image"
# The witness's ONE call site: `__wrap_sleh_abort` carries exactly one entry_note_abt_span call (the
# abort path IS the witness; a second site would time a span the classifier is not about).
_ab_a=$(awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_sleh_abort is not in the image - the abort path the span witness rides is gone"; exit 1; }
_ab_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ab_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_ab_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ab_a" --stop-address="${_ab_x:-0xffffffff}")
_ab_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$_ab_body" || true)
echo "   (0) __wrap_sleh_abort calls entry_note_abt_span: $_ab_sc (want 1)"
[[ "$_ab_sc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_span exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# §4.3 THE ARGUMENT-PASSING FIX: entry_stubs.o must NOT carry an UNDEFINED `entry_counter` (the §2.3
# terminal stand-in).  If the recorder still calls entry_counter(), the stand-in can END THE RUN.
_ab_uc=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o | grep -cE '^[[:space:]]*U entry_counter$' || true)
echo "   (0) xnu_arm_entry_stubs.o U entry_counter: $_ab_uc (want 0 - the §4.3 fix)"
[[ "$_ab_uc" == 0 ]] || {
  echo "REFUSING: entry_stubs.o still references entry_counter (U x$_ab_uc want 0) - the recorder calls the clock directly; the §4.3 argument-passing fix did not take - rebuild"; exit 1; }

# (0a) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals + the two
#      `xnu_live_frzbase_` key literals embedded AND the two DEFINED text symbols.
_frz_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_freeze_' || true)
echo "   (0a) xnu_live_freeze_* literals x$_frz_lit (want 6: n/d/cpsr/ien/pend/ctlr)"
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the 1006 freeze store is NOT in the image (literals x$_frz_lit want 6) - 1007's set is 1006's verbatim, so it must be present - rebuild"; exit 1; }
_idl_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_frzbase_' || true)
echo "   (0a) xnu_live_frzbase_* literals x$_idl_lit (want 2: n/d)"
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the 1006 idle-baseline store is NOT in the image (literals x$_idl_lit want 2) - rebuild"; exit 1; }
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the image - the 1006 freeze witness regressed - rebuild"; exit 1; }
done
echo "   (0a) the two freeze symbols are DEFINED T/t in the image"
# The sampler's ONE call site: `__wrap_machine_idle` carries exactly one entry_freeze_sample call (the
# idle path IS the witness; a second site would time a pass the classifier is not about).
_idl_a=$(awk '$3=="__wrap_machine_idle"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_machine_idle is not in the image - the idle path the freeze sample rides is gone"; exit 1; }
_idl_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_idl_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_idl_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_idl_a" --stop-address="${_idl_x:-0xffffffff}")
_frz_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' <<<"$_idl_body" || true)
echo "   (0a) __wrap_machine_idle calls entry_freeze_sample: $_frz_sc (want 1)"
[[ "$_frz_sc" == 1 ]] || {
  echo "REFUSING: __wrap_machine_idle does not call entry_freeze_sample exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# (0b) THE 1005 CADENCE WITNESS IS A REGRESSION: the four key literals AND the four DEFINED symbols.
_cad_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_cadence_' || true)
echo "   (0b) xnu_live_cadence_* literals x$_cad_lit (want 4: t/sync/tick/irq)"
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the 1005 cadence store is NOT in the image (literals x$_cad_lit want 4) - 1006's set is 1005's verbatim, so it must be present - rebuild"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the image - the 1005 cadence witness regressed - rebuild"; exit 1; }
done
echo "   (0b) the four cadence symbols are DEFINED T/t in the image"
# (0c) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal x1.
_d4_upc=$(strings out/stage90/xnu_arm_entry.elf | grep -c '^xnu_live_sleh_upc$' || true)
echo "   (0c) xnu_live_sleh_upc literal x$_d4_upc (want 1)"
[[ "$_d4_upc" == 1 ]] || {
  echo "REFUSING: the 1004 abort trace is NOT in the image (upc x$_d4_upc want 1) - 1006's set is 1004's verbatim, so it must be present - rebuild"; exit 1; }
# (0d) THE 1002 r3-SAVE FIX IS A REGRESSION. The prologue of locore_fleh_swi must STILL be six-word.
_ls=$(awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
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
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1007.sh."
  exit 1; }
echo "   (0e) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1007 abort-span witness"

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
# 1007 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1007-$(date -u +%Y%m%d-%H%M%S).txt"
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
echo "   (adjudicate the recovered capture with: python3 tools/check_1007_capture.py <capture>"
echo "    - the [0] DEATH GATE and the 1004/1005/1006 clauses are kept, and the [ABT] ABORT-SPAN section"
echo "    is the 1007 rung's own reading: maxcall/maxgap against xnu_live_tick_sum_maxd partitions the"
echo "    ~24.4 s freeze into (A) not on the abort path / (B) between two aborts / (C) inside one abort"
echo "    / (D) after the last abort, with xnu_live_stub_hit_seq==0 proving the §2.3 stand-in did not fire)"
echo "== DONE =="