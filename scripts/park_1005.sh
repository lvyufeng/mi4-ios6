#!/usr/bin/env bash
# ====================================================================================================
# park_1005.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1004.sh.
#
# Park 1005 and append its block to records/revert-set.txt.
#
# 1005 = 1004's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, STALL and ABORT_TRACE all stay 1) + exactly ONE new switch STAGE90_XNU_CADENCE - a
# pure MEASUREMENT rung (no kernel behaviour change) that adds a TIMER-INDEPENDENT cadence witness: a
# monotone counter bumped from the synchronous USVC (syscall) path, published beside the per-tick
# counter and a NEW all-interrupts counter (`entry_board_irq_count` / `g_msm8974_irq_count` in
# pe_msm8974.c) under the four keys `xnu_live_cadence_{t,sync,tick,irq}`; `entry_cadence_summary()` is
# a FUNCTION (not a key) that re-publishes `_sync`/`_tick`/`_irq` late in the run.
#
# 1005 ALSO makes the two sleh seq/back `++` increments atomic (`__atomic_fetch_add` RELAXED -> inline
# ldrex/strex).  That atomicity is MEASUREMENT HYGIENE ONLY: the box boots UNIPROCESSOR
# (`real_ncpus: 1`), so the 1004 missing back-note was never a race and its cause is UNPROVEN - the
# atomic form just removes a POTENTIAL false-positive (a low `sleh_back` masquerading as a
# non-return).  Design: docs/1005-stall-cadence-design.md.
#
# THE FRONTIER 1005 PROBES: the 1004 read (records/1004-press-read.txt) settled the STALL - after dyld
# first touches a shared-cache page the tick delivery COLLAPSES (`xnu_live_tick_sum_maxd` reaches
# 0x1be8b709 ~24.4 s while the machine stays alive).  Every existing tick instrument rides the tick it
# is trying to measure; 1005's ratio `ds/max(dt,1)` is TIMER-INDEPENDENT and partitions the collapse
# into (b1) a DEGRADED timer IRQ (the interrupt counter still rises while the tick cadence falls) vs a
# FULL STOP (the interrupt counter stalls too).
#
# 1005's change is SOURCE-ONLY, in THREE existing src/ files, and NO tree edit and NO stage driver:
#   * src/platform/darwin13/pe_msm8974.c - the all-interrupts counter `g_msm8974_irq_count` (bumped in
#     the board's handler) + the two accessors `entry_board_tick_count` / `entry_board_irq_count`;
#   * src/entry/entry_stubs.c - the syscall-path sample `entry_cadence_sample`, the late summary
#     `entry_cadence_summary`, and the two atomic sleh `++`;
#   * src/entry/entry_trace.c - the pet's ONE call to the summary.
# All gated `#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE`; OFF is byte-identical (the
# guards are `defined()`-first, not a widened constant - [[mi4-off-option-two-spellings]]).
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1005 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1005.sh, run against the linked image): the cadence witness in the linked
# image (the 4 `xnu_live_cadence_` key literals x1 each, the four symbols DEFINED T/t, the one
# sample/summary call site each, and NO `__atomic_fetch_add` libcall - the increments are inline
# ldrex/strex) with the FROZEN 1004 elf (armed-d13-582a19c8, the PRESSED 1004 arm) as the NEGATIVE
# CONTROL (`xnu_live_cadence_` x0, `entry_cadence_{sample,summary}` x0).  The 1004 abort trace
# (`xnu_live_sleh_upc` x1), the 1003 stall instrument and the 1002 r3-save fix are asserted as
# REGRESSIONS (all still present).  The 996/997/998/999/1000/1001 checkers rerun ON the 1005 image
# with the FROZEN images as negative controls (the probe checker's ONE by-design frozen 20 B clause is
# the only allowed FAIL).
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
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor (VERDICT WALK then the tick collapsed - the frontier is the STALL, adjudicated in records/1004-press-read.txt); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the PRESSED predecessor (pid 1 is in the kernel's data-abort return path, adjudicated in records/1003-press-read.txt); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1005 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1005 first"; exit 1; }

# 1005's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_CADENCE=1.
# 1004's abort trace, the 1003 stall switch, the 1002 r3-save fix (USVC hook ON) and 1001's submap
# skip ALL stay on as regressions.
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1005's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1005's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: six things.
# (1) THE 1005 CHANGE (the cadence witness) must be in the linked image: the four `xnu_live_cadence_`
#     key literals (t/sync/tick/irq) AND the four symbols DEFINED T/t.
# (2) THE ATOMICITY: the inline ldrex/strex of the atomic sleh `++` must be present in the image.
# (3) THE 1004 ABORT TRACE is a REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be present.
# (4) THE 1003 STALL INSTRUMENT is a REGRESSION: its recorder + 7 `xnu_live_stall_*` literals must
#     STILL be present (1005 keeps 1004's set verbatim).
# (5) THE 1002 r3-SAVE FIX is a REGRESSION: locore_fleh_swi must STILL carry the six-word save set.
# (6) The FROZEN 1004 elf (out/stage90/frozen/armed-d13-582a19c8/) as the NEGATIVE CONTROL: it must
#     carry `xnu_live_cadence_` x0 AND no `entry_cadence_sample`/`entry_cadence_summary` symbol (a
#     reader that cannot fail certifies nothing).
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (t/sync/tick/irq) (x$_cad_lit, want 4) - the 1005 store is gone"; exit 1; }
# NOTE: `nm | grep -q` is a trap here - `grep -q` exits on the first match, SIGPIPEs `nm`, and `set -o
# pipefail` then reports the pipeline status 141, vetoing a match that exists (the 487/998 SIGPIPE
# lesson).  Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_cad_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_cad_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 symbol is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
echo "park_1005: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (the cadence witness is present)"

_cad_ldrex=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '(ldrex|strex)' || true)
[[ "$_cad_ldrex" -gt 0 ]] || {
  echo "REFUSING: the parked image carries no inline ldrex/strex (x$_cad_ldrex, want > 0) - the 1005 atomic sleh increments did not reach the linked image"; exit 1; }
echo "park_1005: by value - inline ldrex/strex x$_cad_ldrex in the linked image (the atomic sleh increments are present)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1005: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1005: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1005: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1004 elf as the NEGATIVE CONTROL for the new reader.
_ctrl=out/stage90/frozen/armed-d13-582a19c8/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_cad=$(strings "$_ctrl" | grep -c 'xnu_live_cadence_' || true)
  _c_cadsym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_cadence_(sample|summary)$' || true)
  [[ "$_c_cad" == 0 && "$_c_cadsym" == 0 ]] || {
    echo "REFUSING: the frozen 1004 elf already carries the 1005 change (xnu_live_cadence_=$_c_cad want 0, entry_cadence_{sample,summary}=$_c_cadsym want 0) - the control image is not the pre-1005 one"; exit 1; }
  echo "park_1005: by value - NEGATIVE CONTROL (frozen 1004 elf): xnu_live_cadence_ x$_c_cad, entry_cadence_{sample,summary} x$_c_cadsym (the reader can fail)"
else
  echo "park_1005: WARNING - the frozen 1004 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1005: THE CADENCE WITNESS (a timer-independent syscall-path counter beside the tick + a NEW all-interrupts counter, keys xnu_live_cadence_{t,sync,tick,irq}),\n# so "the tick delivery collapsed" partitions into (b1) degraded timer IRQ vs a full stop; + the sleh seq/back atomicity (hygiene).\n' "$ARM"
  printf '# Switch set is 1004-s VERBATIM plus STAGE90_XNU_CADENCE=1 (STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE,\n'
  printf '# SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved\n'
  printf '# (582a19c8 -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is\n' "${ARM#armed-d13-}"
  printf '# 999-s, unchanged); the TREE does NOT move - 1005 has NO tree edit and NO stage driver (the\n'
  printf '# change is THREE src/ files: pe_msm8974.c the all-interrupts counter + accessors, entry_stubs.c the\n'
  printf '# sample/summary/atomic +, entry_trace.c the pet-s one summary call).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1004-press-read.txt): the machine boots, dyld WALKS forward\n'
  printf '# (VERDICT WALK, repeat x0) faulting ~24 of its own pages, then faults the first shared-cache page\n'
  printf '# at far=0x3e8dd000 inside a kernel copyin and SURVIVES it - and then the tick delivery COLLAPSES\n'
  printf '# (xnu_live_tick_sum_maxd reaches 0x1be8b709 ~24.4 s while the machine is still alive).  Every\n'
  printf '# existing tick instrument rides the tick it measures, so 1005 adds a TIMER-INDEPENDENT witness.\n#\n'
  printf '# THE 1005 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1005.sh: THE READING is the cadence ratio\n'
  printf '# ds/max(dt,1) = (the delta of xnu_live_cadence_sync) over the delta of xnu_live_cadence_tick, with\n'
  printf '# xnu_live_cadence_irq (all interrupts) as the tiebreaker - a RATIO that keeps rising while _irq\n'
  printf '# still climbs = (b1) degraded timer IRQ; a ratio AND _irq BOTH frozen = a full stop.  The frozen\n'
  printf '# regression guards are re-verified only under the survival gate.\n#\n'
  printf '# VERIFIED BY VALUE (build_1005.sh): the linked image carries the 4 xnu_live_cadence_* key literals\n'
  printf '# and the four cadence symbols DEFINED T/t, with NO __atomic_fetch_add libcall (the increments are\n'
  printf '# inline ldrex/strex) and the FROZEN 1004 elf (armed-d13-582a19c8) as the NEGATIVE CONTROL\n'
  printf '# (xnu_live_cadence_ x0, entry_cadence_{sample,summary} x0); the 1004 abort trace (xnu_live_sleh_upc\n'
  printf '# x1), the 1003 stall instrument and the 1002 six-word save set asserted present (regressions).  The\n'
  printf '# 996/997/998/999/1000/1001 checkers rerun ON the 1005 image - the probe checker-s ONE by-design\n'
  printf '# frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1005-cadence;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3