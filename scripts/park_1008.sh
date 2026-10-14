#!/usr/bin/env bash
# ====================================================================================================
# park_1008.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1007.sh.
#
# Park 1008 and append its block to records/revert-set.txt.
#
# 1008 = 1007's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS and ABT_SPAN all stay 1) + exactly ONE
# new switch STAGE90_XNU_ABT_FAR - a pure MEASUREMENT rung (no kernel behaviour change) that adds an
# ABORT-FAULT-IDENTITY witness: `__wrap_sleh_abort` forwards the `far`/`type` it ALREADY reads (for the
# sleh trace) to `entry_note_abt_far(call, far, type)`, which publishes the fault ADDRESS and TYPE of
# the storm on the DIRECT channel under the five keys `xnu_live_abt_{far,type,farn,maxfar,maxtype}` -
# the maxcall latch (where `call` sets a new max: publishes `maxfar`/`maxtype`) plus the SAME
# powers-of-two ladder the span recorder uses.
#
# THE FRONTIER 1008 PROBES: the 1007 read (records/1007-press-read.txt) settled the freeze's LOCATION -
# the ~24.4 s `xnu_live_tick_sum_maxd` freeze (0x1be892af = 468,226,735 counts = 24.387 s at 19.2 MHz)
# sits INSIDE ONE `sleh_abort` (`xnu_live_abt_maxcall` = 0x1570e6a5 = 18.735 s = 0.768 x M),
# corroborated by the `abt_call` ladder (0x100 -> 5.322 s, 0x200 -> 3.776 s), riding a FAULT STORM of
# ~2.1 million aborts (abt_n == abt_seq at all 23 rows, both reaching 0x200000) that the CAPPED live
# channel hid at #40 (24,023/24,576 = 97.8 % full, then SDHCI page-ins filled the last 553 to the cap).
# 1008 resolves the one reading 1007 explicitly did NOT produce: **the storm's fault ADDRESS and TYPE**.
# Its `(S) SPREAD` vs `(R) REPEAT` classifier separates "2.1 M DISTINCT faults" (a progressive `far`)
# from "ONE fault retried 2.1 M times" (a `far` REPEATING), which names the frontier's mechanism
# (design docs/1008-design.md).
#
# 1008's change is SOURCE-ONLY, in TWO existing src/ files, and NO tree edit and NO stage driver:
#   * src/entry/entry_stubs.c  - the recorder `entry_note_abt_far(uint32_t call, uint32_t far,
#     uint32_t type)` after the 1007 span recorder's `#endif` (it takes the values as arguments - it
#     NEVER calls `entry_counter()`; it forwards to `entry_write_kv`, the DIRECT cap-exempt channel);
#   * src/entry/entry_trace.c  - the declaration + the ONE call after the span call inside
#     `__wrap_sleh_abort` (`entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type)`), using the
#     file's own `far_`/`type` already read for the sleh trace - no new coprocessor read, no new extern.
# The ABT_FAR guards are SEPARATE from ABT_SPAN's so the OFF image is byte-identical (the guards are
# `defined()`-first, not a widened constant - [[mi4-off-option-two-spellings]]).  build_entry.sh
# REFUSES ABT_FAR=1 with ABT_SPAN=0 (the far recorder sits after the span recorder; the arm key must
# name both).  STAGE90_XNU_ABT_FAR is in NEITHER `$EXTRA` NOR the exported pool defines: it is read by
# ONLY the TWO entry-toolchain objects (entry_stubs.c via STUB_DEFINES, entry_trace.c via
# STUB_DEFINES_TRACE) - no pe_msm8974.c change, no pool-object rebuild, no $EXTRA entry.  Only an
# EXPORTS pin so the record carries it (the same shape as 1007's ABT_SPAN).
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1008 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1008.sh, run against the linked image): the abort-fault identity in the
# linked image (the 11 `xnu_live_abt_` key literals x? each - the 6 span + the 5 far, `entry_note_abt_
# far` DEFINED T/t, and the ONE `b/bl` inside `__wrap_sleh_abort` image-wide), the 1007 span witness as
# a REGRESSION (the 6 span keys, `entry_note_abt_span` DEFINED T/t, exactly ONE `b/bl` to it), and the
# FROZEN 1007 elf (armed-d13-88074ab3, the PRESSED 1007 arm) as the NEGATIVE CONTROL (`xnu_live_abt_`
# x6, `entry_note_abt_span` >= 1, but `entry_note_abt_far` x0 and the far literals x0 - the control
# proves the 1008 change is NOT in the 1007 arm).  The 1006 freeze witness, the 1005 cadence witness,
# the 1004 abort trace (`xnu_live_sleh_upc` x1), the 1003 stall instrument and the 1002 six-word save
# set are asserted as REGRESSIONS (all still present).  The 996/997/998/999/1000/1001 checkers rerun
# ON the 1008 image with the FROZEN images as negative controls (the probe checker's ONE by-design
# frozen 20 B clause is the only allowed FAIL).
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to re-park the
# PRESSED/known arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-88074ab3 ]] && { echo "REFUSING: $LIVE holds 1007's arm (88074ab3), the PRESSED predecessor (VERDICT (C): the ~24.4 s freeze is INSIDE ONE sleh_abort - maxcall 18.735 s = 0.768 x M - riding a STORM of ~2.1 M aborts; the storm's fault ADDRESS is in no capture; adjudicated in records/1007-press-read.txt); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor (VERDICT: the STALL is (b2) a whole-interrupt-path freeze ~24.4 s, adjudicated in records/1005-press-read.txt); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor (VERDICT WALK then the tick collapsed - the frontier was the STALL, adjudicated in records/1004-press-read.txt); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the PRESSED predecessor (pid 1 is in the kernel's data-abort return path, adjudicated in records/1003-press-read.txt); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1008 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1008 first"; exit 1; }

# 1008's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_ABT_FAR=1.
# 1007's abort-span witness, 1006's freeze witness, 1005's cadence witness, 1004's abort trace,
# 1003's stall switch, 1002's r3-save fix (USVC hook ON) and 1001's submap skip ALL stay on as
# regressions.
grep -q '^STAGE90_XNU_ABT_FAR=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_FAR=1 - 1008's abort-fault identity was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_ABT_SPAN=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_SPAN=1 - 1007's abort-span witness is part of 1008's set verbatim (the far recorder sits after the span recorder) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness is part of 1008's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1008's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1008's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1008's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: eight things.
# (0) THE 1008 CHANGE (the abort-fault identity) must be in the linked image: the eleven `xnu_live_abt_`
#     key literals (the 6 span n/seq/gap/call/maxcall/maxgap + the 5 far far/type/farn/maxfar/maxtype),
#     `entry_note_abt_far` DEFINED T/t, AND exactly ONE `b/bl` inside `__wrap_sleh_abort` image-wide.
# (1) THE 1007 CHANGE (the abort-span witness) is a REGRESSION: the six span key literals,
#     `entry_note_abt_span` DEFINED T/t, and exactly ONE `b/bl` to it image-wide must STILL be present.
# (2) THE 1006 CHANGE (the freeze witness) must be in the linked image: the six `xnu_live_freeze_`
#     key literals (n/d/cpsr/ien/pend/ctlr), the two `xnu_live_frzbase_` key literals (n/d), and the two
#     symbols DEFINED T/t (entry_freeze_sample, entry_gic_freeze_state).
# (3) THE 1005 CADENCE WITNESS is a REGRESSION: the four `xnu_live_cadence_` key literals AND the four
#     symbols DEFINED T/t must STILL be present.
# (4) THE 1004 ABORT TRACE is a REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be present.
# (5) THE 1003 STALL INSTRUMENT is a REGRESSION: its recorder + 7 `xnu_live_stall_*` literals must
#     STILL be present.
# (6) THE 1002 r3-SAVE FIX is a REGRESSION: locore_fleh_swi must STILL carry the six-word save set.
# (7) The FROZEN 1007 elf (out/stage90/frozen/armed-d13-88074ab3/) as the NEGATIVE CONTROL: it must
#     carry `xnu_live_abt_` x6 and an `entry_note_abt_span` symbol but NO 1008 literal and NO
#     `entry_note_abt_far` symbol (a reader that cannot fail certifies nothing).
# 1008's NEW instrument (the abort-fault identity), BY VALUE, FIRST.
_ab_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_abt_' || true)
[[ "$_ab_lit" == 11 ]] || {
  echo "REFUSING: the parked image does not carry the 11 xnu_live_abt_ key literals (6 span: n/seq/gap/call/maxcall/maxgap + 5 far: far/type/farn/maxfar/maxtype) (x$_ab_lit, want 11) - the ABT_SPAN or ABT_FAR store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the parked image - the 1008 recorder is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
# `b` OR `bl`: the recorder is a TAIL call (`b <entry_note_abt_far>`), measured on the first build.
_abf_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' || true)
[[ "$_abf_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abf_call call site(s) to entry_note_abt_far (want 1, inside __wrap_sleh_abort) - the 1008 witness is not wired to the abort path"; exit 1; }
echo "park_1008: by value - xnu_live_abt_* literals x$_ab_lit (6 span + 5 far), entry_note_abt_far DEFINED T/t, exactly ONE b/bl (the abort-fault identity is present)"
# 1007's abort-span witness is a REGRESSION: its recorder + ONE call site must STILL be present.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder was dropped (1008 keeps 1007's set verbatim)"; exit 1; }
_ab_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' || true)
[[ "$_ab_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_ab_call call site(s) to entry_note_abt_span (want 1, inside __wrap_sleh_abort) - the 1007 span witness regressed"; exit 1; }
echo "park_1008: by value - entry_note_abt_span DEFINED T/t with exactly ONE b/bl (1007's abort-span witness kept)"
_frz_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_freeze_' || true)
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the parked image does not carry the 6 xnu_live_freeze_ key literals (n/d/cpsr/ien/pend/ctlr) (x$_frz_lit, want 6) - the 1006 store is gone"; exit 1; }
_idl_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_frzbase_' || true)
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the parked image does not carry the 2 xnu_live_frzbase_ key literals (n/d) (x$_idl_lit, want 2) - the 1006 idle-baseline store is gone"; exit 1; }
# NOTE: `nm | grep -q` is a trap here - `grep -q` exits on the first match, SIGPIPEs `nm`, and `set -o
# pipefail` then reports the pipeline status 141, vetoing a match that exists (the 487/998 SIGPIPE
# lesson).  Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_frz_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the parked image - the 1006 symbol is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
echo "park_1008: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The call site must be INSIDE __wrap_machine_idle, image-wide exactly once ([[mi4-hooks-live-at-calls]]).
_frz_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' || true)
[[ "$_frz_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_frz_call call site(s) to entry_freeze_sample (want 1, inside __wrap_machine_idle) - the 1006 sampler is not wired to the idle path"; exit 1; }
echo "park_1008: by value - exactly ONE bl to entry_freeze_sample in the linked image (the idle-path call site)"

# 1005's cadence witness is a REGRESSION: its four key literals and four symbols must STILL be present.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (t/sync/tick/irq) (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed (1008 keeps 1007's set verbatim)"; exit 1; }
done
echo "park_1008: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1008: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1008: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1008: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1007 elf as the NEGATIVE CONTROL for the new reader.
_ctrl=out/stage90/frozen/armed-d13-88074ab3/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
  _c_span=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_span$' || true)
  _c_farsym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_far$' || true)
  _c_farlit=$(strings "$_ctrl" | grep -cE 'xnu_live_abt_(far|type|farn|maxfar|maxtype)$' || true)
  [[ "$_c_ab" == 6 && "$_c_span" -ge 1 && "$_c_farsym" == 0 && "$_c_farlit" == 0 ]] || {
    echo "REFUSING: the frozen 1007 elf does not have the pre-1008 shape (xnu_live_abt_=$_c_ab want 6, entry_note_abt_span=$_c_span want >=1, entry_note_abt_far=$_c_farsym want 0, far literals=$_c_farlit want 0) - the control image is not the pre-1008 one"; exit 1; }
  echo "park_1008: by value - NEGATIVE CONTROL (frozen 1007 elf): xnu_live_abt_ x$_c_ab (the 6 span keys), entry_note_abt_span x$_c_span, entry_note_abt_far x$_c_farsym (the reader can fail), far literals x$_c_farlit"
else
  echo "park_1008: WARNING - the frozen 1007 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1008: THE ABORT-FAULT IDENTITY (forwards the `far`/`type` `__wrap_sleh_abort` already\n# reads to `entry_note_abt_far(call, far, type)`, publishing the fault ADDRESS and TYPE of the storm\n# on the DIRECT channel under the five keys xnu_live_abt_{far,type,farn,maxfar,maxtype} - the maxcall\n# latch plus the powers-of-two ladder), so the capture NAMES the ~18.7 s abort-s fault address and its\n# (S) SPREAD vs (R) REPEAT shape (design docs/1008-design.md).\n' "$ARM"
  printf '# Switch set is 1007-s VERBATIM plus STAGE90_XNU_ABT_FAR=1 (ABT_SPAN, FREEZE_WITNESS, CADENCE,\n'
  printf '# STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and\n'
  printf '# SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (88074ab3 -> %s); the PAYLOAD moves (it embeds\n' "${ARM#armed-d13-}"
  printf '# the entry blob; the 29 s watchdog constant is 999-s, unchanged); the TREE does NOT move - 1008\n'
  printf '# has NO tree edit and NO stage driver (the change is TWO src/ files: entry_stubs.c the recorder\n'
  printf '# entry_note_abt_far, entry_trace.c the declaration + the ONE call inside __wrap_sleh_abort).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1007-press-read.txt): the ~24.4 s xnu_live_tick_sum_maxd freeze\n'
  printf '# (0x1be892af = 468,226,735 counts = 24.387 s at 19.2 MHz) sits INSIDE ONE sleh_abort -\n'
  printf '# xnu_live_abt_maxcall = 0x1570e6a5 = 359,720,613 counts = 18.735 s = 0.768 x M - corroborated by\n'
  printf '# the abt_call ladder (0x100 -> 5.322 s, 0x200 -> 3.776 s), riding a FAULT STORM of ~2.1 M aborts\n'
  printf '# (abt_n == abt_seq at all 23 rows, both reaching 0x200000) that the CAPPED live channel hid at\n'
  printf '# #40 (24,023/24,576 = 97.8 %% full, then SDHCI page-ins filled the last 553 to the cap).  The\n'
  printf '# storm-s fault ADDRESS is in NO capture - 1008 publishes it (the missing reading).\n#\n'
  printf '# THE 1008 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1008.sh: THE READING is xnu_live_abt_far/\n'
  printf '# xnu_live_abt_type (and the latched maxfar/maxtype) vs the span ladder: (S) SPREAD = the ladder-s\n'
  printf '# `far` is DISTINCT and PROGRESSIVE = the ~2.1 M aborts are ~2.1 M distinct faults; (R) REPEAT =\n'
  printf '# the `far` repeats (a small set of addresses) = ONE fault retried = a self-re-entering fault loop.\n#\n'
  printf '# VERIFIED BY VALUE (build_1008.sh): the linked image carries the 11 xnu_live_abt_ key literals\n'
  printf '# (6 span + 5 far), entry_note_abt_far DEFINED T/t, exactly ONE b/bl inside __wrap_sleh_abort\n'
  printf '# image-wide, with the FROZEN 1007 elf (armed-d13-88074ab3) as the NEGATIVE CONTROL (xnu_live_abt_\n'
  printf '# x6, entry_note_abt_span x>=1, but entry_note_abt_far x0 and the far literals x0 - the 1008 change\n'
  printf '# is NOT in the 1007 arm); the 1007 span witness, the 1006 freeze witness, the 1005 cadence\n'
  printf '# witness, the 1004 abort trace (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002\n'
  printf '# six-word save set asserted present (regressions).  The 996/997/998/999/1000/1001 checkers rerun\n'
  printf '# ON the 1008 image - the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1008-abtfar;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"