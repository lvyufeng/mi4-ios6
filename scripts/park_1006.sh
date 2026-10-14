#!/usr/bin/env bash
# ====================================================================================================
# park_1006.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1005.sh.
#
# Park 1006 and append its block to records/revert-set.txt.
#
# 1006 = 1005's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, CADENCE, STALL and ABORT_TRACE all stay 1) + exactly ONE new switch
# STAGE90_XNU_FREEZE_WITNESS - a pure MEASUREMENT rung (no kernel behaviour change) that adds an
# IDLE-PATH FREEZE WITNESS: a sampler called once per pass of `__wrap_machine_idle` that times the
# pass with `entry_counter()` and, on the powers-of-two ladder, publishes the dwell and - when the
# dwell exceeds a threshold - a GIC/CPSR triple read AT THE MOMENT OF THE READING, under the eight
# keys `xnu_live_freeze_{n,d,cpsr,ien,pend,ctlr}` and `xnu_live_frzbase_{n,d}` (the FIRST FOUR passes are
# published UNFILTERED, so the run measures its OWN idle period as a baseline).
#
# THE FRONTIER 1006 PROBES: the 1005 read (records/1005-press-read.txt) settled the STALL - it is
# (b2) a WHOLE-INTERRUPT-PATH FREEZE.  `tick = irq - 3` is CONSTANT across the whole run (every
# interrupt IS a timer tick), the pid-1 SVC path froze with it, for a single repeatable ~24.4 s
# interval (`xnu_live_tick_sum_maxd = 0x1be8bc01`), after which the tick BURSTS (135 -> 479).  The
# machine stays ALIVE (`No errors detected`).  1006 asks THE CAUSE: (i) an IRQ-masked window, (ii) a
# GIC distributor re-mask, (iii) an ISR that never EOI'd, (iv) a non-idle fault loop.
#
# 1006's change is SOURCE-ONLY, in FOUR existing src/ files, and NO tree edit and NO stage driver:
#   * src/entry/entry_stubs.c  - the sampler `entry_freeze_sample` (the idle-counter/ladder/threshold
#     logic) + the `extern` decl of the GIC helper;
#   * src/entry/entry_trace.c  - the ONE call inside `__wrap_machine_idle` (right after the cpsr read);
#   * src/entry/entry_irq.c    - the ONE-function GIC helper `entry_gic_freeze_state` (the only place
#     the GIC is addressed stays single-function - entry_gic.h's four accessors plus this one);
#   * src/entry/entry_gic.h    - the helper's declaration (so entry_stubs.c, which has NO GIC
#     include, can call it).
# All gated `#if defined(STAGE90_XNU_FREEZE_WITNESS) && STAGE90_XNU_FREEZE_WITNESS`; OFF is
# byte-identical (the guards are `defined()`-first, not a widened constant - [[mi4-off-option-two-spellings]]).
# STAGE90_XNU_FREEZE_WITNESS is in NEITHER `$EXTRA` NOR the exported pool defines: it is read by ONLY
# the TWO entry-toolchain objects (entry_stubs.c via STUB_DEFINES, entry_trace.c via
# STUB_DEFINES_TRACE) - no pe_msm8974.c change, no pool-object rebuild, no $EXTRA entry.  Only an
# EXPORTS pin so the record carries it.
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1006 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1006.sh, run against the linked image): the freeze witness in the linked
# image (the 6 `xnu_live_freeze_` key literals + the 2 `xnu_live_frzbase_` key literals x1 each, the two
# symbols DEFINED T/t, and the ONE call site inside `__wrap_machine_idle` image-wide) with the FROZEN
# 1005 elf (armed-d13-ff937225, the PRESSED 1005 arm) as the NEGATIVE CONTROL (`xnu_live_freeze_` x0,
# `entry_freeze_sample` x0).  The 1005 cadence witness, the 1004 abort trace (`xnu_live_sleh_upc` x1),
# the 1003 stall instrument and the 1002 r3-save fix are asserted as REGRESSIONS (all still present).
# The 996/997/998/999/1000/1001 checkers rerun ON the 1006 image with the FROZEN images as negative
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
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor (VERDICT: the STALL is (b2) a whole-interrupt-path freeze ~24.4 s, adjudicated in records/1005-press-read.txt); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor (VERDICT WALK then the tick collapsed - the frontier was the STALL, adjudicated in records/1004-press-read.txt); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the PRESSED predecessor (pid 1 is in the kernel's data-abort return path, adjudicated in records/1003-press-read.txt); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1006 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1006 first"; exit 1; }

# 1006's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_FREEZE_WITNESS=1.
# 1005's cadence witness, 1004's abort trace, the 1003 stall switch, the 1002 r3-save fix (USVC hook
# ON) and 1001's submap skip ALL stay on as regressions.
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1006's set verbatim and must stay on"; exit 1; }
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

# BY VALUE, at the freeze point: seven things.
# (1) THE 1006 CHANGE (the freeze witness) must be in the linked image: the six `xnu_live_freeze_`
#     key literals (n/d/cpsr/ien/pend/ctlr), the two `xnu_live_frzbase_` key literals (n/d), and the two
#     symbols DEFINED T/t (entry_freeze_sample, entry_gic_freeze_state).
# (2) THE 1005 CADENCE WITNESS is a REGRESSION: the four `xnu_live_cadence_` key literals AND the four
#     symbols DEFINED T/t must STILL be present (1006 keeps 1005's set verbatim).
# (3) THE 1004 ABORT TRACE is a REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be present.
# (4) THE 1003 STALL INSTRUMENT is a REGRESSION: its recorder + 7 `xnu_live_stall_*` literals must
#     STILL be present.
# (5) THE 1002 r3-SAVE FIX is a REGRESSION: locore_fleh_swi must STILL carry the six-word save set.
# (6) The FROZEN 1005 elf (out/stage90/frozen/armed-d13-ff937225/) as the NEGATIVE CONTROL: it must
#     carry `xnu_live_freeze_` x0 AND no `entry_freeze_sample` symbol (a reader that cannot fail
#     certifies nothing).
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
echo "park_1006: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The call site must be INSIDE __wrap_machine_idle, image-wide exactly once ([[mi4-hooks-live-at-calls]]).
_frz_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' || true)
[[ "$_frz_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_frz_call call site(s) to entry_freeze_sample (want 1, inside __wrap_machine_idle) - the 1006 sampler is not wired to the idle path"; exit 1; }
echo "park_1006: by value - exactly ONE bl to entry_freeze_sample in the linked image (the idle-path call site)"

# 1005's cadence witness is a REGRESSION: its four key literals and four symbols must STILL be present.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (t/sync/tick/irq) (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed (1006 keeps 1005's set verbatim)"; exit 1; }
done
echo "park_1006: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1006: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1006: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1006: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1005 elf as the NEGATIVE CONTROL for the new reader.
_ctrl=out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_frz=$(strings "$_ctrl" | grep -c 'xnu_live_freeze_' || true)
  _c_frzsym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_freeze_sample$' || true)
  [[ "$_c_frz" == 0 && "$_c_frzsym" == 0 ]] || {
    echo "REFUSING: the frozen 1005 elf already carries the 1006 change (xnu_live_freeze_=$_c_frz want 0, entry_freeze_sample=$_c_frzsym want 0) - the control image is not the pre-1006 one"; exit 1; }
  echo "park_1006: by value - NEGATIVE CONTROL (frozen 1005 elf): xnu_live_freeze_ x$_c_frz, entry_freeze_sample x$_c_frzsym (the reader can fail)"
else
  echo "park_1006: WARNING - the frozen 1005 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1006: THE IDLE-PATH FREEZE WITNESS (a sampler on the powers-of-two ladder of `__wrap_machine_idle`\n# passes, timing each pass with entry_counter() and reading the GIC/CPSR AT the reading, keys\n# xnu_live_freeze_{n,d,cpsr,ien,pend,ctlr} + the unfiltered early baseline xnu_live_frzbase_{n,d}),\n# so the CAUSE of the ~24.4 s whole-interrupt-path freeze partitions into (i) IRQ-masked window,\n# (ii) GIC distributor re-mask, (iii) an ISR that never EOI-d, (iv) a non-idle fault loop.\n' "$ARM"
  printf '# Switch set is 1005-s VERBATIM plus STAGE90_XNU_FREEZE_WITNESS=1 (CADENCE, STALL, ABORT_TRACE,\n'
  printf '# DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay\n'
  printf '# 1); the ENTRY BIN moved (ff937225 -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s\n' "${ARM#armed-d13-}"
  printf '# watchdog constant is 999-s, unchanged); the TREE does NOT move - 1006 has NO tree edit and NO\n'
  printf '# stage driver (the change is FOUR src/ files: entry_stubs.c the sampler, entry_trace.c the ONE\n'
  printf '# idle-path call, entry_irq.c the one GIC helper, entry_gic.h its declaration).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1005-press-read.txt): the STALL is (b2) a WHOLE-INTERRUPT-PATH\n'
  printf '# FREEZE - tick = irq - 3 is CONSTANT across the whole run (every interrupt IS a timer tick), the\n'
  printf '# pid-1 SVC path froze with it, for a single repeatable ~24.4 s interval (xnu_live_tick_sum_maxd =\n'
  printf '# 0x1be8bc01), after which the tick BURSTS (135 -> 479) while the machine stays ALIVE (No errors\n'
  printf '# detected).  1006 asks THE CAUSE.  NOTE (1006-s own correction): the capture CANNOT tie the freeze\n'
  printf '# to a feed - entry_counter() wraps every ~3.7 min and the pet publishes only 8 gated endpoints -\n'
  printf '# so the idle/wfi overlap is made a READING, not an assumption.\n#\n'
  printf '# THE 1006 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1006.sh: THE READING is the classifier of the record\n'
  printf '# with the LARGEST xnu_live_freeze_d - i=(cpsr>>7)&1 (1 = IRQ masked) - against the GIC triple\n'
  printf '# (ien = ISENABLER0 bit 20, pend = ISPENDR0 bit 20, ctlr = CTLR): IRQs ENABLED + line ENABLED+PENDING\n'
  printf '# = a long NON-IDLE stretch (a fault loop or a wfi that did not wake); IRQs MASKED = (i); line\n'
  printf '# DISABLED = (ii); CTLR 0 = a global sub-case of (ii).  ZERO freeze records but the idle baseline\n'
  printf '# present = a VALID silence (the freeze did not overlap the idle path) - a READING, NOT a failure.\n#\n'
  printf '# VERIFIED BY VALUE (build_1006.sh): the linked image carries the 6 xnu_live_freeze_ key literals +\n'
  printf '# the 2 xnu_live_frzbase_ key literals and the two freeze symbols DEFINED T/t, with exactly ONE bl to\n'
  printf '# entry_freeze_sample image-wide, and the FROZEN 1005 elf (armed-d13-ff937225) as the NEGATIVE\n'
  printf '# CONTROL (xnu_live_freeze_ x0, entry_freeze_sample x0); the 1005 cadence witness, the 1004 abort\n'
  printf '# trace (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set asserted\n'
  printf '# present (regressions).  The 996/997/998/999/1000/1001 checkers rerun ON the 1006 image - the\n'
  printf '# probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1006-freeze;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3