#!/usr/bin/env bash
# ====================================================================================================
# park_1015b.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1015.sh.
#
# Park 1015b (STAGE90_XNU_GLOBAL_FIX) and append its block to records/revert-set.txt.
#
# **1015b MOVES THE KERNEL POOL OBJECT, AND THAT IS THE RUNG.** Where 1015 was a pure measurement rung
# and its park asserted the pool was UNMOVED ("still `U entry_note_abt_wf`, exactly ONE R_ARM_CALL"),
# 1015b's park asserts the EXACT OPPOSITE and asserts it FIRST: `osfmk_arm_pmap.o`'s `pmap_flush_tlbs`
# now routes a NON-KERNEL pmap through the full `arm_tlb_flushID` (TLBIALL) instead of the ASID-scoped
# `arm_tlb_flushID_ASID`. The two scripts' pool clauses are exact inverses, and carrying 1015's wording
# into this file would produce a park that refuses its own correct arm (the copy-paste defect class the
# 1015b build's inverse assertion, `build_1015b.sh`'s PYINV block, is there to catch).
# **THE ASSERTED FINGERPRINT IS THE `kernel_pmap` MATERIALIZATION COUNT (3), NOT A CALL COUNT** - see
# block (2): the compiler tail-merges the two identical `arm_tlb_flushID()` sites into ONE `bl` and the
# ASID arm REMAINS in the text, so `arm_tlb_flushID`/`_ASID` both read x1 in the OFF and ON objects and
# cannot discriminate. Asserting those counts was this park's first-draft measurement defect.
#
# THE FIX. `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c:1346-1349` - Apple's flush decision:
#   :1346            if (pmap_is_shared)
#   :1347                arm_tlb_flushID();
#   :1348            else
#   :1349                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);
# `tools/stage_d13_globalfix.sh` (via tools/patch_d13_globalfix.py) inserts ONE arm between Apple's
# `arm_tlb_flushID();` and Apple's `else`:
#   #if defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX
#               else if (pmap != kernel_pmap)
#                   arm_tlb_flushID();
#   #endif
# The `else if (pmap != kernel_pmap)` shape is not cosmetic: `pmap_is_shared` is
# `pmap->pm_shared || (pmap == kernel_pmap)` (pmap.c:1291), so the KERNEL already takes the first arm
# and the added condition is FALSE for it - kernel semantics are unchanged, while every other pmap
# takes the full TLBIALL instead of the ASID-scoped one. OFF removes the three lines entirely (the
# `#if` wraps them), which is why the build's OFF proof still compares byte-identical `.text`.
#
# WHY H-G NEEDS THIS. H-G (docs/1015-design.md; records/1015-press-read.txt §7) is that the CPU is
# using a STALE GLOBAL TLB ENTRY: every user leaf this port emits is GLOBAL (`L2_NG_BIT 0x800`,
# pmap.h:231, is never ORed anywhere tree-wide - 1015 published it RAW as `xnu_live_abt_leafng`, read
# 0 on the storm), and ARM's TLBIASID invalidates ONLY ASID-tagged (nG=1) entries, so a GLOBAL entry
# survives every steady-state flush this port performs. 1015's press read V-AT1 at the hardware: the
# MMU refused a USER-WRITE translation of the storm VA while its own privileged T-CTL control
# succeeded - the CPU's translation is NOT the leaf the walk reads.
#
# 1015b = 1015's switch set VERBATIM (PTE_WITNESS, PMP_ENTER, VME_PROT, ABT_PTE, ABT_PC, ABT_FAR,
# ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE,
# SHREG_GEOM, RESIDENT, USVC, SUBMAP_PMAP_SKIP, FLUSH_WITNESS and all checker/USB-ladder switches
# stay 1, plus 1015's own PERM_WITNESS=1) + exactly ONE new switch STAGE90_XNU_GLOBAL_FIX.
#
# THE 1015b VERDICT CELLS (docs/1015b-design.md §5; read by tools/check_1015b_capture.py):
#   * G-CONFIRMED  the storm STOPS.  Read BY VALUE - the reader recovers the storm VA from the capture
#     (the 1014/1015 storm's `far` moves per run, so no literal identifies it) and requires the storm
#     rows absent and the capture structurally complete (a truncated log is what a stopped storm looks
#     like in bytes, so the truncation guard runs FIRST).  => H-G CONFIRMED IN BEHAVIOUR.
#   * G-REFUTED    the storm PERSISTS at a recurring VA (a POSITIVE measurement: it must point at a
#     storm ROW, never at an absence).  => H-G REFUTED IN BEHAVIOUR; go to 1015's §3b ladder (P-X1)
#     and then to H-X - the CPU's own state (SCTLR/PRRR), not another table read.
#   * G-WITHHOLD   an exact missing/contradictory key is named; NEVER a negative. A reader that cannot
#     withhold certifies nothing ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
# THE ATTRIBUTION GUARD: a stopped storm is only evidence about THIS arm if a `_wfkind=1` row (the
# WIDENED arm - Apple's own `_wfkind` values are 0 and 2) is keyed to the storm VA (`_wfva` or
# `_pcfar` == the recovered storm VA). Otherwise the stop could be a different run's shape, and the
# reader says so in the verdict text rather than implying attribution.
#
# VERIFIED BY VALUE (scripts/build_1015b.sh, run against the pool object, the linked image AND the
# record): the pool object out/xnu_kernel_obj_d13/osfmk_arm_pmap.o materializes `kernel_pmap` THREE
# times inside `pmap_flush_tlbs` (Apple's object has TWO: the `:1291` pmap_is_shared init and the
# `:1382` terminal panic; the inserted `else if (pmap != kernel_pmap)` test is the third). The
# `arm_tlb_flushID_ASID` / `arm_tlb_flushID` CALL counts are REPORTED, not asserted: the compiler
# tail-merges Apple's two identical `arm_tlb_flushID()` sites into ONE `bl`, and the ASID arm REMAINS
# in the text (unreachable for non-kernel pmaps), so both read x1 in the OFF and ON objects alike.
# The inherited 1014
# payload is still staged (tools/patch_d13_flushwitness.py --check) with its ONE `U entry_note_abt_wf`
# relocation intact; the 1015 entry half is undisturbed (all 31 `wd*`/`leafng`/`ww*` keys and
# `entry_note_abt_wd` x1 `(b|bl)`, the six inherited recorders still x1 each); and the arm MOVED off
# 1015's PRESSED arm `armed-d13-e51d14fa` (the pool's `.text` is inside the entry `.bin` whose sha8 IS
# the arm, so a move is expected from the pool segment alone - a build that lands BACK on e51d14fa did
# not link the fix).
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
#
# THE 1015b SWITCH SET, and the one thing that is different. 1015's set VERBATIM (see the 1015 block
# below in scripts/build_1015.sh - it is unchanged here, and every one of its keys is asserted by a
# REGRESSION clause further down this file) PLUS STAGE90_XNU_GLOBAL_FIX=1.
#
# 1015b MOVES THE KERNEL.  `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c` is EDITED by
# tools/stage_d13_globalfix.sh, and the pool object out/xnu_kernel_obj_d13/osfmk_arm_pmap.o is rebuilt
# with the widened flush. `build_entry.sh` only LINKS that object - so the park must verify the LINK,
# not the object alone, which is why the census below runs on the OBJECT (the relocation set and the
# code order) and the build runs it on the image's arm sha8.
#
# THE DECLARATION CARRIER IS A DROP-IN FILE, AND PARKING APPENDS TO IT.  The record
# (`out/stage90/xnu_arm_entry-config.txt`) is written by `src/entry/build_entry.sh`, which is another
# lane's file and does not name this switch (MEASURED x0); `scripts/preflight_boot_check.sh`'s
# ENTRY_CFG_KEYS is a SET DIFFERENCE against that record and names it x0 as well. So the switch that
# MOVED THE KERNEL is declared in `out/stage90/xnu_arm_entry-arm-extra.txt`, which
# `scripts/build_1015b.sh` writes EMPTY and this script appends ONE line to:
#     STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix
# Leaving the file empty after a park would freeze an arm whose own declared surface never names the
# one switch that makes it different from 1015 - the record would describe 1015's kernel.
#
# THE 1015b VERDICT CELLS live in the block appended below (G-CONFIRMED / G-REFUTED / G-WITHHOLD, the
# attribution guard, and the stop definition).  docs/1015b-design.md §5 pre-registers them and
# tools/check_1015b_capture.py adjudicates them.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
# xnu_arm_entry-arm-extra.txt is 1015b's OWN member: it is the ONE surface that declares
# STAGE90_XNU_GLOBAL_FIX (the record cannot - see the refusal block above), so it is frozen with the
# arm it describes. A frozen arm whose declaration file is left behind in out/stage90 is an arm whose
# kernel nobody can read back after the next build overwrites the directory.
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-arm-extra.txt xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img \
         stage90-qcdt.img stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to re-park the
# PRESSED/known arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
# **1015b's OWN REFUSAL, FIRST: `armed-d13-e51d14fa` IS 1015's PRESSED PREDECESSOR.** The pool's
# `.text` is INSIDE the entry `.bin` whose sha8 is the arm (MEASURED:
# `LOAD 0x010000 0x80000000 0x80000000 0x570000 R E`, `pmap_flush_tlbs` at 0x8002968c), so 1015b's
# pool edit MUST move this value. A park of e51d14fa under the name 1015b would freeze, as the arm
# that "moves the kernel", a set of bytes in which the kernel was never moved.
[[ $ARM == armed-d13-e51d14fa ]] && { echo "REFUSING: $LIVE holds 1015's PRESSED arm (e51d14fa) - 1015b's pool edit did NOT reach the linked image and there is nothing here to park. 1015's press (records/1015-press-read.txt) read V-AT1 AT THE HARDWARE: _wdatpar faults (par&1==1) while its null control _wdatparpr succeeds to the leaf's own PA, on a leaf read as AP=3/APX=0/nG=0 with domain 0 / DACR=1 / TTBCR.N=2 - the MMU's translation for a USER WRITE is NOT the leaf the walk reads. H-G is CONFIRMED at the hardware and 1015b is its BEHAVIOURAL half: the full TLBIALL for non-kernel pmaps. Re-run scripts/build_1015b.sh and check its pool-MOVED census before parking"; exit 1; }
[[ $ARM == armed-d13-29ec9e48 ]] && { echo "REFUSING: $LIVE holds 1014's arm (29ec9e48), a PRESSED predecessor (VERDICT: F2 + V1 MEASURED - the ASID-scoped flush RAN under the faulting ASID (wfkind=2, wfself=1, wfttb==wflive==0x81a86000, wfasid=2==wctx=2, wfncpus=1), which REFUTES 1013c's H-FF (pm_l1_phys != TTBR0); AND the leaf the pmap writes IS user-writable (wwl1=ptel1=wkl1=0x81bb2001, wwleaf=wkleaf=wkleaf2=0x841e91bb stable double-read, wwapx=0) yet the store still FS=15 permission-faults at constant far=0xe6000 - BUT the earlier reading 'F1 is dead' was CORRECTED: F2 proves a flush ran, NOT that a GLOBAL entry was invalidated, and this port installs every leaf global (bit11=0), so TLBIASID cannot evict it - H-G is NARROWED, NOT eliminated; that narrowed question was 1015's AT/PAR question, and 1015 ANSWERED it: V-AT1 at the hardware, on a leaf read as AP=3/APX=0/nG=0 with the privileged control succeeding; adjudicated in records/1014-press-read.txt and records/1015-press-read.txt; designs docs/1015-design.md and docs/1015b-design.md); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-9cf2f15e ]] && { echo "REFUSING: $LIVE holds 1013's arm (9cf2f15e), the PRESSED predecessor (VERDICT: JUDGED [WIT] MEASUREMENT DEFECT - the leaf-VALUE witness RAN but the recorder published the WRONG TABLE: the W-SYNC control reads wcva=0x80080000 > N=2's 0x40000000 split, i.e. a TTBR1 address, so wck=0 and V1-V3 are WITHHELD by the rung's own control; W-APEX reads a real writable leaf (wwleaf=0x841e61bb, wwapx=0) but its own provenance cell says wwnoproof=1, and W-WALK read the SAME table yet published wkleaf=0 and NO VA, so no pair could be formed; the leaf the pmap writes IS user-writable yet the store still faults - that is 1014's question; adjudicated in records/1013-press-read.txt; design docs/1014-design.md); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1), the PRESSED predecessor (VERDICT: the PTE-build apex is MEASURED - \`_pmpprot=0x3\` READ|WRITE ARRIVES at pmap_enter_options AND \`_pmppmap & 0xFFFFFF00 == _pmpttbr & 0xFFFFFF00\`, so the pmap writes the SAME L1 table the MMU walks and strip-above-the-apex is REFUTED BY MEASUREMENT; the leaf's VALUE was still never read - that is 1013's question; adjudicated in records/1012-press-read.txt; design docs/1013-design.md); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-fce83837 ]] && { echo "REFUSING: $LIVE holds 1011's arm (fce83837), the PRESSED predecessor (VERDICT: F1a is MEASURED - the (E') tail rows carry vmeprot=0x3 READ|WRITE with vmekr=0 KERN_SUCCESS, so the vm_map ENTRY GRANTS write and the write is LOST between the entry and the PTE; F1b is DENIED BY VALUE; adjudicated in records/1011-press-read.txt; design docs/1012-design.md); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-64391319 ]] && { echo "REFUSING: $LIVE holds 1010b's arm (64391319), the PRESSED predecessor (VERDICT: the mask fix is CORRECT but exposed a SECOND defect - the corrected TTBR0 base reads the TRUE L1, whose L2 table is at PA 0x46300000 OUTSIDE the entry window and the unbounded L2 deref faulted FS=5; adjudicated in records/1010b-press-read.txt); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-0f39207c ]] && { echo "REFUSING: $LIVE holds 1010's arm (0f39207c), the PRESSED predecessor (VERDICT: the storm reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail, because the TTBR0 base was masked with 0xfffffc00 instead of 0xffffc000; adjudicated in records/1010-press-read.txt); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-24997fc5 ]] && { echo "REFUSING: $LIVE holds 1009's arm (24997fc5), the PRESSED predecessor (VERDICT: the storm is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x80f, far=0x9a000; the handler chose the SILENT retry branch; adjudicated in records/1009-press-read.txt); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-0343ef45 ]] && { echo "REFUSING: $LIVE holds 1008's arm (0343ef45), the PRESSED predecessor (VERDICT: the storm is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort; adjudicated in records/1008-press-read.txt); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-88074ab3 ]] && { echo "REFUSING: $LIVE holds 1007's arm (88074ab3), the PRESSED predecessor (VERDICT (C): the ~24.4 s freeze is INSIDE ONE sleh_abort riding a STORM of ~2.1 M aborts); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-002b0683 ]] && { echo "REFUSING: $LIVE holds 1010c's arm (002b0683), the PRESSED predecessor (VERDICT: the walk is WINDOW-BOUNDED - the mask fix reads the TRUE L1 base 0x81a84000, but the storm's L1 slot holds a SECTION descriptor (ptel1=0xfeedface), so the L2 LEAF (APX) is NEVER READ; adjudicated in records/1010c-press-read.txt); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-31bdebf3 ]] && { echo "REFUSING: $LIVE holds 1006's arm (31bdebf3), the PRESSED predecessor (the freeze witness); build 1015b first"; exit 1; }
[[ $ARM == armed-d13-ff937225 ]] && { echo "REFUSING: $LIVE holds 1005's arm (ff937225), the PRESSED predecessor; build 1015b first"; exit 1; }
[[ $ARM == armed-d13-582a19c8 ]] && { echo "REFUSING: $LIVE holds 1004's arm (582a19c8), the PRESSED predecessor; build 1015b first"; exit 1; }
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), a known/stale arm; build 1015b first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one; build 1015b first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1015b first"; exit 1; }

# 1015's OWN key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_PERM_WITNESS=1.  FLUSH_WITNESS (1014), PMP_ENTER (1012), VME_PROT (1011), ABT_PTE
# (1010c), ABT_PC (1009), ABT_FAR (1008), ABT_SPAN (1007), the freeze witness (1006), the cadence
# witness (1005), abort trace (1004), stall (1003), the r3-save fix (1002, USVC hook ON) and the
# submap skip (1001) ALL stay on as regressions.
#
# **AND 1015b's OWN SWITCH IS ASSERTED ON A DIFFERENT SURFACE, BECAUSE IT IS ON A DIFFERENT SURFACE.**
# `STAGE90_XNU_GLOBAL_FIX` is a POOL switch and no writer in `src/entry/` echoes it (MEASURED x0 in
# both `src/entry/build_entry.sh` and `scripts/preflight_boot_check.sh`), so this park must NOT grep
# the record for it - that clause could never pass on a correctly built arm, and a check that fails
# for the wrong reason is not a check. The declarations this park DOES make are, in order:
#   (1) the drop-in file `$LIVE/xnu_arm_entry-arm-extra.txt` names the switch (below);
#   (2) the POOL OBJECT's relocation census - the widened arm is IN the bytes (below);
#   (3) the arm moved off 1015's PRESSED arm `e51d14fa` (the refusal list above).
# The record is still grep'd for 1015's own entry key, immediately below, because that one IS written
# there - the two switches ride two different carriers, and this file reads each where it lives.
grep -q '^STAGE90_XNU_PERM_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_PERM_WITNESS=1 - 1015's permission-INPUTS witness was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_FLUSH_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FLUSH_WITNESS=1 - 1014's flush witness is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_PTE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_PTE_WITNESS=1 - 1013's leaf-value witness was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_PMP_ENTER=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_PMP_ENTER=1 - 1012's apex cell (prot at pmap_enter_options) was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_VME_PROT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_VME_PROT=1 - 1011's (E')/(A) identity is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PTE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PTE=1 - 1010c's L2 walk is part of 1015's set verbatim (PMP_ENTER requires ABT_PTE=1) and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_PC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_PC=1 - 1009's faulting-pc identity is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_FAR=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_FAR=1 - 1008's abort-fault identity is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABT_SPAN=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABT_SPAN=1 - 1007's abort-span witness is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_FREEZE_WITNESS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - 1006's freeze witness is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_CADENCE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_CADENCE=1 - 1005's cadence witness is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1015's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"; exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"; exit 1; }

# ====================================================================================================
# 1015b's OWN CHANGE, BY VALUE, FIRST - BEFORE EVERY 1015 CLAUSE IN THIS FILE.
# **THE ORDER IS THE POINT.** 1015's blocks below assert the pool is UNMOVED; this block asserts it
# MOVED. A park that lost 1015b's change must fail HERE, on its own claim, rather than further down
# where a 1015 clause would stop it for a reason that names the wrong thing. The two are exact
# inverses, and reading them in this order is what keeps a copy-paste from surviving review.
# ====================================================================================================
# (1) THE DECLARATION CARRIER. `scripts/build_1015b.sh` writes `$LIVE/xnu_arm_entry-arm-extra.txt`
#     EMPTY at the TOP of the script (before anything reads it); the park appends the ON arm's ONE line. Append-only and idempotent-
#     hostile by design: if a line is already there, a previous park already claimed this directory,
#     and appending a second would leave two arms' declarations in one file
#     ([[mi4-out-stage90-is-shared-live-state]]).
_b_xfile="$LIVE/xnu_arm_entry-arm-extra.txt"
[[ -f "$_b_xfile" ]] || { echo "REFUSING: $_b_xfile is absent - the cross-lane declaration carrier for STAGE90_XNU_GLOBAL_FIX was not created by scripts/build_1015b.sh. The record cannot carry this switch (no writer in src/entry/ echoes it - MEASURED x0), so an absent carrier means a park would freeze an arm whose kernel-moving switch is declared NOWHERE"; exit 1; }
_b_xn=$(wc -l < "$_b_xfile" || echo 0)
[[ "$_b_xn" == "0" ]] || { echo "REFUSING: $_b_xfile already carries $_b_xn line(s) ('$(head -1 "$_b_xfile")') - the build writes it EMPTY and the park appends exactly one line, so a non-empty file means this out/stage90 has already been parked (or a stale declaration survived a build). Appending again would put two arms' declarations in one file"; exit 1; }
printf 'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix\n' >> "$_b_xfile"
[[ $(wc -l < "$_b_xfile") == 1 && $(cat "$_b_xfile") == 'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix' ]] || { echo "REFUSING: the declaration append to $_b_xfile did not produce exactly the one expected line ('$(cat "$_b_xfile")')"; exit 1; }
echo "park_1015b: the switch that MOVED THE KERNEL is declared in $_b_xfile: '$(cat "$_b_xfile")' (the record cannot carry it - src/entry/build_entry.sh names the switch x0)"

# (2) THE POOL OBJECT'S RELOCATION CENSUS - THE PRE-REGISTERED TEST OF "THE FIX IS IN THE BYTES".
#     `build_entry.sh` only LINKS this object, so a script that staged the tree and exported the switch
#     can still freeze an image built from a STALE object. The census below is taken on the OBJECT
#     (where the fix is made) and the build takes the arm-sha8 test on the IMAGE (where the fix must
#     ARRIVE) - two different surfaces, deliberately, because they fail independently.
#     `pmap_flush_tlbs`'s symbol range is computed from `nm -n`, requiring the NEXT symbol to be
#     `[Tt]`: an awk that takes any following line returns the RELOCATION's own symbol line and
#     reports an 8-byte function (MEASURED - that is how `docs/1015b-design.md` R6 was found).
_b_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_b_obj" ]] || { echo "REFUSING: $_b_obj is absent - the pool build did not produce the object 1015b edits, so the fix cannot be verified in the bytes"; exit 1; }
_b_lo=$(arm-none-eabi-nm "$_b_obj" 2>/dev/null | awk '$3=="pmap_flush_tlbs" && ($2=="T"||$2=="t"){print "0x" $1; exit}')
[[ -n "$_b_lo" ]] || { echo "REFUSING: arm-none-eabi-nm finds no TEXT symbol pmap_flush_tlbs in $_b_obj - the function 1015b edits is not in this object"; exit 1; }
_b_hi=$(arm-none-eabi-nm -n "$_b_obj" 2>/dev/null | awk -v s="${_b_lo#0x}" '
  BEGIN{sv=strtonum("0x" s)}
  {a=strtonum("0x" $1)}
  a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
[[ -n "$_b_hi" ]] || { echo "REFUSING: no TEXT symbol follows pmap_flush_tlbs in $_b_obj - the census range cannot be computed and a census over an uncomputable range measures nothing"; exit 1; }
echo "   POOL: pmap_flush_tlbs occupies $_b_lo .. $_b_hi in $_b_obj (the range every clause below reads)"
_b_asid=$(arm-none-eabi-objdump -r "$_b_obj" 2>/dev/null | awk -v lo="${_b_lo#0x}" -v hi="${_b_hi#0x}" '
  BEGIN{l=strtonum("0x" lo); h=strtonum("0x" hi)}
  /R_ARM_CALL/ && /arm_tlb_flushID_ASID/ {a=strtonum("0x" $1); if (a>=l && a<h) n++}
  END{print n+0}')
_b_full=$(arm-none-eabi-objdump -r "$_b_obj" 2>/dev/null | awk -v lo="${_b_lo#0x}" -v hi="${_b_hi#0x}" '
  BEGIN{l=strtonum("0x" lo); h=strtonum("0x" hi)}
  /R_ARM_CALL/ && /arm_tlb_flushID$/    {a=strtonum("0x" $1); if (a>=l && a<h) n++}
  END{print n+0}')
# **THE FINGERPRINT OF THE INSERTION IS THE `kernel_pmap` MATERIALIZATION COUNT, NOT A CALL COUNT.**
# This block's FIRST draft asserted `_b_asid == 0` and `_b_full >= 2` and an ORDERED pair - all three
# are UNSATISFIABLE and were a measurement defect, MEASURED this session on the built object:
#   * the compiler TAIL-MERGES the two identical `arm_tlb_flushID()` sites (Apple's `:1347` shared arm
#     and the outer `else`) into ONE `bl`, so the object reads x1 whether or not the switch is on -
#     `>= 2` never holds, and an ORDERED PAIR does not exist to order;
#   * the ASID arm REMAINS IN THE TEXT (this rung's own design: "UNREACHABLE ... while remaining in the
#     text"), reached only through the `pmap == kernel_pmap` branch the inserted test makes impossible -
#     so `_b_asid == 0` would be a FAILURE of the declared shape, not a pass.
# What the switch DOES add is a THIRD `kernel_pmap` materialization: Apple materializes it twice (the
# `pmap_is_shared` init at :1291 and the terminal panic at :1382); the inserted `else if (pmap !=
# kernel_pmap)` test is the third. MEASURED by value, OFF/Apple object = 2, 1015b object = 3.
_b_kp=$(arm-none-eabi-objdump -r "$_b_obj" 2>/dev/null | awk -v lo="${_b_lo#0x}" -v hi="${_b_hi#0x}" '
  BEGIN{l=strtonum("0x" lo); h=strtonum("0x" hi)}
  /R_ARM_MOVW_ABS_NC/ && /kernel_pmap/ {a=strtonum("0x" $1); if (a>=l && a<h) n++}
  END{print n+0}')
echo "   POOL: R_ARM_CALL inside pmap_flush_tlbs: arm_tlb_flushID_ASID x$_b_asid (REPORTED, not asserted - the ASID arm remains in the text, unreachable for non-kernel pmaps), arm_tlb_flushID x$_b_full (REPORTED - tail-merged to x1 in both OFF and ON objects); kernel_pmap materializations x$_b_kp (want 3; 1015's frozen object carries 2)"
[[ "$_b_kp" == "3" ]] || { echo "REFUSING: osfmk_arm_pmap.o materializes kernel_pmap $_b_kp time(s) inside pmap_flush_tlbs (want EXACTLY 3). Apple has 2 (`:1291` pmap_is_shared init + the `:1382` terminal panic); 1015b's inserted `else if (pmap != kernel_pmap)` test is the 3rd. A count of 2 means the widened arm did NOT compile in - either the tree text is not staged (tools/patch_d13_globalfix.py --check) or this object was not rebuilt. (This clause's first draft asserted the ASID call's ABSENCE and a pair of full calls; both are unsatisfiable - the compiler tail-merges the full calls and keeps the ASID arm as unreachable text. See the note above.)"; exit 1; }
# AND THE INHERITED 1014 PROBE IS STILL IN THE SAME OBJECT - 1015b inserts BETWEEN Apple's two lines,
# so a correct insertion cannot displace the 1014 payload's ONE `U entry_note_abt_wf` relocation (the
# reader's G-CONFIRMED attribution guard reads `_wfkind`, which that probe publishes).
_b_wf=$(arm-none-eabi-objdump -r "$_b_obj" 2>/dev/null | grep -c 'entry_note_abt_wf' || true)
[[ "$_b_wf" == "1" ]] || { echo "REFUSING: osfmk_arm_pmap.o carries $_b_wf relocation(s) to entry_note_abt_wf (want exactly 1 - the inherited 1014 flush probe). 1015b's insertion must ride BETWEEN Apple's arm_tlb_flushID() and Apple's else; a displaced 1014 probe would leave the reader's attribution guard (a _wfkind=1 row keyed to the storm) without a producer"; exit 1; }
echo "park_1015b: by value - the POOL MOVED and it moved where the design says: kernel_pmap materializations x$_b_kp inside pmap_flush_tlbs (Apple 2 + the inserted test = 3); arm_tlb_flushID x$_b_full / arm_tlb_flushID_ASID x$_b_asid reported (both x1, codegen-invariant); the inherited entry_note_abt_wf relocation x$_b_wf intact"

# BY VALUE, at the freeze point: the 1015 delta FIRST, then the regressions, then the control.
# 1015's NEW instrument (the permission-INPUTS witness + the AT/PAR hardware cell), BY VALUE, FIRST.
_ab_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_abt_' || true)
[[ "$_ab_lit" == 131 ]] || {
  echo "REFUSING: the parked image does not carry the 131 xnu_live_abt_ key literals (49 through 1012 + 1013's 37 [the pressed 1013b elf carries 86] + 1014's 14 flush-witness keys + 1015's 31 permission-INPUTS/AT-PAR/owed-ww keys) (x$_ab_lit, want 131) - the 1011 vme store, the 1012 pmp store, the 1013 witness store, the 1014 flush/CONTEXTIDR store or the 1015 perm/AT store is gone"; exit 1; }
_ab_nm=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf")

# ====================================================================================================
# 1015's NEW instrument (THE PERMISSION-INPUTS WITNESS + THE AT/PAR HARDWARE CELL), BY VALUE, FIRST -
# placed BEFORE the 1014 block so a 1015 park that lost its OWN change fails before it re-asserts the
# older ones.  The rung answers ONE question: when `0xe6000`'s store took FS=15 on a leaf 1014 read as
# user-WRITABLE, what did the MMU ITSELF say - and are the permission INPUTS (L1 domain field, DACR,
# TTBCR.N routing, the leaf's full AP/TEX/nG decode) the ones the walk read?
# ====================================================================================================
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_wd\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_wd is not a DEFINED text symbol in the parked image - the 1015 permission-INPUTS/AT-PAR witness is gone (an object did not rebuild, or a generated stand-in was used)"; exit 1; }
# The fixed keyspace (docs/1015-design.md §2a): 27 `wd*` cells + `_leafng` + the three OWED-1014-FIX
# keys `_wwpre`/`_wwpost`/`_wwmoved` = 31 literals, read as WHOLE-LINE `strings` matches so a
# truncated or re-spelled key is a refusal.
_wd_keys=0
for _wd_key in \
    xnu_live_abt_wdn xnu_live_abt_wdva xnu_live_abt_wddacr xnu_live_abt_wddom xnu_live_abt_wdttbcr xnu_live_abt_wdsplit xnu_live_abt_wdttbr0 xnu_live_abt_wdttbr1 xnu_live_abt_wdl1 xnu_live_abt_wdleaf xnu_live_abt_wdap xnu_live_abt_wdapx xnu_live_abt_wdtex xnu_live_abt_wdcb xnu_live_abt_wdsmall xnu_live_abt_wdpcpu xnu_live_abt_wdthrmap xnu_live_abt_wdmapok xnu_live_abt_wdseen xnu_live_abt_wdctl xnu_live_abt_wdctlok xnu_live_abt_wdempty xnu_live_abt_wdatok xnu_live_abt_wdatpar xnu_live_abt_wdatparpr xnu_live_abt_wdctlat xnu_live_abt_wdsctlr \
    xnu_live_abt_leafng \
    xnu_live_abt_wwpre xnu_live_abt_wwpost xnu_live_abt_wwmoved ; do
  grep -qx "$_wd_key" <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || {
    echo "REFUSING: the parked image does not carry the \`$_wd_key\` literal - the 1015 store is gone"; exit 1; }
  _wd_keys=$((_wd_keys + 1))
done
[[ "$_wd_keys" == 31 ]] || { echo "REFUSING: the 1015 key ladder counted $_wd_keys (want 31)"; exit 1; }
echo "park_1015b: by value - entry_note_abt_wd DEFINED T/t and all 31 1015 key literals present (27 perm-INPUTS/AT-PAR cells wdn/wdva/wddacr/wddom/wdttbcr/wdsplit/wdttbr0/wdttbr1/wdl1/wdleaf/wdap/wdapx/wdtex/wdcb/wdsmall/wdpcpu/wdthrmap/wdmapok/wdseen/wdctl/wdctlok/wdempty/wdatok/wdatpar/wdatparpr/wdctlat/wdsctlr + the GLOBAL precondition leafng + the three OWED-1014-FIX keys wwpre/wwpost/wwmoved)"

# Exactly ONE call site of the recorder IN THE LINKED IMAGE, spelled `(b|bl)` and NEVER `bl`
# ([[mi4-hooks-live-at-calls]]): the compiler TAILS a void call that is the last statement of its
# function, so the image may carry `b <sym>` rather than `bl <sym>` - this was 1013's own build bug.
# `entry_note_abt_wd`'s ONE call is in the abort path (`src/entry/entry_trace.c`, inside
# __wrap_sleh_abort, BEFORE the handler, so the values describe the fault as taken).
_wd_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wd>' || true)
[[ "$_wd_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_wd_call call site(s) to entry_note_abt_wd (want exactly 1) - the 1015 witness is not wired the way the rung measures"; exit 1; }
_wd_ab=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_sleh_abort not in the parked image"; exit 1; }
_wd_abx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_wd_ab#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_wd_abb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_wd_ab" --stop-address="${_wd_abx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <sleh_abort>' <<<"$_wd_abb" || { echo "REFUSING: __wrap_sleh_abort does not call the real sleh_abort body - the pass-through regressed"; exit 1; }
grep -qE '(b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wd>' <<<"$_wd_abb" || { echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_wd - the 1015 permission-INPUTS/AT-PAR witness never fires"; exit 1; }
grep -qE '(b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_cur>' <<<"$_wd_abb" || { echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_cur - the 1013 fault-VA latch the recorder keys on regressed"; exit 1; }
echo "park_1015b: by value - exactly ONE (b|bl) call site to entry_note_abt_wd, inside __wrap_sleh_abort (which still calls <sleh_abort> and <entry_note_abt_cur>)"

# **1015 IS ENTRY-ONLY AND ITS POOL DID NOT MOVE - AND 1015b IS THE EXACT OPPOSITE.**  1015's clause
# here asserted `osfmk_arm_pmap.o` is UNMOVED; that assertion is INVERTED for this rung and CANNOT be
# carried over (`docs/1015b-design.md` R8 - the copy-paste class).  1015b's pool-moved census - the
# `kernel_pmap`-materializations == 3 test on this same object - is taken in
# block (2) ABOVE, BEFORE every 1015 clause, so a park that lost the pool edit stops on ITS OWN claim
# rather than here.  What remains TRUE here, and is asserted, is the part 1015b must NOT disturb:
#   * the 1014 probe is STILL in the object and still called exactly ONCE (`U entry_note_abt_wf` + ONE
#     R_ARM_CALL) - 1015b inserts its arm BETWEEN Apple's two arms and never touches the probe.  If the
#     insertion had displaced it, the reader's attribution guard (`_wfkind=1` keyed to the storm) would
#     lose its producer and 1015b's own verdict cell would become unreadable;
#   * the object carries NO xnu_live_abt_wf* literal (the recorder's body lives in the entry toolchain
#     only - the 1012 pool-define hazard, [[mi4-one-value-two-definitions]]);
#   * the object carries NO entry_note_abt_wd reference (1015b rides 1015's entry-side witness; it adds
#     no new entry recorder and must not pull one into the pool).
_wf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_wf_obj" ]] || { echo "REFUSING: $_wf_obj is absent - the pool build did not produce pmap.o; 1015b's pool-MOVED census has nothing to read"; exit 1; }
grep -qE "^[[:space:]]*U entry_note_abt_wf\$" <(arm-none-eabi-nm "$_wf_obj") || {
  echo "REFUSING: the pool's osfmk_arm_pmap.o does not carry an UNDEFINED reference to entry_note_abt_wf - the 1014 flush probe is gone from the object 1015b edits. 1015b inserts BETWEEN Apple's two arms; a lost probe means the insertion displaced it, and the reader's attribution guard would then have no producer"; exit 1; }
_wf_rel=$(arm-none-eabi-objdump -r "$_wf_obj" | grep -c 'entry_note_abt_wf' || true)
[[ "$_wf_rel" == 1 ]] || {
  echo "REFUSING: osfmk_arm_pmap.o carries $_wf_rel relocations to entry_note_abt_wf (want exactly 1, inside pmap_flush_tlbs) - 1015b must not add or remove a flush-probe call site"; exit 1; }
grep -qE "^[[:space:]]*U entry_note_abt_wd\$" <(arm-none-eabi-nm "$_wf_obj") && {
  echo "REFUSING: the pool's osfmk_arm_pmap.o carries a reference to entry_note_abt_wd - 1015b changes only the FLUSH the storm takes; it must NOT pull 1015's entry-side witness into the pool"; exit 1; }
echo "park_1015b: by value - the pool's osfmk_arm_pmap.o still carries the 1014 probe (U entry_note_abt_wf, exactly ONE R_ARM_CALL) and NO entry_note_abt_wd reference; the MOVED part is the flush arm, censused in block (2) above"

# The 1014 tree probe must remain STAGED + an EDIT OF THE TREE (not a hand edit - external/ is
# gitignored and a hand edit is LOST on re-provision).  It stays staged because FLUSH_WITNESS stays 1.
python3 tools/patch_d13_flushwitness.py "${XNU_TREE:-external/xnu-hd2-darwin13/xnu}" --check >/dev/null || {
  echo "REFUSING: tools/patch_d13_flushwitness.py --check FAILS against the tree - the pmap.c flush probe is not in its staged shape (it must be staged by tools/stage_d13_flushwitness.sh; external/ is GITIGNORED, so a hand edit is LOST on re-provision)"; exit 1; }
echo "park_1015b: by value - the 1014 pmap.c flush probe is still in its staged shape (tools/patch_d13_flushwitness.py --check passes)"

# ====================================================================================================
# 1014's instrument (THE TLB-FLUSH WITNESS + THE CONTEXTIDR WITNESS) is a REGRESSION - 1015 keeps
# 1014's set verbatim, so its two recorders, its 14 keys, its ONE call site each and its
# __wrap_sleh_abort pass-through must ALL still be there.
# ====================================================================================================
for _wf_sym in entry_note_abt_wf entry_note_abt_wctx; do
  grep -qE "^[0-9a-f]+ [Tt] $_wf_sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_wf_sym is not a DEFINED text symbol in the parked image - the 1014 flush/CONTEXTIDR witness regressed (1015 keeps 1014's set verbatim)"; exit 1; }
done
_wf_keys=0
for _wf_key in \
    xnu_live_abt_wfn xnu_live_abt_wfva xnu_live_abt_wfttb xnu_live_abt_wflive xnu_live_abt_wfself xnu_live_abt_wfshared xnu_live_abt_wfncpus xnu_live_abt_wfasid xnu_live_abt_wfkind xnu_live_abt_wfseen xnu_live_abt_wfnx \
    xnu_live_abt_wctxn xnu_live_abt_wctx xnu_live_abt_wctxva ; do
  grep -qx "$_wf_key" <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || {
    echo "REFUSING: the parked image does not carry the \`$_wf_key\` literal - the 1014 store regressed"; exit 1; }
  _wf_keys=$((_wf_keys + 1))
done
[[ "$_wf_keys" == 14 ]] || { echo "REFUSING: the 1014 key ladder counted $_wf_keys (want 14)"; exit 1; }
_wf_pl=$(strings "$_wf_obj" | grep -c '^xnu_live_abt_wf' || true)
[[ "$_wf_pl" == 0 ]] || {
  echo "REFUSING: osfmk_arm_pmap.o carries $_wf_pl xnu_live_abt_wf* string literals - the flush probe was compiled with the RECORDER's body inlined into the pool (the entry-toolchain recorder is the only publisher)"; exit 1; }
echo "park_1015b: by value - entry_note_abt_wf/wctx DEFINED T/t and all 14 1014 key literals present (11 flush cells wfn/wfva/wfttb/wflive/wfself/wfshared/wfncpus/wfasid/wfkind/wfseen/wfnx + 3 CONTEXTIDR cells wctxn/wctx/wctxva)"

for _wf_call_sym in entry_note_abt_wf entry_note_abt_wctx; do
  _wf_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <'"$_wf_call_sym"'>' || true)
  [[ "$_wf_call" == 1 ]] || {
    echo "REFUSING: the parked image carries $_wf_call call site(s) to $_wf_call_sym (want exactly 1) - the 1014 witness regressed"; exit 1; }
done
grep -qE '(b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wctx>' <<<"$_wd_abb" || { echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_wctx - the 1014 CONTEXTIDR witness regressed"; exit 1; }
echo "park_1015b: by value - exactly ONE (b|bl) call site each of entry_note_abt_wf and entry_note_abt_wctx; the CONTEXTIDR call is inside __wrap_sleh_abort (1014's flush witness kept)"

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
echo "park_1015b: by value - xnu_live_abt_* literals x$_ab_lit (49 through 1012 + 1013's 37 + 1014's 14 + 1015's 31), entry_note_abt_pmp/__wrap_pmap_enter_options DEFINED T/t, exactly ONE b/bl to entry_note_abt_pmp, and the wrapper calling pmap_enter_options AND entry_note_abt_pmp (the prot-at-the-apex witness is present)"

# THE 1011 CHANGE (the vm_map_entry effective protection + the faulting thread's own L1 base) is a
# REGRESSION: the wrapper/pass-through, the two 1011 symbols, the ONE b/bl to entry_note_abt_vme, and
# the (A) STAGE90_ACT_MAP immediate (956).  1015 keeps 1014's set verbatim.
for _ab_1011sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1011sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1011sym is not a DEFINED text symbol in the parked image - the 1011 instrument regressed (1015 keeps 1014's set verbatim)"; exit 1; }
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
echo "park_1015b: by value - entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, ONE b/bl to entry_note_abt_vme, the lookup pass-through intact, and the (A) STAGE90_ACT_MAP immediate (956) present (1011's (E')/(A) witness kept)"

# THE 1010 CHANGE (the L2 descriptor + the vm_fault return) is a REGRESSION.
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the parked image - the 1010 instrument regressed (1015 keeps 1014's set verbatim)"; exit 1; }
done
_abt_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' || true)
[[ "$_abt_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abt_call call site(s) to entry_note_abt_pte (want 1, inside __wrap_sleh_abort) - the 1010 witness regressed"; exit 1; }
_abt_wfa=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "REFUSING: __wrap_vm_fault not in the parked image"; exit 1; }
_abt_wfx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_abt_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_abt_wfb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_abt_wfa" --stop-address="${_abt_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - the 1010 pass-through regressed"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_abt_wfb" || { echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the 1010 (B) record regressed"; exit 1; }
echo "park_1015b: by value - entry_note_abt_pte/vmf + __wrap_vm_fault DEFINED T/t, ONE b/bl to entry_note_abt_pte, the __wrap_vm_fault pass-through intact (1010's L2-descriptor + vm_fault-return witness kept)"

# 1009's faulting-pc identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the parked image - the 1009 recorder was dropped (1015 keeps 1014's set verbatim)"; exit 1; }
_abp_call=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' || true)
[[ "$_abp_call" == 1 ]] || {
  echo "REFUSING: the parked image carries $_abp_call call site(s) to entry_note_abt_pc (want 1) - the 1009 witness regressed"; exit 1; }
echo "park_1015b: by value - entry_note_abt_pc DEFINED T/t with exactly ONE b/bl (1009's faulting-pc identity kept)"
# 1008's abort-fault identity is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the parked image - the 1008 recorder was dropped"; exit 1; }
echo "park_1015b: by value - entry_note_abt_far DEFINED T/t (1008's abort-fault identity kept)"
# 1007's abort-span witness is a REGRESSION.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_ab_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the parked image - the 1007 recorder was dropped"; exit 1; }
echo "park_1015b: by value - entry_note_abt_span DEFINED T/t (1007's abort-span witness kept)"
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
echo "park_1015b: by value - xnu_live_freeze_* literals x$_frz_lit, xnu_live_frzbase_* literals x$_idl_lit, the two freeze symbols DEFINED T/t (the freeze witness is present)"

# The 1005 cadence witness is a REGRESSION.
_cad_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_cadence_ key literals (x$_cad_lit, want 4) - 1005's cadence store is gone"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_frz_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the parked image - the 1005 cadence symbol regressed"; exit 1; }
done
echo "park_1015b: by value - xnu_live_cadence_* literals x$_cad_lit, the four cadence symbols DEFINED T/t (1005's witness kept)"

_upc_lit=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_lit" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_lit, want 1) - 1004's abort trace was dropped"; exit 1; }
echo "park_1015b: by value - xnu_live_sleh_upc literal x$_upc_lit (1004's abort trace kept)"

_stall_sym=$(grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' <<<"$_frz_nm" || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1015b: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1015b: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1014 elf as the NEGATIVE CONTROL for the 1015 reader.  REQUIRED (the 1014 elf is the
# PRESSED predecessor and is on disk); an absent control is a REFUSAL, not a warning - a park that
# cannot show the reader CAN fail proves nothing about the reader.
_ctrl=out/stage90/frozen/armed-d13-29ec9e48/xnu_arm_entry.elf
[[ -f "$_ctrl" ]] || {
  echo "REFUSING: the frozen 1014 elf ($_ctrl) is absent - the 1015 NEGATIVE CONTROL cannot be taken; the 1015 reader/witness must be provably loadable AND provably incomplete before 1015 is parked"; exit 1; }
_c_ab=$(strings "$_ctrl" | grep -c 'xnu_live_abt_' || true)
_c_14sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_wf|entry_note_abt_wctx)$' || true)
_c_15sym=$(arm-none-eabi-nm "$_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_wd$' || true)
_c_15lit=$(strings "$_ctrl" | grep -cE 'xnu_live_abt_(wd|leafng)' || true)
[[ "$_c_ab" == 100 && "$_c_14sym" == 2 && "$_c_15sym" == 0 && "$_c_15lit" == 0 ]] || {
  echo "REFUSING: the frozen 1014 elf does not have the pre-1015 shape (xnu_live_abt_=$_c_ab want 100, the 1014 symbols=$_c_14sym want 2, 1015 symbols=$_c_15sym want 0, 1015 wd/leafng literals=$_c_15lit want 0) - the control image is not the pre-1015 one"; exit 1; }
echo "park_1015b: by value - NEGATIVE CONTROL (frozen 1014 elf, REQUIRED): xnu_live_abt_ x$_c_ab (1014's 100 keys), the 1014 symbols x$_c_14sym, 1015 symbols x$_c_15sym (the 1015 reader can fail), 1015 wd/leafng literals x$_c_15lit"

# ====================================================================================================
# 1013's instrument (THE LEAF-VALUE WITNESS) is a REGRESSION - the four recorders, their four call
# sites, and the fixed keyspace must all still be there (1015 keys its whole AT/PAR cell on the SAME
# walker and the SAME `g_abtpte_curva` this rung latches).
# ====================================================================================================
for _wk_sym in entry_note_abt_wleaf entry_note_abt_walk entry_note_abt_wsync entry_note_abt_cur; do
  grep -qE "^[0-9a-f]+ [Tt] $_wk_sym\$" <<<"$_ab_nm" || {
    echo "REFUSING: $_wk_sym is not a DEFINED text symbol in the parked image - the 1013 witness regressed (1015 keys on it)"; exit 1; }
done
# The fixed keyspace (`docs/1013-design.md` §2), read as whole-line `strings` matches so a truncated or
# re-spelled key is a refusal. 37 literals: 11 W-APEX (incl. the provenance cells wwva/wwft/wwpatbl),
# 13 W-WALK (incl. the 1013b VA cell wkva and the double-read wkleaf2/wkstable) + the 2 change-counts
# wwnx/wknx = 14 in the `wk` family count below, 9 W-SYNC (incl. the TTBR1 label wctb), PLUS the 2 raw
# TTBR words wctb0/wctb1.  NOTE: 1015's three OWED-1014-FIX keys (_wwpre/_wwpost/_wwmoved) are in the
# 1015 ladder above, not here - this list is exactly 1013's 37 and must stay 37.
_wk_keys=0
for _wk_key in \
    xnu_live_abt_wwn xnu_live_abt_wwl1 xnu_live_abt_wwl2pa xnu_live_abt_wwleaf xnu_live_abt_wwapx xnu_live_abt_wwnoproof xnu_live_abt_wwpa xnu_live_abt_wwpaok xnu_live_abt_wwva xnu_live_abt_wwft xnu_live_abt_wwpatbl xnu_live_abt_wwnx \
    xnu_live_abt_wkn xnu_live_abt_wkva xnu_live_abt_wk xnu_live_abt_wktb xnu_live_abt_wkl1 xnu_live_abt_wkl2pa xnu_live_abt_wkleaf xnu_live_abt_wkleaf2 xnu_live_abt_wkstable xnu_live_abt_wkapx xnu_live_abt_wkoow xnu_live_abt_wkfsr xnu_live_abt_wkapex xnu_live_abt_wknx \
    xnu_live_abt_wcnt xnu_live_abt_wcva xnu_live_abt_wck xnu_live_abt_wctb xnu_live_abt_wcl1 xnu_live_abt_wcl2pa xnu_live_abt_wcleaf xnu_live_abt_wcapx xnu_live_abt_wcoow \
    xnu_live_abt_wctb0 xnu_live_abt_wctb1 ; do
  grep -qx "$_wk_key" <<<"$(strings "$LIVE/xnu_arm_entry.elf")" || {
    echo "REFUSING: the parked image does not carry the \`$_wk_key\` literal - the 1013 store regressed"; exit 1; }
  _wk_keys=$((_wk_keys + 1))
done
[[ "$_wk_keys" == 37 ]] || { echo "REFUSING: the key ladder counted $_wk_keys (want 37)"; exit 1; }
echo "park_1015b: by value - the four 1013 recorders DEFINED T/t and all 37 1013 key literals present (11 W-APEX + 14 W-WALK + 9 W-SYNC + the 2 raw TTBR words wctb0/wctb1)"
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
echo "park_1015b: by value - exactly ONE call site each of entry_note_abt_wleaf/walk/wsync/cur, the W-APEX call inside the pmap wrapper and W-WALK NOT in it"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1015b: THE GLOBAL-FIX RUNG - the storm-s own flush widened to a FULL TLBIALL, and the\n# behaviour read-out that decides H-G (design docs/1015b-design.md; the cell is pre-registered in\n# tools/check_1015b_capture.py).\n' "$ARM"
  printf '# Switch set is 1015-s VERBATIM plus STAGE90_XNU_GLOBAL_FIX=1 (PTE_WITNESS, PMP_ENTER, VME_PROT,\n'
  printf '# ABT_PTE, ABT_PC, ABT_FAR, ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL, ABORT_TRACE, DYLD_WITNESS,\n'
  printf '# SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC, SUBMAP_PMAP_SKIP, FLUSH_WITNESS and 1015-s\n'
  printf '# own PERM_WITNESS all stay 1); the ENTRY BIN moves (e51d14fa -> %s); the PAYLOAD moves (it embeds\n' "${ARM#armed-d13-}"
  printf '# the entry blob; the 29 s watchdog constant is 999-s, unchanged).\n'
  printf '# **THE TREE MOVES, AND THE KERNEL MOVES WITH IT - THAT IS THE RUNG.** Where 1015 was ENTRY-ONLY,\n'
  printf '# 1015b stages ONE SOURCE EDIT: tools/stage_d13_globalfix.sh inserts, at\n'
  printf '# external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c:1346-1349, a third arm between Apple-s\n'
  printf '# `arm_tlb_flushID();` (:1347) and Apple-s `else` (:1348):\n'
  printf '#       #if defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX\n'
  printf '#                   else if (pmap != kernel_pmap)\n'
  printf '#                       arm_tlb_flushID();\n'
  printf '#       #endif\n'
  printf '# so every NON-KERNEL pmap-s steady-state flush is a FULL TLBIALL instead of the ASID-scoped\n'
  printf '# TLBIASID.  `pmap_is_shared` is `pmap->pm_shared || (pmap == kernel_pmap)` (pmap.c:1291), so the\n'
  printf '# added condition is FALSE for the kernel and the KERNEL-S SEMANTICS ARE UNCHANGED; OFF removes the\n'
  printf '# three lines entirely (the #if wraps them), which is what makes the build-s OFF proof a\n'
  printf '# byte-identical .text comparison rather than a promise.\n'
  printf '# THE POOL OBJECT MOVES: out/xnu_kernel_obj_d13/osfmk_arm_pmap.o now materializes `kernel_pmap`\n'
  printf '# THREE times inside pmap_flush_tlbs - where 1015-s object has TWO (Apple-s :1291 pmap_is_shared\n'
  printf '# init + the :1382 terminal panic; the inserted non-kernel test is the third, at the offset just\n'
  printf '# before the full arm-s `bl`). The arm_tlb_flushID / arm_tlb_flushID_ASID CALL counts are x1/x1 in\n'
  printf '# BOTH objects (codegen: the two identical full-flush sites tail-merge; the ASID arm stays as\n'
  printf '# unreachable text), so they are reported, not asserted. The 1014 flush probe-s ONE entry_note_abt_wf relocation is\n'
  printf '# UNDISTURBED (1015b inserts BETWEEN Apple-s two lines).\n'
  printf '# AND THE DECLARATION: STAGE90_XNU_GLOBAL_FIX is a POOL switch that no writer in src/entry/\n'
  printf '# echoes (MEASURED x0 in build_entry.sh AND in preflight_boot_check.sh-s ENTRY_CFG_KEYS, whose\n'
  printf '# converse check is a SET DIFFERENCE), so the record CANNOT carry it and park_1015b.sh appends it\n'
  printf '# to out/stage90/xnu_arm_entry-arm-extra.txt (frozen beside this file as\n'
  printf '# xnu_arm_entry-arm-extra.txt). A reader that wants to know what this arm-s kernel does must read\n'
  printf '# that line; the config record alone describes 1015-s kernel.\n#\n'
  printf '# WHY 1015b FOLLOWS 1015.  The 1015 press (arm armed-d13-e51d14fa, records/1015-press-read.txt,\n'
  printf '# RESIDENT, wdt_pets=0x61) read V-AT1 AT THE HARDWARE: _wdatpar FAULTS (par & 1 == 1) while its\n'
  printf '# null control _wdatparpr SUCCEEDS (par & 1 == 0) to PA 0x8419d000, on a leaf read as\n'
  printf '# _wdleaf=0x8417e1bb (AP=0b11, APX=0, PA 0x8417e000) with _wddom=0, _wddacr=1, _wdttbcr=2 and\n'
  printf '# _leafng=0 (GLOBAL).  The MMU-s translation for a USER WRITE is therefore NOT the leaf the walk\n'
  printf '# reads. With every user leaf GLOBAL (L2_NG_BIT 0x800, pmap.h:231, is never ORed tree-wide) and\n'
  printf '# TLBIASID unable to evict a GLOBAL entry, a STALE GLOBAL entry is the standing explanation - H-G.\n'
  printf '# 1015b is its BEHAVIOURAL half: if H-G is the cause, giving the storm-s own pmap a full TLBIALL\n'
  printf '# stops the storm. The secondary observation that 1015b-s fix also gives the arm a NEW code path\n'
  printf '# is why the reader has an ATTRIBUTION GUARD (see the cells below).\n#\n'
  printf '# WHY 1015 FOLLOWS 1014.  The 1014 press (arm armed-d13-29ec9e48, records/1014-press-read.txt)\n'
  printf '# MEASURED two cells.  F2 (docs/1014-design.md §2.5): on every tail row wfkind=0x02\n'
  printf '# (arm_tlb_flushID_ASID), wfself=1 (the pmap.c:1298 gate was TRUE), wfttb == wflive ==\n'
  printf '# 0x81a86000 (the pmap-s own L1 table IS the live TTBR0 table at flush time), wfasid=2 == wctx=2\n'
  printf '# (the flush ran under the faulting ASID), wfncpus=1 - so 1013c-s H-FF hypothesis ("the flush\n'
  printf '# never ran because pm_l1_phys != TTBR0") is REFUTED BY VALUE.  V1: three probes read the SAME\n'
  printf '# cell (VA 0x000e6000, L1 0x81bb2001, L2 table 0x81bb2000) and all three read the SAME word\n'
  printf '# 0x841e91bb = PRESENT small page, ap=0b11, apx=0 (USER-WRITABLE), PA 0x841e9000 - YET the CPU\n'
  printf '# still takes FS=15 permission faults on it ~2.6e5 times at a constant far=0xe6000/pc=0x2bed61a0.\n'
  printf '# A WRITABLE LEAF PLUS A FLUSH THAT RAN AND STILL FAULTS means the CPU-s translation is not the\n'
  printf '# leaf the walk reads.  BUT: the earlier reading "F1 (stale TLB) is dead" was CORRECTED - F2\n'
  printf '# proves a flush RAN, NOT that a GLOBAL entry was INVALIDATED.  This port installs EVERY leaf\n'
  printf '# global (bit11=0; L2_NG_BIT is never ORed), so a TLBIASID cannot evict them and H-G\n'
  printf '# STALE_GLOBAL_TLB is NARROWED, NOT eliminated.  That narrowed question - does the CPU-s own\n'
  printf '# translation of 0xe6000 differ from the walked leaf - is this rung.\n#\n'
  printf '# WHAT 1015 MEASURES.  Two passes, both read in the abort context (__wrap_sleh_abort,\n'
  printf '# src/entry/entry_trace.c, BEFORE the handler) so the values describe the fault AS TAKEN.\n'
  printf '#   (1) THE AT/PAR HARDWARE CELL - the deciding pass.  The recorder issues the tree-s OWN idiom\n'
  printf '#       (pmap_extract, pmap.c:2930): `mcr p15,0,cura,c7,c8,3` (armreg_va2pa_uw_ns_write = AT,\n'
  printf '#       stage-1, USER WRITE) on the storm VA, ISBs (`"xisb sy"`, the sequence the AT/PAR pair\n'
  printf '#       requires), then `mrc p15,0,Rt,c7,c4,0` (armreg_par_read).  `par & 1 == 1` is a FAULT;\n'
  printf '#       `par & 0xfffff000` is the PA the MMU itself uses.  Published RAW as _wdatpar.  The\n'
  printf '#       PRIVILEGED cross-check repeats it with `c7,c8,0` (va2pa_pr_ns) on the STORM VA and\n'
  printf '#       publishes it as _wdatparpr: it is the only thing that separates "the entry is user-denied" from "the\n'
  printf '#       entry is not there".  The AT is the STRONGEST instrument here: every other witness asks\n'
  printf '#       the PAGE TABLES what should happen; AT asks the MMU what DOES happen - the same\n'
  printf '#       translation path the faulting store used, in the same thread context, so a stale entry\n'
  printf '#       that shadows the leaf shadows AT too.\n'
  printf '#   (2) THE PERMISSION-INPUTS LADDER - the residual/null pass.  Read at the SAME abort, from NO\n'
  printf '#       NEW WALKER (the L1 and the leaf come from the ONE routine stage90_read_l2_pte): _wddom\n'
  printf '#       (the L1 descriptor-s domain field, bits[8:5]), _wddacr (DACR raw, `mrc p15,0,Rt,c3,c0,0`),\n'
  printf '#       _wdttbcr (TTBCR raw, `c2,c0,2`) + _wdsplit (1 << (32-(ttbcr&7))), _wdttbr0/_wdttbr1\n'
  printf '#       (live raw), _wdl1 and _wdleaf (ONE read; every decode below comes from that local),\n'
  printf '#       _wdap (leaf>>4 & 3), _wdapx (leaf>>9 & 1), _wdtex (leaf>>6 & 7), _wdcb (leaf>>2 & 3),\n'
  printf '#       _wdsmall (leaf>>1 & 1), _wdempty (the walk-s present-test), _wdpcpu (TPIDRPRW),\n'
  printf '#       _wdthrmap (the aborting thread-s own pmap L1 base, the 1011 chain) + _wdmapok (1 iff\n'
  printf '#       every deref landed in [0x80000000,0xFFFEFFFF] - the lineage-s own null control), and\n'
  printf '#       _wdsctlr (SCTLR raw AT THE ABORT - perm-static-s ranked cause #2).\n'
  printf '#   (3) THE NULL CONTROLS.  On each row the recorder issues the SAME AT pair against the W-SYNC\n'
  printf '#       control VA (entry_stubs.c:3153; 1014 already read its leaf as present at PA 0x80080000,\n'
  printf '#       _wcleaf=0x8008021a) and publishes its PRIVILEGED par ON ITS OWN KEY, _wdctlat - NOT on\n'
  printf '#       _wdatparpr, which reads the STORM VA (a DIFFERENT address must never share a key:\n'
  printf '#       [[mi4-one-value-two-definitions]]; the first design draft made exactly that collision and\n'
  printf '#       the lead-s 2026-10-15 ruling split it).  _wdctlok = 1 iff that control read was taken;\n'
  printf '#       _wdseen = 1 iff the cells were actually produced; _wdatok = 1 iff the three AT/PAR pairs\n'
  printf '#       executed AND the control passed.  A missing or mis-executed read can NEVER impersonate a\n'
  printf '#       confirmation.\n#\n'
  printf '# =================================================================================================\n'
  printf '# THE 1015b DECISIVE CELL - PRE-REGISTERED BEFORE THE PRESS (design docs/1015b-design.md §5;\n'
  printf '# adjudicated by tools/check_1015b_capture.py, whose verdict strings are quoted here VERBATIM so\n'
  printf '# the cell cannot be re-worded after the run).  **THIS IS THE CELL THE RUNG EXISTS FOR.**\n'
  printf '# THE QUESTION: H-G says the storm is a STALE GLOBAL TLB ENTRY that TLBIASID could not evict. If\n'
  printf '# so, taking the FULL arm_tlb_flushID() (TLBIALL) for this pmap EVICTS it and the storm STOPS.\n'
  printf '#   * G-CONFIRMED  -  the storm STOPS: H-G CONFIRMED IN BEHAVIOUR.  Read BY VALUE: the reader\n'
  printf '#     RECOVERS the storm VA from the capture rather than matching a literal, because the storm-s\n'
  printf '#     `far` moves per run (1014 read 0xe6000, 1015 read 0x27000, 1011 read 0x61000); no literal\n'
  printf '#     identifies it.  A stop REQUIRES the storm rows to be ABSENT and the capture to be\n'
  printf '#     STRUCTURALLY COMPLETE - the truncation guard runs FIRST, because a truncated log is exactly\n'
  printf '#     what a stopped storm looks like in bytes (and because /proc/last_kmsg is what the carrier\n'
  printf '#     gives back; a missing tail is not a quiet run).\n'
  printf '#   * G-REFUTED   -  the storm PERSISTS: H-G REFUTED IN BEHAVIOUR.  This is a POSITIVE\n'
  printf '#     measurement: the reader must point at a storm ROW, never at an absence.  What follows is not\n'
  printf '#     1015b-s fix tried harder but 1015-s §3b ladder (P-X1: inputs all correct and it still\n'
  printf '#     faults) and then H-X - the CPU-s own state (SCTLR/PRRR via _wdsctlr), not another table\n'
  printf '#     read.  A refutation here is INFORMATIVE, not a null result.\n'
  printf '#   * G-WITHHOLD   -  the reader NAMES the exact missing or contradictory key.  NEVER a negative:\n'
  printf '#     the cell is withheld when the capture cannot decide it.  A reader that cannot withhold\n'
  printf '#     certifies nothing ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).\n'
  printf '# THE STOP DEFINITION IS PRE-REGISTERED AND IS NOT "FEWER ROWS".  A storm row is a tail abort row\n'
  printf '# whose VA equals the BY-VALUE RECOVERED storm VA; the storm is STOPPED iff fewer than three such\n'
  printf '# rows exist AND none of them is at `_pcn >= 0x4000` (the 1014/1015 storm-s own band - it ran to\n'
  printf '# `_pcn=0x40000`).  Three rows at low ordinal is NOT a stop: it is a storm the ladder caught\n'
  printf '# early, which is why the band condition carries half the definition.\n'
  printf '# THE ATTRIBUTION GUARD - READ THIS BEFORE BELIEVING A G-CONFIRMED.  1015b CHANGES KERNEL\n'
  printf '# BEHAVIOUR, so a stopped storm is only evidence about THIS arm if the WIDENED arm is the one the\n'
  printf '# faulting pmap took. The reader therefore requires a `_wfkind=1` row (1014-s W-FLUSH publishes\n'
  printf '# `_wfkind`: Apple-s arm is 0, Apple-s ASID arm is 2, and the widened arm is 1) whose `_wfva` or\n'
  printf '# `_pcfar` EQUALS the recovered storm VA. Without such a row the verdict text says the stop is\n'
  printf '# UNATTRIBUTED rather than implying the fix caused it - because a widened arm that the storm-s\n'
  printf '# pmap never reached cannot have stopped anything, and the remaining explanations (a different\n'
  printf '# dyld, a shorter run, a different fault) would all produce the same absent rows.\n'
  printf '# THE SECOND CELL, re-run over the SAME capture: 1015-s AT/PAR table (V-AT1..4, the block below)\n'
  printf '# and its passive ladder.  The reader re-runs them so that a G-CONFIRMED can be read AGAINST the\n'
  printf '# hardware cell rather than in isolation: V-AT1 (the CPU-s translation is not the walked leaf)\n'
  printf '# PLUS a stopped storm is the strongest pair this rung can produce, and the reader prints both.\n'
  printf '# WHAT WOULD FALSIFY 1015b-S READING ENTIRELY: a G-CONFIRMED with NO `_wfkind=1` row on the storm\n'
  printf '# VA (the attribution guard), or a stop whose capture is TRUNCATED (the truncation guard), or a\n'
  printf '# `_leafng != 0` (a NON-GLOBAL leaf would kill H-G by value before any press - TLBIASID WOULD\n'
  printf '# evict its stale entry - which is why 1015 published the bit RAW).\n'
  printf '# =================================================================================================\n#\n'
  printf '# THE 1015 VERDICT CELLS (docs/1015-design.md §3a - this table was §3c before the ENTRY-ONLY\n'
  printf '# renumber; tools/check_1015_capture.py and docs/1015-harness.md still cite it as §3c - SAME\n'
  printf '# table, old number).  Read on the TAIL ROWS-OWN values (_wdn >= 0x400, _wdva == 0xe6000,\n'
  printf '# _wdseen=1, _wdatok=1), NEVER on a whole-run aggregate (the 1008/1009 measurement defect, not\n'
  printf '# repeated).  The 1014 tail is NOT uniform - the last two rows are DIFFERENT faults inside a\n'
  printf '# different dyld (far~0x3a0xxxxx), so the filter is BOTH the ordinal AND far == 0xe6000:\n'
  printf '#   V-AT0  the control read (_wdctlat) did not come back `par & 1 == 0` with\n'
  printf '#          `(par & 0xfffff000) == 0x80080000`, or _wdctlat is unpublished, or _wdatok=0\n'
  printf '#          => AT_INSTRUMENT_BROKEN: WITHHOLD V-AT1..4.  (SILENCE_IS_A_READING: the AT-s\n'
  printf '#          absence is a reading only because the control proves success is silent-but-readable.)\n'
  printf '#   V-AT1  _wdatpar FAULTS (`par & 1 == 1`) while _wdatparpr SUCCEEDS (`par & 1 == 0`)\n'
  printf '#          => H-G CONFIRMED AT THE HARDWARE - the translation is NOT the user leaf.  The MMU\n'
  printf '#          will not translate 0xe6000 for a user write even though the walked leaf is\n'
  printf '#          ap=0b11/apx=0/nG=0/domain=0; the privileged view succeeds, so this is a user-AP\n'
  printf '#          decision made from a translation that is not the leaf the walk reads => a stale/other\n'
  printf '#          entry.  With _leafng == 0 the culprit is a GLOBAL entry surviving TLBIASID.\n'
  printf '#   V-AT2  _wdatpar SUCCEEDS to the leaf-s OWN PA (`par & 1 == 0` and\n'
  printf '#          `(par & 0xfffff000) == 0x841e9000`) => TRANSLATION_IS_CORRECT_YET_THE_STORE_FAULTED:\n'
  printf '#          H-G REFUTED - the store-s FS=15 is NOT produced by this translation, so the probe-s\n'
  printf '#          key/ordering (g_abtpte_curva) or the DFSR/DFAR pair describes a DIFFERENT access.\n'
  printf '#          NOT a page-table defect - hand back to the observer (§3b-s P-X1 -> H-X, SCTLR/PRRR).\n'
  printf '#   V-AT3  _wdatpar SUCCEEDS to a DIFFERENT PA (`par & 1 == 0` and\n'
  printf '#          `(par & 0xfffff000) != 0x841e9000`) => WRONG_TRANSLATION_IN_THE_TLB: a TLB entry is\n'
  printf '#          serving a translation the tables no longer hold.  Same conclusion as V-AT1, reached\n'
  printf '#          from the PA side, and it NAMES the stale PA by value.\n'
  printf '#   V-AT4  BOTH views fault (`par & 1 == 1` on both) => the entry is not merely user-denied; a\n'
  printf '#          domain/invalid mechanism is in play.  Re-rank toward perm-static cause #2 (SCTLR/PRRR)\n'
  printf '#          before §7.\n'
  printf '# THE PASSIVE LADDER (§3b) rides the SAME rows and is WITHHELD unless _wdctlok=1, _wdseen=1 and\n'
  printf '# (for the lineage cells) _wdmapok=1: P-CTL (_wdctlok=0 or _wdseen=0) => NO_PERM_WITNESS, no\n'
  printf '# verdict; P-AT (_wdatok=0) => NO_AT (the deciding table is unavailable, regardless of\n'
  printf '# everything below); P-D1 _wddom != 0 => L1_DOMAIN_NONZERO; P-D2 _wddom==0 and (_wddacr & 3) != 1\n'
  printf '# => DACR_NOT_CLIENT (H-P CONFIRMED, the only DACR writer is src/mmu.c:4921); P-D3 _wddom==0 and\n'
  printf '# (_wddacr & 3) == 1 => DACR_CLIENT (H-P-s domain form KILLED); P-T1 (_wdttbcr & 7) != 2 =>\n'
  printf '# TTBCR_NOT_N2 (every entry_ttbr_base derivation must be re-read); P-T2 (_wdttbcr&7)==2 and\n'
  printf '# _wdva >= _wdsplit => ROUTED_TO_TTBR1; P-T3 the masked _wdttbr0 != 0x81a86000 => TTBR0_MOVED_\n'
  printf '# AT_ABORT; P-M1 _wdmapok=1 and _wdthrmap != _wdttbr0 => THREAD_PMAP_NE_TTBR0; P-L1 _wdsmall=1\n'
  printf '# and (_wdapx!=0 or _wdap!=0b11) => LEAF_NOT_USER_WRITABLE (contradicts 1014-s stable\n'
  printf '# double-read); P-L3 _wdsmall=0 => NOT_A_SMALL_PAGE; P-S1 (_wdsctlr bits TRE(28)/AFE(1)/M(0))\n'
  printf '# differs from the console-install reading => SCTLR_MOVED (H-X is live); P-X1 all of\n'
  printf '# P-D3/P-T1..P-T3/P-M1/P-L1/P-L3/P-S1 killed AND V-AT2 => INPUTS_ALL_CORRECT_STILL_FAULTS (the\n'
  printf '# next rung is the observer, not the pmap).\n'
  printf '# A NOTE ON _leafng: `_leafng == 0` (GLOBAL) is NOT by itself a verdict - it is the PRECONDITION\n'
  printf '# of H-G, published RAW so the reader sees it without decoding the packed leaf.  `_leafng == 1`\n'
  printf '# (non-global) would KILL H-G by value before any press: if the leaf were ASID-tagged, TLBIASID\n'
  printf '# WOULD evict its stale entry.  The checker asserts _leafng == 0 at the storm as H-G-s\n'
  printf '# precondition (the 1014 leaf-s bit11 is 0).\n'
  printf '# The filter is the rung-s provenance statement: the recorder keys on 1013-s g_abtpte_curva\n'
  printf '# (_wdva) AND 1014-s storm identity, because the 24576-entry channel would otherwise be spent\n'
  printf '# before the storm (the 998 cap, the 1007 ladder lesson, [[mi4-measurement-defects]]).\n#\n'
  printf '# KEYS (the fixed keyspace, docs/1015-design.md §2a): the 27 permission-INPUTS/AT-PAR cells\n'
  printf '# wdn/wdva/wddacr/wddom/wdttbcr/wdsplit/wdttbr0/wdttbr1/wdl1/wdleaf/wdap/wdapx/wdtex/wdcb/\n'
  printf '# wdsmall/wdpcpu/wdthrmap/wdmapok/wdseen/wdctl/wdctlok/wdempty/wdatok/wdatpar/wdatparpr/\n'
  printf '# wdctlat/wdsctlr; the H-G precondition cell xnu_live_abt_leafng; and the THREE OWED-1014-FIX\n'
  printf '# keys xnu_live_abt_wwpre/wwpost/wwmoved (published from entry_note_abt_wleaf so that 1013-s\n'
  printf '# _wwnoproof becomes a READING - the pre- and post-call TTBR0 bases plus a moved flag - rather\n'
  printf '# than the bare, polarity-inverted 1 that 1014-s HARD #1 adjudicated).  No two probes share a\n'
  printf '# key: every one of the 31 greps to 0 tree-wide before it was added.\n'
  printf '# THE THREE OWED-1014-FIX KEYS are 1015-s only ENTRY-SIDE correction and they are the reason the\n'
  printf '# 1013 W-APEX cell is re-read this rung: 1014 could not form a (VA, L1, L2) pair because W-APEX-s\n'
  printf '# own provenance cell contradicted the SAME row-s pmp cells.\n#\n'
  printf '# VERIFIED BY VALUE (scripts/build_1015b.sh + this park - read the two scripts, this list is a\n'
  printf '# summary and the checks are the authority).  **1015b-S OWN CLAIM FIRST, BECAUSE IT IS THE ONE\n'
  printf '# THAT MAKES THIS ARM DIFFERENT:** the pool object out/xnu_kernel_obj_d13/osfmk_arm_pmap.o MOVED\n'
  printf '# - inside pmap_flush_tlbs it materializes kernel_pmap THREE times (1015-s frozen copy: TWO -\n'
  printf '# the :1291 pmap_is_shared init + the :1382 terminal panic; the inserted non-kernel test is the\n'
  printf '# third, at the offset just before the full arm-s `bl`). The arm_tlb_flushID / arm_tlb_flushID_ASID\n'
  printf '# call counts are x1/x1 in BOTH objects (codegen-invariant: the two identical full-flush sites\n'
  printf '# tail-merge into one `bl`, and the ASID arm remains as unreachable text), so they are reported\n'
  printf '# only. The 1014 flush probe-s ONE `U entry_note_abt_wf` relocation is UNDISTURBED, and\n'
  printf '# tools/patch_d13_flushwitness.py --check still PASSes - 1015b inserts BETWEEN Apple-s two lines.\n'
  printf '# THE DECLARATION rides out/stage90/xnu_arm_entry-arm-extra.txt, frozen beside this file as\n'
  printf '# xnu_arm_entry-arm-extra.txt: `STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix`. The config\n'
  printf '# record does NOT carry the switch and is NOT expected to (no writer in src/entry/ echoes it).\n'
  printf '# AND THE ARM MOVED OFF 1015-S PRESSED ARM armed-d13-e51d14fa - the pool-s .text is inside the\n'
  printf '# entry .bin whose sha8 IS the arm, so the move is expected from the pool segment alone, and a\n'
  printf '# build landing back on e51d14fa is the one failure this rung-s build refuses first.\n#\n'
  printf '# THE INHERITED 1015 HALF, asserted as a PRECONDITION (the reader reads the rung-s verdict off\n'
  printf '# these keys): the image carries 131 xnu_live_abt_ key\n'
  printf '# literals (1014-s measured 100 + 1015-s 31) and all 31 1015 keys; entry_note_abt_wd is DEFINED\n'
  printf '# T/t with exactly ONE `(b|bl)` call site (the (b|bl) rule - the compiler TAILS a void call that\n'
  printf '# ends its function; this was 1013-s own build bug), living inside __wrap_sleh_abort, which still\n'
  printf '# calls <sleh_abort> (pass-through) AND <entry_note_abt_cur> (the fault-VA latch the recorder\n'
  printf '# keys on); with the FROZEN 1014 elf (armed-d13-29ec9e48) as a REQUIRED NEGATIVE CONTROL\n'
  printf '# (xnu_live_abt_ x100, the 1014 symbols present, but the 1015 symbol x0 and the 1015 wd/leafng\n'
  printf '# literals x0 - the 1015 change is NOT in the 1014 arm, so the reader CAN fail); the 1014 flush\n'
  printf '# witness (14 keys), the 1013 witness, the 1012 apex, the 1011 (E-prime)/(A) witness, the\n'
  printf '# 1010/1009/1008/1007 witnesses, the 1006 freeze witness, the 1005 cadence witness, the 1004\n'
  printf '# abort trace (xnu_live_sleh_upc x1), the 1003 stall instrument and the 1002 six-word save set\n'
  printf '# asserted present (regressions).\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1015b-globalfix;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"