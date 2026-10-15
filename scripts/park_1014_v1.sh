#!/usr/bin/env bash
# ====================================================================================================
# park_1014_v1.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1013.sh.
#
# Park 1014 (V1 = STAGE90_XNU_FLUSH_WITNESS) and append its block to records/revert-set.txt.
#
# 1014 (V1) = 1013-s switch set VERBATIM (PTE_WITNESS, PMP_ENTER, VME_PROT, ABT_PTE, ABT_PC, ABT_FAR,
# ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE,
# SHREG_GEOM, RESIDENT, USVC, SUBMAP_PMAP_SKIP and all checker/USB-ladder switches stay 1) + exactly
# ONE new switch STAGE90_XNU_FLUSH_WITNESS - a pure MEASUREMENT rung (no kernel behaviour change) that
# reads the ONE link 1013 left unread: the INVALIDATION.  1013 measured a leaf the pmap writes that IS
# user-WRITABLE (APX=0) in a table whose L1 base equals the live TTBR0 base, yet the store still
# permission-faults ~2.1 M times.  A writable leaf that still faults means the MMU is not reading that
# leaf, so 1014 asks: did the TLB flush that should invalidate the stale translation actually reach
# THIS cpu, for THIS ASID?
#
# THE PROBE IS A SOURCE EDIT INTO osfmk/arm/pmap.c, NOT A --wrap.  Measured: `pmap_flush_tlbs` has 13
# R_ARM_CALL relocations and ALL 13 are INTRA-OBJECT (11 caller functions inside arm/pmap.c), and
# `grep -rln pmap_flush_tlbs` matches arm/pmap.c ONLY - so a `--wrap=pmap_flush_tlbs` would redirect
# ZERO call sites and report a green build while measuring NOTHING.  The edit is gated by
# `#if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS` and is staged by
# tools/stage_d13_flushwitness.sh (+ tools/patch_d13_flushwitness.py, idempotent, with --check and
# --revert) because external/ is GITIGNORED - a hand edit there is LOST on re-provision.
#
# THE 1014 (V1) VERDICT CELLS (docs/1014-design.md §2.5) are read from the TAIL ROWS-OWN values
# (ordinals >= 0x400 on the powers-of-two ladder), NEVER on a whole-run change count (the 1008/1009
# measurement defect, not repeated):
#   * tail wfkind == 0 => F1 STALE_TLB_NO_FLUSH: the gate at :1298 was FALSE so NO ARM flush
#     instruction executed and the stale translation survived the pmap-s own write.  The direct
#     explanation of 1013-s result.
#   * tail wfkind == 2 AND wfasid == wctx => F2 FLUSH_RAN_ASID_MATCH: the ASID-scoped flush ran for
#     the ASID the CPU was under, so the invalidation is not the defect.
#   * tail wfkind == 2 AND wfasid != wctx => F3 ASID_MISMATCH: the flush was for another ASID.
#   * tail wfkind == 1 => F4 FLUSH_RAN_FULL: a whole-TLB invalidate ran; staleness cannot survive it.
#   * every tail row wfseen == 0 => F5 NULL CONTROL (:1295 never ran): REPORT, do not conclude.
#   * no tail row (wfn == 0) => F6 NO_FLUSH_WITNESS: no flush for the storm-s VA at all.
#
# VERIFIED BY VALUE (build_1014_v1.sh, run against the linked image AND the pool object): 100
# `xnu_live_abt_` key literals (49 through 1012 + 1013-s 37 [the pressed 1013b elf carries 86] +
# 1014-s 14) and all 14 1014 keys; the two
# recorders DEFINED T/t with exactly ONE `(b|bl)` call site each (the (b|bl) rule - the compiler TAILS
# a void call that ends its function; this was 1013-s own build bug); the pool-s osfmk_arm_pmap.o
# carrying `U entry_note_abt_wf` with exactly ONE R_ARM_CALL relocation and ZERO wf literals (the
# pmap.c edit DID reach the kernel pool - the 1012 pool-define hazard); the 1013 witness, the 1012
# apex, the 1011 (E-prime)/(A) witness, the 1010/1009/1008/1007 witnesses, the 1006 freeze witness,
# the 1005 cadence witness, the 1004 abort trace (`xnu_live_sleh_upc` x1), the 1003 stall instrument
# and the 1002 six-word save set as REGRESSIONS; and the FROZEN 1013 elf (armed-d13-9cf2f15e, the
# PRESSED predecessor) as a REQUIRED NEGATIVE CONTROL (xnu_live_abt_ x82, the 1013 symbols present,
# but the 1014 symbols x0 and the 1014 literals x0 - the reader CAN fail).  The 996-1013 checkers
# rerun ON the 1014 image - the probe checker-s ONE by-design frozen 20 B clause is the only allowed
# FAIL.
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
[[ $ARM == armed-d13-9cf2f15e ]] && { echo "REFUSING: $LIVE holds 1013's arm (9cf2f15e), the PRESSED predecessor (VERDICT: JUDGED [WIT] MEASUREMENT DEFECT - the leaf-VALUE witness RAN but the recorder published the WRONG TABLE: the W-SYNC control reads wcva=0x80080000 > N=2's 0x40000000 split, i.e. a TTBR1 address, so wck=0 and V1-V3 are WITHHELD by the rung's own control; W-APEX reads a real writable leaf (wwleaf=0x841e61bb, wwapx=0) but its own provenance cell says wwnoproof=1, and W-WALK read the SAME table yet published wkleaf=0 and NO VA, so no pair could be formed; the leaf the pmap writes IS user-writable yet the store still faults - that is 1014's question; adjudicated in records/1013-press-read.txt; design docs/1014-design.md); build 1014 first"; exit 1; }
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1), the PRESSED predecessor (VERDICT: the PTE-build apex is MEASURED - \`_pmpprot=0x3\` READ|WRITE ARRIVES at pmap_enter_options AND \`_pmppmap & 0xFFFFFF00 == _pmpttbr & 0xFFFFFF00\`, so the pmap writes the SAME L1 table the MMU walks and strip-above-the-apex is REFUTED BY MEASUREMENT; the leaf's VALUE was still never read - that is 1013's question; adjudicated in records/1012-press-read.txt; design docs/1013-design.md); build 1013 first"; exit 1; }
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
[[ "$_ab_lit" == 100 ]] || {
  echo "REFUSING: the parked image does not carry the 100 xnu_live_abt_ key literals (49 through 1012 + 1013's 35 [the pressed 1013b elf carries 86] + 1014's 14 flush-witness keys) (x$_ab_lit, want 100) - the 1011 vme store, the 1012 pmp store, the 1013 witness store or the 1014 flush/CONTEXTIDR store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")

# ====================================================================================================
# 1014's (V1) NEW instrument (THE TLB-FLUSH WITNESS + THE CONTEXTIDR WITNESS), BY VALUE, FIRST -
# placed BEFORE the 1013 block so a 1014 park that lost its OWN change fails before it re-asserts the
# older ones.  The rung answers ONE question: when `leo_enter` rebuilt the leaf it also called
# `pmap_flush_tlbs` - did that flush actually RUN on this CPU, for the ASID the CPU was running
# under?  (1013's press measured a leaf that IS user-writable yet still faults.)
# ====================================================================================================
for _wf_sym in entry_note_abt_wf entry_note_abt_wctx; do
  grep -qE "^[0-9a-f]+ [Tt] $_wf_sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_wf_sym is not a DEFINED text symbol in the parked image - the 1014 flush/CONTEXTIDR witness is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
done
# The fixed keyspace (docs/1014-design.md §2.2): 11 `wf` cells + 3 `wctx` cells = 14 literals, read as
# WHOLE-LINE `strings` matches so a truncated or re-spelled key is a refusal.
_wf_keys=0
for _wf_key in \
    xnu_live_abt_wfn xnu_live_abt_wfva xnu_live_abt_wfttb xnu_live_abt_wflive xnu_live_abt_wfself xnu_live_abt_wfshared xnu_live_abt_wfncpus xnu_live_abt_wfasid xnu_live_abt_wfkind xnu_live_abt_wfseen xnu_live_abt_wfnx \
    xnu_live_abt_wctxn xnu_live_abt_wctx xnu_live_abt_wctxva ; do
  grep -qx "$_wf_key" <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || {
    echo "REFUSING: the parked image does not carry the \`$_wf_key\` literal - the 1014 store is gone"; exit 1; }
  _wf_keys=$((_wf_keys + 1))
done
[[ "$_wf_keys" == 14 ]] || { echo "REFUSING: the 1014 key ladder counted $_wf_keys (want 14)"; exit 1; }
echo "park_1014: by value - entry_note_abt_wf/wctx DEFINED T/t and all 14 1014 key literals present (11 flush cells wfn/wfva/wfttb/wflive/wfself/wfshared/wfncpus/wfasid/wfkind/wfseen/wfnx + 3 CONTEXTIDR cells wctxn/wctx/wctxva)"

# The FLUSH probe's call site is a SOURCE EDIT INTO pmap.c, NOT a --wrap (docs/1014-design.md §2.3:
# `pmap_flush_tlbs` has 13 R_ARM_CALL relocations and ALL of them are INTRA-OBJECT, so a --wrap would
# redirect ZERO call sites).  Proven by value IN THE POOL OBJECT - the entry link cannot show it,
# because `pmap.o` is linked in as a whole object and the call is a local `bl`.
_wf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_wf_obj" ]] || { echo "REFUSING: $_wf_obj is absent - the pool build did not produce pmap.o; the 1014 flush probe cannot be verified"; exit 1; }
grep -qE "^[[:space:]]*U entry_note_abt_wf\$" <(arm-none-eabi-nm "$_wf_obj") || {
  echo "REFUSING: the pool's osfmk_arm_pmap.o does not carry an UNDEFINED reference to entry_note_abt_wf - the SOURCE EDIT into pmap_flush_tlbs did not compile in (the switch never reached the pool's DEFINES - the 1012 pool-define hazard, [[mi4-one-value-two-definitions]])"; exit 1; }
_wf_rel=$(arm-none-eabi-objdump -r "$_wf_obj" | grep -c 'entry_note_abt_wf' || true)
[[ "$_wf_rel" == 1 ]] || {
  echo "REFUSING: osfmk_arm_pmap.o carries $_wf_rel relocations to entry_note_abt_wf (want exactly 1, inside pmap_flush_tlbs) - the flush probe is not wired exactly once"; exit 1; }
_wf_pl=$(strings "$_wf_obj" | grep -c '^xnu_live_abt_wf' || true)
[[ "$_wf_pl" == 0 ]] || {
  echo "REFUSING: osfmk_arm_pmap.o carries $_wf_pl xnu_live_abt_wf* string literals - the flush probe was compiled with the RECORDER's body inlined into the pool (the entry-toolchain recorder is the only publisher)"; exit 1; }
grep -qx 'xnu_live_abt_wfn' <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || { echo "REFUSING: xnu_live_abt_wfn is not in the linked entry image"; exit 1; }
echo "park_1014: by value - the pool's osfmk_arm_pmap.o carries U entry_note_abt_wf with exactly ONE R_ARM_CALL relocation and ZERO wf literals (the pmap.c SOURCE EDIT compiled in; a --wrap would have redirected zero call sites)"

# Exactly ONE call site of each recorder IN THE LINKED IMAGE, spelled `(b|bl)` and NEVER `bl`
# ([[mi4-hooks-live-at-calls]]): the compiler TAILS a void call that is the last statement of its
# function, so the image may carry `b <sym>` rather than `bl <sym>` - this was 1013's own build bug.
# `entry_note_abt_wf`'s ONE call is the pool-side pmap.c edit; `entry_note_abt_wctx`'s ONE call is in
# `__wrap_sleh_abort` (the CONTEXTIDR that governs the faulting store is the one live when the abort
# was TAKEN).
for _wf_call_sym in entry_note_abt_wf entry_note_abt_wctx; do
  _wf_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <'"$_wf_call_sym"'>' || true)
  [[ "$_wf_call" == 1 ]] || {
    echo "REFUSING: the parked image carries $_wf_call call site(s) to $_wf_call_sym (want exactly 1) - the 1014 witness is not wired the way the rung measures"; exit 1; }
done
_wf_ab=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_sleh_abort not in the parked image"; exit 1; }
_wf_abx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_wf_ab#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_wf_abb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_wf_ab" --stop-address="${_wf_abx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <sleh_abort>' <<<"$_wf_abb" || { echo "REFUSING: __wrap_sleh_abort does not call the real sleh_abort body - the pass-through regressed"; exit 1; }
grep -qE '(b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wctx>' <<<"$_wf_abb" || { echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_wctx - the 1014 CONTEXTIDR witness never fires"; exit 1; }
grep -qE '(b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_cur>' <<<"$_wf_abb" || { echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_cur - the 1013 fault-VA latch 1014's recorder keys on regressed"; exit 1; }
echo "park_1014: by value - exactly ONE (b|bl) call site each of entry_note_abt_wf and entry_note_abt_wctx; the CONTEXTIDR call is inside __wrap_sleh_abort (which still calls <sleh_abort> and <entry_note_abt_cur>)"

# The 1014 switch must be in the record, and the tree probe must be STAGED + an EDIT OF THE TREE (not a
# hand edit - external/ is gitignored and a hand edit is LOST on re-provision).
grep -q '^STAGE90_XNU_FLUSH_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FLUSH_WITNESS=1 - 1014's flush witness was not recorded this run"; exit 1; }
python3 tools/patch_d13_flushwitness.py "${XNU_TREE:-external/xnu-hd2-darwin13/xnu}" --check >/dev/null || {
  echo "REFUSING: tools/patch_d13_flushwitness.py --check FAILS against the tree - the pmap.c flush probe is not in its staged shape (it must be staged by tools/stage_d13_flushwitness.sh; external/ is GITIGNORED, so a hand edit is LOST on re-provision)"; exit 1; }

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
echo "park_1014: by value - xnu_live_abt_* literals x$_ab_lit (49 through 1012 + 1013's 37 + 1014's 14), entry_note_abt_pmp/__wrap_pmap_enter_options DEFINED T/t, exactly ONE b/bl to entry_note_abt_pmp, and the wrapper calling pmap_enter_options AND entry_note_abt_pmp (the prot-at-the-apex witness is present)"

# THE 1011 CHANGE (the vm_map_entry effective protection + the faulting thread's own L1 base) is a
# REGRESSION: the wrapper/pass-through, the two 1011 symbols, the ONE b/bl to entry_note_abt_vme, and
# the (A) STAGE90_ACT_MAP immediate (956).  1014 keeps 1013's set verbatim.
for _ab_1011sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1011sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1011sym is not a DEFINED text symbol in the parked image - the 1011 instrument regressed (1014 keeps 1013's set verbatim)"; exit 1; }
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
echo "park_1014: by value - entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, ONE b/bl to entry_note_abt_vme, the lookup pass-through intact, and the (A) STAGE90_ACT_MAP immediate (956) present (1011's (E')/(A) witness kept)"

# THE 1010 CHANGE (the L2 descriptor + the vm_fault return) is a REGRESSION.
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the parked image - the 1010 instrument regressed (1014 keeps 1013's set verbatim)"; exit 1; }
done
_abt_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' || true)
[[ "$_abt_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abt_call call site(s) to entry_note_abt_pte (want 1, inside __wrap_sleh_abort) - the 1010 witness regressed"; exit 1; }
_abt_wfa=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_fault not in the parked image"; exit 1; }
_abt_wfx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_abt_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_abt_wfb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_abt_wfa" --stop-address="${_abt_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - the 1010 pass-through regressed"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the 1010 (B) record regressed"; exit 1; }
echo "park_1014: by value - entry_note_abt_pte/vmf + __wrap_vm_fault DEFINED T/t, ONE b/bl to entry_note_abt_pte, the __wrap_vm_fault pass-through intact (1010's L2-descriptor + vm_fault-return witness kept)"

# 1009's faulting-pc identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the parked image - the 1009 recorder was dropped (1014 keeps 1013's set verbatim)"; exit 1; }
_abp_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' || true)
[[ "$_abp_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abp_call call site(s) to entry_note_abt_pc (want 1) - the 1009 witness regressed"; exit 1; }
echo "park_1014: by value - entry_note_abt_pc DEFINED T/t with exactly ONE b/bl (1009's faulting-pc identity kept)"
# 1008's abort-fault identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the parked image - the 1008 recorder was dropped"; exit 1; }
echo "park_1014: by value - entry_note_abt_far DEFINED T/t (1008's abort-fault identity kept)"
# 1007's abort-span witness is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder was dropped"; exit 1; }
echo "park_1014: by value - entry_note_abt_span DEFINED T/t (1007's abort-span witness kept)"
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
echo "park_1014: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The 1005 cadence witness is a REGRESSION.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed"; exit 1; }
done
echo "park_1014: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1014: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1014: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1014: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1013 elf as the NEGATIVE CONTROL for the 1014 reader.  REQUIRED (the 1013 elf is the
# PRESSED predecessor and is on disk); an absent control is a REFUSAL, not a warning - a park that
# cannot show the reader CAN fail proves nothing about the reader.
_ctrl=out/stage90/frozen/armed-d13-9cf2f15e/xnu_arm_entry.elf
[[ -f "$_ctrl" ]] || {
  echo "REFUSING: the frozen 1013 elf ($_ctrl) is absent - the 1014 NEGATIVE CONTROL cannot be taken; the 1013 reader/witness must be provably loadable AND provably incomplete before 1014 is parked"; exit 1; }
_c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
_c_13sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_wleaf|entry_note_abt_walk|entry_note_abt_wsync|entry_note_abt_cur)$' || true)
_c_14sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_wf|entry_note_abt_wctx)$' || true)
_c_14lit=$(strings "$_ctrl" | grep -cE 'xnu_live_abt_(wf|wctx)' || true)
[[ "$_c_ab" == 82 && "$_c_13sym" == 4 && "$_c_14sym" == 0 && "$_c_14lit" == 0 ]] || {
  echo "REFUSING: the frozen 1013 elf does not have the pre-1014 shape (xnu_live_abt_=$_c_ab want 82, the 1013 symbols=$_c_13sym want 4, 1014 symbols=$_c_14sym want 0, 1014 flush/CONTEXTIDR literals=$_c_14lit want 0) - the control image is not the pre-1014 one"; exit 1; }
echo "park_1014: by value - NEGATIVE CONTROL (frozen 1013 elf, REQUIRED): xnu_live_abt_ x$_c_ab (1013's 82 keys), the 1013 symbols x$_c_13sym, 1014 symbols x$_c_14sym (the 1014 reader can fail), 1014 literals x$_c_14lit"

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
# re-spelled key is a refusal. 37 literals: 11 W-APEX (incl. the provenance cells wwva/wwft/wwpatbl),
# 13 W-WALK (incl. the 1013b VA cell wkva and the double-read wkleaf2/wkstable) + the 2 change-counts
# wwnx/wknx = 14 in the `wk`
# family count below, 9 W-SYNC (incl. the TTBR1 label wctb), PLUS the 2 raw TTBR words wctb0/wctb1.
# The ladder counts the 37 the build asserts.
_wk_keys=0
for _wk_key in \
    xnu_live_abt_wwn xnu_live_abt_wwl1 xnu_live_abt_wwl2pa xnu_live_abt_wwleaf xnu_live_abt_wwapx xnu_live_abt_wwnoproof xnu_live_abt_wwpa xnu_live_abt_wwpaok xnu_live_abt_wwva xnu_live_abt_wwft xnu_live_abt_wwpatbl xnu_live_abt_wwnx \
    xnu_live_abt_wkn xnu_live_abt_wkva xnu_live_abt_wk xnu_live_abt_wktb xnu_live_abt_wkl1 xnu_live_abt_wkl2pa xnu_live_abt_wkleaf xnu_live_abt_wkleaf2 xnu_live_abt_wkstable xnu_live_abt_wkapx xnu_live_abt_wkoow xnu_live_abt_wkfsr xnu_live_abt_wkapex xnu_live_abt_wknx \
    xnu_live_abt_wcnt xnu_live_abt_wcva xnu_live_abt_wck xnu_live_abt_wctb xnu_live_abt_wcl1 xnu_live_abt_wcl2pa xnu_live_abt_wcleaf xnu_live_abt_wcapx xnu_live_abt_wcoow \
    xnu_live_abt_wctb0 xnu_live_abt_wctb1 ; do
  grep -qx "$_wk_key" <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || {
    echo "REFUSING: the parked image does not carry the \`$_wk_key\` literal - the 1013 store is gone"; exit 1; }
  _wk_keys=$((_wk_keys + 1))
done
[[ "$_wk_keys" == 37 ]] || { echo "REFUSING: the key ladder counted $_wk_keys (want 37)"; exit 1; }
echo "park_1014: by value - the four recorders DEFINED T/t and all 37 1013 key literals present (11 W-APEX incl. wwva/wwft/wwpatbl + 14 W-WALK incl. the 1013b VA cell wkva, wkleaf2/wkstable, wknx + 9 W-SYNC incl. the TTBR1 label wctb + the 2 raw TTBR words wctb0/wctb1)"
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
echo "park_1014: by value - exactly ONE call site each of entry_note_abt_wleaf/walk/wsync/cur, the W-APEX call inside the pmap wrapper and W-WALK NOT in it"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1014 (V1): THE TLB-FLUSH WITNESS - did the flush that should invalidate the stale\n# translation actually REACH this CPU, for the ASID the store ran under? (design docs/1014-design.md)\n' "$ARM"
  printf '# Switch set is 1013-s VERBATIM plus STAGE90_XNU_FLUSH_WITNESS=1 (PTE_WITNESS, PMP_ENTER, VME_PROT,\n'
  printf '# ABT_PTE, ABT_PC, ABT_FAR, ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS,\n'
  printf '# SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY\n'
  printf '# BIN moves (9cf2f15e -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog\n' "${ARM#armed-d13-}"
  printf '# constant is 999-s, unchanged); the TREE DOES move - 1014 has ONE tree edit (osfmk/arm/pmap.c, the\n'
  printf '# pmap_flush_tlbs probe, staged by tools/stage_d13_flushwitness.sh + tools/patch_d13_flushwitness.py,\n'
  printf '# both idempotent and re-runnable because external/ is GITIGNORED) and NO new --wrap.\n#\n'
  printf '# WHY 1014 (V1) FOLLOWS 1013.  The 1013 press (arm armed-d13-9cf2f15e, records/1013-press-read.txt)\n'
  printf '# was JUDGED a MEASUREMENT DEFECT: its W-SYNC control reads wcva=0x80080000, ABOVE the TTBR0/TTBR1\n'
  printf '# split 0x40000000 for TTBCR.N=2, so wck=0 (a TTBR1 walk) and the rung-s OWN control WITHHELD V1-V3;\n'
  printf '# its W-APEX rows read a REAL writable leaf (wwleaf=0x841e61bb, wwapx=0) but their own provenance\n'
  printf '# cell says wwnoproof=1 (pm_l1_phys != the live TTBR0 base), so the cell is not provably the\n'
  printf '# fault-s; and W-WALK read the SAME L2 table yet published wkleaf=0 AND NO VA, so no (VA, L1, L2)\n'
  printf '# pair could be formed.  What 1013 DID establish BY VALUE: the leaf the pmap writes carries APX=0\n'
  printf '# (user-WRITABLE) in a table whose L1 base equals the live TTBR0 base, AND the store still faults\n'
  printf '# (fsr=0x80f, FS=15 level-2, WnR=1) ~2.1 M times.  A WRITABLE LEAF THAT STILL FAULTS means the\n'
  printf '# MMU is not reading that leaf - so the ONE unread link in the chain is the INVALIDATION: whether\n'
  printf '# pmap_flush_tlbs RAN at all, on WHICH cpu, and for WHICH ASID.  That is this rung.\n#\n'
  printf '# WHAT 1014 (V1) MEASURES.  `pmap_flush_tlbs` (osfmk/arm/pmap.c:1274-1328) takes the GATE at\n'
  printf '# :1298 `flush_self = (ttb == ttbr_pmap || pmap_is_shared)`.  If the gate is FALSE, NO ARM flush\n'
  printf '# instruction executes and the stale translation SURVIVES the pmap-s own write - the leaf in\n'
  printf '# MEMORY is writable while the TLB still holds the old read-only entry, which is EXACTLY the\n'
  printf '# shape 1013 measured.  The probe records, per call: the VA (startv), pmap->pm_l1_phys, the\n'
  printf '# IN-LOOP TTBR0 read & 0xFFFFFF00 (the value :1298 actually compares), flush_self,\n'
  printf '# pmap_is_shared, pmap_asid_ncpus (a RUNTIME value - project records that say 0 are WRONG; it is\n'
  printf '# 1 here), pmap->pm_asid & 0xFF, and the FLUSH KIND: 0 = no flush, 1 = full flushID,\n'
  printf '# 2 = ASID-scoped flushID.  A `seen` null-control cell covers the shape where :1295 never ran, so\n'
  printf '# a `self=0` row cannot be misread as a DECISION.  In parallel __wrap_sleh_abort records\n'
  printf '# CONTEXTIDR & 0xFF - the ASID the HARDWARE tags this fault-s translation with - so "the flush\n'
  printf '# was for ASID A while the CPU ran under ASID B" is visible rather than assumed; _wfasid alone\n'
  printf '# cannot answer that.\n#\n'
  printf '# THE 1014 (V1) VERDICT CELLS (docs/1014-design.md §2.5, read from the TAIL ROWS-OWN values -\n'
  printf '# any _wfn >= 0x400 - NEVER a whole-run max(); the 1008/1009 measurement defect is not repeated).\n'
  printf '# The W-SYNC control (1013) must still be sound, else WITNESS_METHOD_BROKEN and the cells are\n'
  printf '# WITHHELD:\n'
  printf '#   F1  _wfself = 0 at EVERY tail row for this VA => FLUSH_REFUSED_TTB_MISMATCH - H-FF CONFIRMED.\n'
  printf '#       The test at pmap.c:1298-1299 was FALSE: pmap->pm_l1_phys != (armreg_ttbr_read() &\n'
  printf '#       0xFFFFFF00) AT FLUSH TIME and the pmap is not shared, so NO FLUSH RAN AT ALL.  This is\n'
  printf '#       the surviving V1 mechanism, and it agrees with 1013-s wwl1=0x81bb1000 != the storm-s\n'
  printf '#       TTBR0 base 0x81a86000.  _wfkind = 0 is the same row read another way.\n'
  printf '#   F2  _wfself = 1 AND _wfttb == _wflive (the bases agree at flush time) and the fault persists\n'
  printf '#       => FLUSH_RAN_STILL_FAULTS: the enter-s flush was NOT refused; combined with 1013-s\n'
  printf '#       writable leaf this REFUTES H-FF and forces the "the table read is not the table walked"\n'
  printf '#       branch.  NOTE: with arm_context_switch-s full TLBIALL on every TTBR0 change and pm_asid\n'
  printf '#       single-written, an ASID-scoped mismatch cannot be the cause - this cell must NOT be read\n'
  printf '#       as one.\n'
  printf '#   F3  _wfself = 1 AND _wfkind = 2 and the fault persists => ASID_PATH_RAN_STILL_FAULTS: the\n'
  printf '#       ASID-scoped invalidate ran and the store still permission-faults => NOT ASID staleness.\n'
  printf '#       Remaining V1-adjacent suspects, NAMED NOT GUESSED: the TLB-s own view (a section mapping\n'
  printf '#       at this VA, a TTBR1 walk) or the DACR/DOMAIN - the L1 domain field built at pmap.c:3163\n'
  printf '#       is 0 and src/mmu.c:5526 sets DACR to 0x00000003 (domain 0 = manager, which bypasses AP\n'
  printf '#       checks), so a domain that is not manager on the faulting CPU produces FS=15 with no leaf\n'
  printf '#       defect at all.\n'
  printf '#   F4  _wfself = 1 AND _wfkind = 1 (the FULL arm_tlb_flushID) and the fault persists =>\n'
  printf '#       FULL_FLUSH_STILL_FAULTS: the whole TLB was invalidated for this CPU and the store still\n'
  printf '#       faults => staleness is DEAD; the entry is rebuilt from the TABLE, so the table the MMU\n'
  printf '#       walks is not the table 1013 read.  CROSS-CHECK against 1013-s V3 cell (wwl2pa vs wkl2pa):\n'
  printf '#       if they were EQUAL this is a HARD CONTRADICTION and the next rung must re-open the walk.\n'
  printf '#   F5  _wfn = 0 at the tail while the storm ran => NO_FLUSH_WITNESS: the flush recorder never\n'
  printf '#       fired on the storm VA (a provenance/keying failure - the flush runs outside the handler-s\n'
  printf '#       g_abtpte_curva window) => REBUILD, DO NOT CONCLUDE.\n'
  printf '#   F6  _wfncpus differs from the boot-s own "All ASID/asids enabled" line => HARD (stale\n'
  printf '#       decode): the published runtime value disagrees with the boot-s own print.\n'
  printf '#   (HARD) any _wfkind/_wfasid that does NOT match the disassembled branch taken - the same\n'
  printf '#       stale-decode class.\n'
  printf '#   (NULL CONTROL) _wfseen = 0 on a row means the cpu loop-s :1295 read never executed, so that\n'
  printf '#       row-s _wflive is not a reading and its _wfself = 0 is NOT a DECISION - such a row must\n'
  printf '#       NEVER be counted toward F1.\n'
  printf '# The filter is the rung-s provenance statement: pmap_flush_tlbs runs for other VAs constantly\n'
  printf '# (pmap_init alone calls it with endv = 0xFFFFFFFF), so without keying on 1013-s\n'
  printf '# g_abtpte_curva the 24576-entry channel would be spent before the storm (the 998 cap, the 1007\n'
  printf '# ladder lesson, [[mi4-measurement-defects]]).\n#\n'
  printf '# KEYS (the fixed keyspace, docs/1014-design.md §2.2): the flush witness wfn/wfva/wfttb/wflive/\n'
  printf '# wfself/wfshared/wfncpus/wfasid/wfkind/wfseen/wfnx; the CONTEXTIDR witness wctxn/wctx/wctxva.\n'
  printf '# wflive is published RAW (pmap.c:1298 masks by 0xFFFFFF00 only) - masking it to the 4 KB\n'
  printf '# entry_ttbr_base would manufacture agreement at the F1/F2 cell ([[mi4-one-value-two-definitions]]).\n'
  printf '# No two probes share a key (the §2.2 collision proof: every one of the 14 greps to 0 tree-wide\n'
  printf '# before it was added; xnu_live_abt_wfmap was DROPPED from the design table this run - it has no\n'
  printf '# source - and replaced by xnu_live_abt_wfseen, the null-control cell).\n#\n'
  printf '# THE PROBE IS A SOURCE EDIT, NOT A --wrap (docs/1014-design.md §2.3, measured):\n'
  printf '# `pmap_flush_tlbs` has 13 R_ARM_CALL relocations and ALL 13 are INTRA-OBJECT (11 caller\n'
  printf '# functions inside arm/pmap.c); `grep -rln pmap_flush_tlbs` matches arm/pmap.c ONLY.  A\n'
  printf '# --wrap=pmap_flush_tlbs would therefore redirect ZERO call sites and report a green build while\n'
  printf '# measuring NOTHING ([[mi4-a-claim-in-a-comment-is-not-a-check]]).  The probe is gated by\n'
  printf '# `#if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS`, so with the switch OFF\n'
  printf '# the tree-s pmap.c compiles to byte-identical .text (proven in build_1014_v1.sh by rebuilding\n'
  printf '# patched-OFF against reverted-OFF with the SAME command line - the whole object is identical\n'
  printf '# after normalising the `pmap.c:NNNN` __LINE__ strings in asserts; the 42-line gate shifts every\n'
  printf '# assert message below it EVEN WITH THE GATE CLOSED, so a raw cmp can never succeed and the\n'
  printf '# normalisation is stated rather than hidden).\n#\n'
  printf '# THE ONE PLACE THE DESIGN COULD NOT BE FOLLOWED EXACTLY (stated, not papered over): §2.1(a1)\n'
  printf '# places the recorder call at the function-s single exit and passes Apple-s `ttbr_pmap`.  That\n'
  printf '# identifier is declared INSIDE the cpu loop-s body (:1295), so the single-exit call did not\n'
  printf '# compile (`use of undeclared identifier ttbr_pmap`).  The fix is a gated function-scope\n'
  printf '# `static uint32_t wf_live;` carrying the in-loop `armreg_ttbr_read() & 0xFFFFFF00` (reset to 0\n'
  printf '# per call, and a `seen` flag set at the same statement).  Apple-s own declarator is left\n'
  printf '# BYTE-IDENTICAL in both the #if and the #else arm, so the OFF path is unchanged.\n#\n'
  printf '# VERIFIED BY VALUE (build_1014_v1.sh + this park): the image carries 100 xnu_live_abt_ key\n'
  printf '# literals (49 through 1012 + 1013-s 37 + 1014-s 14) and all 14 1014 keys; entry_note_abt_wf and\n'
  printf '# entry_note_abt_wctx are DEFINED T/t with exactly ONE `(b|bl)` call site each (the (b|bl) rule -\n'
  printf '# the compiler TAILS a void call that ends its function); the CONTEXTIDR call lives inside\n'
  printf '# __wrap_sleh_abort, which still calls <sleh_abort> (pass-through) AND <entry_note_abt_cur> (the\n'
  printf '# fault-VA latch the recorder keys on); the pool-s osfmk_arm_pmap.o carries `U entry_note_abt_wf`\n'
  printf '# with exactly ONE R_ARM_CALL relocation and ZERO wf literals (the pmap.c source edit DID compile\n'
  printf '# into the kernel pool - the 1012 pool-define hazard); with the FROZEN 1013 elf\n'
  printf '# (armed-d13-9cf2f15e) as a REQUIRED NEGATIVE CONTROL (xnu_live_abt_ x82, the 1013 symbols x4,\n'
  printf '# but the 1014 symbols x0 and the 1014 literals x0 - the 1014 change is NOT in the 1013 arm, so\n'
  printf '# the reader CAN fail); the 1013 witness, the 1012 apex, the 1011 (E-prime)/(A) witness, the\n'
  printf '# 1010/1009/1008/1007 witnesses, the 1006 freeze witness, the 1005 cadence witness, the 1004\n'
  printf '# abort trace (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set\n'
  printf '# asserted present (regressions).  The 996-1013 checkers rerun ON the 1014 image - the probe\n'
  printf '# checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1014a-flushwitness;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"
