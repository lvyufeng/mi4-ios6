#!/usr/bin/env bash
# ====================================================================================================
# park_1011.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1010.sh.
#
# Park 1011 and append its block to records/revert-set.txt.
#
# 1011 = 1010c's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS, ABT_SPAN, ABT_FAR, ABT_PC AND
# ABT_PTE all stay 1) + exactly ONE new switch STAGE90_XNU_VME_PROT - a pure MEASUREMENT rung (no
# kernel behaviour change) that reads the ONE CELL 1010c could not: the vm_map_entry's EFFECTIVE
# PROTECTION for the storm's fault (cell (E')), beside the faulting thread's OWN L1 base (cell (A)).
#
# WHY 1011 FOLLOWS 1010c.  The 1010c walk (arm armed-d13-002b0683, records/1010c-press-read.txt) is
# WINDOW-BOUNDED and faults OUT OF WINDOW at the storm: the mask fix (0xffffc000) reads the TRUE L1
# base 0x81a84000, but the L1 slot the storm's VA indexes holds a SECTION descriptor (`ptel1 =
# 0xfeedface`, bits[1:0]=0b10), NOT the COARSE L1 that FS=15 (a level-2 permission fault) requires -
# so the walk returns early (`entry_stubs.c:2291`, `(l1e & 3) != 1`) and the L2 LEAF (and its APX bit)
# is NEVER READ.  F1a (the entry grants write but the write is lost prot->PTE) and F1b (the accept
# path's unconditional `kr = KERN_SUCCESS`, vm_fault.c:2478, is the wrong condition) are therefore
# UNDECIDABLE from 1010c.  1011 decides them WITHOUT a tree patch and WITHOUT a struct offset:
#   (E') `__wrap_vm_map_lookup_locked` publishes the out-param `out_prot` - the entry's EFFECTIVE
#        protection `vm_map_lookup_locked` computes (vm_map.c:9061 `prot = entry->protection`, :9139
#        demotes it for a read fault on a `needs_copy` entry, :9167 `*out_prot = prot`).  Keyed
#        xnu_live_abt_{vmen,vmekr,vmeprot,vmefault,vmeva,vmenx}.  `prot & VM_PROT_WRITE` => F1a;
#        `!(prot & VM_PROT_WRITE)` => F1b (the accept path's KERN_SUCCESS was unconditional).
#   (A)  `stage90_abt_thread_l1_base()` publishes the faulting thread's own L1 base via
#        `current_thread()` (TPIDRPRW) -> `thread->map` (STAGE90_ACT_MAP=956) -> `map->pmap`
#        (STAGE90_MAP_PMAP=44) -> `pmap->pm_l1_phys` (STAGE90_PMAP_L1_PHYS=0, D13-only), keyed
#        xnu_live_abt_ptethr.  COMPARED to `_ptettbr` (the live TTBR0 the walk sampled): if they agree
#        the walk read the right table and the non-coarse `_ptel1` is a real L1 slot; if they differ
#        the walk and the handler look at DIFFERENT tables - the 1010c frontier, MEASURED not assumed.
#
# 1011's change is SOURCE-ONLY, in THREE existing src/ files plus ONE conditional `--wrap`, and NO tree
# edit and NO stage driver:
#   * src/entry/entry_stubs.c   - `g_abtpte_vmelatch`; the (A) reader `stage90_abt_thread_l1_base`
#     (static, VME-gated, D13-gated); the ONE `_ptethr` store in `entry_note_abt_pte`; the (E')
#     recorder `entry_note_abt_vme(kr, prot, fault_type, vaddr)` (args BY VALUE, forwards to
#     `entry_write_kv`, the DIRECT cap-exempt channel);
#   * src/entry/entry_trace.c   - the `entry_note_abt_vme` declaration + the NEW
#     `__wrap_vm_map_lookup_locked` (a PURE PASS-THROUGH except behind `g_abtpte_vmelatch`);
#   * src/entry/entry_saved_state.h - `STAGE90_PMAP_L1_PHYS 0` (D13 branch only: 4570's `struct pmap`
#     leads with `tte`, no `pm_l1_phys`; the 4570 arm publishes the 0xFFFFFFFF sentinel);
#   * src/entry/build_entry.sh  - `--wrap=vm_map_lookup_locked` added to `TRACE_LDFLAGS` ONLY when
#     `VME_PROT=1`, so the OFF image carries no whole-kernel-lookup wrap (the §8.1 wrap surface).
# The VME_PROT guard is the CONJUNCTION `ABT_FAR && ABT_PC && ABT_PTE && VME_PROT` (1011 reads nothing
# 1008/1009/1010 did not, so it is inert unless all three are also on), SEPARATE from the others' so
# the OFF image is byte-identical (the guards are `defined()`-first, not a widened constant -
# [[mi4-off-option-two-spellings]]).  build_entry.sh REFUSES VME_PROT=1 with ABT_FAR=0 OR ABT_PC=0 OR
# ABT_PTE=0.  STAGE90_XNU_VME_PROT is in NEITHER `$EXTRA` NOR the exported pool defines: it is read by
# ONLY the TWO entry-toolchain objects (entry_stubs.c via STUB_DEFINES, entry_trace.c via
# STUB_DEFINES_TRACE) - no pe_msm8974.c change, no pool-object rebuild.  Only an EXPORTS pin so the
# record carries it.
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1011 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1011.sh, run against the linked image): the (E')-protection + (A)-thread-L1
# witness in the linked image (the 43 `xnu_live_abt_` key literals - 1010c's 36 plus the 6 vme
# vmen/vmekr/vmeprot/vmefault/vmeva/vmenx and the 1 ptethr, `entry_note_abt_vme` and
# `__wrap_vm_map_lookup_locked` DEFINED T/t, exactly ONE `bl` to `entry_note_abt_vme` image-wide,
# `__wrap_vm_map_lookup_locked` calling `<vm_map_lookup_locked>` (pass-through) AND
# `entry_note_abt_vme`, and the (A) STAGE90_ACT_MAP immediate (956) present in the recorder or the
# inlined helper), the 1010/1009/1008/1007 witnesses as REGRESSIONS, and the FROZEN 1010c elf
# (armed-d13-002b0683, the PRESSED predecessor) as the NEGATIVE CONTROL (`xnu_live_abt_` x36, the
# 1010 symbols present, but the 1011 symbols x0 and the 1011 literals x0 - the control proves the 1011
# change is NOT in the 1010c arm).  The 1006 freeze witness, the 1005 cadence witness, the 1004 abort
# trace (`xnu_live_sleh_upc` x1), the 1003 stall instrument and the 1002 six-word save set are asserted
# as REGRESSIONS (all still present).  The 996/997/998/999/1000/1001 checkers rerun ON the 1011 image
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
[[ $ARM == armed-d13-002b0683 ]] && { echo "REFUSING: $LIVE holds 1010c's arm (002b0683), the PRESSED predecessor (VERDICT: the walk is WINDOW-BOUNDED - the mask fix reads the TRUE L1 base 0x81a84000, but the storm's L1 slot holds a SECTION descriptor (ptel1=0xfeedface, bits[1:0]=0b10), so the walk returns early and the L2 LEAF (APX) is NEVER READ; F1a/F1b UNDECIDABLE; adjudicated in records/1010c-press-read.txt; design docs/1011-design-v2.md); build 1011 first"; exit 1; }
[[ $ARM == armed-d13-64391319 ]] && { echo "REFUSING: $LIVE holds 1010b's arm (64391319), the PRESSED predecessor (VERDICT: the mask fix is CORRECT but exposed a SECOND defect - the corrected TTBR0 base reads the TRUE L1, whose L2 table is at PA 0x46300000 OUTSIDE the entry window [0x80000000,0x9e400000), and the unbounded L2 deref faulted FS=5, killing the run at launchd-start before the storm; adjudicated in records/1010b-press-read.txt); build 1011 first"; exit 1; }
[[ $ARM == armed-d13-0f39207c ]] && { echo "REFUSING: $LIVE holds 1010's arm (0f39207c), the PRESSED predecessor (VERDICT: the storm reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail, impossible under fsr=0x80f, because the TTBR0 base was masked with the L1->L2 mask 0xfffffc00 instead of 0xffffc000; adjudicated in records/1010-press-read.txt; build 1011 first"; exit 1; }
[[ $ARM == armed-d13-24997fc5 ]] && { echo "REFUSING: $LIVE holds 1009's arm (24997fc5), the PRESSED predecessor (VERDICT: the storm is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x80f = WnR|FS=0xf, far=0x9a000 a LOW user VA; the handler chose the SILENT retry branch; adjudicated in records/1009-press-read.txt); build 1011 first"; exit 1; }
[[ $ARM == armed-d13-0343ef45 ]] && { echo "REFUSING: $LIVE holds 1008's arm (0343ef45), the PRESSED predecessor (VERDICT: the storm is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort, from ordinal 0x400 through 0x200000; adjudicated in records/1008-press-read.txt); build 1011 first"; exit 1; }
[[ $ARM == armed-d13-88074ab3 ]] && { echo "REFUSING: $LIVE holds 1007's arm (88074ab3), the PRESSED predecessor (VERDICT (C): the ~24.4 s freeze is INSIDE ONE sleh_abort riding a STORM of ~2.1 M aborts); build 1011 first"; exit 1; }
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor; build 1011 first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor; build 1011 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one; build 1011 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1011 first"; exit 1; }

# 1011's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_VME_PROT=1.  ABT_PTE (1010c), ABT_PC (1009), ABT_FAR (1008), ABT_SPAN (1007), the freeze
# witness (1006), the cadence witness (1005), abort trace (1004), stall (1003), the r3-save fix (1002,
# USVC hook ON) and the submap skip (1001) ALL stay on as regressions.
grep -q '^STAGE90_XNU_VME_PROT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_VME_PROT=1 - 1011's (E')/（A) identity was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PTE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PTE=1 - 1010c's L2 walk is part of 1011's set verbatim (VME_PROT requires ABT_PTE=1) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PC=1 - 1009's faulting-pc identity is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_FAR=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_FAR=1 - 1008's abort-fault identity is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_SPAN=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_SPAN=1 - 1007's abort-span witness is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1011's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"; exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"; exit 1; }

# BY VALUE, at the freeze point: the 1011 delta FIRST, then the regressions, then the control.
# 1011's NEW instrument (the vm_map_lookup_locked out_prot + the thread's own L1 base), BY VALUE, FIRST.
_ab_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_abt_' || true)
[[ "$_ab_lit" == 43 ]] || {
  echo "REFUSING: the parked image does not carry the 43 xnu_live_abt_ key literals (1010c's 36 + 1011's 6 vme: vmen/vmekr/vmeprot/vmefault/vmeva/vmenx + 1 ptethr) (x$_ab_lit, want 43) - the ABT_SPAN/FAR/PC/PTE store or the 1011 vme/ptethr store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
for _ab_1011sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1011sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1011sym is not a DEFINED text symbol in the parked image - the 1011 instrument is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
# `b` OR `bl`: the (E') recorder is called from the wrapper (measured on the build).
_vme_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' || true)
[[ "$_vme_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_vme_call call site(s) to entry_note_abt_vme (want 1, inside __wrap_vm_map_lookup_locked) - the 1011 (E') witness is not wired"; exit 1; }
# The vm_map_lookup_locked wrapper's PASS-THROUGH is the §8.1 rule made a check: it must call BOTH the
# real body (the pass-through) and entry_note_abt_vme (the (E') record).
#     NOTE: `--wrap=vm_map_lookup_locked` renames `__real_vm_map_lookup_locked` -> `vm_map_lookup_locked`
#     for the linked reference, so the disassembly has `bl <vm_map_lookup_locked>`, never
#     `<__real_vm_map_lookup_locked>` (the same rule measured for vm_fault in 1010).
_vmew=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_map_lookup_locked"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_map_lookup_locked not in the parked image"; exit 1; }
_vmewx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_vmew#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_vmewb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_vmew" --stop-address="${_vmewx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_map_lookup_locked>' <<<"$_vmewb" || { echo "REFUSING: __wrap_vm_map_lookup_locked does not call the real vm_map_lookup_locked body - it is not a pass-through (docs/1011-design-v2.md §2.3 / §8.1)"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' <<<"$_vmewb" || { echo "REFUSING: __wrap_vm_map_lookup_locked does not call entry_note_abt_vme - the (E') record never fires"; exit 1; }
# The (A) deref chain: entry_note_abt_pte or its inlined static helper must carry the thread->map offset.
_pte_adr=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="entry_note_abt_pte"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: entry_note_abt_pte not in the parked image"; exit 1; }
_l1_adr=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="stage90_abt_thread_l1_base"{print "0x" $1; found=1} END{exit(found?0:0)}')
_regions=''
for _base in "$_pte_adr" "$_l1_adr"; do
  [[ -n "$_base" ]] || continue
  _nx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_base#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
  _regions+=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_base" --stop-address="${_nx:-0xffffffff}")
done
grep -qE '(#956\b|#0x3bc|movw[[:space:]]+r[0-9]+, #956)' <<<"$_regions" || { echo "REFUSING: neither entry_note_abt_pte nor stage90_abt_thread_l1_base carries the STAGE90_ACT_MAP immediate (956) - the 1011 (A) reader did not compile in"; exit 1; }
echo "park_1011: by value - xnu_live_abt_* literals x$_ab_lit (1010c's 36 + 6 vme + 1 ptethr), entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, exactly ONE b/bl to entry_note_abt_vme, the wrapper calling vm_map_lookup_locked AND entry_note_abt_vme, and the (A) STAGE90_ACT_MAP immediate (956) present (the (E')-protection + (A)-thread-L1 witness is present)"

# THE 1010 CHANGE (the L2 descriptor + the vm_fault return) is a REGRESSION: the 36 xnu_live_abt_
# literals baseline (already inside the 43 above), the three 1010 symbols, the ONE b/bl to
# entry_note_abt_pte, and the __wrap_vm_fault pass-through.  The 1010 change is NOT dropped by 1011.
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the parked image - the 1010 instrument regressed (1011 keeps 1010c's set verbatim)"; exit 1; }
done
_abt_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' || true)
[[ "$_abt_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abt_call call site(s) to entry_note_abt_pte (want 1, inside __wrap_sleh_abort) - the 1010 witness regressed"; exit 1; }
_abt_wfa=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_fault not in the parked image"; exit 1; }
_abt_wfx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_abt_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_abt_wfb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_abt_wfa" --stop-address="${_abt_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - the 1010 pass-through regressed"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the 1010 (B) record regressed"; exit 1; }
echo "park_1011: by value - entry_note_abt_pte/vmf + __wrap_vm_fault DEFINED T/t, ONE b/bl to entry_note_abt_pte, the __wrap_vm_fault pass-through intact (1010's L2-descriptor + vm_fault-return witness kept)"

# 1009's faulting-pc identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the parked image - the 1009 recorder was dropped (1011 keeps 1010c's set verbatim)"; exit 1; }
_abp_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' || true)
[[ "$_abp_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abp_call call site(s) to entry_note_abt_pc (want 1) - the 1009 witness regressed"; exit 1; }
echo "park_1011: by value - entry_note_abt_pc DEFINED T/t with exactly ONE b/bl (1009's faulting-pc identity kept)"
# 1008's abort-fault identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the parked image - the 1008 recorder was dropped"; exit 1; }
echo "park_1011: by value - entry_note_abt_far DEFINED T/t (1008's abort-fault identity kept)"
# 1007's abort-span witness is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder was dropped"; exit 1; }
echo "park_1011: by value - entry_note_abt_span DEFINED T/t with exactly ONE b/bl (1007's abort-span witness kept)"
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
echo "park_1011: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The 1005 cadence witness is a REGRESSION.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed"; exit 1; }
done
echo "park_1011: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1011: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1011: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1011: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1010c elf as the NEGATIVE CONTROL for the 1011 reader.
_ctrl=out/stage90/frozen/armed-d13-002b0683/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
  _c_1010sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_pte|entry_note_abt_vmf|__wrap_vm_fault)$' || true)
  _c_11sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_vme|__wrap_vm_map_lookup_locked)$' || true)
  _c_11lit=$(strings "$_ctrl" | grep -cE 'xnu_live_abt_(vme|ptethr)' || true)
  [[ "$_c_ab" == 36 && "$_c_1010sym" == 3 && "$_c_11sym" == 0 && "$_c_11lit" == 0 ]] || {
    echo "REFUSING: the frozen 1010c elf does not have the pre-1011 shape (xnu_live_abt_=$_c_ab want 36, 1010 symbols=$_c_1010sym want 3, 1011 symbols=$_c_11sym want 0, 1011 literals=$_c_11lit want 0) - the control image is not the pre-1011 one"; exit 1; }
  echo "park_1011: by value - NEGATIVE CONTROL (frozen 1010c elf): xnu_live_abt_ x$_c_ab (1010c's 36 keys), the 1010 symbols x$_c_1010sym, 1011 symbols x$_c_11sym (the reader can fail), 1011 literals x$_c_11lit"
else
  echo "park_1011: WARNING - the frozen 1010c elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1011: THE vm_map_entry EFFECTIVE PROTECTION + THE FAULTING THREAD-S OWN L1 BASE\n# (publishes vm_map_lookup_locked-s out_prot for the storm-s fault, and the L1 base the faulting\n# thread-s own map/pmap names, so the reader can decide F1a vs F1b WITHOUT a tree patch and WITHOUT a\n# struct offset; design docs/1011-design-v2.md)\n' "$ARM"
  printf '# Switch set is 1010c-s VERBATIM plus STAGE90_XNU_VME_PROT=1 (ABT_PTE, ABT_PC, ABT_FAR, ABT_SPAN,\n'
  printf '# FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM,\n'
  printf '# RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (002b0683 -> %s); the\n' "${ARM#armed-d13-}"
  printf '# PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is 999-s, unchanged); the\n'
  printf '# TREE does NOT move - 1011 has NO tree edit and NO stage driver (the change is THREE src/ files\n'
  printf '# plus ONE conditional --wrap: entry_stubs.c the (A) reader + g_abtpte_vmelatch + the _ptethr store\n'
  printf '# + the (E-prime) recorder, entry_trace.c the declaration + __wrap_vm_map_lookup_locked,\n'
  printf '# entry_saved_state.h STAGE90_PMAP_L1_PHYS=0, build_entry.sh --wrap=vm_map_lookup_locked only when\n'
  printf '# VME_PROT=1).\n#\n'
  printf '# THE FRONTIER 1011 PROBES (records/1010c-press-read.txt): the storm is a USER-mode WRITE permission\n'
  printf '# fault - pc=0x2be4d1a0 (in the dyld shared cache), fsr=0x0000080f (WnR=1 | FS=0xf "level-2 perm\n'
  printf '# fault"), far=0x5d000 (a LOW user VA). The 1010c walk is WINDOW-BOUNDED but reads the WRONG TABLE\n'
  printf '# at the storm: with the mask fixed to 0xffffc000 it reads the TRUE L1 base 0x81a84000, whose slot\n'
  printf '# for the fault VA holds a SECTION descriptor (ptel1=0xfeedface, bits[1:0]=0b10), so the walk\n'
  printf '# returns early (entry_stubs.c:2291, (l1e&3)!=1) and the L2 LEAF (APX) is NEVER read. F1a/F1b are\n'
  printf '# UNDECIDABLE from 1010c.\n#\n'
  printf '# THE 1011 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted):\n'
  printf '# THE HEADLINE MUST be decided from the TAIL ROWS-OWN xnu_live_abt_vmeprot (ordinals >= 0x400 on the\n'
  printf '# powers-of-two ladder), NEVER on the whole-run change count (the 1009 measurement defect, not\n'
  printf '# repeated). (E-prime) DECISION TREE (docs/1011-design-v2.md §3):\n'
  printf '#   * xnu_live_abt_vmeprot & VM_PROT_WRITE (0x2) != 0 at the tail => F1a: the vm_map_entry GRANTS\n'
  printf '#     write, so the write is LOST between the entry and the PTE (the prot->PTE path, or the entry-s\n'
  printf '#     protection is not what the fault is serviced with).\n'
  printf '#   * (xnu_live_abt_vmeprot & 0x2) == 0 at the tail => F1b: the entry does NOT grant write, so the\n'
  printf '#     accept path-s UNCONDITIONAL `kr = KERN_SUCCESS` (vm_fault.c:2478) is the wrong condition -\n'
  printf '#     the fault is accepted although the entry is read-only, and the retry is forever.\n'
  printf '#   * xnu_live_abt_vmekr names vm_map_lookup_locked-s return (KERN_SUCCESS=0 / KERN_INVALID_ADDRESS=1\n'
  printf '#     / KERN_PROTECTION_FAILURE=2 / KERN_NO_SPACE=3).  _vmefault is the fault_type (VM_PROT_*).\n'
  printf '# (A) THE L1-BASE CELL: compare xnu_live_abt_ptethr (the faulting thread-s own L1 base) with\n'
  printf '# xnu_live_abt_ptettbr (the live TTBR0 the walk sampled).  AGREE => the walk read the right table\n'
  printf '# and the non-coarse _ptel1 IS a real L1 slot (the storm VA is served by a SECTION mapping, or the\n'
  printf '# TTBR0 the handler runs with is not the one that faulted).  DIFFER => the walk and the handler look\n'
  printf '# at DIFFERENT tables - the 1010c frontier, now MEASURED.\n#\n'
  printf '# VERIFIED BY VALUE (build_1011.sh): the linked image carries the 43 xnu_live_abt_ key literals\n'
  printf '# (1010c-s 36 + the 6 vme vmen/vmekr/vmeprot/vmefault/vmeva/vmenx + the 1 ptethr),\n'
  printf '# entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, exactly ONE bl to entry_note_abt_vme,\n'
  printf '# the __wrap_vm_map_lookup_locked body calling <vm_map_lookup_locked> (pass-through) AND\n'
  printf '# entry_note_abt_vme, and the (A) STAGE90_ACT_MAP immediate (956) present, with the FROZEN 1010c\n'
  printf '# elf (armed-d13-002b0683) as the NEGATIVE CONTROL (xnu_live_abt_ x36, the 1010 symbols present,\n'
  printf '# but the 1011 symbols x0 and the 1011 literals x0 - the 1011 change is NOT in the 1010c arm);\n'
  printf '# the 1010/1009/1008/1007 witnesses, the 1006 freeze witness, the 1005 cadence witness, the 1004\n'
  printf '# abort trace (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set\n'
  printf '# asserted present (regressions).  The 996/997/998/999/1000/1001 checkers rerun ON the 1011\n'
  printf '# image - the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1011-vmeprot;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"