#!/usr/bin/env bash
# ====================================================================================================
# press_1009.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1008.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1009 = 1008's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC, SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS,
# ABT_SPAN and ABT_FAR stay 1) plus ONE new switch STAGE90_XNU_ABT_PC.  1009 is a MEASUREMENT rung (no
# kernel behaviour change): it adds a FAULTING-PC IDENTITY witness so WHICH instruction the storm
# re-runs, and the DFSR (WnR + status) it re-runs with, are readings.  `__wrap_sleh_abort` forwards the
# `regs->pc`/`fsr`/`regs->cpsr`/`far_` it ALREADY holds (regs is the saved-state frame; fsr/far_ are the
# pair read for the sleh trace) to `entry_note_abt_pc(pc, fsr, cpsr, far)`, which publishes them on the
# DIRECT (cap-exempt) channel under the seven keys `xnu_live_abt_{pcn,pc,fsr,abtcpsr,pcfar,nfar,nfsr}`
# - the SAME powers-of-two ladder 1008 uses (attaching pc/fsr/cpsr to the tail rows) plus the
# change-counts `nfar`/`nfsr` (written WHERE THEY CHANGE) that decide REPEAT-vs-SPREAD by value.
#
# WHY (the 1008 rung, the baseline, and the frontier 1008 named).  1008's press read
# (records/1008-press-read.txt, arm armed-d13-0343ef45) turned the frontier from a STORM into a REPEAT:
# the ~2.1 M-abort storm collapses onto the CONSTANT `far=0x0007b000` (type=4 = STAGE90_T_DATA_ABT, a
# DATA abort) from ordinal 0x400 through 0x200000 (~2.09 M aborts), riding the ~18.7 s handler call
# (`xnu_live_abt_maxcall` = 0x1570c1a7).  The `far` walks 12 distinct values over ordinals 1..0x200,
# then collapses onto `0x7b000` at 0x400 - a single-address RETRY LOOP, not a demand walk.  But 1008
# says WHERE, not WHAT: it never names the faulting instruction (`regs->pc`) nor the DFSR, and its
# reader reported `MIXED` because it counted the distinct `far` values over the WHOLE ladder, blind to
# the tail being 61x the walked prefix.  1009 publishes the faulting PC, the DFSR and the change-counts,
# resolving the readings 1008 did NOT produce.
# The 1009 change is SOURCE-ONLY, across TWO files (NO tree edit, NO stage driver): the recorder
# `entry_note_abt_pc(uint32_t pc, uint32_t fsr, uint32_t cpsr, uint32_t far)` in src/entry/entry_stubs.c
# (it takes the values as arguments - it NEVER calls `entry_counter()`, and it forwards to
# `entry_write_kv`, the DIRECT cap-exempt channel); and the declaration + the ONE call after the far
# call inside `__wrap_sleh_abort` in src/entry/entry_trace.c (`entry_note_abt_pc(((const uint32_t
# *)(uintptr_t)regs)[STAGE90_SS_PC / 4], fsr, ((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_CPSR / 4],
# far_)`, using the frame's own `regs->pc`/`regs->cpsr` slots and the `fsr`/`far_` already read for the
# sleh trace - no new cp15 read, no new link edge).  All gated `#if defined(STAGE90_XNU_ABT_FAR) &&
# STAGE90_XNU_ABT_FAR && defined(STAGE90_XNU_ABT_PC) && STAGE90_XNU_ABT_PC` so OFF is byte-identical
# (1008's arm stays reproducible, design §3.1).  STAGE90_XNU_ABT_PC is in NEITHER $EXTRA NOR the
# exported pool defines: only the TWO entry-toolchain objects read it.  build_entry.sh REFUSES ABT_PC=1
# with ABT_FAR=0 (the pc recorder sits after the far recorder).  Design: docs/1009-design.md.
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
# 1009 PRESS PRE-REGISTRATION
# ====================================================================================================
# SWITCH SET.  1008's verbatim (STAGE90_XNU_ABT_FAR=1, STAGE90_XNU_ABT_SPAN=1,
# STAGE90_XNU_FREEZE_WITNESS=1, STAGE90_XNU_CADENCE=1, STAGE90_XNU_STALL=1, STAGE90_XNU_DYLD_WITNESS=1,
# STAGE90_XNU_SHREG_PROBE=1, STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1,
# STAGE90_XNU_USVC=1, STAGE90_XNU_SUBMAP_PMAP_SKIP=1, STAGE90_XNU_ABORT_TRACE=1, USB ladder
# PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0).  NEW switch
# STAGE90_XNU_ABT_PC=1 (the faulting-pc identity).  CADENCE=1, STALL=1, USVC=1, SUBMAP_PMAP_SKIP=1,
# ABORT_TRACE=1, FREEZE_WITNESS=1, STAGE90_XNU_ABT_SPAN=1, STAGE90_XNU_ABT_FAR=1 AND
# STAGE90_XNU_ABT_PC=1 must ALL be in the record.
#
# (0) THE 1009 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the EIGHTEEN
#     `xnu_live_abt_` key literals (the 6 span: n/seq/gap/call/maxcall/maxgap; the 5 far:
#     far/type/farn/maxfar/maxtype; the 7 pc: pcn/pc/fsr/abtcpsr/pcfar/nfar/nfsr) embedded in
#     out/stage90/xnu_arm_entry.elf, AND `entry_note_abt_pc` a DEFINED text symbol (a generated stand-in
#     would show 0), AND exactly ONE `b|bl <entry_note_abt_pc>` INSIDE `__wrap_sleh_abort`
#     ([[mi4-hooks-live-at-calls]] - inlined code between calls is unreachable; the call is the
#     wrapper's LAST statement, so a TAIL `b` is the expected spelling and a `bl`-only reader
#     false-refuses), exactly as build_1009.sh's delta clause and scripts/park_1009.sh assert.  If the
#     change is NOT in the image, NOTHING below may be read as a test - rebuild.
# (0a) THE 1008 ABORT-FAULT IDENTITY IS A REGRESSION: the five far `xnu_live_abt_` key literals (inside
#     the (0) EIGHTEEN) AND `entry_note_abt_far` a DEFINED text symbol AND exactly ONE `b|bl
#     <entry_note_abt_far>` inside `__wrap_sleh_abort` must STILL be present (1009's set is 1008's
#     verbatim).  A x0 means the far witness was dropped - rebuild.  PLUS the §4.3 fix
#     ([[mi4-1007-abort-span-rung]]): out/stage90/xnu_arm_entry_stubs.o must STILL have NO
#     `U entry_counter` (the §2.3 terminal stand-in that would END THE RUN on the abort path stays gone).
# (0b) THE 1007 SPAN WITNESS IS A REGRESSION: the SIX span `xnu_live_abt_` key literals (inside the (0)
#     EIGHTEEN) AND `entry_note_abt_span` a DEFINED text symbol AND exactly ONE `b|bl
#     <entry_note_abt_span>` inside `__wrap_sleh_abort` must STILL be present.  A x0 means the span
#     witness was dropped - rebuild.
# (0c) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals (n/d/cpsr/ien/
#     pend/ctlr) and the two `xnu_live_frzbase_` key literals (n/d) AND the two symbols
#     `entry_freeze_sample` / `entry_gic_freeze_state` must STILL be in the image x0 each (1009's set is
#     1008's verbatim).  A x0 means the freeze witness was dropped - rebuild.
# (0d) THE 1005 CADENCE WITNESS IS A REGRESSION: the four `xnu_live_cadence_` key literals (t/sync/tick/
#     irq) AND the four symbols `entry_board_tick_count` / `entry_board_irq_count` / `entry_cadence_sample`
#     / `entry_cadence_summary` must STILL be in the image.  A x0 means the cadence witness was dropped.
# (0e) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be in the
#     image x1.
# (0f) THE 1002 r3-SAVE FIX IS A REGRESSION: locore_fleh_swi's prologue must STILL be the six-word window
#     `push {r0, r1, r2, r3, r8, lr}` / `bl <entry_note_user_svc>` / `pop {r0, r1, r2, r3, r8, lr}`.  A
#     FOUR-word window means the fix regressed - rebuild.
# (F) THE FRONTIER UNDER TEST - WHICH INSTRUCTION THE STORM RE-RUNS, AND ITS DFSR (the readings 1008 did
#     not produce).  records/1008-press-read.txt, BY VALUE: the ~2.1 M-abort storm collapses onto the
#     CONSTANT `far=0x0007b000` (type=4 = STAGE90_T_DATA_ABT, a DATA abort) from ordinal 0x400 through
#     0x200000 (~2.09 M aborts) - ONE fault address retried ~2.09 M times - riding the ~18.7 s handler
#     call (`xnu_live_abt_maxcall` = 0x1570c1a7).  The ladder's `far` walks 12 distinct values (0x1000
#     ... 0x340e93c8) over ordinals 1..0x200, then collapses onto 0x7b000 at 0x400; the 1008 reader
#     reported `MIXED` because it counted the distinct values over the WHOLE ladder, blind to the TAIL.
#     THE READING (keys xnu_live_abt_pcn / _pc / _fsr / _abtcpsr / _pcfar, and the change-counts _nfar /
#     _nfsr): pair the pc rows to 1008's far rows by KEY EQUALITY (`pcn == farn` at every paired row -
#     both recorders advance one per abort, so a single `(n, farn, pcn, pc, fsr, abtcpsr, pcfar, nfar,
#     nfsr)` tuple reconstructs one abort across the recorders).  The partition (design §5):
#       (R) REPEAT   - `xnu_live_abt_nfar == 1` AND `xnu_live_abt_nfsr == 1` held across the tail (from
#                      ordinal 0x400 to 0x200000) => ONE faulting address, ONE status, retried ~2.09 M
#                      times.  This is the by-value statement 1008's set-based reader could not make.
#       CYCLING      - `nfsr > 1` => the retries are NOT identical (the fault STATUS cycles) - the
#                      "same fault re-run" reading is refuted; the DFSR carries the cycle.
#       SPREAD       - `nfar > 1` across the tail => the "constant far" was a sampling artifact; the
#                      storm spreads (1008's reading was wrong).
#       (U) USER     - `_abtcpsr` a USER-mode CPSR (and `_pcfar < 0x80000000`) => the loop is a USER
#                      reference, contradicting the 1007 live tail (kernel copyin); relocates the
#                      frontier to the user thread.
#       (K) KERNEL   - `_abtcpsr` a KERNEL-mode CPSR (0x…13 SVC / 0x…93 / 0x…d3) => the loop re-runs in
#                      the kernel - consistent with 1004/1007/1008.
#       PC CONST     - the tail `_pc` is CONSTANT => one instruction references 0x7b000: the loop is
#                      NAMED by its symbol.  A tail `_pc` that VARIES => different instructions reference
#                      the same address (a demand pattern) - the falsifier.
#     This is a pure MEASUREMENT: the run is EXPECTED to still freeze (1009 changes no kernel
#     behaviour) - the loop's IDENTITY is the finding.
#     FALSIFIERS (design §6): (a) NO `xnu_live_abt_pc`/`_pcn`/`_fsr` keys at all while `xnu_live_abt_n`
#     is present => the ABT_PC half did not reach the image (rebuild); (b) `xnu_live_abt_pcn` GAPS from
#     `xnu_live_abt_n` at a paired row => the pc recorder did not run once per abort - an INSTRUMENT
#     failure, not a kernel reading (why `pcn` is published at all); (c) `nfar > 1` across the tail =>
#     the "constant far" was a sampling artifact - the storm spreads (the strongest refutation of the
#     REPEAT interpretation, i.e. reading SPREAD); (d) `nfsr > 1` => the retries differ in status, not
#     one fault re-run (CYCLING); (e) the tail `_pc` NOT constant => different instructions reference
#     0x7b000; (f) `_abtcpsr` shows a mode CHANGE across the tail => the loop crosses the user/kernel
#     boundary; (g) `_fsr` a value the type dispatch cannot produce => the wrapper's pair read is
#     mismatched ([[mi4-measurement-defects]]).
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np ret=12`,
#     the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the whole capture
#     must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr>
#     size=<ptr> lr=<ptr>`, BOUNDED (max 8).  EXPECTED >= 1; most likely EXACTLY 2, obj = 999's two
#     VMHINT991 objects 0xc0005b48 / 0xc0005ab0.  ABSENCE while the (4) gate holds = rebuild.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1009_capture.py carries the gate: its [0] DEATH GATE + the kept
#     1004/1005/1006/1007/1008 clauses): no panic + the exec-done trio + the five probe keys + records
#     after the exec-returned point.  A faulting-pc-identity verdict is a READING taken from a live
#     image, so it counts only if the boot reached the print site (the [ABT3] section never certifies on
#     a dead run).
# (5) THE 1006 FREEZE WITNESS (kept a regression; keys xnu_live_freeze_* / xnu_live_frzbase_*).  1006
#     read it as a VALID SILENCE (zero freeze records, the idle path never entered) - 1009 expects the
#     SAME silence (a pure measurement changes nothing).
# (6) THE 1005 CADENCE WITNESS (kept a regression AND a second reading).  Keys xnu_live_cadence_t / _sync
#     / _tick / _irq plus the entry_cadence_summary() triples.  The ratio ds/max(dt,1) is the (b1)/(b2)/
#     full-stop partition; 1009 expects the SAME (b2) shape (a pure measurement changes nothing).
# (7) THE ABORT TRACE (1004, kept).  The full ordered sleh sequence (keys xnu_live_sleh_seq / _type /
#     _fsr / _far / _thr / _upc): a `far` that REPEATS = LOOP; a `far` that strictly ADVANCES = WALK.
# (8) THE USER-SVC FRONTIER (kept; keys xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr / _r0).
#     `_pid` = 1; `_pc` a user VA; `_cpsr` a USER-mode CPSR.
# (9) THE PET (kept).  The feed gate is `count >= bark/2` = 14.5 s.  If 1009 lives past ~14.5 s with the
#     read door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` MUST appear.
# (10) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
#     thinning ordinal stays 64.  The sleh caps stay RAISED to 512 under 1004's switch.
# (11) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) the (0) change is NOT in the image =>
#     REBUILD; (b) the 230x `usermode data abort at 0x4` recurs past `check_np ret=12` => the 1002
#     r3-save fix REGRESSED - rebuild; (c) `xnu_live_abt_pc`/`_pcn`/`_fsr` x0 in the capture while
#     `xnu_live_abt_n` is present => the ABT_PC half did not reach the image (the (0) rebuild signal);
#     (d) `xnu_live_abt_pcn` GAPS from `xnu_live_abt_n` at a paired row => an INSTRUMENT failure;
#     (e) a NON-MONOTONE `xnu_live_abt_n` => a MEASUREMENT defect; (f) death ELSEWHERE, later => the
#     frontier ADVANCED - record by value.  The FROZEN REGRESSION GUARDS - the geometry line exactly
#     once, `IOS7LAB cache mapping` x4, the shared region x1, `check_np ret=12` x1, `IOS7SHREG map_file
#     kr=0`, `IOS7SHREG map_slide ret=0` - are re-verified only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent - read the
# skipped-block shape IN THE IMAGE before any judgement; (c) the log ends with a capped key and no
# direct key after it = rebuild; (d) the log ends in a direct record = that record's moment IS the
# frontier finding this rung was built to produce - read the last direct keys (abt-pc last, then
# abt-far, then abt-span, then freeze, then cadence, then sleh, then usvc, then pet) as the frontier, and
# classify the loop's MECHANISM by (R)/CYCLING/SPREAD/(U)/(K)/PC CONST from the pc/fsr/abtcpsr ladder
# and the nfar/nfsr change-counts; (e) the (0) change is not in the image = the witness is not in the
# image - rebuild, nothing else is a test.
# ====================================================================================================
if ! grep -q '^# 1009 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1009 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003/1004/1005/1006/1007/1008"
  echo "          pattern: switch set / THE POINTS / failure signatures / falsification / regression"
  echo "          guards). The build side: scripts/build_1009.sh + scripts/park_1009.sh + the 1009"
  echo "          clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-0343ef45 ]] && {
  echo "REFUSING: out/stage90 holds 1008's arm (0343ef45) - the PRESSED predecessor (frontier: the storm"
  echo "is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort, from ordinal 0x400 through"
  echo "0x200000 - ~2.09 M retries of one fault address, riding the ~18.7 s handler call; adjudicated in"
  echo "records/1008-press-read.txt)."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-88074ab3 ]] && {
  echo "REFUSING: out/stage90 holds 1007's arm (88074ab3) - the PRESSED predecessor (frontier: the"
  echo "~24.4 s freeze is INSIDE ONE sleh_abort - maxcall 18.735 s = 0.768 x M - riding a FAULT STORM"
  echo "of ~2.1 M aborts whose fault ADDRESS is in no capture; adjudicated in"
  echo "records/1007-press-read.txt). Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-31bdebf3 ]] && {
  echo "REFUSING: out/stage90 holds 1006's arm (31bdebf3) - the PREVIOUS predecessor (frontier: the"
  echo "~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered;"
  echo "adjudicated in records/1006-press-read.txt). Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-ff937225 ]] && {
  echo "REFUSING: out/stage90 holds 1005's arm (ff937225) - the PRESSED predecessor (frontier: the STALL"
  echo "is (b2) a whole-interrupt-path freeze ~24.4 s; adjudicated in records/1005-press-read.txt)."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-582a19c8 ]] && {
  echo "REFUSING: out/stage90 holds 1004's arm (582a19c8) - the PRESSED predecessor (frontier: the"
  echo "STALL after dyld first touches a shared-cache page; adjudicated in records/1004-press-read.txt)."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - already pressed 2026-10-13 (pid 1 in the"
  echo "kernel's data-abort return path; adjudicated in records/1003-press-read.txt)."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt). Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1009 (scripts/build_1009.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1009 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1009 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip, 1003's stall + 1004's abort-trace instrument + 1005's cadence witness + 1006's freeze witness + 1007's abort-span witness + 1008's abort-fault identity AND 1009's faulting-pc identity =="
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
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004/1005/1006/1007/1008/1009)
STAGE90_XNU_ABORT_TRACE the abort trace - raised sleh caps + the frame's user pc (1004's; kept verbatim)
STAGE90_XNU_CADENCE the cadence witness - a timer-independent (syscall) sample + the all-interrupt counter (1005's; kept verbatim)
STAGE90_XNU_FREEZE_WITNESS the idle-path freeze witness - the dwell sampler + the GIC/CPSR triple (1006's; kept verbatim)
STAGE90_XNU_ABT_SPAN the abort-span witness - the gap + handler duration per sleh_abort (1007's; kept verbatim)
STAGE90_XNU_ABT_FAR the abort-fault identity - the fault ADDRESS + TYPE of the storm's aborts on the DIRECT channel (1008's; kept verbatim)
STAGE90_XNU_ABT_PC the faulting-pc identity - WHICH instruction faults + the DFSR + the change-counts on the DIRECT channel (1009's; a pure measurement rung)
EOF
echo "   (record carries the 1008 verbatim switch set PLUS the new STAGE90_XNU_ABT_PC; USVC, SUBMAP_PMAP_SKIP, STALL, ABORT_TRACE, CADENCE, FREEZE_WITNESS, ABT_SPAN and ABT_FAR all on)"

echo "== 2-0. the 1009 faulting-pc identity in the image (read by value BEFORE the press) + the 1008 abort-fault identity + the 1007 abort-span witness + the 1006 freeze witness + the 1005 cadence witness + the 1004 abort trace + the 1002 r3-save fix as regressions =="
# (0) THE 1009 CHANGE IS IN THE IMAGE: the EIGHTEEN `xnu_live_abt_` key literals embedded (the 6 span +
#     the 5 far + the 7 pc) AND the `entry_note_abt_pc` DEFINED text symbol (a generated stand-in would
#     show 0) AND exactly ONE `b|bl <entry_note_abt_pc>` INSIDE `__wrap_sleh_abort`
#     ([[mi4-hooks-live-at-calls]]).
_abt_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_abt_' || true)
echo "   (0) xnu_live_abt_* literals x$_abt_lit (want 18: 6 span n/seq/gap/call/maxcall/maxgap + 5 far far/type/farn/maxfar/maxtype + 7 pc pcn/pc/fsr/abtcpsr/pcfar/nfar/nfsr)"
[[ "$_abt_lit" == 18 ]] || {
  echo "REFUSING: the 1009 faulting-pc store is NOT in the image (literals x$_abt_lit want 18) - rebuild"; exit 1; }
# NOTE: `nm | grep -q` under `set -o pipefail` is a trap - `grep -q` exits on the first match, SIGPIPEs
# `nm`, and the pipeline's status becomes 141, vetoing a match that exists (the 487/998 SIGPIPE lesson).
# Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_abt_nm=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf)
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the image - an object did not rebuild or a stand-in was linked - rebuild"; exit 1; }
echo "   (0) entry_note_abt_pc is DEFINED T/t in the image"
# The witness's ONE call site: `__wrap_sleh_abort` carries exactly one entry_note_abt_pc call (the
# abort path IS the witness; a second site would publish a fault the classifier is not about).  The call
# is the wrapper's LAST statement, so a TAIL `b` is the expected spelling - accept `b` OR `bl`.
_ab_a=$(awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_sleh_abort is not in the image - the abort path the pc witness rides is gone"; exit 1; }
_ab_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ab_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_ab_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ab_a" --stop-address="${_ab_x:-0xffffffff}")
_ab_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' <<<"$_ab_body" || true)
echo "   (0) __wrap_sleh_abort calls entry_note_abt_pc: $_ab_pc (want 1)"
[[ "$_ab_pc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_pc exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# (0a) THE 1008 ABORT-FAULT IDENTITY IS A REGRESSION: the `entry_note_abt_far` DEFINED text symbol AND
#      exactly one `b|bl <entry_note_abt_far>` inside `__wrap_sleh_abort` (its five far key literals are
#      inside the (0) EIGHTEEN).  1009's set is 1008's verbatim.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the image - the 1008 recorder was dropped (1009 keeps 1008's set verbatim) - rebuild"; exit 1; }
_ab_fc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' <<<"$_ab_body" || true)
echo "   (0a) __wrap_sleh_abort calls entry_note_abt_far: $_ab_fc (want 1)"
[[ "$_ab_fc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_far exactly once (0 = the call site moved; >1 = it runs twice) - the 1008 abort-fault identity regressed - rebuild"; exit 1; }
# (0b) THE 1007 SPAN WITNESS IS A REGRESSION: the `entry_note_abt_span` DEFINED text symbol AND exactly
#      one `b|bl <entry_note_abt_span>` inside `__wrap_sleh_abort` (its six key literals are inside the
#      (0) EIGHTEEN).  1009's set is 1008's verbatim.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the image - the 1007 recorder was dropped (1009 keeps 1008's set verbatim) - rebuild"; exit 1; }
_ab_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$_ab_body" || true)
echo "   (0b) __wrap_sleh_abort calls entry_note_abt_span: $_ab_sc (want 1)"
[[ "$_ab_sc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_span exactly once (0 = the call site moved; >1 = it runs twice) - the 1007 span witness regressed - rebuild"; exit 1; }
# §4.3 THE ARGUMENT-PASSING FIX (1007, kept a regression): entry_stubs.o must NOT carry an UNDEFINED
# `entry_counter` (the §2.3 terminal stand-in).  If a recorder still calls entry_counter(), the stand-in
# can END THE RUN.
_ab_uc=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o | grep -cE '^[[:space:]]*U entry_counter$' || true)
echo "   (0b) xnu_arm_entry_stubs.o U entry_counter: $_ab_uc (want 0 - the §4.3 fix, kept as a regression)"
[[ "$_ab_uc" == 0 ]] || {
  echo "REFUSING: entry_stubs.o still references entry_counter (U x$_ab_uc want 0) - a recorder calls the clock directly; the §4.3 argument-passing fix regressed - rebuild"; exit 1; }

# (0c) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals + the two
#      `xnu_live_frzbase_` key literals embedded AND the two DEFINED text symbols.
_frz_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_freeze_' || true)
echo "   (0c) xnu_live_freeze_* literals x$_frz_lit (want 6: n/d/cpsr/ien/pend/ctlr)"
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the 1006 freeze store is NOT in the image (literals x$_frz_lit want 6) - 1009's set is 1008's verbatim, so it must be present - rebuild"; exit 1; }
_idl_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_frzbase_' || true)
echo "   (0c) xnu_live_frzbase_* literals x$_idl_lit (want 2: n/d)"
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the 1006 idle-baseline store is NOT in the image (literals x$_idl_lit want 2) - rebuild"; exit 1; }
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the image - the 1006 freeze witness regressed - rebuild"; exit 1; }
done
echo "   (0c) the two freeze symbols are DEFINED T/t in the image"
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
echo "   (0c) __wrap_machine_idle calls entry_freeze_sample: $_frz_sc (want 1)"
[[ "$_frz_sc" == 1 ]] || {
  echo "REFUSING: __wrap_machine_idle does not call entry_freeze_sample exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# (0d) THE 1005 CADENCE WITNESS IS A REGRESSION: the four key literals AND the four DEFINED symbols.
_cad_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_cadence_' || true)
echo "   (0d) xnu_live_cadence_* literals x$_cad_lit (want 4: t/sync/tick/irq)"
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the 1005 cadence store is NOT in the image (literals x$_cad_lit want 4) - 1009's set is 1008's verbatim, so it must be present - rebuild"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the image - the 1005 cadence witness regressed - rebuild"; exit 1; }
done
echo "   (0d) the four cadence symbols are DEFINED T/t in the image"
# (0e) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal x1.
_d4_upc=$(strings out/stage90/xnu_arm_entry.elf | grep -c '^xnu_live_sleh_upc$' || true)
echo "   (0e) xnu_live_sleh_upc literal x$_d4_upc (want 1)"
[[ "$_d4_upc" == 1 ]] || {
  echo "REFUSING: the 1004 abort trace is NOT in the image (upc x$_d4_upc want 1) - 1009's set is 1008's verbatim, so it must be present - rebuild"; exit 1; }
# (0f) THE 1002 r3-SAVE FIX IS A REGRESSION. The prologue of locore_fleh_swi must STILL be six-word.
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
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1009.sh."
  exit 1; }
echo "   (0f) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1009 faulting-pc identity"

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
# 1009 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1009-$(date -u +%Y%m%d-%H%M%S).txt"
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
echo "   (adjudicate the recovered capture with: python3 tools/check_1009_capture.py <capture>"
echo "    - the [0] DEATH GATE and the 1004/1005/1006/1007/1008 clauses are kept, and the [ABT3]"
echo "    FAULTING-PC-IDENTITY section is the 1009 rung's own reading: the pc/fsr/abtcpsr ladder"
echo "    (xnu_live_abt_pcn/_pc/_fsr/_abtcpsr/_pcfar, paired to 1008's far rows by pcn==farn) and the"
echo "    nfar/nfsr change-counts name WHICH instruction the constant-far loop re-runs and its DFSR, and"
echo "    classify it (R) REPEAT / CYCLING / SPREAD / (U) USER / (K) KERNEL / PC CONST, with"
echo "    xnu_live_stub_hit_seq==0 proving the §2.3 stand-in did not fire)"
echo "== DONE =="