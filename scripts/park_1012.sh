#!/usr/bin/env bash
# ====================================================================================================
# park_1012.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1011.sh.
#
# Park 1012 and append its block to records/revert-set.txt.
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
[[ "$_ab_lit" == 49 ]] || {
  echo "REFUSING: the parked image does not carry the 49 xnu_live_abt_ key literals (1011's 43 + 1012's 6 pmp: pmpn/pmpprot/pmppa/pmppmap/pmpttbr/pmpnx) (x$_ab_lit, want 49) - the 1011 vme/ptethr store or the 1012 pmp store is gone"; exit 1; }
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
echo "park_1012: by value - xnu_live_abt_* literals x$_ab_lit (1011's 43 + 6 pmp), entry_note_abt_pmp/__wrap_pmap_enter_options DEFINED T/t, exactly ONE b/bl to entry_note_abt_pmp, and the wrapper calling pmap_enter_options AND entry_note_abt_pmp (the prot-at-the-apex witness is present)"

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

# THE FROZEN 1011 elf as the NEGATIVE CONTROL for the 1012 reader.
_ctrl=out/stage90/frozen/armed-d13-fce83837/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
  _c_1011sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_vme|__wrap_vm_map_lookup_locked)$' || true)
  _c_12sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_pmp|__wrap_pmap_enter_options)$' || true)
  _c_12lit=$(strings "$_ctrl" | grep -c 'xnu_live_abt_pmp' || true)
  [[ "$_c_ab" == 43 && "$_c_1011sym" == 2 && "$_c_12sym" == 0 && "$_c_12lit" == 0 ]] || {
    echo "REFUSING: the frozen 1011 elf does not have the pre-1012 shape (xnu_live_abt_=$_c_ab want 43, 1011 symbols=$_c_1011sym want 2, 1012 symbols=$_c_12sym want 0, 1012 literals=$_c_12lit want 0) - the control image is not the pre-1012 one"; exit 1; }
  echo "park_1012: by value - NEGATIVE CONTROL (frozen 1011 elf): xnu_live_abt_ x$_c_ab (1011's 43 keys), the 1011 symbols x$_c_1011sym, 1012 symbols x$_c_12sym (the reader can fail), 1012 literals x$_c_12lit"
else
  echo "park_1012: WARNING - the frozen 1011 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1012: THE PTE-BUILD APEX - the `prot` handed to pmap_enter_options\n# (publishes the leaf-s own INPUT via --wrap=pmap_enter_options: leo_enter REBUILDS the leaf from that\n# `prot` unconditionally, so a write-bearing `prot` here MUST yield a writable leaf and a read-only one\n# here IS the F1a loss; design docs/1012-design.md)\n' "$ARM"
  printf '# Switch set is 1011-s VERBATIM plus STAGE90_XNU_PMP_ENTER=1 (VME_PROT, ABT_PTE, ABT_PC, ABT_FAR,\n'
  printf '# ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE,\n'
  printf '# SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (fce83837 ->\n'
  printf '# %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is 999-s,\n' "${ARM#armed-d13-}"
  printf '# unchanged); the TREE does NOT move - 1012 has NO tree edit and NO stage driver (the change is TWO\n'
  printf '# src/ files plus ONE conditional --wrap: entry_stubs.c the recorder `entry_note_abt_pmp` + the\n'
  printf '# shared OR-guard VA key, entry_trace.c the declaration + __wrap_pmap_enter_options,\n'
  printf '# build_entry.sh --wrap=pmap_enter_options only when PMP_ENTER=1).  The companion TREE probe\n'
  printf '# STAGE90_XNU_WRSTRIP (tools/patch_d13_wrstrip.py, the vm_fault.c strip sites) is SEPARATE and\n'
  printf '# optional - this arm does not require it.\n#\n'
  printf '# THE FRONTIER 1012 PROBES (records/1011-press-read.txt): 1011 DECIDED the fault class BY VALUE -\n'
  printf '# the (E-prime) tail rows carry vmeprot=0x3 (READ|WRITE) with vmekr=0 (KERN_SUCCESS), so the vm_map\n'
  printf '# ENTRY GRANTS write and F1b is REFUTED BY VALUE; F1a stands - the write is LOST between the entry\n'
  printf '# and the PTE.  What 1011 could NOT name is WHERE.  On this tree pmap_enter_options dispatches to\n'
  printf '# leo_enter (pmap.c:3181), which REBUILDS the leaf UNCONDITIONALLY from the `prot` it is handed\n'
  printf '# (leo_pte_build -> pmap_vm_prot_to_page_flags CLEARS APX iff prot & VM_PROT_WRITE), and every\n'
  printf '# leo_enter path ends in pmap_flush_tlbs.  PMAP_ENTER_OPTIONS (osfmk/vm/pmap.h:399) is a\n'
  printf '# CROSS-object call, so --wrap=pmap_enter_options redirects every linker-visible caller (pmap.c-s\n'
  printf '# own intra-object pmap_enter -> pmap_enter_options is NOT wrapped).\n#\n'
  printf '# THE 1012 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted):\n'
  printf '# THE HEADLINE MUST be decided from the TAIL ROWS-OWN xnu_live_abt_pmpprot (ordinals >= 0x400 on the\n'
  printf '# powers-of-two ladder), NEVER on the whole-run change count (the 1009 measurement defect, not\n'
  printf '# repeated).  THE DECISION TREE (docs/1012-design.md §4):\n'
  printf '#   * tail xnu_live_abt_pmpprot & VM_PROT_WRITE (0x2) == 0 => (b) a STRIP SITE fired above the\n'
  printf '#     apex: `prot` LOST write.  Prime suspects: the RetryFault block vm_fault.c:3845 (`prot &=\n'
  printf '#     retry_prot`, whose verify re-lookup deliberately passes fault_type & ~VM_PROT_WRITE) and :3855\n'
  printf '#     (the ~VM_PROT_WRITE clear after a copy-object change) - both INVISIBLE to 1011-s single-shot\n'
  printf '#     (E-prime) latch.  The fix is at the strip site; the F4 retry-bound\n'
  printf '#     (tools/patch_d13_abt_retry_bound.py) is the safety net.\n'
  printf '#   * tail xnu_live_abt_pmpprot & 0x2 != 0 AND bits[31:12] of xnu_live_abt_pmppmap == bits[31:12] of\n'
  printf '#     xnu_live_abt_pmpttbr => (c) the apex AGREES with the MMU-walked base but the leaf is still\n'
  printf '#     read-only => the loss is INSIDE leo_enter/leo_pte_build (read pmap.c:1640-1682 by value with\n'
  printf '#     the measured `prot`).\n'
  printf '#   * tail xnu_live_abt_pmpprot & 0x2 != 0 AND bits[31:12] of pmppmap != bits[31:12] of pmpttbr =>\n'
  printf '#     (c) an IDENTITY FAULT: the pmap writes one L1, the MMU walks another (a pmap_expand_ttb hoist\n'
  printf '#     not followed by a TTBR0 load, or a stale pm_l1_phys).\n'
  printf '#   * NO tail row => the storm-s enter did NOT reach the wrapper (the intra-object blindness): fall\n'
  printf '#     back to the tree instrument.  xnu_live_abt_pmpnx is the DISTINCT-`prot` change count (an\n'
  printf '#     OBSERVATION on the whole run, not the headline).  The _pmppmap/_pmpttbr pair ALSO closes the\n'
  printf '#     1010c/1011 walk-mask question for good.\n#\n'
  printf '# VERIFIED BY VALUE (build_1012.sh): the linked image carries the 49 xnu_live_abt_ key literals\n'
  printf '# (1011-s 43 + the 6 pmp pmpn/pmpprot/pmppa/pmppmap/pmpttbr/pmpnx), entry_note_abt_pmp and\n'
  printf '# __wrap_pmap_enter_options DEFINED T/t, exactly ONE bl to entry_note_abt_pmp, the\n'
  printf '# __wrap_pmap_enter_options body calling <pmap_enter_options> (pass-through) AND entry_note_abt_pmp,\n'
  printf '# with the FROZEN 1011 elf (armed-d13-fce83837) as the NEGATIVE CONTROL (xnu_live_abt_ x43, the 1011\n'
  printf '# symbols present, but the 1012 symbols x0 and the 1012 literals x0 - the 1012 change is NOT in the\n'
  printf '# 1011 arm); the 1011 (E-prime)/(A) witness, the 1010/1009/1008/1007 witnesses, the 1006 freeze\n'
  printf '# witness, the 1005 cadence witness, the 1004 abort trace (xnu_live_sleh_upc x1), the 1003 stall\n'
  printf '# instrument and the 1002 six-word save set asserted present (regressions).  The\n'
  printf '# 996/997/998/999/1000/1001 checkers rerun ON the 1012 image - the probe checker-s ONE by-design\n'
  printf '# frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1012-pmpenter;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"