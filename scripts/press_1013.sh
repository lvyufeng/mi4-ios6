#!/usr/bin/env bash
# ====================================================================================================
# press_1013.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1012.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1013 = 1012's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC, SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS,
# ABT_SPAN, ABT_FAR, ABT_PC, ABT_PTE, VME_PROT and PMP_ENTER stay 1) plus ONE new switch
# STAGE90_XNU_PTE_WITNESS.  1013 is a MEASUREMENT rung (no kernel behaviour change): it reads the ONE
# cell the fault is actually about - THE VALUE OF THE LEAF the pmap writes, and the LOCATION the MMU's
# own walk finds it at.
#
# WHY 1013 FOLLOWS 1012.  1012 MEASURED the apex BY VALUE (records/1012-press-read.txt, arm
# armed-d13-ffdc8ba1): every TAIL row (ordinal >= 0x400) carries `_pmpprot=0x3` (READ|WRITE) AND
# `_pmppmap & 0xFFFFFF00` (0x81a86000) == `_pmpttbr & 0xFFFFFF00` - so the write ARRIVES at
# `pmap_enter_options` and the pmap writes the SAME L1 table the MMU walks.  strip-above-the-apex is
# REFUTED BY MEASUREMENT.  Yet the USER store still permission-faults (`fsr=0x80f`, FS=15 level-2,
# WnR=1) ~2.1 M times.  Two shapes survive: (c1) the apex VA keying (`g_abtpte_pmpva`) is not the
# storm's published `far` - a MEASUREMENT defect; (c2) a SECOND writer re-ROs the leaf, or the PTE
# `pmap_pte` returns is not the slot the MMU walks.  1013 reads the LEAF itself and the location the
# MMU finds it at, which is what tells (c1) from (c2).
#
# THE 1013 CHANGE IS SOURCE-ONLY, across TWO files, with NO new --wrap and NO tree edit (it rides
# 1012's `--wrap=pmap_enter_options` and the existing `--wrap=sleh_abort`):
#   (W-APEX) `entry_note_abt_wleaf(va, pmap, pa, fault_type)` in src/entry/entry_stubs.c, called from
#            `__wrap_pmap_enter_options` (src/entry/entry_trace.c) AFTER `__real_...` returns.  Walks
#            the PMAP'S OWN table from `pm_l1_phys` (re-read INSIDE the recorder, so a `pmap_expand`
#            inside the enter cannot make it read a RETIRED table) with Apple's own index math
#            (`pmap.c:1786-1824`, `tte_offset`/`pte_offset` `pmap.h:198-199`) and publishes the L1
#            descriptor, the L2 table base, THE LEAF, its APX bit, the PA cross-check, `_wwnoproof`
#            (1 = `pm_l1_phys` is NOT the live TTBR0 base, so the table read is not provably the live
#            one - a NULL CONTROL), and THE PROVENANCE CELLS `_wwva`/`_wwft`/`_wwpatbl` (the enter's
#            OWN `va`, `fault_type` and "the entered page is a page-TABLE page").
#            KEYED ON THE CURRENT FAULT: `entry_note_abt_cur(far_)` is called from `__wrap_sleh_abort`
#            BEFORE `__real_sleh_abort`, because the enter W-APEX observes happens INSIDE that handler.
#            1012's key (`_pmppva`) is written AFTER the handler returns, so it names the PREVIOUS
#            abort's far - the provenance defect this rung must not inherit
#            (docs/1012-probe-keying.md §1.4/§1.6).
#   (W-WALK) `entry_note_abt_walk(va, fsr)` in entry_stubs.c, called from `__wrap_sleh_abort` right
#            beside the 1010 `entry_note_abt_pte(far_)`.  Deps on the SAME walker
#            `stage90_read_l2_pte` (a second walker is how 1010 and 1010b each produced a defective
#            reading - one wrong mask each).  Publishes the location tag, the L1/L2-base/leaf, `fsr`,
#            `_wkapex` = "my leaf and the (A) walk's OWN recorded leaf agree" (the MEASUREMENT that
#            settles 1012's `ptel2=0` HARD anomaly instead of an inference), and THE DOUBLE-READ
#            (`_wkleaf2` + `_wkstable`): the L2 word is read a SECOND time after a `dsb`, so a race with
#            a concurrent table write is distinguishable from a real absent leaf.
#   (W-SYNC) `entry_note_abt_wsync(void)` in entry_stubs.c, called from `__wrap_sleh_abort` AFTER both
#            the (A) and W-WALK calls.  The CONTROL at a known-good mapped VA
#            (`gPhysBase + 0x00080000`, the same window `stage90_read_l2_pte` bounds its loads with).
#            If the control reads a correct writable leaf and the storm VA reads a read-only/absent
#            one the witness is sound; if the control is itself wrong, 1013 reports a MEASUREMENT
#            defect and concludes NOTHING.
# All four gated `#if defined(STAGE90_XNU_ABT_FAR) && ... && defined(STAGE90_XNU_PMP_ENTER) &&
# STAGE90_XNU_PMP_ENTER && defined(STAGE90_XNU_PTE_WITNESS) && STAGE90_XNU_PTE_WITNESS` so OFF is
# byte-identical (1012's arm stays reproducible).  STAGE90_XNU_PTE_WITNESS is in NEITHER $EXTRA NOR the
# exported pool defines: only the TWO entry-toolchain objects read it.  build_entry.sh REFUSES
# PTE_WITNESS=1 unless ABT_FAR=1 AND ABT_PC=1 AND ABT_PTE=1 AND PMP_ENTER=1.  Design: docs/1013-design.md.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible.  RESIDENT rung
# (POST_END_TICKS=0): the primary channel is /proc/last_kmsg, read by scripts/recover_last_kmsg.sh
# after the device comes back (step 4 - a plain boot of the FLASHED ROM is the carrier, NEVER
# `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1013 PRESS PRE-REGISTRATION
# ====================================================================================================
# SWITCH SET.  1012's verbatim (STAGE90_XNU_ABT_PC=1, ABT_FAR=1, ABT_SPAN=1, FREEZE_WITNESS=1,
# CADENCE=1, STALL=1, DYLD_WITNESS=1, SHREG_PROBE=1, SHREG_WIRE=1, SHREG_GEOM=1, RESIDENT=1, USVC=1,
# SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1, USB ladder PROBE/DEV/ENUM/STREAM=1, ABT_PTE=1, VME_PROT=1,
# PMP_ENTER=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0).  NEW switch
# STAGE90_XNU_PTE_WITNESS=1 (THE LEAF-VALUE WITNESS).  CADENCE=1, STALL=1, USVC=1,
# SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1, FREEZE_WITNESS=1, ABT_SPAN=1, ABT_FAR=1, ABT_PC=1, ABT_PTE=1,
# VME_PROT=1, PMP_ENTER=1 AND PTE_WITNESS=1 must ALL be in the record.
#
# (0) THE 1013 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the EIGHTY-SIX (86)
#     `xnu_live_abt_` key literals (1012's 49: the 6 span, the 5 far, the 7 pc, the 12 pte/vmf, the 6
#     1010c walk-window, the 7 1011 vme+ptethr, the 6 1012 pmp - PLUS 1013's 35: 11 W-APEX
#     wwn/wwl1/wwl2pa/wwleaf/wwapx/wwnoproof/wwpa/wwpaok/wwva/wwft/wwpatbl + wwnx, 12 W-WALK
#     wkn/wk/wktb/wkl1/wkl2pa/wkleaf/wkleaf2/wkstable/wkapx/wkoow/wkfsr/wkapex + wknx, 9 W-SYNC
#     wcnt/wcva/wck/wctb/wcl1/wcl2pa/wcleaf/wcapx/wcoow, PLUS the 2 raw TTBR words
#     wctb0/wctb1) embedded in out/stage90/xnu_arm_entry.elf, AND
#     `entry_note_abt_wleaf`/`entry_note_abt_walk`/`entry_note_abt_wsync`/`entry_note_abt_cur` a DEFINED
#     text symbol each (a generated stand-in would show 0), AND exactly ONE `bl` to each recorder
#     ([[mi4-hooks-live-at-calls]]), AND the W-APEX one INSIDE `__wrap_pmap_enter_options` while
#     `entry_note_abt_walk` is NOT in it (W-WALK's recording belongs to the ABORT path), AND that
#     wrapper ALSO calling the real `pmap_enter_options` body (the §8.1 pass-through rule:
#     `--wrap=pmap_enter_options` renames `__real_pmap_enter_options` -> `pmap_enter_options` for the
#     linked reference, so the disassembly has `bl <pmap_enter_options>`), exactly as build_1013.sh's
#     delta clause asserts.  `entry_note_abt_cur(far_)` must be called BEFORE `__real_sleh_abort` (the
#     provenance ordering - the enter W-APEX observes happens INSIDE that handler).  If the change is
#     NOT in the image, NOTHING below may be read as a test - rebuild.
# (0a) THE 1012 APEX (PMP_ENTER) IS A REGRESSION (this rung's FRONTIER INPUT - W-APEX rides its
#     `--wrap`): the six pmp key literals AND `entry_note_abt_pmp`/`__wrap_pmap_enter_options` DEFINED
#     text symbols AND exactly ONE `b|bl <entry_note_abt_pmp>` inside `__wrap_pmap_enter_options` must
#     STILL be present.
# (0b) THE 1011 (E')/(A) IDENTITY IS A REGRESSION: the seven vme+ptethr key literals (inside the (0)
#     EIGHTY-SIX (86)) AND `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked` DEFINED text symbols
#     AND exactly ONE `b|bl <entry_note_abt_vme>` inside `__wrap_vm_map_lookup_locked` must STILL be
#     present, plus the (A) STAGE90_ACT_MAP immediate (956).
# (0c) THE 1010c L2-DESCRIPTOR / WALK-WINDOW IS A REGRESSION: the six walk-window literals AND the
#     THREE symbols `entry_note_abt_pte` / `entry_note_abt_vmf` / `__wrap_vm_fault`, with exactly ONE
#     `b|bl <entry_note_abt_pte>` INSIDE `__wrap_sleh_abort` (a TAIL `b` is the expected spelling).
#     NOTE: 1013 carries the FIXED walk (`entry_ttbr_base`), and W-WALK now REUSES that walker.
# (0d) THE 1009/1008/1007/1006/1005/1004/1002 WITNESSES ARE REGRESSIONS: the pc/far/span key literals,
#     `entry_note_abt_pc`/`_far`/`_span` DEFINED T/t with exactly ONE `b|bl` each inside
#     `__wrap_sleh_abort`; the 1008 §4.3 fix (`xnu_arm_entry_stubs.o` has NO `U entry_counter`); the
#     six `xnu_live_freeze_` + two `xnu_live_frzbase_` literals and the two freeze symbols; the four
#     `xnu_live_cadence_` literals and the four cadence symbols; `xnu_live_sleh_upc` x1; and the 1002
#     r3-save fix (locore_fleh_swi's six-word window, four-word x0).
# (F) THE FRONTIER UNDER TEST - WHAT LEAF THE PMAP ACTUALLY WRITES, AND WHERE THE MMU FINDS IT.  1012
#     MEASURED the apex (`_pmpprot=0x3`, `_pmppmap & 0xFFFFFF00 == _pmpttbr & 0xFFFFFF00`) and REFUTED
#     strip-above-the-apex BY MEASUREMENT; the leaf's VALUE was never read.  THE DECISIVE CELLS are
#     `_wwleaf`/`_wwapx` (the leaf the pmap wrote, and its APX bit: 1 = user READ-ONLY) against
#     `_wkleaf`/`_wkapx` and the LOCATION `_wkl2pa` (the L2 table the MMU's own walk reaches).
#     THE READING.  Pair each row to the pc/far rows by KEY EQUALITY (`wwn` == `wkn` == `pcn` == `farn`
#     at a paired row - the recorders advance one per abort).  THE HEADLINE is decided from the TAIL
#     rows' OWN values (ordinals >= 0x400 on the powers-of-two ladder), NEVER on the whole-run
#     change-counts `_wwnx`/`_wknx` (the 1008/1009 measurement defect - do not repeat it; the ladder
#     rule is `n <= 4 || n & (n-1) == 0`, the SAME one (A)/(B)/(E')/[PMP] use).  THE TABLE
#     (docs/1013-design.md §3):
#       V0  THE PROVENANCE GATE, applied BEFORE V1-V3 (the 1012 apex defect this rung must not inherit:
#           docs/1012-probe-keying.md §1.4/§1.6).  EVERY tail W-APEX row must carry `_wwva` == the
#           storm's `far` AND `_wwft` == 0x3 (READ|WRITE) AND `_wwpatbl` == 0 (the entered page is NOT
#           a page-TABLE page).  A row failing ANY of the three is a NULL CONTROL, NOT a "write reaches
#           the apex for the faulting VA" reading.  If EVERY tail row fails =>
#           APEX_OBSERVED_A_TABLE_PAGE (or APEX_WRONG_VA / APEX_WRONG_FAULT_TYPE): THE 1012 CELL'S REAL
#           SHAPE - every 1012 apex row carried `pa=0x8421a` (PA 0x8421a000), consistent with a
#           page-table-page install, NOT the storm's own small page.  REPORT it; do NOT conclude
#           V1/V2/V3 from a table-page row.  V0 short-circuits V1-V3 exactly as the control does.
#       V1  W-APEX leaf APX=0 (WRITABLE) at W-APEX's own location AND `_wkl2pa == _wwl2pa` =>
#           LEAF_WRITABLE_STILL_FAULTS: the leaf IS writable in the table the pmap wrote and the MMU
#           walks, yet the store still faults => a STALE TLB / a second table the TLB holds / the flush
#           did not reach this CPU.  Next rung instruments pmap_flush_tlbs + the ASID at the fault.
#       V2  W-APEX leaf APX=1 (READ-ONLY) at W-APEX's own location AND `_wkl2pa == _wwl2pa` =>
#           F1a_CONFIRMED_LEAF_DEFECT: the pmap's write did NOT stick, or a SECOND writer re-RO'd the
#           leaf.  Next rung instruments the second writer (pmap_page_protect etc.).
#       V3  any readable W-APEX leaf AND `_wkl2pa != _wwl2pa` => IDENTITY_FAULT_L2: the apex writes one
#           L2 and the MMU walks another.  (1012 measured the L1 bases EQUAL, so the divergence is at
#           the L2 - a pmap_expand hoist / a pm_l1_virt vs pm_l1_phys L2 mismatch.)
#       V4  W-APEX present but `_wwnoproof=1` (`pm_l1_phys` != the live TTBR0 base at that row) =>
#           APEX_NOT_PROOF: a by-value CONTRADICTION with 1012; REPORT it, do not conclude from it.
#       V5  W-APEX absent (`_wwn==0`) yet the storm ran => NO_APEX_WITNESS: the enter never reached the
#           wrapper; fall back to the tree instrument.
#       V6  W-APEX leaf absent/0 while `_wwl1` is a COARSE descriptor => HARD (stale 0): a present L2
#           with no leaf is impossible for a correct walk - a MEASUREMENT defect, never a value.
#       W-SYNC control wrong (`_wcoow=1` OR `_wck=0` OR `_wcapx != 0`) => WITNESS_METHOD_BROKEN: the
#           witness method is broken on this device (or the managed window is TTBR1 territory, the
#           EXPECTED null-control outcome).  Report the measurement defect.  DO NOT CONCLUDE.
#     ORDER OF DECISION: the W-SYNC control is read FIRST (and short-circuits V1-V3 into
#     WITNESS_METHOD_BROKEN), then the V0 provenance gate (short-circuiting V1-V3 into APEX_*), then
#     V1-V6.  W-WALK also publishes `_wkapex` = "my leaf and the (A) walk's OWN recorded leaf agree" -
#     the MEASUREMENT that settles 1012's `ptel2=0` HARD anomaly; `_wkapex=0` => at least one walk is
#     wrong => the leaf is a null control, never a silent re-attribution - AND THE DOUBLE-READ
#     `_wkleaf2`/`_wkstable`: the L2 word read a SECOND time after a `dsb`.  `_wkstable=1` => the two
#     reads AGREE, so the value is STABLE and a race with a concurrent table write is DEAD as an
#     explanation; `_wkstable=0` => the table moved between the reads and the leaf is a NULL CONTROL.
#     PA CROSS-CHECK (adjudication, not a verdict cell): `_wwpa` is the PA page number the enter named
#     and `_wwpaok` = 1 iff the leaf's own address field (`bits[31:12]`) maps that page (`pa << 12`).  A
#     0 means the walk read a leaf for a DIFFERENT VA or the table moved.
#     This is a pure MEASUREMENT: the run is EXPECTED to still freeze (same (b2) shape as 1012, ~24.39
#     s) - the leaf's identity is the finding.
#     FALSIFIERS (design §5 + the ladder/hook classes): (a) NO `xnu_live_abt_w*` keys at all while
#     `xnu_live_abt_n` is present => the PTE_WITNESS half did not reach the image (rebuild); (b)
#     `xnu_live_abt_wwn` GAPS from `xnu_live_abt_n` at a paired row => the W-APEX recorder did not run
#     once per abort - an INSTRUMENT failure; (c) a recorder present but its ONE call site or the
#     wrapper's pass-through `bl <pmap_enter_options>` absent => rebuild; (d) a NON-MONOTONE
#     `xnu_live_abt_wwn` => a MEASUREMENT defect; (e) `_wwnx` > 1 with mixed `_wwleaf` at the tail =>
#     the leaf CYCLES (decide by the TAIL rows, not this count); (f) death ELSEWHERE, later => the
#     frontier ADVANCED - record by value.
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np ret=12`,
#     the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the whole capture
#     must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: ...`, BOUNDED (max 8).  EXPECTED >= 1.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1013_capture.py carries the gate: its [0] DEATH GATE + the kept
#     1004..1012 clauses).  A leaf verdict is a READING taken from a live image, so it counts only if
#     the boot reached the print site.
# (5) THE 1006 FREEZE WITNESS (kept a regression).  1013 expects the SAME silence.
# (6) THE 1005 CADENCE WITNESS (kept a regression).  1013 expects the SAME (b2) shape.
# (7) THE ABORT TRACE (1004, kept).  `far` REPEATS = LOOP; strictly ADVANCES = WALK.
# (8) THE USER-SVC FRONTIER (kept; keys xnu_live_usvc_*).  `_pid` = 1; `_pc` a user VA.
# (9) THE PET (kept).  If 1013 lives past ~14.5 s with the read door in use, `xnu_live_wdt_pet_n` MUST
#     appear.
# (10) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
#     thinning ordinal stays 64.  The sleh caps stay RAISED to 512 under 1004's switch.
# (11) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): the (0)/(F) list above.  The FROZEN
#     REGRESSION GUARDS - the geometry line exactly once, `IOS7LAB cache mapping` x4, the shared region
#     x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0` - are re-verified
#     only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent; (c) the log ends
# with a capped key and no direct key after it = rebuild; (d) the log ends in a direct record = that
# record's moment IS the frontier finding - read the last direct keys (abt-wsync, abt-walk, abt-wleaf,
# then abt-pmp, then abt-pte, ...) as the frontier, and classify by the V1..V6 table from the tail
# `_wwleaf`/`_wwapx`/`_wkl2pa`/`_wkapex` and the W-SYNC control; (e) the (0) change is not in the image
# = the witness is not in the image - rebuild, nothing else is a test.
# ====================================================================================================
if ! grep -q '^# 1013 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1013 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003/1004/1005/1006/1007/1008/"
  echo "          1009/1010/1011/1012 pattern: switch set / THE POINTS / failure signatures / falsification /"
  echo "          regression guards). The build side: scripts/build_1013.sh + scripts/park_1013.sh + the"
  echo "          1013 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-ffdc8ba1 ]] && {
  echo "REFUSING: out/stage90 holds 1012's arm (ffdc8ba1) - the PRESSED predecessor (VERDICT: the PTE-build"
  echo "apex is MEASURED - every tail row carries _pmpprot=0x3 (READ|WRITE) AND _pmppmap & 0xFFFFFF00"
  echo "(0x81a86000) == _pmpttbr & 0xFFFFFF00, so the write ARRIVES at pmap_enter_options and the pmap"
  echo "writes the SAME L1 table the MMU walks; strip-above-the-apex is REFUTED BY MEASUREMENT.  The"
  echo "leaf's VALUE was still never read - that is 1013's question; adjudicated in"
  echo "records/1012-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce83837 ]] && {
  echo "REFUSING: out/stage90 holds 1011's arm (fce83837) - the PRESSED predecessor (VERDICT: the F1a/F1b"
  echo "DECISION is MEASURED - F1a.  The (E') tail rows carry vmekr=0 (KERN_SUCCESS) with vmeprot=0x3"
  echo "(READ|WRITE) on vmefault=0x3 (a WRITE fault) at vmeva=0x61000, so vm_map_lookup_locked's out_prot"
  echo "GRANTS write and its refusal arm (vm_map.c:9087) did not fire - F1b is FALSIFIED BY VALUE; the write"
  echo "is LOST between prot and the PTE.  The (A) walk's TTBR0 base mask was WRONG (0xffffc000 vs the"
  echo "port's TTBCR.N=2 alignment 0xfffff000) so the L2 leaf has been unreadable since 1010b - fixed in"
  echo "entry_ttbr_base (07914cf); adjudicated in records/1011-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-002b0683 ]] && {
  echo "REFUSING: out/stage90 holds 1010c's arm (002b0683) - the PRESSED predecessor (VERDICT: the walk is"
  echo "WINDOW-BOUNDED and reaches an OUT-OF-WINDOW L2 table; with the mask fixed to 0xffffc000 it reads the"
  echo "TRUE L1 base 0x81a84000, but the storm's L1 slot holds a SECTION descriptor ptel1=0xfeedface"
  echo "(bits[1:0]=0b10), so the walk returns early (entry_stubs.c:2291, (l1e&3)!=1) and the L2 LEAF (and"
  echo "its APX bit) is NEVER read - F1a/F1b UNDECIDABLE; adjudicated in records/1010c-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-64391319 ]] && {
  echo "REFUSING: out/stage90 holds 1010b's arm (64391319) - the PRESSED predecessor (VERDICT: the TTBR0"
  echo "mask fix is CORRECT but it exposed a SECOND defect in the SAME walk - the corrected base reads the"
  echo "TRUE L1, whose L2 table is at PA 0x46300000, BELOW the entry image's identity window"
  echo "[0x80000000,0x9e400000); the walk dereferences it with NO window bound, so the L2 leaf load faults"
  echo "FS=5 (pc=0x80002644 = its own ldr) and the run DIES at launchd-start BEFORE the storm; the leaf is"
  echo "STILL unread; adjudicated in records/1010b-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-0f39207c ]] && {
  echo "REFUSING: out/stage90 holds 1010's arm (0f39207c) - the PRESSED predecessor (VERDICT: the storm"
  echo "reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail (impossible under"
  echo "fsr=0x80f, FS=15 level-2 permission fault) because the TTBR0 base was masked with the L1->L2 mask"
  echo "0xfffffc00 instead of the base mask 0xffffc000; adjudicated in records/1010-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-24997fc5 ]] && {
  echo "REFUSING: out/stage90 holds 1009's arm (24997fc5) - the PRESSED predecessor (frontier: the storm"
  echo "is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x0000080f ="
  echo "WnR=1|FS=0xf, cpsr=0x60000030, far=0x9a000 a LOW user VA, constant from ordinal 0x400 through"
  echo "0x200000 - the handler chose the SILENT retry branch, so vm_fault returned KERN_SUCCESS||"
  echo "KERN_ABORTED; adjudicated in records/1009-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-0343ef45 ]] && {
  echo "REFUSING: out/stage90 holds 1008's arm (0343ef45) - the PRESSED predecessor (frontier: the storm"
  echo "is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort, from ordinal 0x400 through"
  echo "0x200000 - ~2.09 M retries of one fault address, riding the ~18.7 s handler call; adjudicated in"
  echo "records/1008-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-88074ab3 ]] && {
  echo "REFUSING: out/stage90 holds 1007's arm (88074ab3) - the PRESSED predecessor (frontier: the"
  echo "~24.4 s freeze is INSIDE ONE sleh_abort - maxcall 18.735 s = 0.768 x M - riding a FAULT STORM"
  echo "of ~2.1 M aborts whose fault ADDRESS is in no capture; adjudicated in"
  echo "records/1007-press-read.txt). Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-31bdebf3 ]] && {
  echo "REFUSING: out/stage90 holds 1006's arm (31bdebf3) - the PREVIOUS predecessor (frontier: the"
  echo "~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered;"
  echo "adjudicated in records/1006-press-read.txt). Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-ff937225 ]] && {
  echo "REFUSING: out/stage90 holds 1005's arm (ff937225) - the PRESSED predecessor (frontier: the STALL"
  echo "is (b2) a whole-interrupt-path freeze ~24.4 s; adjudicated in records/1005-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-582a19c8 ]] && {
  echo "REFUSING: out/stage90 holds 1004's arm (582a19c8) - the PRESSED predecessor (frontier: the"
  echo "STALL after dyld first touches a shared-cache page; adjudicated in records/1004-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - already pressed 2026-10-13 (pid 1 in the"
  echo "kernel's data-abort return path; adjudicated in records/1003-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt). Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1013 (scripts/build_1013.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1013 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1013 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip, 1003's stall + 1004's abort-trace instrument + 1005's cadence witness + 1006's freeze witness + 1007's abort-span witness + 1008's abort-fault identity + 1009's faulting-pc identity + 1010c's L2-descriptor + 1011's (E-prime) entry-protection + (A) thread-L1-base + 1012's PTE-build apex AND 1013's LEAF-VALUE WITNESS =="
while read -r key why; do
  grep -q "^${key}=1" out/stage90/xnu_arm_entry-config.txt || {
    echo "REFUSING: record has no ${key}=1 - ${why}"; exit 1; }
done <<'EOF'
STAGE90_XNU_USB_PROBE the USB ladder stage 1 (probe)
STAGE90_XNU_USB_DEV the USB device stage (the reader below would capture an empty file)
STAGE90_XNU_USB_ENUM the USB enumeration stage
STAGE90_XNU_USB_STREAM the USB streaming stage
STAGE90_XNU_DYLD_WITNESS the message/cacheBase readings
STAGE90_XNU_SHREG_PROBE the IOS7SHREG ladder lines
STAGE90_XNU_SHREG_WIRE the IOS7LAB cache mapping lines (997's reading)
STAGE90_XNU_SHREG_GEOM the geometry/stack half (kept from 998/999; G1 held)
STAGE90_XNU_RESIDENT the WDT door feed's gate AND the tick probes (999's, kept)
STAGE90_XNU_USVC the SVC frontier hook - 1002's r3-save fix lives INSIDE it (must stay on)
STAGE90_XNU_SUBMAP_PMAP_SKIP the is_submap guard in vm_map_enter's tail (1001's; kept)
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004/1005/1006/1007/1008/1009/1010/1011)
STAGE90_XNU_ABORT_TRACE the abort trace - raised sleh caps + the frame's user pc (1004's; kept verbatim)
STAGE90_XNU_CADENCE the cadence witness - a timer-independent (syscall) sample + the all-interrupt counter (1005's; kept verbatim)
STAGE90_XNU_FREEZE_WITNESS the idle-path freeze witness - the dwell sampler + the GIC/CPSR triple (1006's; kept verbatim)
STAGE90_XNU_ABT_SPAN the abort-span witness - the gap + handler duration per sleh_abort (1007's; kept verbatim)
STAGE90_XNU_ABT_FAR the abort-fault identity - the fault ADDRESS + TYPE of the storm's aborts on the DIRECT channel (1008's; kept verbatim)
STAGE90_XNU_ABT_PC the faulting-pc identity - WHICH instruction faults + the DFSR + the change-counts on the DIRECT channel (1009's; kept verbatim)
STAGE90_XNU_ABT_PTE the L2-descriptor + vm_fault-return identity - walks TTBR0 to the L2 small-page descriptor for the fault VA and reads what vm_fault returned (1010c's; kept verbatim)
STAGE90_XNU_VME_PROT the vm_map_entry effective protection + the faulting thread's own L1 base - the (E') __wrap_vm_map_lookup_locked publishes out_prot; the (A) reader publishes pm_l1_phys (1011's; kept verbatim)
STAGE90_XNU_PMP_ENTER the PTE-build apex - the `prot` handed to pmap_enter_options via --wrap=pmap_enter_options (1012's; kept verbatim)
STAGE90_XNU_PTE_WITNESS the LEAF-VALUE WITNESS - the leaf the pmap writes (W-APEX) + the location the MMU walks to (W-WALK) + a known-good CONTROL (W-SYNC) (1013's; a pure measurement rung)
EOF
echo "   (record carries 1012's verbatim switch set PLUS the new STAGE90_XNU_PTE_WITNESS; USVC, SUBMAP_PMAP_SKIP, STALL, ABORT_TRACE, CADENCE, FREEZE_WITNESS, ABT_SPAN, ABT_FAR, ABT_PC, ABT_PTE, VME_PROT and PMP_ENTER all on)"

echo "== 2-0. the 1013 LEAF-VALUE WITNESS in the image (read by value BEFORE the press) + the 1012 PTE-build apex, the 1011 (E')/(A), the 1010c L2-descriptor/walk-window, the 1009 pc identity, the 1008 far identity, the 1007 span witness, the 1006 freeze witness, the 1005 cadence witness, the 1004 abort trace and the 1002 r3-save fix as regressions =="
# (0) THE 1013 CHANGE IS IN THE IMAGE: the EIGHTY-SIX (86) `xnu_live_abt_` key literals embedded
#     (1012's 49 + 1013's 35 witness keys incl. the TTBR1 label _wctb and the 1013b VA cell _wkva
#     + the 2 raw TTBR words
#     _wctb0/_wctb1) AND the four witness recorders DEFINED text symbols
#     AND exactly ONE `bl <entry_note_abt_pmp>` INSIDE `__wrap_pmap_enter_options`, which ALSO calls the
#     real `pmap_enter_options` body ([[mi4-hooks-live-at-calls]] - the wrapper MUST be a pass-through).
_abt_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_abt_' || true)
echo "   (0) xnu_live_abt_* literals x$_abt_lit (want 86: 1012's 49 + 1013's 35 witness keys + the 2 raw TTBR words)"
[[ "$_abt_lit" == 86 ]] || {
  echo "REFUSING: the 1013 witness store (or the 1012 pmp, the 1011 (E')/(A) and the 1010c L2-descriptor + walk-window stores) is NOT in the image (literals x$_abt_lit want 86) - rebuild"; exit 1; }
# NOTE: `nm | grep -q` under `set -o pipefail` is a trap - `grep -q` exits on the first match, SIGPIPEs
# `nm`, and the pipeline's status becomes 141, vetoing a match that exists (the 487/998 SIGPIPE lesson).
# Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_abt_nm=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf)
for _abt_1012sym in entry_note_abt_pmp __wrap_pmap_enter_options; do
  grep -qE "^[0-9a-f]+ [Tt] $_abt_1012sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_abt_1012sym is not a DEFINED text symbol in the image - the 1012 apex instrument is gone (an object did not rebuild or a stand-in was linked) - rebuild"; exit 1; }
done
echo "   (0) entry_note_abt_pmp / __wrap_pmap_enter_options are DEFINED T/t in the image"
# The wrapper's PASS-THROUGH: `--wrap=pmap_enter_options` renames `__real_pmap_enter_options` ->
# `pmap_enter_options` for the linked reference, so the disassembly has `bl <pmap_enter_options>` (the
# real body), NEVER `<__real_pmap_enter_options>` (MEASURED - the same reader fix 1010's vm_fault block
# carries; grepping for `__real_...` false-refuses a CORRECT image).
_pmpw=$(awk '$3=="__wrap_pmap_enter_options"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_pmap_enter_options is not in the image - the apex wrapper is gone"; exit 1; }
_pmpwx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_pmpw#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_pmpwb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_pmpw" --stop-address="${_pmpwx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <pmap_enter_options>' <<<"$_pmpwb" || {
  echo "REFUSING: __wrap_pmap_enter_options does not call the real pmap_enter_options body - it is not a pass-through (docs/1012-design.md §4/§2) - rebuild"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_pmp>' <<<"$_pmpwb" || {
  echo "REFUSING: __wrap_pmap_enter_options does not call entry_note_abt_pmp - the apex record never fires - rebuild"; exit 1; }
_pmpw_all=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_pmp>' || true)
echo "   (0) __wrap_pmap_enter_options calls the real pmap_enter_options AND entry_note_abt_pmp; image-wide entry_note_abt_pmp call sites: $_pmpw_all (want 1)"
[[ "$_pmpw_all" == 1 ]] || {
  echo "REFUSING: entry_note_abt_pmp does not have exactly one call site (0 = the apex call did not reach the image; >1 = a second site) - rebuild"; exit 1; }
_wke_all=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_wleaf>' || true)
for _wk_sym in entry_note_abt_wleaf entry_note_abt_walk entry_note_abt_wsync entry_note_abt_cur; do
  grep -qE "^[0-9a-f]+ [Tt] $_wk_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_wk_sym is not a DEFINED text symbol in the image - the 1013 witness is gone (an object did not rebuild or a stand-in was linked) - rebuild"; exit 1; }
  _wk_c=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <'"$_wk_sym"'>' || true)
  [[ "$_wk_c" == 1 ]] || {
    echo "REFUSING: the image carries $_wk_c call site(s) to $_wk_sym (want 1) - the 1013 witness is not wired - rebuild"; exit 1; }
done
# The W-APEX call must be INSIDE __wrap_pmap_enter_options, and W-WALK must NOT be
# ([[mi4-hooks-live-at-calls]] - W-WALK's recording belongs to the ABORT path, never the enter path).
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_wleaf>' <<<"$_pmpwb" || {
  echo "REFUSING: __wrap_pmap_enter_options does not call entry_note_abt_wleaf - the W-APEX leaf is never read - rebuild"; exit 1; }
if grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_walk>' <<<"$_pmpwb"; then
  echo "REFUSING: __wrap_pmap_enter_options calls entry_note_abt_walk - W-WALK's recording was moved off the abort path - rebuild"; exit 1; fi
echo "   (0) the four 1013 witness recorders are DEFINED T/t with ONE call site each; the W-APEX call is inside __wrap_pmap_enter_options and W-WALK is NOT (image-wide entry_note_abt_wleaf call sites: $_wke_all)"
# (0x) THE 1011 (E')/(A) IDENTITY IS A REGRESSION (the FRONTIER INPUT the apex cell is read against):
#      `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked` DEFINED T/t, its PASS-THROUGH calling the real
#      `vm_map_lookup_locked`, exactly ONE `bl <entry_note_abt_vme>` (the §8.1 rule: the wrapper MUST
#      return the real lookup's value on the non-abort case).  Its seven key literals are inside the (0) 49.
for _vme_11sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_vme_11sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_vme_11sym is not a DEFINED text symbol in the image - the 1011 (E') instrument regressed (1012 keeps 1011's set verbatim) - rebuild"; exit 1; }
done
_vme_w=$(awk '$3=="__wrap_vm_map_lookup_locked"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_vm_map_lookup_locked is not in the image - the (E') wrapper is gone"; exit 1; }
_vme_wx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_vme_w#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_vme_wb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_vme_w" --stop-address="${_vme_wx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_map_lookup_locked>' <<<"$_vme_wb" || {
  echo "REFUSING: __wrap_vm_map_lookup_locked does not call the real vm_map_lookup_locked body - it is not a pass-through (docs/1011-design-v2.md §2.3) - rebuild"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' <<<"$_vme_wb" || {
  echo "REFUSING: __wrap_vm_map_lookup_locked does not call entry_note_abt_vme - the (E') record never fires - rebuild"; exit 1; }
_vme_seq=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' || true)
[[ "$_vme_seq" == 1 ]] || {
  echo "REFUSING: the image carries $_vme_seq call site(s) to entry_note_abt_vme (want 1, inside __wrap_vm_map_lookup_locked) - rebuild"; exit 1; }
echo "   (0x) the 1011 (E')/(A) witness is in the image: entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, ONE bl to entry_note_abt_vme, the wrapper calling vm_map_lookup_locked AND entry_note_abt_vme"
# The (A) reader's deref chain: entry_note_abt_pte OR its inlined static helper must carry the
# thread->map immediate (STAGE90_ACT_MAP=956).  `stage90_abt_thread_l1_base` is `static`, so the
# compiler may keep it as a symbol or inline it - scan the union of the RECORDER's region and (if it
# exists) the helper's region.  NOT __wrap_sleh_abort's region: the recorder is a SEPARATE symbol.
_pte_adr=$(awk '$3=="entry_note_abt_pte"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: entry_note_abt_pte is not in the image - the 1010c recorder is gone"; exit 1; }
_pte_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_pte_adr#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_l1_adr=$(awk '$3=="stage90_abt_thread_l1_base"{print "0x" $1; found=1} END{exit(found?0:0)}' <<<"$_abt_nm")
_regions=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="${_pte_adr}" --stop-address="${_pte_x:-0xffffffff}")
if [[ -n "$_l1_adr" ]]; then
  _l1_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_l1_adr#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
  _regions+=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_l1_adr" --stop-address="${_l1_x:-0xffffffff}")
fi
grep -qE '(#956\b|#0x3bc|movw[[:space:]]+r[0-9]+, #956)' <<<"$_regions" || {
  echo "REFUSING: neither entry_note_abt_pte nor stage90_abt_thread_l1_base carries the STAGE90_ACT_MAP immediate (956) - the 1011 (A) reader did not compile in - rebuild"; exit 1; }
echo "   (0x) the 1011 (A) STAGE90_ACT_MAP immediate (956) present (helper symbol: ${_l1_adr:-inlined})"
# (0y) THE 1010c L2-DESCRIPTOR / WALK-WINDOW IS A REGRESSION: the three 1010 symbols DEFINED T/t and the
#      ONE b|bl to entry_note_abt_pte rides __wrap_sleh_abort (its six walk-window + twelve pte/vmf
#      literals are inside the (0) 49).  NOTE: 1012 carries the FIXED walk (`entry_ttbr_base`, 07914cf).
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the image - the 1010c instrument regressed (1012 keeps 1011's set verbatim) - rebuild"; exit 1; }
done
_ab_a=$(awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_sleh_abort is not in the image - the abort path the pte witness rides is gone"; exit 1; }
_ab_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ab_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_ab_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ab_a" --stop-address="${_ab_x:-0xffffffff}")
_ab_pte=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' <<<"$_ab_body" || true)
echo "   (0y) __wrap_sleh_abort calls entry_note_abt_pte: $_ab_pte (want 1)"
[[ "$_ab_pte" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_pte exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# The vm_fault wrapper's PASS-THROUGH is the §8.1 rule made a check: it must call BOTH the real
# vm_fault body (the pass-through) AND entry_note_abt_vmf (the (B) record).
_wfa=$(awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_vm_fault is not in the image - the vm_fault-return witness is gone"; exit 1; }
_wfx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_wfb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_wfa" --stop-address="${_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_wfb" || {
  echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - it is not a pass-through (docs/1010-design.md §8.1) - rebuild"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_wfb" || {
  echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the (B) record never fires - rebuild"; exit 1; }
echo "   (0y) __wrap_vm_fault calls the real vm_fault AND entry_note_abt_vmf (the §8.1 pass-through made a check)"
# (0c) THE 1009 FAULTING-PC IDENTITY IS A REGRESSION: the `entry_note_abt_pc` DEFINED text symbol AND
#      exactly one `b|bl <entry_note_abt_pc>` inside `__wrap_sleh_abort` (its seven pc key literals are
#      inside the (0) FORTY-NINE (49)).
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the image - the 1009 recorder was dropped (1012 keeps 1011's set verbatim) - rebuild"; exit 1; }
_ab_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' <<<"$_ab_body" || true)
echo "   (0c) __wrap_sleh_abort calls entry_note_abt_pc: $_ab_pc (want 1)"
[[ "$_ab_pc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_pc exactly once (0 = the call site moved; >1 = it runs twice) - the 1009 faulting-pc identity regressed - rebuild"; exit 1; }
# (0d) THE 1008 ABORT-FAULT IDENTITY IS A REGRESSION: the `entry_note_abt_far` DEFINED text symbol AND
#      exactly one `b|bl <entry_note_abt_far>` inside `__wrap_sleh_abort`.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the image - the 1008 recorder was dropped - rebuild"; exit 1; }
_ab_fc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' <<<"$_ab_body" || true)
echo "   (0d) __wrap_sleh_abort calls entry_note_abt_far: $_ab_fc (want 1)"
[[ "$_ab_fc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_far exactly once - the 1008 abort-fault identity regressed - rebuild"; exit 1; }
# (0e) THE 1007 SPAN WITNESS IS A REGRESSION: the `entry_note_abt_span` DEFINED text symbol AND exactly
#      one `b|bl <entry_note_abt_span>` inside `__wrap_sleh_abort`.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the image - the 1007 recorder was dropped - rebuild"; exit 1; }
_ab_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$_ab_body" || true)
echo "   (0e) __wrap_sleh_abort calls entry_note_abt_span: $_ab_sc (want 1)"
[[ "$_ab_sc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_span exactly once - the 1007 span witness regressed - rebuild"; exit 1; }
# §4.3 THE ARGUMENT-PASSING FIX (1007, kept a regression): entry_stubs.o must NOT carry an UNDEFINED
# `entry_counter` (the §2.3 terminal stand-in).  If a recorder still calls entry_counter(), the stand-in
# can END THE RUN.
_ab_uc=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o | grep -cE '^[[:space:]]*U entry_counter$' || true)
echo "   (0d) xnu_arm_entry_stubs.o U entry_counter: $_ab_uc (want 0 - the §4.3 fix, kept as a regression)"
[[ "$_ab_uc" == 0 ]] || {
  echo "REFUSING: entry_stubs.o still references entry_counter (U x$_ab_uc want 0) - a recorder calls the clock directly; the §4.3 argument-passing fix regressed - rebuild"; exit 1; }

# (0f) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals + the two
#      `xnu_live_frzbase_` key literals embedded AND the two DEFINED text symbols.
_frz_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_freeze_' || true)
echo "   (0f) xnu_live_freeze_* literals x$_frz_lit (want 6: n/d/cpsr/ien/pend/ctlr)"
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the 1006 freeze store is NOT in the image (literals x$_frz_lit want 6) - 1012's set is 1011's verbatim, so it must be present - rebuild"; exit 1; }
_idl_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_frzbase_' || true)
echo "   (0f) xnu_live_frzbase_* literals x$_idl_lit (want 2: n/d)"
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the 1006 idle-baseline store is NOT in the image (literals x$_idl_lit want 2) - rebuild"; exit 1; }
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the image - the 1006 freeze witness regressed - rebuild"; exit 1; }
done
echo "   (0f) the two freeze symbols are DEFINED T/t in the image"
# (0g) THE 1005 CADENCE WITNESS IS A REGRESSION: the four key literals AND the four DEFINED symbols.
_cad_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_cadence_' || true)
echo "   (0g) xnu_live_cadence_* literals x$_cad_lit (want 4: t/sync/tick/irq)"
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the 1005 cadence store is NOT in the image (literals x$_cad_lit want 4) - 1012's set is 1011's verbatim, so it must be present - rebuild"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the image - the 1005 cadence witness regressed - rebuild"; exit 1; }
done
echo "   (0g) the four cadence symbols are DEFINED T/t in the image"
# (0h) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal x1.
_d4_upc=$(strings out/stage90/xnu_arm_entry.elf | grep -c '^xnu_live_sleh_upc$' || true)
echo "   (0h) xnu_live_sleh_upc literal x$_d4_upc (want 1)"
[[ "$_d4_upc" == 1 ]] || {
  echo "REFUSING: the 1004 abort trace is NOT in the image (upc x$_d4_upc want 1) - 1012's set is 1011's verbatim, so it must be present - rebuild"; exit 1; }
# (0i) THE 1002 r3-SAVE FIX IS A REGRESSION. The prologue of locore_fleh_swi must STILL be six-word.
_ls=$(awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: locore_fleh_swi is not in the image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
echo "   locore_fleh_swi ($_ls..${_lsx:-end}): six-word window x$_w6 (want 1), four-word x$_w4 (want 0)"
grep -B1 -A2 'bl.*<entry_note_user_svc>' <<<"$_lb" | sed 's/^/       /'
[[ "$_w6" == 1 && "$_w4" == 0 ]] || {
  echo "REFUSING: the 1002 r3-save fix REGRESSED (six-word window x$_w6 want 1, four-word x$_w4 want 0) -"
  echo "the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported, or the tree is not at the 1002 form."
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1012.sh."
  exit 1; }
echo "   (0i) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1012 PTE-build apex (prot at pmap_enter_options)"

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.** /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 1012 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1012-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is a normal outcome here, not a
# failure, so do not let `set -e` abort before the reader is stopped and its capture is reported.
set +e
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"
RUN_RC=$?
set -e
echo "   (runner exit $RUN_RC; a resident rung not returning shows as 2)"

kill "$USB_PID" 2>/dev/null || true
echo "== USB console capture: $USB_OUT =="
if [ -s "$USB_OUT" ]; then echo "   bytes read: $(wc -c < "$USB_OUT")"; else
  echo "   (empty - the ladder did not reach the idle path, or the payload stopped before it)"
fi

echo "== 4. recovery: the run's own log via /proc/last_kmsg (the carrier is the FLASHED ROM) =="
# The recovery is the same either way: the device must come back ONCE (on its own, or by the
# operator's power press) and the NEXT boot's /proc/last_kmsg holds the run as its previous.
# --read-only when Android is already up (do NOT reboot it); no flags when it sits in fastboot (the
# script reboots the installed ROM - NEVER `fastboot boot`).
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet; if it is still dark, the operator power-press is the next step, then:"
  echo "    scripts/recover_last_kmsg.sh --read-only while Android is up; do not boot anything else first)"
fi
echo "   (adjudicate the recovered capture with: python3 tools/check_1013_capture.py <capture>"
echo "    - the [0] DEATH GATE, the [ABT4] L2-descriptor/vm_fault-return section, the [PMP] apex section"
echo "    and the 1004..1012 clauses are kept as regressions; the NEW [WIT] section is 1013's own reading."
echo "    THE HEADLINE is the LEAF DECISION from the TAIL rows' OWN xnu_live_abt_wwn/wwleaf/wwapx and"
echo "    wkl2pa/wkleaf (ordinals >= 0x400), NEVER a whole-run max/change-count (the 1008/1009 measurement"
echo "    defect, not repeated).  THE CONTROL IS READ FIRST - if the W-SYNC control is wrong"
echo "    (wcoow=1 OR wck=0 OR wcapx!=0) the verdict is WITNESS_METHOD_BROKEN and NOTHING is concluded."
echo "    THE PROVENANCE GATE (V0) IS APPLIED NEXT: every tail W-APEX row must carry wwva == the storm's"
echo "    far AND wwft == 0x3 AND wwpatbl == 0; if EVERY tail row fails => APEX_OBSERVED_A_TABLE_PAGE (the"
echo "    1012 cell's real shape - every 1012 row carried pa=0x8421a, a page-table-page install) /"
echo "    APEX_WRONG_VA / APEX_WRONG_FAULT_TYPE - REPORT it, do NOT conclude V1-V3 from it."
echo "    THE WALK GATE (V0) IS APPLIED NEXT: wkstable must be 1 (the double-read AGREED); if wkstable == 0"
echo "    the L2 word MOVED between the two walks => WALK_UNSTABLE_NULL, a NULL CONTROL - V1-V3 withheld"
echo "    (the location comparison was taken over a table that does not hold still).  Then:"
echo "      V1  wwleaf APX=0 (WRITABLE) AND wkl2pa == wwl2pa  =>  LEAF_WRITABLE_STILL_FAULTS: the leaf IS"
echo "        writable in the table the pmap wrote and the MMU walks, yet the store still faults => a"
echo "        STALE TLB / a second table the TLB holds / the flush did not reach this CPU.  Next rung:"
echo "        instrument pmap_flush_tlbs + the ASID at the fault."
echo "      V2  wwleaf APX=1 (READ-ONLY) AND wkl2pa == wwl2pa  =>  F1a_CONFIRMED_LEAF_DEFECT: the pmap's"
echo "        write did NOT stick, or a SECOND writer re-RO'd the leaf.  Next rung: the second writer"
echo "        (pmap_page_protect etc.)."
echo "      V3  any readable wwleaf AND wkl2pa != wwl2pa  =>  IDENTITY_FAULT_L2: the apex writes one L2 and"
echo "        the MMU walks another (1012 measured the L1 bases EQUAL, so the divergence is at the L2)."
echo "      V4  wwn present but wwnoproof=1  =>  APEX_NOT_PROOF (a by-value contradiction with 1012 -"
echo "        report, do not conclude)."
echo "      V5  wwn == 0 yet the storm ran  =>  NO_APEX_WITNESS (fall back to the tree instrument)."
echo "      V6  wwleaf == 0 while wwl1 is COARSE  =>  HARD (stale 0): a MEASUREMENT defect, never a value."
echo "      ALSO read wkapex = 1 (W-WALK and the (A) walk agree - this settles 1012's ptel2=0 anomaly),"
echo "      wkstable = 1 (the L2 word read TWICE - the second after a dsb - gave the SAME value, so the"
echo "      leaf is real and no concurrent table write explains it; 0 => the leaf is a NULL CONTROL),"
echo "      wwpaok = 1 (the leaf maps the page the enter named), and xnu_live_stub_hit_seq==0 (the §2.3"
echo "      stand-in did not fire)."
echo "== DONE =="
