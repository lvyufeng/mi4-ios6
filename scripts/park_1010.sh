#!/usr/bin/env bash
# ====================================================================================================
# park_1010.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1009.sh.
#
# Park 1010 and append its block to records/revert-set.txt.
#
# 1010 = 1009's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS, ABT_SPAN, ABT_FAR AND ABT_PC all
# stay 1) + exactly ONE new switch STAGE90_XNU_ABT_PTE - a pure MEASUREMENT rung (no kernel behaviour
# change) that reads ONE CELL FROM EACH SIDE of the vm_map-vs-PTE disagreement 1009 named:
#   (A) `entry_note_abt_pte(uint32_t va)` walks TTBR0 to the L2 small-page descriptor for the fault VA
#       (`stage90_read_l2_pte`, the walk of docs/1010-pte-walk.md §4, keyed on TTBCR.N) and publishes
#       it under the DIRECT-channel keys xnu_live_abt_{pten,pteva,ptel2,ptel1,ptettbr,pteu} on 1008/
#       1009's SAME powers-of-two ladder, plus the change-count xnu_live_abt_ptenx.  The decisive cell
#       is `L2_ACCESS_APX` (bit 9): APX=1 => the page is USER READ-ONLY though the fault asked for a
#       WRITE - exactly "vm_fault said success, the PTE is read-only" (the cs-taint/prot-downgrade
#       path, vm_fault.c:2476-2485 -> arm/pmap.c:1110-1113).
#   (B) `entry_note_abt_vmf(uint32_t ret, uint32_t va, uint32_t prot)` records what `vm_fault` RETURNED
#       (called from the NEW `__wrap_vm_fault`, a PURE PASS-THROUGH except behind a latch), under
#       xnu_live_abt_{vmfn,vmfret,vmfva,vmfprot,vmfnret}.
#
# THE FRONTIER 1010 PROBES: the 1009 read (records/1009-press-read.txt, arm armed-d13-24997fc5) named
# the storm BY VALUE - from ordinal 0x400 through 0x200000 (~2.09 M aborts) the faulting instruction is
# CONSTANT (`pc=0x2be8a1a0`, in the dyld shared cache), the DFSR is CONSTANT (`fsr=0x0000080f` =
# WnR=1 (a write) | FS=0xf "permission fault, level 2"), the abort ran in USER mode
# (`cpsr=0x60000030`), and `far=0x9a000` is a LOW user VA (below the TTBR0 split 0x40000000, in one of
# launchd's own COW/data segments).  What 1009 could NOT say is WHY the write permission fault is never
# dismissed: the handler PRINTS on the failure branch and is SILENT on the retry branch, and the capture
# shows the silent branch (not one `usermode data abort` line over 2.1 M faults), so vm_fault returned
# KERN_SUCCESS || KERN_ABORTED - the handler retries forever.  1010 names WHICH side of the disagreement
# is wrong (design docs/1010-design.md).
#
# 1010's change is SOURCE-ONLY, in TWO existing src/ files plus ONE conditional `--wrap`, and NO tree
# edit and NO stage driver:
#   * src/entry/entry_stubs.c  - the walk `stage90_read_l2_pte` + the recorders `entry_note_abt_pte`
#     and `entry_note_abt_vmf` after the 1009 recorder's `#endif` (args BY VALUE - they never call
#     `entry_counter()`; they forward to `entry_write_kv`, the DIRECT cap-exempt channel);
#   * src/entry/entry_trace.c  - the declarations + the ONE `entry_note_abt_pte(far_)` call after the
#     1009 call inside `__wrap_sleh_abort`, and the NEW `__wrap_vm_fault` (a pure pass-through);
#   * src/entry/build_entry.sh - `--wrap=vm_fault` added to `TRACE_LDFLAGS` ONLY when `ABT_PTE=1`, so
#     the OFF image carries no such wrap (the §8.1 whole-kernel-wrap surface).
# The ABT_PTE guard is the CONJUNCTION `ABT_FAR && ABT_PC && ABT_PTE` (1010 reads nothing 1008/1009 did
# not, so it is inert unless both are also on), SEPARATE from the others' so the OFF image is
# byte-identical (the guards are `defined()`-first, not a widened constant - [[mi4-off-option-two-
# spellings]]).  build_entry.sh REFUSES ABT_PTE=1 with ABT_FAR=0 OR ABT_PC=0.  STAGE90_XNU_ABT_PTE is
# in NEITHER `$EXTRA` NOR the exported pool defines: it is read by ONLY the TWO entry-toolchain objects
# (entry_stubs.c via STUB_DEFINES, entry_trace.c via STUB_DEFINES_TRACE) - no pe_msm8974.c change, no
# pool-object rebuild, no $EXTRA entry.  Only an EXPORTS pin so the record carries it.
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1010 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1010.sh, run against the linked image): the L2-descriptor + vm_fault-return
# witness in the linked image (the 36 `xnu_live_abt_` key literals - the 6 span + the 5 far + the 7 pc
# + the 12 pte/vmf + the 6 1010c walk-window, `entry_note_abt_pte`/`entry_note_abt_vmf`/`__wrap_vm_fault`
# DEFINED T/t, the ONE
# `b/bl` to `entry_note_abt_pte` inside `__wrap_sleh_abort` image-wide, and the `__wrap_vm_fault` body
# calling BOTH `__real_vm_fault` (pass-through) and `entry_note_abt_vmf`), the 1009/1008/1007 witnesses
# as REGRESSIONS, and the FROZEN 1009 elf (armed-d13-24997fc5, the PRESSED 1009 arm) as the NEGATIVE
# CONTROL (`xnu_live_abt_` x18, `entry_note_abt_pc` >= 1, but the 1010 symbols x0 and the 1010 literals
# x0 - the control proves the 1010 change is NOT in the 1009 arm).  The 1006 freeze witness, the 1005
# cadence witness, the 1004 abort trace (`xnu_live_sleh_upc` x1), the 1003 stall instrument and the
# 1002 six-word save set are asserted as REGRESSIONS (all still present).  The 996/997/998/999/1000/
# 1001 checkers rerun ON the 1010 image with the FROZEN images as negative controls (the probe
# checker's ONE by-design frozen 20 B clause is the only allowed FAIL).
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
[[ $ARM == armed-d13-64391319 ]] && { echo "REFUSING: $LIVE holds 1010b's arm (64391319), the PRESSED predecessor (VERDICT: the mask fix is CORRECT but exposed a SECOND defect - the corrected TTBR0 base reads the TRUE L1, whose L2 table is at PA 0x46300000 OUTSIDE the entry window [0x80000000,0x9e400000), and the unbounded L2 deref faulted FS=5, killing the run at launchd-start before the storm; the leaf is STILL unread; adjudicated in records/1010b-press-read.txt); build 1010c first"; exit 1; }
[[ $ARM == armed-d13-0f39207c ]] && { echo "REFUSING: $LIVE holds 1010's arm (0f39207c), the PRESSED predecessor (VERDICT: the storm reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail, impossible under fsr=0x80f, because the TTBR0 base was masked with the L1->L2 mask 0xfffffc00 instead of 0xffffc000; adjudicated in records/1010-press-read.txt); build 1010b first"; exit 1; }
[[ $ARM == armed-d13-24997fc5 ]] && { echo "REFUSING: $LIVE holds 1009's arm (24997fc5), the PRESSED predecessor (VERDICT: the storm is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x80f = WnR|FS=0xf, far=0x9a000 a LOW user VA; the handler chose the SILENT retry branch, so vm_fault returned KERN_SUCCESS||KERN_ABORTED; adjudicated in records/1009-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-0343ef45 ]] && { echo "REFUSING: $LIVE holds 1008's arm (0343ef45), the PRESSED predecessor (VERDICT: the storm is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort, from ordinal 0x400 through 0x200000 - ~2.09 M retries of one fault address, riding the ~18.7 s handler call; adjudicated in records/1008-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-88074ab3 ]] && { echo "REFUSING: $LIVE holds 1007's arm (88074ab3), the PRESSED predecessor (VERDICT (C): the ~24.4 s freeze is INSIDE ONE sleh_abort - maxcall 18.735 s = 0.768 x M - riding a STORM of ~2.1 M aborts; the storm's fault ADDRESS is in no capture; adjudicated in records/1007-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor (VERDICT: the STALL is (b2) a whole-interrupt-path freeze ~24.4 s, adjudicated in records/1005-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor (VERDICT WALK then the tick collapsed - the frontier was the STALL, adjudicated in records/1004-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the PRESSED predecessor (pid 1 is in the kernel's data-abort return path, adjudicated in records/1003-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1010 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1010 first"; exit 1; }

# 1010's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_ABT_PTE=1.  1009's ABT_PC, 1008's ABT_FAR, 1007's ABT_SPAN, 1006's freeze witness,
# 1005's cadence witness, 1004's abort trace, 1003's stall switch, 1002's r3-save fix (USVC hook ON)
# and 1001's submap skip ALL stay on as regressions.
grep -q '^STAGE90_XNU_ABT_PTE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PTE=1 - 1010's L2-descriptor identity was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PC=1 - 1009's faulting-pc identity is part of 1010's set verbatim (the 1010 call sits after the pc call) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_FAR=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_FAR=1 - 1008's abort-fault identity is part of 1010's set verbatim (the pc recorder sits after the far recorder) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_SPAN=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_SPAN=1 - 1007's abort-span witness is part of 1010's set verbatim (the far recorder sits after the span recorder) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness is part of 1010's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1010's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1010's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1010's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"; exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"; exit 1; }

# BY VALUE, at the freeze point: nine things.
# (0) THE 1010 CHANGE (the L2 descriptor + the vm_fault return) must be in the linked image: the
#     thirty-six `xnu_live_abt_` key literals (the 6 span n/seq/gap/call/maxcall/maxgap + the 5 far
#     far/type/farn/maxfar/maxtype + the 7 pc pcn/pc/fsr/abtcpsr/pcfar/nfar/nfsr + the 12 pte/vmf
#     pten/pteva/ptel2/ptel1/ptettbr/pteu/ptenx/vmfn/vmfret/vmfva/vmfprot/vmfnret + the 6 1010c
#     walk-window pteoow/ptealias/ptemapok/pteslot/ptewbase/ptewlen),
#     `entry_note_abt_pte`/`entry_note_abt_vmf`/`__wrap_vm_fault` DEFINED T/t, exactly ONE `b/bl` to
#     `entry_note_abt_pte` inside `__wrap_sleh_abort` image-wide, and the `__wrap_vm_fault` body
#     calling BOTH `__real_vm_fault` and `entry_note_abt_vmf`.
# (1) THE 1009 CHANGE (the faulting-pc identity) is a REGRESSION: `entry_note_abt_pc` DEFINED T/t and
#     exactly ONE `b/bl` to it image-wide must STILL be present (its 7 pc literals are inside the (0)
#     twenty-nine).
# (2) THE 1008 CHANGE (the abort-fault identity) is a REGRESSION: `entry_note_abt_far` DEFINED T/t and
#     exactly ONE `b/bl` to it image-wide.
# (3) THE 1007 CHANGE (the abort-span witness) is a REGRESSION: the six span key literals,
#     `entry_note_abt_span` DEFINED T/t, and exactly ONE `b/bl` to it image-wide.
# (4) THE 1006 CHANGE (the freeze witness) is a REGRESSION: the six `xnu_live_freeze_` key literals,
#     the two `xnu_live_frzbase_` key literals, and the two symbols DEFINED T/t.
# (5) THE 1005 CADENCE WITNESS is a REGRESSION: the four `xnu_live_cadence_` key literals AND the four
#     symbols DEFINED T/t.
# (6) THE 1004 ABORT TRACE is a REGRESSION: the `xnu_live_sleh_upc` key literal.
# (7) THE 1003 STALL INSTRUMENT and the 1002 r3-SAVE FIX are REGRESSIONS.
# (8) The FROZEN 1009 elf (out/stage90/frozen/armed-d13-24997fc5/) as the NEGATIVE CONTROL: it must
#     carry `xnu_live_abt_` x18 and an `entry_note_abt_pc` symbol but NO 1010 symbol and NO 1010
#     literal (a reader that cannot fail certifies nothing).
# 1010's NEW instrument (the L2 descriptor + the vm_fault return), BY VALUE, FIRST.
_ab_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_abt_' || true)
[[ "$_ab_lit" == 36 ]] || {
  echo "REFUSING: the parked image does not carry the 36 xnu_live_abt_ key literals (6 span + 5 far + 7 pc + 12 pte: pten/pteva/ptel2/ptel1/ptettbr/pteu/ptenx/vmfn/vmfret/vmfva/vmfprot/vmfnret + 6 1010c walk-window: pteoow/ptealias/ptemapok/pteslot/ptewbase/ptewlen) (x$_ab_lit, want 36) - the ABT_SPAN/FAR/PC store, the 1010 pte/vmf store or the 1010c walk-window store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the parked image - the 1010 instrument is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
# `b` OR `bl`: the recorder is a TAIL call (`b <entry_note_abt_pte>`), measured on the first build.
_abt_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' || true)
[[ "$_abt_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abt_call call site(s) to entry_note_abt_pte (want 1, inside __wrap_sleh_abort) - the 1010 witness is not wired to the abort path"; exit 1; }
# The vm_fault wrapper's PASS-THROUGH is the §8.1 rule made a check: it must call BOTH the real
# vm_fault body (the pass-through) and entry_note_abt_vmf (the (B) record).
#     NOTE: `--wrap=vm_fault` renames `__real_vm_fault` -> `vm_fault` for the linked reference, so the
#     disassembly has `bl <vm_fault>`, never `<__real_vm_fault>` (MEASURED this build).
_abt_wfa=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_fault not in the parked image"; exit 1; }
_abt_wfx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_abt_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_abt_wfb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_abt_wfa" --stop-address="${_abt_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - it is not a pass-through (docs/1010-design.md §8.1)"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the (B) record never fires"; exit 1; }
echo "park_1010: by value - xnu_live_abt_* literals x$_ab_lit (6 span + 5 far + 7 pc + 12 pte/vmf), entry_note_abt_pte/vmf + __wrap_vm_fault DEFINED T/t, exactly ONE b/bl to entry_note_abt_pte, the wrapper calling __real_vm_fault AND entry_note_abt_vmf (the L2-descriptor + vm_fault-return witness is present)"
# 1009's faulting-pc identity is a REGRESSION: its recorder + ONE call site must STILL be present.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the parked image - the 1009 recorder was dropped (1010 keeps 1009's set verbatim)"; exit 1; }
_abp_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' || true)
[[ "$_abp_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abp_call call site(s) to entry_note_abt_pc (want 1, inside __wrap_sleh_abort) - the 1009 witness regressed"; exit 1; }
echo "park_1010: by value - entry_note_abt_pc DEFINED T/t with exactly ONE b/bl (1009's faulting-pc identity kept)"
# 1008's abort-fault identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the parked image - the 1008 recorder was dropped"; exit 1; }
_abf_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' || true)
[[ "$_abf_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abf_call call site(s) to entry_note_abt_far (want 1) - the 1008 abort-fault identity regressed"; exit 1; }
echo "park_1010: by value - entry_note_abt_far DEFINED T/t with exactly ONE b/bl (1008's abort-fault identity kept)"
# 1007's abort-span witness is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder was dropped"; exit 1; }
_ab_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' || true)
[[ "$_ab_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_ab_call call site(s) to entry_note_abt_span (want 1) - the 1007 span witness regressed"; exit 1; }
echo "park_1010: by value - entry_note_abt_span DEFINED T/t with exactly ONE b/bl (1007's abort-span witness kept)"
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
echo "park_1010: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The call site must be INSIDE __wrap_machine_idle, image-wide exactly once ([[mi4-hooks-live-at-calls]]).
_frz_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' || true)
[[ "$_frz_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_frz_call call site(s) to entry_freeze_sample (want 1, inside __wrap_machine_idle) - the 1006 sampler is not wired to the idle path"; exit 1; }
echo "park_1010: by value - exactly ONE bl to entry_freeze_sample in the linked image (the idle-path call site)"

# 1005's cadence witness is a REGRESSION: its four key literals and four symbols must STILL be present.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (t/sync/tick/irq) (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed (1010 keeps 1009's set verbatim)"; exit 1; }
done
echo "park_1010: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1010: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1010: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1010: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1009 elf as the NEGATIVE CONTROL for the new reader.
_ctrl=out/stage90/frozen/armed-d13-24997fc5/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
  _c_pcsym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_pc$' || true)
  _c_presym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_pte|entry_note_abt_vmf|__wrap_vm_fault)$' || true)
  _c_prelit=$(strings "$_ctrl" | grep -cE 'xnu_live_abt_(pte|vmf)' || true)
  [[ "$_c_ab" == 18 && "$_c_pcsym" -ge 1 && "$_c_presym" == 0 && "$_c_prelit" == 0 ]] || {
    echo "REFUSING: the frozen 1009 elf does not have the pre-1010 shape (xnu_live_abt_=$_c_ab want 18, entry_note_abt_pc=$_c_pcsym want >=1, 1010 symbols=$_c_presym want 0, 1010 literals=$_c_prelit want 0) - the control image is not the pre-1010 one"; exit 1; }
  echo "park_1010: by value - NEGATIVE CONTROL (frozen 1009 elf): xnu_live_abt_ x$_c_ab (the 6 span + 5 far + 7 pc keys), entry_note_abt_pc x$_c_pcsym, 1010 symbols x$_c_presym (the reader can fail), 1010 literals x$_c_prelit"
else
  echo "park_1010: WARNING - the frozen 1009 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1010: THE L2 DESCRIPTOR + THE vm_fault RETURN (walks TTBR0 to the L2 small-page\n# descriptor for the fault VA and publishes it, plus the vm_fault return code, so the reader can say\n# WHICH side of the vm_map-vs-PTE disagreement 1009 named is wrong; design docs/1010-design.md,\n# docs/1010-pte-walk.md)\n' "$ARM"
  printf '# Switch set is 1009-s VERBATIM plus STAGE90_XNU_ABT_PTE=1 (ABT_PC, ABT_FAR, ABT_SPAN,\n'
  printf '# FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM,\n'
  printf '# RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (24997fc5 -> %s); the\n' "${ARM#armed-d13-}"
  printf '# PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is 999-s, unchanged); the\n'
  printf '# TREE does NOT move - 1010 has NO tree edit and NO stage driver (the change is TWO src/ files\n'
  printf '# plus ONE conditional --wrap: entry_stubs.c the walk stage90_read_l2_pte + the recorders\n'
  printf '# entry_note_abt_pte/entry_note_abt_vmf, entry_trace.c the declarations + the ONE call +\n'
  printf '# __wrap_vm_fault, build_entry.sh --wrap=vm_fault only when ABT_PTE=1).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1009-press-read.txt): the storm is a USER-mode WRITE permission\n'
  printf '# fault - pc=0x2be8a1a0 (in the dyld shared cache), fsr=0x0000080f (WnR=1 (a write) | FS=0xf\n'
  printf '# "permission fault, level 2"), cpsr=0x60000030 (USER mode, IRQ on), far=0x9a000 (a LOW user VA,\n'
  printf '# below the TTBR0 split 0x40000000, in one of launchd-s own COW/data segments) - CONSTANT from\n'
  printf '# ordinal 0x400 through 0x200000 (~2.09 M aborts).  What 1009 could NOT say is WHY the fault is\n'
  printf '# never dismissed: the handler PRINTS on the failure branch and is SILENT on the retry branch,\n'
  printf '# and the capture shows the silent branch, so vm_fault returned KERN_SUCCESS || KERN_ABORTED.\n#\n'
  printf '# THE 1010 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1010.sh: THE HEADLINE (A) is the L2 descriptor at the\n'
  printf '# TAIL ordinals - xnu_live_abt_ptel2 / xnu_live_abt_pten paired with xnu_live_abt_pcfar.  Decide\n'
  printf '# CONSTANT-vs-CYCLING on the TAIL ROWS- OWN _ptel2 (ordinals >= 0x400), NEVER on the whole-run\n'
  printf '# xnu_live_abt_ptenx (the 1009 measurement defect, not repeated).  Decode _ptel2: bits[1:0] type\n'
  printf '# (0b10 = small page), bit 9 = APX (1 = user READ-ONLY), bit 5 = USER, bit 0 = NX,\n'
  printf '# bits[31:12] = PA.  The expected row is APX=1 + vm_fault KERN_SUCCESS => "the PTE is read-only\n'
  printf '# though vm_fault said success" - the cs-taint/prot-downgrade path.  (B) is xnu_live_abt_vmfret\n'
  printf '# (KERN_SUCCESS=0 / KERN_ABORTED=14) - it splits KERN_SUCCESS from KERN_ABORTED and is the only\n'
  printf '# source of that distinction.\n#\n'
  printf '# VERIFIED BY VALUE (build_1010.sh): the linked image carries the 36 xnu_live_abt_ key literals\n'
  printf '# (6 span + 5 far + 7 pc + 12 pte/vmf + 6 1010c walk-window: pteoow/ptealias/ptemapok/pteslot/\n'
  printf '# ptewbase/ptewlen), entry_note_abt_pte/entry_note_abt_vmf/__wrap_vm_fault\n'
  printf '# DEFINED T/t, exactly ONE b/bl to entry_note_abt_pte inside __wrap_sleh_abort image-wide, and\n'
  printf '# the __wrap_vm_fault body calling __real_vm_fault AND entry_note_abt_vmf, with the FROZEN 1009\n'
  printf '# elf (armed-d13-24997fc5) as the NEGATIVE CONTROL (xnu_live_abt_ x18, entry_note_abt_pc x>=1,\n'
  printf '# but the 1010 symbols x0 and the 1010 literals x0 - the 1010 change is NOT in the 1009 arm);\n'
  printf '# the 1009/1008/1007 witnesses, the 1006 freeze witness, the 1005 cadence witness, the 1004\n'
  printf '# abort trace (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set\n'
  printf '# asserted present (regressions).  The 996/997/998/999/1000/1001 checkers rerun ON the 1010\n'
  printf '# image - the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1010-abtpte;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"