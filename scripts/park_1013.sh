#!/usr/bin/env bash
# ====================================================================================================
# park_1013.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1012.sh.
# (1012 was itself modelled on park_1011.sh.)
#
# Park 1013 and append its block to records/revert-set.txt.
#
# 1012 = 1011's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS, ABT_SPAN, ABT_FAR, ABT_PC, ABT_PTE
# AND VME_PROT all stay 1) + exactly ONE new switch STAGE90_XNU_PMP_ENTER - a pure MEASUREMENT rung
# (no kernel behaviour change) that reads the LAST cell of the prot->PTE chain 1011 left open: the
# `prot` handed to `pmap_enter_options`, the LEAF'S OWN INPUT, via a conditional
# `--wrap=pmap_enter_options`. A companion TREE probe switch STAGE90_XNU_WRSTRIP
# (tools/patch_d13_wrstrip.py, vm_fault.c strip sites) may also be on; it is NOT a park gate here.
#
# WHY 1012 FOLLOWS 1011.  The 1011 press (arm armed-d13-fce83837, records/1011-press-read.txt) DECIDED
# the fault class BY VALUE: the (E') tail rows carry `vmeprot=0x3` (VM_PROT_READ|VM_PROT_WRITE) with
# `vmekr=0` (KERN_SUCCESS), so the vm_map ENTRY GRANTS write; F1b (the accept path's unconditional
# `kr = KERN_SUCCESS`, vm_fault.c:2478, being the wrong condition) is DENIED BY VALUE.  Hence F1a: the
# write is LOST between the entry and the PTE.  What 1011 could NOT name is WHERE.  1012's APEX cell
# names it.  On this tree `pmap_enter_options` dispatches to `leo_enter` (`pmap.c:3181`), which REBUILDS
# the leaf UNCONDITIONALLY from the `prot` it is handed (`leo_pte_build` -> `pmap_vm_prot_to_page_flags`,
# which CLEARS APX iff `prot & VM_PROT_WRITE`), and every `leo_enter` path ends in `pmap_flush_tlbs`.
# So a `prot` that carries WRITE here MUST yield a writable leaf; a `prot` LACKING WRITE here IS the
# loss.  `PMAP_ENTER_OPTIONS` (`osfmk/vm/pmap.h:399`) is a CROSS-object call, so a
# `--wrap=pmap_enter_options` redirects EVERY linker-visible caller (`pmap.c`'s own intra-object
# `pmap_enter` -> `pmap_enter_options` is NOT wrapped - a limitation stated at the site).
#
# THE 1012 DECISION TREE (docs/1012-design.md §4) is a two-way bisector read from the TAIL ROWS' OWN
# `_pmpprot` (ordinals >= 0x400 on the powers-of-two ladder), NEVER on a whole-run change count (the
# 1009 measurement defect, not repeated):
#   * tail `_pmpprot & VM_PROT_WRITE` == 0 => (b) a strip site SKIPPED the write: `prot` lost WRITE
#     above the apex (prime suspects: the RetryFault block, vm_fault.c:3845/3855, which 1011's
#     single-shot latch could not see).  Fix at the strip site; the F4 retry-bound
#     (tools/patch_d13_abt_retry_bound.py) is the safety net.
#   * tail `_pmpprot & 0x2` != 0 AND `_pmppmap` bits[31:12] == `_pmpttbr` bits[31:12] => (c) the apex
#     AGREES with the MMU-walked base but the leaf is still read-only => the loss is INSIDE
#     `leo_enter`/`leo_pte_build` (pmap.c:1640-1682, read by value with the measured `prot`).
#   * tail `_pmpprot & 0x2` != 0 AND `_pmppmap` bits[31:12] != `_pmpttbr` bits[31:12] => (c) an
#     IDENTITY FAULT: the pmap writes one L1, the MMU walks another.
#   * NO tail row => the storm's enter did NOT reach the wrapper (the intra-object blindness): fall
#     back to the tree instrument (STAGE90_XNU_WRSTRIP).
# The `_pmppmap`/`_pmpttbr` pair ALSO closes the walk-mask question for good.
#
# 1012's change is SOURCE-ONLY, in TWO existing src/ files plus ONE conditional `--wrap`, and NO tree
# edit and NO stage driver (the WRSTRIP tree probe is SEPARATE and optional):
#   * src/entry/entry_stubs.c   - the recorder `entry_note_abt_pmp(va, prot, pa, pm_l1_phys)` and its
#     statics `g_abtpte_pmpn`/`g_abtpte_pmplastprot`/`g_abtpte_pmpnx`, behind the defined()-first
#     `#if defined(STAGE90_XNU_PMP_ENTER) && STAGE90_XNU_PMP_ENTER`; the shared OR-guard cell
#     `g_abtpte_pmpva` and its ONE store in `entry_note_abt_pte` (the latch chain that arms it);
#   * src/entry/entry_trace.c   - the `entry_note_abt_pmp` declaration + the NEW
#     `__wrap_pmap_enter_options` (a PURE PASS-THROUGH except for the ONE recorder call);
#   * src/entry/build_entry.sh  - `--wrap=pmap_enter_options` added to `TRACE_LDFLAGS` ONLY when
#     `PMP_ENTER=1`, so the OFF image carries no whole-kernel enter wrap (the §8.1 wrap surface).
# The recorder keys on the 1010 latch chain, so build_entry.sh REFUSES `PMP_ENTER=1` unless the full
# 1010 ladder (ABT_FAR=1 && ABT_PC=1 && ABT_PTE=1) is on.  STAGE90_XNU_PMP_ENTER is in NEITHER `$EXTRA`
# NOR the exported pool defines: it is read by ONLY the TWO entry-toolchain objects (entry_stubs.c via
# STUB_DEFINES, entry_trace.c via STUB_DEFINES_TRACE) - no pe_msm8974.c change, no pool-object rebuild.
# Only an EXPORTS pin so the record carries it.
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1012 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1012.sh, run against the linked image): the apex witness in the linked image
# (the 49 `xnu_live_abt_` key literals - 1011's 43 plus the 6 pmp pmpn/pmpprot/pmppa/pmppmap/
# pmpttbr/pmpnx, `entry_note_abt_pmp` and `__wrap_pmap_enter_options` DEFINED T/t, exactly ONE `bl` to
# `entry_note_abt_pmp` image-wide, `__wrap_pmap_enter_options` calling `<pmap_enter_options>`
# (pass-through) AND `entry_note_abt_pmp`), the 1011 (E')/(A) witness, the 1010/1009/1008/1007
# witnesses, the 1006 freeze witness, the 1005 cadence witness, the 1004 abort trace
# (`xnu_live_sleh_upc` x1), the 1003 stall instrument and the 1002 six-word save set as REGRESSIONS, and
# the FROZEN 1011 elf (armed-d13-fce83837, the PRESSED predecessor) as the NEGATIVE CONTROL
# (`xnu_live_abt_` x43, the 1011 symbols present, but the 1012 symbols x0 and the 1012 literals x0 -
# the control proves the 1012 change is NOT in the 1011 arm).  The 996/997/998/999/1000/1001 checkers
# rerun ON the 1012 image with the FROZEN images as negative controls (the probe checker's ONE by-design
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
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1), the PRESSED predecessor (VERDICT: the PTE-build apex is MEASURED - `_pmpprot=0x3` READ|WRITE ARRIVES at pmap_enter_options AND `_pmppmap & 0xFFFFFF00 == _pmpttbr & 0xFFFFFF00`, so the pmap writes the SAME L1 table the MMU walks and strip-above-the-apex is REFUTED BY MEASUREMENT; the leaf's VALUE was still never read - that is 1013's question; adjudicated in records/1012-press-read.txt; design docs/1013-design.md); build 1013 first"; exit 1; }
[[ $ARM == armed-d13-fce83837 ]] && { echo "REFUSING: $LIVE holds 1011's arm (fce83837), the PRESSED predecessor (VERDICT: F1a is MEASURED - the (E') tail rows carry vmeprot=0x3 READ|WRITE with vmekr=0 KERN_SUCCESS, so the vm_map ENTRY GRANTS write and the write is LOST between the entry and the PTE; F1b is DENIED BY VALUE; adjudicated in records/1011-press-read.txt; design docs/1012-design.md); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-64391319 ]] && { echo "REFUSING: $LIVE holds 1010b's arm (64391319), the PRESSED predecessor (VERDICT: the mask fix is CORRECT but exposed a SECOND defect - the corrected TTBR0 base reads the TRUE L1, whose L2 table is at PA 0x46300000 OUTSIDE the entry window and the unbounded L2 deref faulted FS=5; adjudicated in records/1010b-press-read.txt); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-0f39207c ]] && { echo "REFUSING: $LIVE holds 1010's arm (0f39207c), the PRESSED predecessor (VERDICT: the storm reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail, because the TTBR0 base was masked with 0xfffffc00 instead of 0xffffc000; adjudicated in records/1010-press-read.txt); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-24997fc5 ]] && { echo "REFUSING: $LIVE holds 1009's arm (24997fc5), the PRESSED predecessor (VERDICT: the storm is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x80f, far=0x9a000; the handler chose the SILENT retry branch; adjudicated in records/1009-press-read.txt); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-0343ef45 ]] && { echo "REFUSING: $LIVE holds 1008's arm (0343ef45), the PRESSED predecessor (VERDICT: the storm is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort; adjudicated in records/1008-press-read.txt); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-88074ab3 ]] && { echo "REFUSING: $LIVE holds 1007's arm (88074ab3), the PRESSED predecessor (VERDICT (C): the ~24.4 s freeze is INSIDE ONE sleh_abort riding a STORM of ~2.1 M aborts); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-002b0683 ]] && { echo "REFUSING: $LIVE holds 1010c's arm (002b0683), the PRESSED predecessor (VERDICT: the walk is WINDOW-BOUNDED - the mask fix reads the TRUE L1 base 0x81a84000, but the storm's L1 slot holds a SECTION descriptor (ptel1=0xfeedface), so the L2 LEAF (APX) is NEVER READ; adjudicated in records/1010c-press-read.txt); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-31bdebf3 ]] && { echo "REFUSING: $LIVE holds 1006's arm (31bdebf3), the PRESSED predecessor (the freeze witness); build 1012 first"; exit 1; }
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor; build 1012 first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor; build 1012 first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), a known/stale arm; build 1012 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one; build 1012 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1012 first"; exit 1; }

# 1012's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_PMP_ENTER=1.  VME_PROT (1011), ABT_PTE (1010c), ABT_PC (1009), ABT_FAR (1008), ABT_SPAN
# (1007), the freeze witness (1006), the cadence witness (1005), abort trace (1004), stall (1003), the
# r3-save fix (1002, USVC hook ON) and the submap skip (1001) ALL stay on as regressions.
grep -q '^STAGE90_XNU_PTE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_PTE_WITNESS=1 - 1013's leaf-value witness was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_PMP_ENTER=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_PMP_ENTER=1 - 1012's apex cell (prot at pmap_enter_options) was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_VME_PROT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_VME_PROT=1 - 1011's (E')/(A) identity is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PTE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PTE=1 - 1010c's L2 walk is part of 1012's set verbatim (PMP_ENTER requires ABT_PTE=1) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PC=1 - 1009's faulting-pc identity is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_FAR=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_FAR=1 - 1008's abort-fault identity is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_SPAN=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_SPAN=1 - 1007's abort-span witness is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1012's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"; exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"; exit 1; }

# BY VALUE, at the freeze point: the 1012 delta FIRST, then the regressions, then the control.
# 1012's NEW instrument (the `prot` at pmap_enter_options), BY VALUE, FIRST.
_ab_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_abt_' || true)
[[ "$_ab_lit" == 82 ]] || {
  echo "REFUSING: the parked image does not carry the 82 xnu_live_abt_ key literals (49 through 1012 + 1013's 33 witness keys) (x$_ab_lit, want 82) - the 1011 vme/ptethr store, the 1012 pmp store or the 1013 witness store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")

for _ab_1012sym in entry_note_abt_pmp __wrap_pmap_enter_options; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1012sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1012sym is not a DEFINED text symbol in the parked image - the 1012 instrument is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
# `b` OR `bl`: the apex recorder is called from the wrapper (measured on the build).
_pmp_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pmp>' || true)
[[ "$_pmp_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_pmp_call call site(s) to entry_note_abt_pmp (want 1, inside __wrap_pmap_enter_options) - the 1012 apex witness is not wired"; exit 1; }
# The pmap_enter_options wrapper's PASS-THROUGH is the §8.1 rule made a check: it must call BOTH the
# real body (the pass-through) and entry_note_abt_pmp (the apex record).
#     NOTE: `--wrap=pmap_enter_options` renames `__real_pmap_enter_options` -> `pmap_enter_options`
#     for the linked reference, so the disassembly has `bl <pmap_enter_options>`, never
#     `<__real_pmap_enter_options>` (the same rule measured for vm_fault in 1010 and
#     vm_map_lookup_locked in 1011).
_pmpw=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_pmap_enter_options"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_pmap_enter_options not in the parked image"; exit 1; }
_pmpwx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_pmpw#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_pmpwb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_pmpw" --stop-address="${_pmpwx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <pmap_enter_options>' <<<"$_pmpwb" || { echo "REFUSING: __wrap_pmap_enter_options does not call the real pmap_enter_options body - it is not a pass-through (docs/1012-design.md §4)"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_pmp>' <<<"$_pmpwb" || { echo "REFUSING: __wrap_pmap_enter_options does not call entry_note_abt_pmp - the apex record never fires"; exit 1; }
echo "park_1012: by value - xnu_live_abt_* literals x$_ab_lit (49 through 1012 + 1013's 33), entry_note_abt_pmp/__wrap_pmap_enter_options DEFINED T/t, exactly ONE b/bl to entry_note_abt_pmp, and the wrapper calling pmap_enter_options AND entry_note_abt_pmp (the prot-at-the-apex witness is present)"

# THE 1011 CHANGE (the vm_map_entry effective protection + the faulting thread's own L1 base) is a
# REGRESSION: the wrapper/pass-through, the two 1011 symbols, the ONE b/bl to entry_note_abt_vme, and
# the (A) STAGE90_ACT_MAP immediate (956).  1012 keeps 1011's set verbatim.
for _ab_1011sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1011sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1011sym is not a DEFINED text symbol in the parked image - the 1011 instrument regressed (1012 keeps 1011's set verbatim)"; exit 1; }
done
_vme_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' || true)
[[ "$_vme_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_vme_call call site(s) to entry_note_abt_vme (want 1, inside __wrap_vm_map_lookup_locked) - the 1011 (E') witness regressed"; exit 1; }
_vmew=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_map_lookup_locked"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_map_lookup_locked not in the parked image"; exit 1; }
_vmewx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_vmew#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_vmewb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_vmew" --stop-address="${_vmewx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_map_lookup_locked>' <<<"$_vmewb" || { echo "REFUSING: __wrap_vm_map_lookup_locked does not call the real vm_map_lookup_locked body - the 1011 pass-through regressed"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' <<<"$_vmewb" || { echo "REFUSING: __wrap_vm_map_lookup_locked does not call entry_note_abt_vme - the 1011 (E') record regressed"; exit 1; }
# The (A) deref chain: entry_note_abt_pte or its inlined static helper must carry the thread->map offset.
_pte_adr=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="entry_note_abt_pte"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: entry_note_abt_pte not in the parked image"; exit 1; }
_l1_adr=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="stage90_abt_thread_l1_base"{print "0x" $1; found=1} END{exit(found?0:0)}')
_regions=''
for _base in "$_pte_adr" "$_l1_adr"; do
  [[ -n "$_base" ]] || continue
  _nx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_base#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
  _regions+=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_base" --stop-address="${_nx:-0xffffffff}")
done
grep -qE '(#956\b|#0x3bc|movw[[:space:]]+r[0-9]+, #956)' <<<"$_regions" || { echo "REFUSING: neither entry_note_abt_pte nor stage90_abt_thread_l1_base carries the STAGE90_ACT_MAP immediate (956) - the 1011 (A) reader regressed"; exit 1; }
echo "park_1012: by value - entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, ONE b/bl to entry_note_abt_vme, the lookup pass-through intact, and the (A) STAGE90_ACT_MAP immediate (956) present (1011's (E')/(A) witness kept)"

# THE 1010 CHANGE (the L2 descriptor + the vm_fault return) is a REGRESSION.
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the parked image - the 1010 instrument regressed (1012 keeps 1011's set verbatim)"; exit 1; }
done
_abt_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' || true)
[[ "$_abt_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abt_call call site(s) to entry_note_abt_pte (want 1, inside __wrap_sleh_abort) - the 1010 witness regressed"; exit 1; }
_abt_wfa=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_fault not in the parked image"; exit 1; }
_abt_wfx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_abt_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_abt_wfb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_abt_wfa" --stop-address="${_abt_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - the 1010 pass-through regressed"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the 1010 (B) record regressed"; exit 1; }
echo "park_1012: by value - entry_note_abt_pte/vmf + __wrap_vm_fault DEFINED T/t, ONE b/bl to entry_note_abt_pte, the __wrap_vm_fault pass-through intact (1010's L2-descriptor + vm_fault-return witness kept)"

# 1009's faulting-pc identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the parked image - the 1009 recorder was dropped (1012 keeps 1011's set verbatim)"; exit 1; }
_abp_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' || true)
[[ "$_abp_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abp_call call site(s) to entry_note_abt_pc (want 1) - the 1009 witness regressed"; exit 1; }
echo "park_1012: by value - entry_note_abt_pc DEFINED T/t with exactly ONE b/bl (1009's faulting-pc identity kept)"
# 1008's abort-fault identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the parked image - the 1008 recorder was dropped"; exit 1; }
echo "park_1012: by value - entry_note_abt_far DEFINED T/t (1008's abort-fault identity kept)"
# 1007's abort-span witness is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder was dropped"; exit 1; }
echo "park_1012: by value - entry_note_abt_span DEFINED T/t (1007's abort-span witness kept)"
_frz_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_freeze_' || true)
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the parked image does not carry the 6 xnu_live_freeze_ key literals (x$_frz_lit, want 6) - the 1006 store is gone"; exit 1; }
_idl_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_frzbase_' || true)
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the parked image does not carry the 2 xnu_live_frzbase_ key literals (x$_idl_lit, want 2) - the 1006 idle-baseline store is gone"; exit 1; }
_frz_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the parked image - the 1006 symbol is gone"; exit 1; }
done
echo "park_1012: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The 1005 cadence witness is a REGRESSION.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed"; exit 1; }
done
echo "park_1012: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1012: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1012: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1012: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1012 elf as the NEGATIVE CONTROL for the 1013 reader.
_ctrl=out/stage90/frozen/armed-d13-ffdc8ba1/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
  _c_12sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_pmp|__wrap_pmap_enter_options)$' || true)
  _c_13sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_wleaf|entry_note_abt_walk|entry_note_abt_wsync|entry_note_abt_cur)$' || true)
  _c_13lit=$(strings "$_ctrl" | grep -cE 'xnu_live_abt_(wwleaf|wkleaf|wcleaf|wwnoproof|wkapex|wwva|wwft|wwpatbl|wkleaf2|wkstable)' || true)
  [[ "$_c_ab" == 49 && "$_c_12sym" == 2 && "$_c_13sym" == 0 && "$_c_13lit" == 0 ]] || {
    echo "REFUSING: the frozen 1012 elf does not have the pre-1013 shape (xnu_live_abt_=$_c_ab want 49, the 1012 symbols=$_c_12sym want 2, 1013 symbols=$_c_13sym want 0, 1013 leaf literals=$_c_13lit want 0) - the control image is not the pre-1013 one"; exit 1; }
  echo "park_1013: by value - NEGATIVE CONTROL (frozen 1012 elf): xnu_live_abt_ x$_c_ab (1012's 49 keys), the 1012 symbols x$_c_12sym, 1013 symbols x$_c_13sym (the reader can fail), 1013 leaf literals x$_c_13lit"
else
  echo "park_1013: WARNING - the frozen 1012 elf is absent; the negative control was NOT checked this park"
fi

# ====================================================================================================
# 1013's NEW instrument (THE LEAF-VALUE WITNESS), BY VALUE, FIRST - the three recorders, their three
# call sites, and the fixed keyspace. Placed BEFORE the 1012 regression block so a 1013 park that lost
# its own change fails before it re-asserts the older ones.
# ====================================================================================================
for _wk_sym in entry_note_abt_wleaf entry_note_abt_walk entry_note_abt_wsync entry_note_abt_cur; do
  grep -qE "^[0-9a-f]+ [Tt] $_wk_sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_wk_sym is not a DEFINED text symbol in the parked image - the 1013 witness is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
# The fixed keyspace (`docs/1013-design.md` §2), read as whole-line `strings` matches so a truncated or
# re-spelled key is a refusal. 33 literals: 11 W-APEX (incl. the provenance cells wwva/wwft/wwpatbl),
# 12 W-WALK (incl. the double-read wkleaf2/wkstable) + the 2 change-counts wwnx/wknx = 13 in the `wk`
# family count below, 8 W-SYNC.  The ladder counts the 33 the build asserts.
_wk_keys=0
for _wk_key in \
    xnu_live_abt_wwn xnu_live_abt_wwl1 xnu_live_abt_wwl2pa xnu_live_abt_wwleaf xnu_live_abt_wwapx xnu_live_abt_wwnoproof xnu_live_abt_wwpa xnu_live_abt_wwpaok xnu_live_abt_wwva xnu_live_abt_wwft xnu_live_abt_wwpatbl xnu_live_abt_wwnx \
    xnu_live_abt_wkn xnu_live_abt_wk xnu_live_abt_wktb xnu_live_abt_wkl1 xnu_live_abt_wkl2pa xnu_live_abt_wkleaf xnu_live_abt_wkleaf2 xnu_live_abt_wkstable xnu_live_abt_wkapx xnu_live_abt_wkoow xnu_live_abt_wkfsr xnu_live_abt_wkapex xnu_live_abt_wknx \
    xnu_live_abt_wcnt xnu_live_abt_wcva xnu_live_abt_wck xnu_live_abt_wcl1 xnu_live_abt_wcl2pa xnu_live_abt_wcleaf xnu_live_abt_wcapx xnu_live_abt_wcoow ; do
  grep -qx "$_wk_key" <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || {
    echo "REFUSING: the parked image does not carry the \`$_wk_key\` literal - the 1013 store is gone"; exit 1; }
  _wk_keys=$((_wk_keys + 1))
done
[[ "$_wk_keys" == 33 ]] || { echo "REFUSING: the key ladder counted $_wk_keys (want 33)"; exit 1; }
echo "park_1013: by value - the four recorders DEFINED T/t and all 33 1013 key literals present (11 W-APEX incl. wwva/wwft/wwpatbl + 13 W-WALK incl. wkleaf2/wkstable/wknx + 8 W-SYNC)"
# Exactly ONE call site of each recorder, and the W-APEX one lives INSIDE the pmap wrapper while
# W-WALK must NOT ([[mi4-hooks-live-at-calls]] - W-WALK's recording belongs to the abort path).
_wk_w=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_pmap_enter_options"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_pmap_enter_options not in the parked image - the 1012 apex the 1013 leaf is read at is gone"; exit 1; }
_wk_wx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_wk_w#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_wk_wb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_wk_w" --stop-address="${_wk_wx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <pmap_enter_options>' <<<"$_wk_wb" || { echo "REFUSING: __wrap_pmap_enter_options does not call the real pmap_enter_options body - the pass-through regressed"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_wleaf>' <<<"$_wk_wb" || { echo "REFUSING: __wrap_pmap_enter_options does not call entry_note_abt_wleaf - the 1013 apex leaf is never read"; exit 1; }
if grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_walk>' <<<"$_wk_wb"; then
  echo "REFUSING: __wrap_pmap_enter_options calls entry_note_abt_walk - W-WALK's recording was moved off the abort path"; exit 1; fi
for _wk_call_sym in entry_note_abt_wleaf entry_note_abt_walk entry_note_abt_wsync entry_note_abt_cur; do
  _wk_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <'"$_wk_call_sym"'>' || true)
  [[ "$_wk_call" == 1 ]] || {
    echo "REFUSING: the parked image carries $_wk_call call site(s) to $_wk_call_sym (want 1) - the 1013 witness is not wired"; exit 1; }
done
echo "park_1013: by value - exactly ONE call site each of entry_note_abt_wleaf/walk/wsync/cur, the W-APEX call inside the pmap wrapper and W-WALK NOT in it"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1013: THE LEAF-VALUE WITNESS - the leaf the pmap ACTUALLY writes, and the location\n# the MMU-s own walk finds it at (design docs/1013-design.md)\n' "$ARM"
  printf '# Switch set is 1012-s VERBATIM plus STAGE90_XNU_PTE_WITNESS=1 (PMP_ENTER, VME_PROT, ABT_PTE,\n'
  printf '# ABT_PC, ABT_FAR, ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS,\n'
  printf '# SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY\n'
  printf '# BIN moved (ffdc8ba1 -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog\n' "${ARM#armed-d13-}"
  printf '# constant is 999-s, unchanged); the TREE does NOT move - 1013 has NO tree edit, NO stage driver and\n'
  printf '# NO new --wrap (the change is TWO src/ files: entry_stubs.c the four recorders\n'
  printf '# entry_note_abt_wleaf/walk/wsync/cur + g_abtpte_ptel2rec + g_abtpte_curva, entry_trace.c the four\n'
  printf '# declarations + the four call sites; it rides 1012-s --wrap=pmap_enter_options and the existing\n'
  printf '# --wrap=sleh_abort).\n#\n'
  printf '# WHY 1013 FOLLOWS 1012.  The 1012 press (arm armed-d13-ffdc8ba1, records/1012-press-read.txt)\n'
  printf '# MEASURED the apex BY VALUE: every TAIL row (ordinal >= 0x400) carries xnu_live_abt_pmpprot=0x3\n'
  printf '# (READ|WRITE) AND xnu_live_abt_pmppmap & 0xFFFFFF00 (0x81a86000) == xnu_live_abt_pmpttbr &\n'
  printf '# 0xFFFFFF00 - so the write ARRIVES at pmap_enter_options and the pmap writes the SAME L1 table the\n'
  printf '# MMU walks.  strip-above-the-apex (the last F1a candidate) is REFUTED BY MEASUREMENT.  Yet the\n'
  printf '# USER store still permission-faults (fsr=0x80f, FS=15 level-2, WnR=1) ~2.1 M times.  The ONE cell\n'
  printf '# nobody has read is the cell the fault is about: THE VALUE OF THE LEAF.  1013 reads it, three\n'
  printf '# ways, and changes nothing.\n#\n'
  printf '# THE 1013 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted):\n'
  printf '# THE HEADLINE MUST be decided from the TAIL ROWS-OWN values (ordinal >= 0x400), NEVER on a\n'
  printf '# whole-run change count (the 1008/1009 measurement defect, not repeated).  THE DECISION TABLE\n'
  printf '# (docs/1013-design.md §3):\n'
  printf '#   V1  W-APEX leaf APX=0 (WRITABLE) at W-APEX-s own location AND W-WALK-s wkl2pa == wwl2pa =>\n'
  printf '#       LEAF_WRITABLE_STILL_FAULTS: the leaf IS writable in the table the pmap wrote and the MMU\n'
  printf '#       walks, yet the store still faults => a STALE TLB / a second table the TLB holds / the\n'
  printf '#       flush did not reach this CPU.  Next rung instruments pmap_flush_tlbs + the ASID at the\n'
  printf '#       fault.\n'
  printf '#   V2  W-APEX leaf APX=1 (READ-ONLY) at W-APEX-s own location AND wkl2pa == wwl2pa =>\n'
  printf '#       F1a_CONFIRMED_LEAF_DEFECT: the pmap-s write did NOT stick, or a SECOND writer re-RO-d the\n'
  printf '#       leaf.  Next rung instruments the second writer (pmap_page_protect etc.).\n'
  printf '#   V3  any readable W-APEX leaf AND wkl2pa != wwl2pa => IDENTITY_FAULT_L2: the apex writes one L2\n'
  printf '#       and the MMU walks another.  (1012 measured the L1 bases EQUAL, so the divergence is at the\n'
  printf '#       L2 - a pmap_expand hoist / a pm_l1_virt vs pm_l1_phys L2 mismatch.)\n'
  printf '#   V4  W-APEX present but wwnoproof=1 (pm_l1_phys != the live TTBR0 base at that row) =>\n'
  printf '#       APEX_NOT_PROOF: a by-value CONTRADICTION with 1012; REPORT it, do not conclude from it.\n'
  printf '#   V5  W-APEX absent (wwn == 0) yet the storm ran => NO_APEX_WITNESS: the enter never reached the\n'
  printf '#       wrapper; fall back to the tree instrument.\n'
  printf '#   V6  W-APEX leaf absent/0 while wwl1 is a COARSE descriptor => HARD (stale 0): a present L2 with\n'
  printf '#       no leaf is impossible for a correct walk - a MEASUREMENT defect, never a value.\n'
  printf '#   V0  THE PROVENANCE GATE (applied BEFORE V1-V3; the 1012 apex defect this rung must not inherit,\n'
  printf '#       docs/1012-probe-keying.md §1.4/§1.6).  EVERY TAIL W-APEX row must carry wwva == the storm-s\n'
  printf '#       far AND wwft == 0x3 (READ|WRITE) AND wwpatbl == 0 (the entered page is NOT a page-table\n'
  printf '#       page).  A row failing ANY of the three is a NULL CONTROL, NOT a "write reaches the apex for\n'
  printf '#       the faulting VA" reading: if EVERY tail row fails => APEX_OBSERVED_A_TABLE_PAGE (or\n'
  printf '#       APEX_WRONG_VA / APEX_WRONG_FAULT_TYPE): the 1012 cell-s real shape - every row carried\n'
  printf '#       pa=0x8421a (PA 0x8421a000), a PAGE-TABLE-PAGE install.  REPORT it; do NOT conclude V1/V2/V3\n'
  printf '#       from a table-page row.  V0 short-circuits V1-V3 exactly as the control does.\n'
  printf '#   V0-prime  THE WALK GATE (applied BEFORE V1-V3): the tail W-WALK row must carry wkstable == 1\n'
  printf '#       (the double-read AGREED).  wkstable == 0 => the L2 word MOVED between the two walks =>\n'
  printf '#       WALK_UNSTABLE_NULL, a NULL CONTROL: the location comparison V1/V2/V3 rest on was taken over\n'
  printf '#       a table that does not hold still.  V1-V3 withheld; REPORT it (a moving table under a\n'
  printf '#       faulting VA is itself a finding).  A MISSING wkstable (pre-double-read capture) does NOT gate.\n'
  printf '#   W-SYNC control wrong (wcoow=1 OR wck=0 OR wcapx != 0) => WITNESS_METHOD_BROKEN: the witness\n'
  printf '#       method itself is broken on this device (or the managed window is TTBR1 territory, which is\n'
  printf '#       the EXPECTED null-control outcome).  Report the measurement defect.  DO NOT CONCLUDE.\n'
  printf '# The W-SYNC control is read FIRST and short-circuits V1-V3 into WITNESS_METHOD_BROKEN, exactly as\n'
  printf '# the rung requires; the provenance gate V0 then short-circuits V1-V3 into APEX_*_TABLE_PAGE/\n'
  printf '# WRONG_VA/WRONG_FAULT_TYPE.  W-WALK also publishes xnu_live_abt_wkapex = "my leaf and the (A)\n'
  printf '# walk-s OWN recorded leaf agree" AND the DOUBLE-READ xnu_live_abt_wkleaf2 + xnu_live_abt_wkstable\n'
  printf '# (the L2 word read a SECOND time after a dsb): wkstable=1 => the zero is STABLE and no race with a\n'
  printf '# concurrent table write can explain it; wkstable=0 => the leaf is a NULL CONTROL (the table moved\n'
  printf '# between the reads).  This is the MEASUREMENT that settles 1012-s ptel2=0 HARD anomaly instead of\n'
  printf '# an inference.\n#\n'
  printf '# KEYS (the fixed keyspace, docs/1013-design.md §2): W-APEX wwn/wwl1/wwl2pa/wwleaf/wwapx/\n'
  printf '# wwnoproof/wwpa/wwpaok/wwva/wwft/wwpatbl/wwnx; W-WALK wkn/wk/wktb/wkl1/wkl2pa/wkleaf/wkleaf2/\n'
  printf '# wkstable/wkapx/wkoow/wkfsr/wkapex/wknx; W-SYNC wcnt/wcva/wck/wcl1/wcl2pa/wcleaf/wcapx/wcoow.\n'
  printf '# No two probes share a key.\n#\n'
  printf '# VERIFIED BY VALUE (build_1013.sh): the image carries the 82 xnu_live_abt_ key literals and all 33\n'
  printf '# 1013 keys; the four recorders DEFINED T/t with exactly ONE call site each; the W-APEX call inside\n'
  printf '# __wrap_pmap_enter_options (which still calls <pmap_enter_options>, the pass-through) and W-WALK\n'
  printf '# NOT in it; with the FROZEN 1012 elf (armed-d13-ffdc8ba1) as the NEGATIVE CONTROL (xnu_live_abt_\n'
  printf '# x49, the 1012 symbols present, but the 1013 symbols x0 and the 1013 leaf literals x0 - the 1013\n'
  printf '# change is NOT in the 1012 arm); the 1012 apex, the 1011 (E-prime)/(A) witness, the 1010/1009/\n'
  printf '# 1008/1007 witnesses, the 1006 freeze witness, the 1005 cadence witness, the 1004 abort trace\n'
  printf '# (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set asserted present\n'
  printf '# (regressions).  The 996-1012 checkers rerun ON the 1013 image - the probe checker-s ONE by-design\n'
  printf '# frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1013-ptewitness;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"
