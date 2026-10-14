#!/usr/bin/env bash
# ====================================================================================================
# park_1007.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1006.sh.
#
# Park 1007 and append its block to records/revert-set.txt.
#
# 1007 = 1006's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE and FREEZE_WITNESS all stay 1) + exactly ONE new switch
# STAGE90_XNU_ABT_SPAN - a pure MEASUREMENT rung (no kernel behaviour change) that adds an ABORT-SPAN
# WITNESS: `__wrap_sleh_abort` brackets each `__real_sleh_abort` call with the free-running generic
# timer and calls `entry_note_abt_span(t0, call)`, which publishes per abort the GAP to the previous
# abort and the handler's own DURATION, under the six keys `xnu_live_abt_{n,seq,gap,call,maxcall,maxgap}`
# (first four in full, then the powers-of-two ladder; the two maxima latched WHERE THEY CHANGE).
#
# THE FRONTIER 1007 PROBES: the 1006 read (records/1006-press-read.txt) settled the frontier's PATH -
# the ~24.4 s `xnu_live_tick_sum_maxd` freeze (0x1be87a47 = 468,220,487 counts = 24.386 s at 19.2 MHz)
# does NOT overlap the idle path (`__wrap_machine_idle` is NEVER entered; its sampler fired 0x).  The
# terminal frame (1005) is `sleh_abort+0x468`, an unconditional forward branch, NOT a fault-return
# self-loop - so the freeze rides the NON-IDLE abort path (1004 saw ~40 aborts walking forward).  1007
# puts the clock on that path: the capture says whether the 24.4 s sits BETWEEN two aborts, INSIDE one
# abort, or AFTER the last abort (design docs/1007-design.md §5 classifier).
#
# 1007's change is SOURCE-ONLY, in TWO existing src/ files, and NO tree edit and NO stage driver:
#   * src/entry/entry_stubs.c  - the recorder `entry_note_abt_span(uint32_t t0, uint32_t call)` (it
#     takes the clock VALUES as arguments - it NEVER calls `entry_counter()`);
#   * src/entry/entry_trace.c  - the declaration + the ONE bracket inside `__wrap_sleh_abort`, using the
#     file's own `static inline entry_counter()` - no new extern, no new link edge.
# 1007 ALSO carries §4.3 (the MANDATORY fix): the 1006 sampler's `entry_counter()` CALL in
# `entry_stubs.c` is converted to an argument (`void entry_freeze_sample(uint32_t now)`, its caller at
# `__wrap_machine_idle` passing the clock).  This closes a by-value DEFECT in the inherited image
# (design §2.3): `entry_stubs.c`'s `entry_counter` resolved to a TERMINAL generated stand-in
# (`8049cce4 T entry_counter` -> `entry_stub_hit` -> `entry_epilogue`) that would END THE RUN on the
# abort path - dormant at 1006 only because the idle path it rode was never entered.
# All gated `#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN`; OFF is byte-identical (the
# guards are `defined()`-first, not a widened constant - [[mi4-off-option-two-spellings]]).
# STAGE90_XNU_ABT_SPAN is in NEITHER `$EXTRA` NOR the exported pool defines: it is read by ONLY the
# TWO entry-toolchain objects (entry_stubs.c via STUB_DEFINES, entry_trace.c via STUB_DEFINES_TRACE) -
# no pe_msm8974.c change, no pool-object rebuild, no $EXTRA entry.  Only an EXPORTS pin so the record
# carries it (the same shape as 1006's FREEZE_WITNESS).
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1007 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1007.sh, run against the linked image): the abort-span witness in the linked
# image (the 6 `xnu_live_abt_` key literals x1 each, `entry_note_abt_span` DEFINED T/t, and the ONE `bl`
# inside `__wrap_sleh_abort` image-wide), the §4.3 fix (entry_stubs.o has NO `U entry_counter`; the
# generated stand-in is gone from realstubs.c), and the FROZEN 1006 elf (armed-d13-31bdebf3, the PRESSED
# 1006 arm) as the NEGATIVE CONTROL (`xnu_live_abt_` x0, `entry_note_abt_span` x0 - and by value the
# control DOES carry the `entry_counter` stand-in at 8049cce4, demonstrating the §2.3 defect 1007
# closes).  The 1006 freeze witness, the 1005 cadence witness, the 1004 abort trace (`xnu_live_sleh_upc`
# x1), the 1003 stall instrument and the 1002 r3-save fix are asserted as REGRESSIONS (all still present).
# The 996/997/998/999/1000/1001 checkers rerun ON the 1007 image with the FROZEN images as negative
# controls (the probe checker's ONE by-design frozen 20 B clause is the only allowed FAIL).
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
[[ $ARM == armed-d13-31bdebf3 ]] && { echo "REFUSING: $LIVE holds 1006's arm (31bdebf3), the PRESSED predecessor (VERDICT: the ~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered - so the freeze rides the NON-IDLE abort path; adjudicated in records/1006-press-read.txt); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor (VERDICT: the STALL is (b2) a whole-interrupt-path freeze ~24.4 s, adjudicated in records/1005-press-read.txt); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor (VERDICT WALK then the tick collapsed - the frontier was the STALL, adjudicated in records/1004-press-read.txt); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the PRESSED predecessor (pid 1 is in the kernel's data-abort return path, adjudicated in records/1003-press-read.txt); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1007 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1007 first"; exit 1; }

# 1006's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_FREEZE_WITNESS=1.
# 1005's cadence witness, 1004's abort trace, the 1003 stall switch, the 1002 r3-save fix (USVC hook
# ON) and 1001's submap skip ALL stay on as regressions.
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness is part of 1007's set verbatim and must stay on"; exit 1; }
# 1007's NEW key MUST be in the record: the park refuses an arm whose record does not carry
# STAGE90_XNU_ABT_SPAN=1.
grep -q '^STAGE90_XNU_ABT_SPAN=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_SPAN=1 - 1007's abort-span witness was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1007's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1006's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1006's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: eight things.
# (0) THE 1007 CHANGE (the abort-span witness) must be in the linked image: the six `xnu_live_abt_`
#     key literals (n/seq/gap/call/maxcall/maxgap), `entry_note_abt_span` DEFINED T/t, exactly ONE `bl`
#     inside `__wrap_sleh_abort` image-wide, AND the §4.3 fix (entry_stubs.o has NO `U entry_counter`;
#     realstubs.c has NO terminal `entry_counter` stand-in).
# (1) THE 1006 CHANGE (the freeze witness) must be in the linked image: the six `xnu_live_freeze_`
#     key literals (n/d/cpsr/ien/pend/ctlr), the two `xnu_live_frzbase_` key literals (n/d), and the two
#     symbols DEFINED T/t (entry_freeze_sample, entry_gic_freeze_state).
# (2) THE 1005 CADENCE WITNESS is a REGRESSION: the four `xnu_live_cadence_` key literals AND the four
#     symbols DEFINED T/t must STILL be present (1007 keeps 1006's set verbatim).
# (3) THE 1004 ABORT TRACE is a REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be present.
# (4) THE 1003 STALL INSTRUMENT is a REGRESSION: its recorder + 7 `xnu_live_stall_*` literals must
#     STILL be present.
# (5) THE 1002 r3-SAVE FIX is a REGRESSION: locore_fleh_swi must STILL carry the six-word save set.
# (6) The FROZEN 1006 elf (out/stage90/frozen/armed-d13-31bdebf3/) as the NEGATIVE CONTROL: it must
#     carry `xnu_live_abt_` x0 AND no `entry_note_abt_span` symbol (a reader that cannot fail
#     certifies nothing).  By value it DOES carry the `entry_counter` stand-in at 8049cce4 - the §2.3
#     defect 1007 closes.
# 1007's NEW instrument (the abort-span witness), BY VALUE, FIRST.
_ab_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_abt_' || true)
[[ "$_ab_lit" == 6 ]] || {
  echo "REFUSING: the parked image does not carry the 6 xnu_live_abt_ key literals (n/seq/gap/call/maxcall/maxgap) (x$_ab_lit, want 6) - the 1007 store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
# `b` OR `bl`: the recorder is a TAIL call (`b <entry_note_abt_span>`), measured on the first build.
_ab_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' || true)
[[ "$_ab_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_ab_call call site(s) to entry_note_abt_span (want 1, inside __wrap_sleh_abort) - the 1007 witness is not wired to the abort path"; exit 1; }
echo "park_1007: by value - xnu_live_abt_* literals x$_ab_lit, entry_note_abt_span DEFINED T/t, exactly ONE b/bl (the abort-span witness is present)"
# §4.3 - the MANDATORY companion fix that closes the §2.3 terminal stand-in.
if [[ -f out/stage90/xnu_arm_entry_stubs.o ]]; then
  _ab_u=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o 2>/dev/null | grep -cE '^[[:space:]]*U entry_counter$' || true)
  [[ "$_ab_u" == 0 ]] || {
    echo "REFUSING: entry_stubs.o still carries an undefined \`U entry_counter\` - the §2.3 landmine: on the abort path (which 1007 rides ~40x) the terminal stand-in would end the run at entry_stub_hit -> entry_epilogue"; exit 1; }
  echo "park_1007: by value - entry_stubs.o has NO \`U entry_counter\` (§4.3 argument-passing fix holds)"
fi
if [[ -f out/stage90/xnu_arm_entry_realstubs.c ]]; then
  _ab_gen=$(grep -cE '^void entry_counter\(void\)' out/stage90/xnu_arm_entry_realstubs.c || true)
  [[ "$_ab_gen" == 0 ]] || {
    echo "REFUSING: the terminal entry_counter STAND-IN is still generated in realstubs.c - the §4.3 fix did not reach the sampler"; exit 1; }
  echo "park_1007: by value - realstubs.c generates NO \`void entry_counter(void)\` stand-in (§2.3 closed)"
fi
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
echo "park_1007: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The call site must be INSIDE __wrap_machine_idle, image-wide exactly once ([[mi4-hooks-live-at-calls]]).
_frz_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' || true)
[[ "$_frz_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_frz_call call site(s) to entry_freeze_sample (want 1, inside __wrap_machine_idle) - the 1006 sampler is not wired to the idle path"; exit 1; }
echo "park_1007: by value - exactly ONE bl to entry_freeze_sample in the linked image (the idle-path call site)"

# 1005's cadence witness is a REGRESSION: its four key literals and four symbols must STILL be present.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (t/sync/tick/irq) (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed (1006 keeps 1005's set verbatim)"; exit 1; }
done
echo "park_1007: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1007: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1007: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1007: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1006 elf as the NEGATIVE CONTROL for the new reader.
_ctrl=out/stage90/frozen/armed-d13-31bdebf3/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
  _c_absym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_span$' || true)
  [[ "$_c_ab" == 0 && "$_c_absym" == 0 ]] || {
    echo "REFUSING: the frozen 1006 elf already carries the 1007 change (xnu_live_abt_=$_c_ab want 0, entry_note_abt_span=$_c_absym want 0) - the control image is not the pre-1007 one"; exit 1; }
  _c_ec=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_counter$' || true)
  echo "park_1007: by value - NEGATIVE CONTROL (frozen 1006 elf): xnu_live_abt_ x$_c_ab, entry_note_abt_span x$_c_absym (the reader can fail); the control ALSO carries the §2.3 entry_counter stand-in x$_c_ec (want >= 1)"
else
  echo "park_1007: WARNING - the frozen 1006 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1007: THE ABORT-SPAN WITNESS (brackets each `__wrap_sleh_abort` call with the free-running\n# generic timer and publishes, per abort, the GAP to the previous abort and the handler`s own\n# DURATION, keys xnu_live_abt_{n,seq,gap,call,maxcall,maxgap}; first four in full then a\n# powers-of-two ladder; the two maxima latched WHERE THEY CHANGE), so the capture says whether the\n# ~24.4 s freeze sits BETWEEN two aborts, INSIDE one abort, or AFTER the last abort (design §5\n# classifier A/B/C/D).\n' "$ARM"
  printf '# Switch set is 1006-s VERBATIM plus STAGE90_XNU_ABT_SPAN=1 (FREEZE_WITNESS, CADENCE, STALL,\n'
  printf '# ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and\n'
  printf '# SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (31bdebf3 -> %s); the PAYLOAD moves (it embeds\n' "${ARM#armed-d13-}"
  printf '# the entry blob; the 29 s watchdog constant is 999-s, unchanged); the TREE does NOT move - 1007\n'
  printf '# has NO tree edit and NO stage driver (the change is TWO src/ files: entry_stubs.c the recorder\n'
  printf '# entry_note_abt_span, entry_trace.c the declaration + the ONE bracket inside __wrap_sleh_abort),\n'
  printf '# PLUS the MANDATORY §4.3 fix (entry_freeze_sample takes the clock as an argument).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1006-press-read.txt): the ~24.4 s xnu_live_tick_sum_maxd freeze\n'
  printf '# (0x1be87a47 = 468,220,487 counts = 24.386 s at 19.2 MHz) does NOT overlap the idle path -\n'
  printf '# __wrap_machine_idle is NEVER entered on the recent D13 tail (its sampler fired 0x).  The terminal\n'
  printf '# frame is sleh_abort+0x468, an unconditional forward branch, NOT a fault-return self-loop - so the\n'
  printf '# freeze rides the NON-IDLE abort path (1004 saw ~40 aborts walking forward).  1007 brackets that\n'
  printf '# path.  NOTE (1007-s own correction): 1007 CLOSES a by-value defect in the INHERITED image - the\n'
  printf '# 1006 sampler called entry_counter() from entry_stubs.c, which resolved to a TERMINAL generated\n'
  printf '# stand-in (8049cce4 T entry_counter -> entry_stub_hit -> entry_epilogue) that would END THE RUN on\n'
  printf '# the abort path; dormant only because the idle path it rode is never entered.  §4.3 routes the\n'
  printf '# clock in as an argument and the build refuses the stand-in (§6e).\n#\n'
  printf '# THE 1007 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1007.sh: THE READING is xnu_live_abt_maxcall and\n'
  printf '# xnu_live_abt_maxgap against xnu_live_tick_sum_maxd (M): (A) both << M = the 24.4 s is NOT on the\n'
  printf '# abort path; (B) maxgap ~ M, maxcall small = the freeze is BETWEEN two aborts; (C) maxcall ~ M =\n'
  printf '# ONE abort took 24.4 s; (D) n/seq plateau with no key near M = the freeze is AFTER the last abort.\n'
  printf '# xnu_live_stub_hit_seq == 0 is the §2.3 guard (the terminal stand-in must NOT fire).\n#\n'
  printf '# VERIFIED BY VALUE (build_1007.sh): the linked image carries the 6 xnu_live_abt_ key literals,\n'
  printf '# entry_note_abt_span DEFINED T/t, exactly ONE bl inside __wrap_sleh_abort image-wide, and the §4.3\n'
  printf '# fix (entry_stubs.o has NO U entry_counter; realstubs.c generates NO entry_counter stand-in), with\n'
  printf '# the FROZEN 1006 elf (armed-d13-31bdebf3) as the NEGATIVE CONTROL (xnu_live_abt_ x0,\n'
  printf '# entry_note_abt_span x0 - and by value it DOES carry the entry_counter stand-in at 8049cce4, the\n'
  printf '# §2.3 defect 1007 closes); the 1006 freeze witness, the 1005 cadence witness, the 1004 abort trace\n'
  printf '# (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set asserted present\n'
  printf '# (regressions).  The 996/997/998/999/1000/1001 checkers rerun ON the 1007 image - the probe\n'
  printf '# checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1007-abtspan;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3