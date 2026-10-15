#!/usr/bin/env bash
# ====================================================================================================
# press_1015b.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1015.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1015b = 1015's switch set VERBATIM (PTE_WITNESS,
# PMP_ENTER, VME_PROT, ABT_PTE, ABT_PC, ABT_FAR, ABT_SPAN, FREEZE_WITNESS, CADENCE, STALL,
# ABORT_TRACE, DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC, SUBMAP_PMAP_SKIP,
# FLUSH_WITNESS and 1015's own PERM_WITNESS all stay 1) plus ONE new switch STAGE90_XNU_GLOBAL_FIX.
#
# **THIS IS THE FIRST BEHAVIOUR CHANGE SINCE 986.** 1015 was a measurement rung; 1015b EDITS THE
# KERNEL: `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c`'s `pmap_flush_tlbs` now takes the FULL
# `arm_tlb_flushID()` (TLBIALL) for a non-kernel pmap instead of the ASID-scoped
# `arm_tlb_flushID_ASID`. The whole rung is one pre-registered behavioural question, and it is the
# question 1015 could not answer from tables or from the MMU's own translation:
#
#   DID THE STALE GLOBAL TLB ENTRY GO AWAY WHEN THE FLUSH FINALLY COULD EVICT IT?
#
# WHY 1015b FOLLOWS 1015.  1015's press (arm `armed-d13-e51d14fa`, records/1015-press-read.txt;
# RESIDENT, `wdt_pets=0x61`, no panic) read **V-AT1 AT THE HARDWARE**: `_wdatpar` FAULTS
# (`par & 1 == 1`) while its null control `_wdatparpr` SUCCEEDS (`par & 1 == 0`) to PA 0x8419d000, on
# a leaf read as `_wdleaf=0x8417e1bb` (AP=0b11, APX=0, PA 0x8417e000) with `_wddom=0`, `_wddacr=1`,
# `_wdttbcr=2`, `_wdsplit=0x40000000`, `_wdctlat=0x80080268` (the control satisfied) and
# **`_leafng=0` (GLOBAL)**.  The MMU's translation for a USER WRITE of the storm VA is therefore NOT
# the leaf the walk reads - measurably, by the MMU itself.
#
# WHY A GLOBAL ENTRY SURVIVES.  Every user leaf this port emits is GLOBAL: `L2_NG_BIT 0x800`
# (`pmap.h:231`, bit 11) is **never ORed anywhere tree-wide**, and 1015 published that bit RAW as
# `xnu_live_abt_leafng` so it did not have to be decoded out of the packed leaf.  ARMv7's TLBIASID
# invalidates ONLY ASID-tagged (`nG=1`) entries; a GLOBAL entry survives it.  So `pmap_flush_tlbs`'s
# steady-state arm - which 1014 MEASURED as the one that runs (`_wfkind=2`, under the matching ASID
# `_wfasid==_wctx`) - cannot evict the very entries that shadow the newer, correct leaf.  That is H-G,
# and 1015b is its cure.
#
# THE FIX (docs/1015b-design.md §1).  `tools/stage_d13_globalfix.sh` inserts, at
# `osfmk/arm/pmap.c:1346-1349` - between Apple's `arm_tlb_flushID();` (:1347) and Apple's `else`
# (:1348) - this third arm:
#       #if defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX
#                   else if (pmap != kernel_pmap)
#                       arm_tlb_flushID();
#       #endif
# The `else if (pmap != kernel_pmap)` shape is not cosmetic.  `pmap_is_shared` is
# `pmap->pm_shared || (pmap == kernel_pmap)` (`pmap.c:1291`), so the kernel already takes the first
# arm and the added condition is FALSE for it: KERNEL SEMANTICS ARE UNCHANGED, and the widened arm
# cannot be "unreachable by construction" the way a widened `if (pmap_is_shared || pmap !=
# kernel_pmap)` would be.  With the switch OFF the three lines are not compiled at all (the `#if`
# wraps them), which is what makes the build's OFF proof a byte-identical `.text` comparison.
#
# WHAT IS DIFFERENT ABOUT THIS ARM, OPERATIONALLY: **THE KERNEL POOL OBJECT MOVES**.
# `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` is no longer 1014's. `build_entry.sh` only LINKS it, so
# the press-side check below reads the OBJECT's relocation census (the ASID call GONE inside
# `pmap_flush_tlbs`, the two full-TLBIALL calls present and in code order) AND the arm sha8 (the
# pool's `.text` is inside `xnu_arm_entry.bin`, so the arm moves even if the entry objects did not).
#
# AND THE DECLARATION: STAGE90_XNU_GLOBAL_FIX is a POOL switch that NO writer in `src/entry/` echoes
# (MEASURED x0 in `src/entry/build_entry.sh` and in `scripts/preflight_boot_check.sh`'s
# `ENTRY_CFG_KEYS` - whose converse check is a SET DIFFERENCE, so naming a key there that the record
# cannot carry would make the gate refuse every press).  The arm's declaration therefore rides
# `out/stage90/xnu_arm_entry-arm-extra.txt`, which `scripts/build_1015b.sh` writes EMPTY and
# `scripts/park_1015b.sh` appends one line to:
#     STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix
# **THE RECORD ALONE DESCRIBES 1015's KERNEL, NOT THIS ARM'S.** A reader that wants to know what
# this arm's kernel does must read that file; this script checks it before the press.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. RESIDENT rung (POST_END_TICKS=0): the primary channel is
# /proc/last_kmsg, read by scripts/recover_last_kmsg.sh after the device comes back (step 4 - a plain
# boot of the FLASHED ROM is the carrier, NEVER `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1015b PRESS PRE-REGISTRATION
# ====================================================================================================
# SWITCH SET.  1015's verbatim (STAGE90_XNU_ABT_PC=1, ABT_FAR=1, ABT_SPAN=1, FREEZE_WITNESS=1,
# CADENCE=1, STALL=1, DYLD_WITNESS=1, SHREG_PROBE=1, SHREG_WIRE=1, SHREG_GEOM=1, RESIDENT=1, USVC=1,
# SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1, USB ladder PROBE/DEV/ENUM/STREAM=1, ABT_PTE=1, VME_PROT=1,
# PMP_ENTER=1, PTE_WITNESS=1, FLUSH_WITNESS=1, PERM_WITNESS=1); CARD_COW=1, HDD_WRITE=0; RESIDENT
# rung (POST_END_TICKS=0).  NEW switch STAGE90_XNU_GLOBAL_FIX=1 (THE GLOBAL-FIX - the widened flush).
# CADENCE=1, STALL=1, USVC=1, SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1, FREEZE_WITNESS=1, ABT_SPAN=1,
# ABT_FAR=1, ABT_PC=1, ABT_PTE=1, VME_PROT=1, PMP_ENTER=1, PTE_WITNESS=1, FLUSH_WITNESS=1 AND
# PERM_WITNESS=1 must ALL be in the record.
#
# **(0g) STAGE90_XNU_GLOBAL_FIX IS *NOT* IN THE RECORD, AND THAT IS NOT A DEFECT - IT IS WHERE THE
# SWITCH LIVES.**  The record is written by `src/entry/build_entry.sh`, which does not name this
# switch (MEASURED x0), and `scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS` does not either
# (MEASURED x0) - deliberately, because that gate's converse check is a SET DIFFERENCE against the
# record and a key no writer produces would make it refuse every press.  THE DECLARATION IS THE
# DROP-IN FILE `out/stage90/xnu_arm_entry-arm-extra.txt`, which must carry exactly:
#       STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix
# **AND THE BINARY SIDE IS THE POOL OBJECT, NOT THE ENTRY KEYS.**  `out/xnu_kernel_obj_d13/
# osfmk_arm_pmap.o` must carry, inside `pmap_flush_tlbs`, EXACTLY THREE `R_ARM_MOVW_ABS_NC
# kernel_pmap` materializations.  THIS IS A MEASURED REPLACEMENT FOR A FIRST CLAUSE THAT COULD
# NOT PASS: it asked for ZERO `arm_tlb_flushID_ASID` (the ASID arm REMAINS in the text by design -
# only the non-kernel path changes, since `pmap_is_shared` is true for the kernel) and for TWO
# `arm_tlb_flushID` calls (the compiler TAIL-MERGES the two identical call sites into ONE).  The
# count that discriminates is the ADDRESS-MATERIALIZATION count: UNFIXED/Apple's object = 2
# (`pmap_is_shared` init + terminal panic), 1015b's ON object = 3 (the inserted `pmap != kernel_pmap`
# test is the third).  The inherited 1014 probe's ONE `U entry_note_abt_wf` relocation must stay
# UNDISTURBED.
#     **IF EITHER HALF IS ABSENT, STOP - NOTHING BELOW IS A TEST.**  An arm whose pool object is still
#     1015's is a run of 1015's kernel with a script claiming a behaviour change: every G-* cell read
#     off it would describe the WRONG KERNEL, and the reader's own answer (a persisting storm) would
#     look like a refutation of H-G when it is only a refutation of the missing edit.
#
# (G) THE CELL THIS RUNG EXISTS FOR - PRE-REGISTERED, AND READ BY VALUE.  `tools/check_1015b_capture.py`
#     is the adjudicator; its verdict strings are quoted here as they will be printed:
#   * G-CONFIRMED  -  H-G CONFIRMED IN BEHAVIOUR: **THE STORM STOPS.**  The reader RECOVERS the storm
#     VA from the capture instead of matching a literal (the storm's `far` moves per run: 1014 read
#     0xe6000, 1015 read 0x27000, 1011 read 0x61000 - no literal identifies it), requires the storm
#     rows ABSENT, and requires the capture to be STRUCTURALLY COMPLETE.  The TRUNCATION GUARD RUNS
#     FIRST: a truncated log is exactly what a stopped storm looks like in bytes, and /proc/last_kmsg
#     is what the carrier gives back - so "no tail" can never be read as "the storm stopped".
#   * G-REFUTED   -  H-G REFUTED IN BEHAVIOUR: the storm PERSISTS.  This is a POSITIVE measurement -
#     the reader must point at a storm ROW, never at an absence.  WHAT FOLLOWS IS NOT 1015b's FIX TRIED
#     HARDER: it is 1015's §3b passive ladder (P-X1: every input correct and it STILL faults) and then
#     H-X - the CPU's OWN state (SCTLR/PRRR, published raw as `_wdsctlr`), not another table read.
#     A refutation here is INFORMATIVE: it eliminates the last table-side explanation.
#   * G-WITHHOLD   -  the reader NAMES the exact missing or contradictory key.  NEVER a negative.  A
#     reader that cannot withhold certifies nothing ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
#   THE STOP DEFINITION, PRE-REGISTERED SO IT CANNOT BE RE-WORDED AFTER THE RUN: a storm row is a tail
#   abort row whose VA EQUALS the by-value recovered storm VA; the storm is STOPPED iff FEWER THAN
#   THREE such rows exist AND none of them is at `_pcn >= 0x4000` (the 1014/1015 storm's own band - it
#   ran to `_pcn=0x40000`).  Three rows at low ordinal is NOT a stop: it is a storm the ladder caught
#   early, which is why the band condition carries half the definition.
#   THE ATTRIBUTION GUARD - READ IT BEFORE BELIEVING A G-CONFIRMED.  1015b CHANGES KERNEL BEHAVIOUR,
#   so a stopped storm is evidence about THIS arm only if the WIDENED arm is the one the faulting pmap
#   took.  The reader therefore requires a `_wfkind=1` row (1014's W-FLUSH publishes `_wfkind`: Apple's
#   shared arm is 0, Apple's ASID arm is 2, the widened arm is 1) whose `_wfva` or `_pcfar` EQUALS the
#   recovered storm VA.  WITHOUT SUCH A ROW THE STOP IS UNATTRIBUTED, and the reader says so instead of
#   implying the fix caused it: a widened arm the storm's pmap never reached cannot have stopped
#   anything, and a different dyld / a shorter run / a different fault would produce the same absent
#   rows.  (The guard is not paranoia: 1014's own capture carries `_wfkind=1` rows on OTHER VAs, which
#   is precisely the trap.)
#   **AND 1015's AT/PAR CELL IS RE-RUN OVER THE SAME CAPTURE** so the two readings can be held
#   together: V-AT1 (the CPU's translation is not the walked leaf) + a STOPPED storm is the strongest
#   pair this rung can produce, and the reader prints both.
#
# (0) THE 1015 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the ONE HUNDRED AND
#     THIRTY-ONE (131) `xnu_live_abt_` key literals (1012's 49: the 6 span, the 5 far, the 7 pc, the
#     12 pte/vmf, the 6 1010c walk-window, the 7 1011 vme+ptethr, the 6 1012 pmp - PLUS 1013's 35: 11
#     W-APEX wwn/wwl1/wwl2pa/wwleaf/wwapx/wwnoproof/wwpa/wwpaok/wwva/wwft/wwpatbl + wwnx, 12 W-WALK
#     wkn/wk/wktb/wkl1/wkl2pa/wkleaf/wkleaf2/wkstable/wkapx/wkoow/wkfsr/wkapex + wknx, 9 W-SYNC
#     wcnt/wcva/wck/wctb/wcl1/wcl2pa/wcleaf/wcapx/wcoow, PLUS the 2 raw TTBR words wctb0/wctb1 [=86] -
#     PLUS 1014's 14: the 11 W-FLUSH wfn/wfva/wfttb/wflive/wfself/wfshared/wfncpus/wfasid/wfkind/
#     wfseen/wfnx + the 3 W-CTX wctxn/wctx/wctxva [=100] - PLUS 1015's 31: the 27 `wd*` keys
#     wdn/wdva/wddacr/wddom/wdttbcr/wdsplit/wdttbr0/wdttbr1/wdl1/wdleaf/wdap/wdapx/wdtex/wdcb/wdsmall/
#     wdpcpu/wdthrmap/wdmapok/wdseen/wdctl/wdctlok/wdempty/wdatok/wdatpar/wdatparpr/wdctlat/wdsctlr,
#     PLUS the GLOBAL-vs-ASID cell `leafng`, PLUS the 1014-§6 owed cells `wwpre`/`wwpost`/`wwmoved`)
#     embedded in out/stage90/xnu_arm_entry.elf, AND `entry_note_abt_wd` a DEFINED text symbol (a
#     generated stand-in would show 0), AND exactly ONE `b|bl` to it, INSIDE `__wrap_sleh_abort`
#     ([[mi4-hooks-live-at-calls]]; the `(b|bl)` regex, NOT `bl`-only - a void call that ends its
#     function is TAIL-called and appears as `b <sym>`), AND the 1013/1014 witnesses still wired
#     exactly as 1014's build asserted.  If the change is NOT in the image, NOTHING below may be read
#     as a test - rebuild.
# (0z) THE 1014 TLB-FLUSH WITNESS IS A REGRESSION (this rung's FRONTIER INPUT - the AT cell is read
#     AGAINST the fact that an ASID-scoped flush already ran): the FOURTEEN (14) 1014 key literals
#     AND `entry_note_abt_wf`/`entry_note_abt_wctx` DEFINED text symbols AND exactly ONE `(b|bl)` call
#     site each must STILL be present.
# (0a) THE 1013 LEAF-VALUE WITNESS IS A REGRESSION (the leaf whose nG bit 1015 publishes as
#     `_leafng`): the EIGHTY-SIX (86) 1013-era key literals AND
#     `entry_note_abt_wleaf`/`entry_note_abt_walk`/`entry_note_abt_wsync`/`entry_note_abt_cur`
#     DEFINED text symbols AND exactly ONE `bl <entry_note_abt_wleaf>` INSIDE
#     `__wrap_pmap_enter_options` (which ALSO calls the real `pmap_enter_options` body, the §8.1
#     pass-through rule) must STILL be present, with `entry_note_abt_walk` NOT in that wrapper
#     (W-WALK's recording belongs to the ABORT path).
# (0b) THE 1012 APEX (PMP_ENTER) IS A REGRESSION (W-APEX rides its `--wrap`): the six pmp key literals
#     AND `entry_note_abt_pmp`/`__wrap_pmap_enter_options` DEFINED
#     text symbols AND exactly ONE `b|bl <entry_note_abt_pmp>` inside `__wrap_pmap_enter_options` must
#     STILL be present.
# (0b) THE 1011 (E')/(A) IDENTITY IS A REGRESSION: the seven vme+ptethr key literals (inside the (0)
#     EIGHTY-SIX (86)) AND `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked` DEFINED text symbols
#     AND exactly ONE `b|bl <entry_note_abt_vme>` inside `__wrap_vm_map_lookup_locked` must STILL be
#     present, plus the (A) STAGE90_ACT_MAP immediate (956).
# (0c) THE 1010c L2-DESCRIPTOR / WALK-WINDOW IS A REGRESSION: the six walk-window literals AND the
#     THREE symbols `entry_note_abt_pte` / `entry_note_abt_vmf` / `__wrap_vm_fault`, with exactly ONE
#     `b|bl <entry_note_abt_pte>` INSIDE `__wrap_sleh_abort` (a TAIL `b` is the expected spelling).
#     NOTE: 1015 carries the FIXED walk (`entry_ttbr_base`), and W-WALK now REUSES that walker.
# (0d) THE 1009/1008/1007/1006/1005/1004/1002 WITNESSES ARE REGRESSIONS: the pc/far/span key literals,
#     `entry_note_abt_pc`/`_far`/`_span` DEFINED T/t with exactly ONE `b|bl` each inside
#     `__wrap_sleh_abort`; the 1008 §4.3 fix (`xnu_arm_entry_stubs.o` has NO `U entry_counter`); the
#     six `xnu_live_freeze_` + two `xnu_live_frzbase_` literals and the two freeze symbols; the four
#     `xnu_live_cadence_` literals and the four cadence symbols; `xnu_live_sleh_upc` x1; and the 1002
#     r3-save fix (locore_fleh_swi's six-word window, four-word x0).
# (AT) THE FRONTIER UNDER TEST - ASK THE MMU, NOT THE TABLES.  1014's F2 (the ASID-scoped flush RAN
#     under the matching ASID) is the INPUT; the H-G hypothesis (STALE_GLOBAL_TLB) is that a GLOBAL
#     translation survives TLBIASID and shadows the newer user-RW leaf.  THE DECISIVE CELLS are
#     `_wdatpar` (AT, stage-1, USER WRITE, on the storm VA - the hardware's OWN answer) against
#     `_wdatparpr` (the privileged read, the SAME VA) and `_wdctlat` (the T-CTL control, the W-SYNC
#     VA), gated by `_wdatok`.  Read on tail rows with `_wdn >= 0x400` AND `_wdva == 0xe6000` AND
#     `_wdseen=1` AND `_wdatok=1` (the SAME powers-of-two ladder rule the other rungs use -
#     `n <= 4 || n & (n-1) == 0` - NEVER a whole-run change-count: the 1008/1009 measurement defect,
#     not repeated).  THE AT TABLE (docs/1015-design.md §3a):
#       V-AT0  the control read `_wdctlat` did NOT come back `par & 1 == 0` with
#              `par & 0xfffff000 == 0x80080000`, OR `_wdctlat` is unpublished, OR `_wdatok=0` =>
#              AT_INSTRUMENT_BROKEN: WITHHOLD V-AT1..V-AT4 (and the whole passive ladder).  A missing
#              or mis-executed read can never impersonate a confirmation (the SILENCE rule).
#       V-AT1  `_wdatpar` bit0 == 1 (the USER-WRITE AT FAULTS) while `_wdatparpr` bit0 == 0 =>
#              H-G CONFIRMED AT HARDWARE: the CPU itself refuses a translation the walked leaf
#              permits (leaf ap=0b11, apx=0, domain=0), and the privileged view succeeds - the
#              translation in use is NOT the leaf the walk reads.  With `_leafng == 0` (GLOBAL) the
#              culprit is an entry surviving TLBIASID.
#       V-AT2  `_wdatpar` bit0 == 0 AND `(_wdatpar & 0xfffff000) == 0x841e9000` => the hardware
#              translates the write to EXACTLY the leaf's PA 0x841e9000 (1014's `_wwleaf=0x841e91bb`,
#              bits[31:12] = 0x841e9) with `_wdatparpr` bit0 == 0 => TRANSLATION IS CORRECT YET THE
#              STORE FAULTED: H-G REFUTED, and the FS=15 is therefore NOT produced by this
#              translation - the probe's key/ordering or the DFSR/DFAR pair describes a different
#              access (re-rank to the SCTLR cause, `_wdsctlr`).  A PROBE DEFECT, not a page-table one.
#       V-AT3  `_wdatpar` bit0 == 0 AND `(_wdatpar & 0xfffff000) != 0x841e9000` with
#              `_wdatparpr` bit0 == 0 => WRONG TRANSLATION IN THE TLB: the VA translates successfully
#              but to a PA that is not the leaf's - a TLB entry is serving a translation the tables no
#              longer hold; this NAMES THE STALE PA BY VALUE.  Same conclusion as V-AT1, from the PA
#              side.  READ THE PA AND COMPARE IT BY VALUE - a wrong PA here is a finding, not a
#              parse error.
#       V-AT4  BOTH `_wdatpar` and `_wdatparpr` fault (bit0 == 1) => BOTH VIEWS FAULT: the user-AP
#              story is not the whole story (the privileged view is refused too) - a domain/invalid
#              mechanism is in play.  RE-RANK TO THE SCTLR CAUSE before §7.
#     `_wdatok` is the leg's GATE (T-AT/T-CTL folded): 0 => NO V-AT VERDICT AT ALL.  `_wdctlat` is the
#     T-CTL NULL CONTROL - it is the AT pair aimed at the W-SYNC control VA (PA 0x80080000), NOT the
#     storm's VA, and its OWN key by the 2026-10-15 ruling: a row whose `_wdatparpr` still carries PA
#     0x80080000 is a recorder that has not made the change (one key, two definitions - withhold).
#     `_leafng` is the H-G PRECONDITION, published so the reader need not decode the packed leaf:
#     `_leafng == 0` (GLOBAL) is required for H-G; `_leafng == 1` (ASID-tagged) would KILL H-G by
#     value before any press (TLBIASID WOULD evict it).
# (PL) THE PASSIVE LADDER - same press, the residual and the null controls (docs/1015-design.md §3b).
#     On the same tail rows, withheld unless `_wdctlok=1`, `_wdseen=1` and (for the lineage cells)
#     `_wdmapok=1`.  The PERMISSION-INPUTS cells are read BY VALUE here: DACR `_wddacr`, domain
#     `_wddom`, TTBCR `_wdttbcr`/`_wdsplit`, the leaf's AP/TEX/CB/nG (`_wdap`/`_wdapx`/`_wdtex`/
#     `_wdcb`/`_wdsmall`/`_leafng`), the thread's own pmap chain `_wdthrmap`/`_wdpcpu` (against
#     `_wdttbr0`), and SCTLR `_wdsctlr`.
#       P-CTL  `_wdctlok=0` or `_wdseen=0`  =>  NO_PERM_WITNESS (rebuild; no verdict).
#       P-AT   `_wdatok=0`  =>  NO_AT: the deciding table (AT) is unavailable; the rung cannot
#              conclude, regardless of everything below.
#       P-D1   `_wddom != 0`  =>  L1_DOMAIN_NONZERO.
#       P-D2   `_wddom == 0` and `(_wddacr & 3u) != 1u`  =>  DACR_NOT_CLIENT (H-P CONFIRMED).
#       P-D3   `_wddom == 0` and `(_wddacr & 3u) == 1u`  =>  DACR_CLIENT (H-P's domain form KILLED).
#       P-T1   `(_wdttbcr & 7u) != 2u`  =>  TTBCR_NOT_N2 (every `entry_ttbr_base` derivation re-read).
#       P-T2   `(_wdttbcr & 7u) == 2u` and `_wdva >= _wdsplit`  =>  ROUTED_TO_TTBR1.
#       P-T3   `_wdttbr0` (masked `2^(14-n)`) != `0x81a86000`  =>  TTBR0_MOVED_AT_ABORT.
#       P-M1   `_wdmapok=1` and `_wdthrmap != _wdttbr0`  =>  THREAD_PMAP_NE_TTBR0.
#       P-L1   `_wdsmall=1` and (`_wdapx != 0` or `_wdap != 0b11`)  =>  LEAF_NOT_USER_WRITABLE
#              (contradicts 1014's stable double-read).
#       P-L3   `_wdsmall=0`  =>  NOT_A_SMALL_PAGE.
#       P-S1   `_wdsctlr` bits TRE(28)/AFE(1)/M(0) differ from the console-install reading  =>
#              SCTLR_MOVED (H-X is live; the next rung reads the CPU's own state).
#       P-X1   all of P-D3/P-T1..P-T3/P-M1/P-L1/P-L3/P-S1 killed AND V-AT2  =>
#              INPUTS_ALL_CORRECT_STILL_FAULTS (the translation is right and the store still faults;
#              the next rung is the observer, not the pmap).
#     This is a pure MEASUREMENT rung: the run is EXPECTED to still freeze (same (b2) shape as
#     1012/1013/1014, ~24.39 s) - the translation's PROVENANCE is the finding.  The behaviour
#     question ("does evicting the global entry remove the fault?") is DELIBERATELY deferred to
#     1015b (docs/1015-design.md §2b/§3c) - one question per rung.
#     FALSIFIERS (design §5): (a) NO `xnu_live_abt_wd*` keys at all while `xnu_live_abt_n` is present
#     => the PERM_WITNESS half did not reach the image (rebuild); (b) `xnu_live_abt_wdn` GAPS from
#     `xnu_live_abt_n` at a paired row => the recorder did not run once per abort - an INSTRUMENT
#     failure; (c) a recorder present but its ONE `b|bl` call site absent => rebuild; (d) a
#     NON-MONOTONE `xnu_live_abt_wdn` => a MEASUREMENT defect; (e) `_wdatok=0` at EVERY tail row =>
#     V-AT0/P-AT, the instrument never fired - REPORT the measurement defect, conclude NOTHING (never
#     "stale TLB is refuted"); (f) `_wdctlat` carrying PA `0x80080000` in a `_wdatparpr` row => the
#     one-key-two-definitions collision - withhold; (g) death ELSEWHERE, later => the frontier
#     ADVANCED - record by value; (h) `_leafng == 1` at the tail => H-G dies by value (documented
#     precondition, not a hope).
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np ret=12`,
#     the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the whole capture
#     must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: ...`, BOUNDED (max 8).  EXPECTED >= 1.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1013_capture.py carries the gate AND the leaf reading;
#     tools/check_1014_capture.py carries the flush reading; tools/check_1015_capture.py carries the
#     AT/PAR reading and the passive ladder; **tools/check_1015b_capture.py carries THIS RUNG'S
#     decisive G-* cell**).  A verdict is a READING taken from a live image, so it
#     counts only if the boot reached the print site.
# (5) THE 1006 FREEZE WITNESS (kept a regression).  1015b expects the SAME silence.
# (6) THE 1005 CADENCE WITNESS (kept a regression).  1015b expects the SAME (b2) shape.
# (7) THE ABORT TRACE (1004, kept).  `far` REPEATS = LOOP; strictly ADVANCES = WALK.
# (8) THE USER-SVC FRONTIER (kept; keys xnu_live_usvc_*).  `_pid` = 1; `_pc` a user VA.
# (9) THE PET (kept).  If 1015b lives past ~14.5 s with the read door in use, `xnu_live_wdt_pet_n` MUST
#     appear.
# (9g) THE WIDENED FLUSH COSTS TIME AND THIS ARM MEASURES IT.  A full TLBIALL per flush is strictly
#     more work than a TLBIASID, and 1014's `_pcn` ladder already recorded a stall whose budget this
#     arm could consume: IF THE CAP IS REACHED BEFORE THE STORM'S BAND, THAT IS ITSELF A READING (the
#     ladder's reach is reported by the reader as MAX `_pcn`), NOT A WITHHOLD - and it is NOT a
#     G-CONFIRMED, because a ladder that never reached the band never saw the storm either way.
# (10) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
#     thinning ordinal stays 64.  The sleh caps stay RAISED to 512 under 1004's switch.
# (11) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): the (0g)/(0)/(AT) lists above, and the
#     (G) cell's OWN falsifiers: a G-CONFIRMED with NO `_wfkind=1` row on the storm VA (the
#     attribution guard), a stop read off a TRUNCATED capture (the truncation guard), or a
#     `_leafng != 0` at the tail - a NON-GLOBAL leaf would kill H-G BY VALUE before any press,
#     because TLBIASID WOULD evict an ASID-tagged stale entry (which is why 1015 published the bit).  The FROZEN
#     REGRESSION GUARDS - the geometry line exactly once, `IOS7LAB cache mapping` x4, the shared region
#     x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0` - are re-verified
#     only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent; (c) the log ends
# with a capped key and no direct key after it = rebuild; (d) the log ends in a direct record = that
# record's moment IS the frontier finding - read the last direct keys (abt-wd, then abt-wctx, abt-wf,
# then abt-wsync, abt-walk, abt-wleaf, then abt-pmp, then abt-pte, ...) as the frontier, and classify
# by the V-AT0..4 table from the tail `_wdatpar`/`_wdatparpr`/`_wdctlat`/`_wdatok` (and the P-* ladder
# from the tail `_wddom`/`_wddacr`/`_wdttbcr`/`_wdthrmap`/`_wdap`/`_wdapx`/`_wdsctlr`, with `_leafng`
# as the H-G precondition and the W-SYNC/W-FLUSH controls as the INPUT); (e) the (0) change is not in
# the image = the witness is not in the image - rebuild, nothing else is a test; (f) **THE STORM IS
# ABSENT BUT NO `_wfkind=1` ROW IS KEYED TO IT** = a stop this arm cannot be credited for: run the
# attribution guard by hand before reporting anything as G-CONFIRMED.
# ====================================================================================================
if ! grep -q '^# 1015b PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1015b pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003/1004/1005/1006/1007/1008/"
  echo "          1009/1010/1011/1012/1013/1014/1015 pattern: switch set / THE POINTS / failure signatures"
  echo "          / falsification / regression guards). The build side: scripts/build_1015b.sh +"
  echo "          scripts/park_1015b.sh + tools/stage_d13_globalfix.sh (+ the pool clause in"
  echo "          tools/build_xnu_arm_kernel.sh is NOT required - EXTRA is this switch's compile carrier)."
  exit 1
fi

# **THE 1015b DECLARATION CHECK, BEFORE ANY ARM REFUSAL.** The switch that MOVED THE KERNEL is not in
# the record and cannot be (MEASURED x0 in src/entry/build_entry.sh and in the preflight gate); it is
# declared by ONE line in a drop-in file. A press of an arm whose declaration is missing would produce
# a capture whose adjudication the reader would have to attach to the wrong kernel.
if [[ -f out/stage90/xnu_arm_entry-arm-extra.txt ]] && grep -qx 'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix' out/stage90/xnu_arm_entry-arm-extra.txt; then
  echo "   (0g) the arm declares STAGE90_XNU_GLOBAL_FIX=1 (out/stage90/xnu_arm_entry-arm-extra.txt)"
else
  echo "REFUSING: out/stage90/xnu_arm_entry-arm-extra.txt does not carry the line"
  echo "          'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix'."
  echo "          The config record CANNOT carry this switch - no writer in src/entry/ echoes it (MEASURED"
  echo "          x0), and scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS is a SET DIFFERENCE against the"
  echo "          record, so naming it there would make the gate refuse every press. The drop-in file is"
  echo "          the ONLY declaration of the one switch that makes this arm a behaviour change."
  echo "          Appended by scripts/park_1015b.sh at the freeze point. If it is missing, either the arm"
  echo "          was never parked (scripts/build_1015b.sh writes the file EMPTY) or the park was run"
  echo "          against a different out/stage90 than the one in front of you."
  exit 1
fi

# **AND THE POOL OBJECT: THIS RUNG'S OWN BINARY-SIDE CHECK.** For every rung up to 1015 the pool did
# not move, so no press needed to look; 1015b's kernel IS the object. The census is on the OBJECT (the
# surface the fix is made on) while the build's is on the arm sha8 (the surface the fix must ARRIVE
# on) - two surfaces that fail independently, on purpose.
_b_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_b_obj" ]] || { echo "REFUSING: $_b_obj is absent - the pool object 1015b edits is not on disk, so nothing here can show the kernel moved"; exit 1; }
_b_lo=$(arm-none-eabi-nm "$_b_obj" 2>/dev/null | awk '$3=="pmap_flush_tlbs" && ($2=="T"||$2=="t"){print "0x" $1; exit}')
_b_hi=$(arm-none-eabi-nm -n "$_b_obj" 2>/dev/null | awk -v s="${_b_lo#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
[[ -n "$_b_lo" && -n "$_b_hi" ]] || { echo "REFUSING: pmap_flush_tlbs's symbol range could not be computed in $_b_obj (lo='$_b_lo' hi='$_b_hi') - without it the census below measures nothing"; exit 1; }
# **AND THE RIGHT FINGERPRINT IS THE `kernel_pmap` MATERIALIZATION COUNT, NOT THE CALL COUNTS.**
# A first version of this clause asserted `arm_tlb_flushID_ASID == 0` and `arm_tlb_flushID >= 2`.
# BOTH ARE IMPOSSIBLE AND THE CLAUSE COULD NEVER PASS (MEASURED, 1015b's own build):
#   * the ASID arm REMAINS IN THE TEXT by design - the insertion is Tautologically TRUE for the
#     kernel (`pmap_is_shared == pmap->pm_shared || pmap == kernel_pmap`), so only the non-kernel
#     path changes; `arm_tlb_flushID_ASID` still has 1 relocation (MEASURED @0x630).  Wanting 0
#     contradicts the design this script is testing.
#   * the compiler TAIL-MERGES the two identical `arm_tlb_flushID()` call sites into ONE `bl`
#     (MEASURED: x1 @0x574).  Wanting >= 2 asks for an artifact the toolchain does not emit.
# THE COUNT THAT DOES DISCRIMINATE: the inserted test `pmap != kernel_pmap` materializes the
# `kernel_pmap` ADDRESS one extra time.  MEASURED by value: OFF/Apple's object carries
# `R_ARM_MOVW_ABS_NC kernel_pmap` x2 (@0x440 `pmap_is_shared` init, @0x604 terminal panic);
# 1015b's ON object carries x3 (@0x440, @0x560 the inserted test, @0x608).  Same class as the
# `_b_wf` clause below (a MOVED-vs-ADDED count cannot be faked by a count alone, so the build's
# own linked census reads the same fingerprint at the resolved `movw` immediate).
_b_kp=$(arm-none-eabi-objdump -r "$_b_obj" 2>/dev/null | awk -v lo="${_b_lo#0x}" -v hi="${_b_hi#0x}" 'BEGIN{l=strtonum("0x" lo); h=strtonum("0x" hi)} /R_ARM_MOVW_ABS_NC/ && /kernel_pmap/ {a=strtonum("0x" $1); if (a>=l && a<h) n++} END{print n+0}')
echo "   (0g) POOL $_b_obj: pmap_flush_tlbs $_b_lo..$_b_hi - kernel_pmap materializations x$_b_kp (want EXACTLY 3; 1015's object carries 2)"
[[ "$_b_kp" == "3" ]] || { echo "REFUSING: the pool object is NOT 1015b's - pmap_flush_tlbs materializes kernel_pmap x$_b_kp time(s) (want EXACTLY 3; UNFIXED carries 2: the pmap_is_shared init + the terminal panic). The inserted non-kernel arm IS that third materialization. A press here would measure 1015's kernel under 1015b's adjudication, and a G-REFUTED read off it would refute nothing but the missing edit"; exit 1; }
_b_wf=$(arm-none-eabi-objdump -r "$_b_obj" 2>/dev/null | grep -c 'entry_note_abt_wf' || true)
[[ "$_b_wf" == "1" ]] || { echo "REFUSING: $_b_obj carries $_b_wf relocation(s) to entry_note_abt_wf (want 1 - the inherited 1014 flush probe). 1015b inserts BETWEEN Apple's two lines, so a displaced probe means the insertion did not happen where the design says - and the reader's attribution guard (_wfkind=1 keyed to the storm) would have no producer"; exit 1; }

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
# **1015b's OWN ARM REFUSAL, FIRST.** This is the one that matters most for this rung and it is a
# DIFFERENT KIND of failure from every arm below it: 1015's switch set is ENTRY-ONLY and 1015b's is
# not, so an arm still equal to 1015's means THE POOL EDIT DID NOT REACH THE LINKED IMAGE. The pool's
# `.text` is INSIDE `xnu_arm_entry.bin` (MEASURED: `LOAD 0x010000 0x80000000 0x80000000 0x570000 R E`,
# `pmap_flush_tlbs` at 0x8002968c), so a correct rebuild MUST move this sha8. A press here would run
# 1015's kernel while this script's adjudication talks about 1015b's fix.
[[ $EXPECT_ARM == armed-d13-e51d14fa ]] && {
  echo "REFUSING: out/stage90 holds 1015's arm (e51d14fa) - THIS RUNG'S PRESSED PREDECESSOR, and the"
  echo "          ONE stale arm 1015b must never press."
  echo "          (1015's VERDICT, records/1015-press-read.txt: **V-AT1 - H-G CONFIRMED AT THE HARDWARE.**"
  echo "          _wdatpar faults (par&1==1) while its null control _wdatparpr SUCCEEDS (par&1==0) to PA"
  echo "          0x8419d000; _wdleaf=0x8417e1bb is AP=0b11/APX=0 (user-writable), PA 0x8417e000, with"
  echo "          _wddom=0, _wddacr=1, _wdttbcr=2, _wdsplit=0x40000000, _wdctlat=0x80080268 (control"
  echo "          satisfied) and _leafng=0 (GLOBAL).  The MMU's translation for a USER WRITE is NOT the"
  echo "          leaf the walk reads.  The storm ran to _pcn=0x40000 with far=0x00027000, and the run"
  echo "          was RESIDENT, wdt_pets=0x61, no panic.)"
  echo "          THE ALARM: this arm's bytes are 1015's, so the widened flush is NOT in them. Check, in"
  echo "          this order: (1) tools/patch_d13_globalfix.py --check against the tree; (2)"
  echo "          -DSTAGE90_XNU_GLOBAL_FIX=1 in EXTRA - the ONE and ONLY compile channel (this switch"
  echo "          has NO DEFINES+= clause AND no environment path: MEASURED 0 mentions in"
  echo "          tools/build_xnu_arm_kernel.sh, so exporting it does nothing); (3) that"
  echo "          osfmk_arm_pmap.o was actually RECOMPILED (a stale object is linked happily); (4) the"
  echo "          pool census THIS script just printed ((0g) above)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-29ec9e48 ]] && {
  echo "REFUSING: out/stage90 holds 1014's arm (29ec9e48) - the PRESSED predecessor you are SUPERSEDING."
  echo "          (VERDICT: F2 was measured, NOT F1 - the ASID-scoped flush RAN under the MATCHING ASID"
  echo "          (_wfkind=2, _wfasid=0x02 == _wctx=0x02) and the V1 leaf reading holds:"
  echo "          LEAF_WRITABLE_STILL_FAULTS (_wwleaf=0x841e91bb, _wwapx=0, _wkl2pa == _wwl2pa).  BUT the"
  echo "          record's 'stale TLB is dead' conclusion was a MEASUREMENT DEFECT: TLBIASID cannot evict a"
  echo "          GLOBAL entry and every leaf here is GLOBAL (L2_NG_BIT never ORed).  Corrected in"
  echo "          records/1014-press-read.txt; design docs/1015-design.md §8b.  H-G survives - that is"
  echo "          1015's question, asked of the MMU itself via the ARMv7 AT instruction.)"
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-9bf25f8f ]] && {
  echo "REFUSING: out/stage90 holds 1013b's arm (9bf25f8f) - the PRESSED predecessor (VERDICT: MEASUREMENT"
  echo "DEFECT - the W-SYNC control could NOT be read (wck=0, TTBR1 territory); W-APEX read a real writable"
  echo "leaf (wwleaf=0x841e91bb, wwapx=0) but wwnoproof=1, so it is not proof; and W-WALK read the SAME"
  echo "table yet published wkleaf=0 because the walker's L2 type test (l2 & 3) != 2 rejected every XN"
  echo "page - fixed in 1013c (entry_stubs.c: the 4K-descriptor test ABTPTE_L2_SMALL_PAGE); adjudicated in"
  echo "records/1013b-press-read.txt; design docs/1015-design.md)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-9cf2f15e ]] && {
  echo "REFUSING: out/stage90 holds 1013's arm (9cf2f15e) - the PRESSED predecessor (VERDICT: the same"
  echo "MEASUREMENT DEFECT class - the witness RAN but returned WITNESS_METHOD_BROKEN on the W-SYNC"
  echo "control (wck=0); the surviving leaf reading is V1 LEAF_WRITABLE_STILL_FAULTS, which is exactly"
  echo "the INPUT 1014's flush witness tests; adjudicated in records/1013-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-ffdc8ba1 ]] && {
  echo "REFUSING: out/stage90 holds 1012's arm (ffdc8ba1) - the PRESSED predecessor (VERDICT: the PTE-build"
  echo "apex is MEASURED - every tail row carries _pmpprot=0x3 (READ|WRITE) AND _pmppmap & 0xFFFFFF00"
  echo "(0x81a86000) == _pmpttbr & 0xFFFFFF00, so the write ARRIVES at pmap_enter_options and the pmap"
  echo "writes the SAME L1 table the MMU walks; strip-above-the-apex is REFUTED BY MEASUREMENT.  The"
  echo "leaf's VALUE was still never read - that is 1013's question; adjudicated in"
  echo "records/1012-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce83837 ]] && {
  echo "REFUSING: out/stage90 holds 1011's arm (fce83837) - the PRESSED predecessor (VERDICT: the F1a/F1b"
  echo "DECISION is MEASURED - F1a.  The (E') tail rows carry vmekr=0 (KERN_SUCCESS) with vmeprot=0x3"
  echo "(READ|WRITE) on vmefault=0x3 (a WRITE fault) at vmeva=0x61000, so vm_map_lookup_locked's out_prot"
  echo "GRANTS write and its refusal arm (vm_map.c:9087) did not fire - F1b is FALSIFIED BY VALUE; the write"
  echo "is LOST between prot and the PTE.  The (A) walk's TTBR0 base mask was WRONG (0xffffc000 vs the"
  echo "port's TTBCR.N=2 alignment 0xfffff000) so the L2 leaf has been unreadable since 1010b - fixed in"
  echo "entry_ttbr_base (07914cf); adjudicated in records/1011-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-002b0683 ]] && {
  echo "REFUSING: out/stage90 holds 1010c's arm (002b0683) - the PRESSED predecessor (VERDICT: the walk is"
  echo "WINDOW-BOUNDED and reaches an OUT-OF-WINDOW L2 table; with the mask fixed to 0xffffc000 it reads the"
  echo "TRUE L1 base 0x81a84000, but the storm's L1 slot holds a SECTION descriptor ptel1=0xfeedface"
  echo "(bits[1:0]=0b10), so the walk returns early (entry_stubs.c:2291, (l1e&3)!=1) and the L2 LEAF (and"
  echo "its APX bit) is NEVER read - F1a/F1b UNDECIDABLE; adjudicated in records/1010c-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-64391319 ]] && {
  echo "REFUSING: out/stage90 holds 1010b's arm (64391319) - the PRESSED predecessor (VERDICT: the TTBR0"
  echo "mask fix is CORRECT but it exposed a SECOND defect in the SAME walk - the corrected base reads the"
  echo "TRUE L1, whose L2 table is at PA 0x46300000, BELOW the entry image's identity window"
  echo "[0x80000000,0x9e400000); the walk dereferences it with NO window bound, so the L2 leaf load faults"
  echo "FS=5 (pc=0x80002644 = its own ldr) and the run DIES at launchd-start BEFORE the storm; the leaf is"
  echo "STILL unread; adjudicated in records/1010b-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-0f39207c ]] && {
  echo "REFUSING: out/stage90 holds 1010's arm (0f39207c) - the PRESSED predecessor (VERDICT: the storm"
  echo "reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail (impossible under"
  echo "fsr=0x80f, FS=15 level-2 permission fault) because the TTBR0 base was masked with the L1->L2 mask"
  echo "0xfffffc00 instead of the base mask 0xffffc000; adjudicated in records/1010-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-24997fc5 ]] && {
  echo "REFUSING: out/stage90 holds 1009's arm (24997fc5) - the PRESSED predecessor (frontier: the storm"
  echo "is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x0000080f ="
  echo "WnR=1|FS=0xf, cpsr=0x60000030, far=0x9a000 a LOW user VA, constant from ordinal 0x400 through"
  echo "0x200000 - the handler chose the SILENT retry branch, so vm_fault returned KERN_SUCCESS||"
  echo "KERN_ABORTED; adjudicated in records/1009-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-0343ef45 ]] && {
  echo "REFUSING: out/stage90 holds 1008's arm (0343ef45) - the PRESSED predecessor (frontier: the storm"
  echo "is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort, from ordinal 0x400 through"
  echo "0x200000 - ~2.09 M retries of one fault address, riding the ~18.7 s handler call; adjudicated in"
  echo "records/1008-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-88074ab3 ]] && {
  echo "REFUSING: out/stage90 holds 1007's arm (88074ab3) - the PRESSED predecessor (frontier: the"
  echo "~24.4 s freeze is INSIDE ONE sleh_abort - maxcall 18.735 s = 0.768 x M - riding a FAULT STORM"
  echo "of ~2.1 M aborts whose fault ADDRESS is in no capture; adjudicated in"
  echo "records/1007-press-read.txt). Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-31bdebf3 ]] && {
  echo "REFUSING: out/stage90 holds 1006's arm (31bdebf3) - the PREVIOUS predecessor (frontier: the"
  echo "~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered;"
  echo "adjudicated in records/1006-press-read.txt). Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-ff937225 ]] && {
  echo "REFUSING: out/stage90 holds 1005's arm (ff937225) - the PRESSED predecessor (frontier: the STALL"
  echo "is (b2) a whole-interrupt-path freeze ~24.4 s; adjudicated in records/1005-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-582a19c8 ]] && {
  echo "REFUSING: out/stage90 holds 1004's arm (582a19c8) - the PRESSED predecessor (frontier: the"
  echo "STALL after dyld first touches a shared-cache page; adjudicated in records/1004-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - already pressed 2026-10-13 (pid 1 in the"
  echo "kernel's data-abort return path; adjudicated in records/1003-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt). Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1015b (scripts/build_1015b.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1015 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1015 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip, 1003's stall + 1004's abort-trace instrument + 1005's cadence witness + 1006's freeze witness + 1007's abort-span witness + 1008's abort-fault identity + 1009's faulting-pc identity + 1010c's L2-descriptor + 1011's (E-prime) entry-protection + (A) thread-L1-base + 1012's PTE-build apex + 1013's LEAF-VALUE WITNESS + 1014's TLB-FLUSH WITNESS AND 1015's AT/PAR PERMISSION WITNESS =="
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
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004/1005/1006/1007/1008/1009/1010/1011/1012/1013/1014)
STAGE90_XNU_ABORT_TRACE the abort trace - raised sleh caps + the frame's user pc (1004's; kept verbatim)
STAGE90_XNU_CADENCE the cadence witness - a timer-independent (syscall) sample + the all-interrupt counter (1005's; kept verbatim)
STAGE90_XNU_FREEZE_WITNESS the idle-path freeze witness - the dwell sampler + the GIC/CPSR triple (1006's; kept verbatim)
STAGE90_XNU_ABT_SPAN the abort-span witness - the gap + handler duration per sleh_abort (1007's; kept verbatim)
STAGE90_XNU_ABT_FAR the abort-fault identity - the fault ADDRESS + TYPE of the storm's aborts on the DIRECT channel (1008's; kept verbatim)
STAGE90_XNU_ABT_PC the faulting-pc identity - WHICH instruction faults + the DFSR + the change-counts on the DIRECT channel (1009's; kept verbatim)
STAGE90_XNU_ABT_PTE the L2-descriptor + vm_fault-return identity - walks TTBR0 to the L2 small-page descriptor for the fault VA and reads what vm_fault returned (1010c's; kept verbatim)
STAGE90_XNU_VME_PROT the vm_map_entry effective protection + the faulting thread's own L1 base - the (E') __wrap_vm_map_lookup_locked publishes out_prot; the (A) reader publishes pm_l1_phys (1011's; kept verbatim)
STAGE90_XNU_PMP_ENTER the PTE-build apex - the `prot` handed to pmap_enter_options via --wrap=pmap_enter_options (1012's; kept verbatim)
STAGE90_XNU_PTE_WITNESS the LEAF-VALUE WITNESS - the leaf the pmap writes (W-APEX) + the location the MMU walks to (W-WALK) + a known-good CONTROL (W-SYNC) (1013's; kept verbatim)
STAGE90_XNU_FLUSH_WITNESS the TLB-flush witness (1014's; a pure measurement rung) - the flush decision + the CONTEXTIDR at the fault
STAGE90_XNU_PERM_WITNESS the AT/PAR PERMISSION WITNESS (1015's; kept verbatim) - asks the MMU itself whether the storm VA translates for a user write
EOF
echo "   (and OUTSIDE this record: STAGE90_XNU_GLOBAL_FIX=1, declared in out/stage90/xnu_arm_entry-arm-extra.txt - the (0g) checks above. It is NOT in the record because no writer in src/entry/ echoes it, and the preflight gate is a SET DIFFERENCE)."
echo "   (record carries 1015's verbatim switch set (the 1014 switches PLUS PERM_WITNESS); USVC, SUBMAP_PMAP_SKIP, STALL, ABORT_TRACE, CADENCE, FREEZE_WITNESS, ABT_SPAN, ABT_FAR, ABT_PC, ABT_PTE, VME_PROT, PMP_ENTER, PTE_WITNESS and FLUSH_WITNESS all on; and GLOBAL_FIX declared outside the record - (0g))"
# AND THE RECORD MUST NOT NAME THE POOL SWITCH: if it did, either a hand edit put it there or a writer
# appeared in src/entry/, and both cases mean the record and the drop-in file can now disagree about
# which kernel this arm is ([[mi4-one-value-two-definitions]]).
! grep -q '^STAGE90_XNU_GLOBAL_FIX=' out/stage90/xnu_arm_entry-config.txt || { echo "REFUSING: the config record names STAGE90_XNU_GLOBAL_FIX, but no writer in src/entry/ produces that line - so it came from a hand edit, and the record and xnu_arm_entry-arm-extra.txt could now disagree about which kernel this arm is. The declaration belongs in the drop-in file, and ONLY there"; exit 1; }
# AND THE AE-CHANGE STAMP: 1015b is the first rung since 986 whose kernel differs. If the pool census
# above passed, this is redundant by construction - it is here so that a reader of the press LOG sees
# the arm's identity in the same breath as the switch set.
echo "   (0g) the entry image's own record still carries STAGE90_ENTRY_ARM_CHANGE=$({ awk -F= '\$1==\"STAGE90_ENTRY_ARM_CHANGE\"{print \$2}' out/stage90/xnu_arm_entry-config.txt; })"

echo "== 2-0. the 1015 AT/PAR PERMISSION WITNESS + the inherited witnesses, all read by value BEFORE the press; 1015b's OWN change is the POOL object and its declaration - checked at (0g) above, NOT here, because it is not an entry-side key =="
# (0) THE 1015 CHANGE IS IN THE IMAGE: the ONE HUNDRED AND THIRTY-ONE (131) `xnu_live_abt_` key
#     literals embedded (1012's 49 + 1013's 35 witness keys incl. the TTBR1 label _wctb and the 1013b
#     VA cell _wkva + the 2 raw TTBR words _wctb0/_wctb1 [= 86] + 1014's 14: the 11 W-FLUSH keys and
#     the 3 W-CTX keys [= 100] + 1015's 31: the 27 `wd*` keys, `leafng`, and `wwpre`/`wwpost`/`wwmoved`)
#     AND the 1015 recorder DEFINED text symbol (read by the 1015 block below) - and, as regressions,
#     the two 1014 recorders AND the four 1013 witness recorders DEFINED text symbols
#     AND exactly ONE `bl <entry_note_abt_pmp>` INSIDE `__wrap_pmap_enter_options`, which ALSO calls the
#     real `pmap_enter_options` body ([[mi4-hooks-live-at-calls]] - the wrapper MUST be a pass-through).
_abt_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_abt_' || true)
echo "   (0) xnu_live_abt_* literals x$_abt_lit (want 131: 49 through 1012 + 1013's 35 witness keys + the 2 raw TTBR words wctb0/wctb1 [=86] + 1014's 14 flush-witness keys [=100] + 1015's 31: 27 wd* + leafng + wwpre/wwpost/wwmoved)"
[[ "$_abt_lit" == 131 ]] || {
  echo "REFUSING: the 1015 AT/PAR store (or the 1014 flush-witness store, the 1013 witness store, the 1012 pmp, the 1011 (E')/(A) and the 1010c L2-descriptor + walk-window stores) is NOT in the image (literals x$_abt_lit want 131) - rebuild"; exit 1; }
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
# (0n) THE 1014 TLB-FLUSH WITNESS IS A REGRESSION (the AT cell is read AGAINST it: an ASID-scoped flush
#      ALREADY RAN, so the question is whether it could have evicted the entry AT is about to use).
#      By value: the two recorders `entry_note_abt_wf` / `entry_note_abt_wctx` are a DEFINED `T/t` text
#      symbol each (a generated stand-in would show 0) AND each has EXACTLY ONE image-wide `(b|bl)` call
#      site ([[mi4-hooks-live-at-calls]] - the `(b|bl)` regex, NOT `bl`-only: a void call that ends its
#      function is TAIL-called and appears as `b <sym>`), AND all FOURTEEN 1014 key literals are
#      embedded.  STAGE90_XNU_FLUSH_WITNESS=1 in the record is NOT enough - the tree insertion can be a
#      no-op ([[mi4-a-claim-in-a-comment-is-not-a-check]]), so the image is read by value.
_abt_str=$(strings out/stage90/xnu_arm_entry.elf)
for _wf_sym in entry_note_abt_wf entry_note_abt_wctx; do
  grep -qE "^[0-9a-f]+ [Tt] $_wf_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_wf_sym is not a DEFINED text symbol in the image - the 1014 flush witness is gone (an object did not rebuild or a stand-in was linked) - rebuild"; exit 1; }
  _wf_c=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <'"$_wf_sym"'>' || true)
  echo "   (0n) $_wf_sym is DEFINED T/t; image-wide (b|bl) call sites: $_wf_c (want 1)"
  [[ "$_wf_c" == 1 ]] || {
    echo "REFUSING: the image carries $_wf_c call site(s) to $_wf_sym (want exactly 1) - the 1014 flush witness is not wired - rebuild"; exit 1; }
done
for _wf_key in xnu_live_abt_wfn xnu_live_abt_wfva xnu_live_abt_wfttb xnu_live_abt_wflive xnu_live_abt_wfself xnu_live_abt_wfshared xnu_live_abt_wfncpus xnu_live_abt_wfasid xnu_live_abt_wfkind xnu_live_abt_wfseen xnu_live_abt_wfnx xnu_live_abt_wctxn xnu_live_abt_wctx xnu_live_abt_wctxva; do
  grep -q "^${_wf_key}\$" <<<"$_abt_str" || {
    echo "REFUSING: the 1014 flush-witness key literal ${_wf_key} is NOT in the image - rebuild"; exit 1; }
done
echo "   (0n) all 14 1014 flush-witness key literals are embedded: xnu_live_abt_wfn xnu_live_abt_wfva xnu_live_abt_wfttb xnu_live_abt_wflive xnu_live_abt_wfself xnu_live_abt_wfshared xnu_live_abt_wfncpus xnu_live_abt_wfasid xnu_live_abt_wfkind xnu_live_abt_wfseen xnu_live_abt_wfnx (the 11 W-FLUSH) + xnu_live_abt_wctxn xnu_live_abt_wctx xnu_live_abt_wctxva (the 3 W-CTX)"
# (0p) THE 1015 AT/PAR PERMISSION WITNESS IS IN THE IMAGE - the NEW half this rung tests, and the ONLY
#      cell that asks the hardware rather than the tables.  By value: the recorder `entry_note_abt_wd`
#      is a DEFINED `T/t` text symbol (a generated stand-in would show 0) AND has EXACTLY ONE
#      image-wide `(b|bl)` call site ([[mi4-hooks-live-at-calls]] - the `(b|bl)` regex, NOT `bl`-only),
#      INSIDE `__wrap_sleh_abort` (the recorder must run BEFORE the handler, so the AT pair describes
#      the context the faulting store used; [[mi4-hooks-live-at-calls]] - a recorder called anywhere
#      else answers about a different context), AND all THIRTY-ONE 1015 key literals are embedded -
#      the 27 `wd*` keys, the H-G precondition `leafng` (the leaf's raw bit11: GLOBAL vs ASID-tagged),
#      and the three 1014-§6 owed cells `wwpre`/`wwpost`/`wwmoved` (the pre- and post-call
#      `pm_l1_phys` bases, so 1014's `_wwnoproof` is a reading rather than a bare 1).
#      STAGE90_XNU_PERM_WITNESS=1 in the record is NOT enough - the tree insertion can be a no-op
#      ([[mi4-a-claim-in-a-comment-is-not-a-check]]), so the image is read by value.
for _wd_key in xnu_live_abt_wdn xnu_live_abt_wdva xnu_live_abt_wddacr xnu_live_abt_wddom \
               xnu_live_abt_wdttbcr xnu_live_abt_wdsplit xnu_live_abt_wdttbr0 xnu_live_abt_wdttbr1 \
               xnu_live_abt_wdl1 xnu_live_abt_wdleaf xnu_live_abt_wdap xnu_live_abt_wdapx \
               xnu_live_abt_wdtex xnu_live_abt_wdcb xnu_live_abt_wdsmall xnu_live_abt_wdpcpu \
               xnu_live_abt_wdthrmap xnu_live_abt_wdmapok xnu_live_abt_wdseen xnu_live_abt_wdctl \
               xnu_live_abt_wdctlok xnu_live_abt_wdempty xnu_live_abt_wdatok xnu_live_abt_wdatpar \
               xnu_live_abt_wdatparpr xnu_live_abt_wdctlat xnu_live_abt_wdsctlr \
               xnu_live_abt_leafng xnu_live_abt_wwpre xnu_live_abt_wwpost xnu_live_abt_wwmoved; do
  grep -q "^${_wd_key}\$" <<<"$_abt_str" || {
    echo "REFUSING: the 1015 AT/PAR key literal ${_wd_key} is NOT in the image - rebuild"; exit 1; }
done
echo "   (0p) all 31 1015 key literals are embedded: the 27 wd* (wdn wdva wddacr wddom wdttbcr wdsplit wdttbr0 wdttbr1 wdl1 wdleaf wdap wdapx wdtex wdcb wdsmall wdpcpu wdthrmap wdmapok wdseen wdctl wdctlok wdempty wdatok wdatpar wdatparpr wdctlat wdsctlr) + leafng + wwpre/wwpost/wwmoved"
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_wd\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_wd is not a DEFINED text symbol in the image - the 1015 AT/PAR witness is gone (an object did not rebuild or a stand-in was linked) - rebuild"; exit 1; }
_wd_c=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wd>' || true)
echo "   (0p) entry_note_abt_wd is DEFINED T/t; image-wide (b|bl) call sites: $_wd_c (want 1)"
[[ "$_wd_c" == 1 ]] || {
  echo "REFUSING: the image carries $_wd_c call site(s) to entry_note_abt_wd (want exactly 1) - the 1015 AT/PAR witness is not wired - rebuild"; exit 1; }
# NOTE: the "the call site is INSIDE __wrap_sleh_abort" assertion is made in the (0y) block below,
# where `_ab_body` (the wrapper's own disassembly) is computed - asserting it here would read an
# EMPTY variable and refuse a correct image.
# (0x) THE 1011 (E')/(A) IDENTITY IS A REGRESSION (the FRONTIER INPUT the apex cell is read against):
#      `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked` DEFINED T/t, its PASS-THROUGH calling the real
#      `vm_map_lookup_locked`, exactly ONE `bl <entry_note_abt_vme>` (the §8.1 rule: the wrapper MUST
#      return the real lookup's value on the non-abort case).  Its seven key literals are inside the (0) 49.
for _vme_11sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_vme_11sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_vme_11sym is not a DEFINED text symbol in the image - the 1011 (E') instrument regressed (1015 keeps 1014's set verbatim) - rebuild"; exit 1; }
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
#      literals are inside the (0) 49).  NOTE: 1015 carries the FIXED walk (`entry_ttbr_base`, 07914cf).
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the image - the 1010c instrument regressed (1015 keeps 1014's set verbatim) - rebuild"; exit 1; }
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
# (0p) THE 1015 RECORDER'S CALL SITE IS INSIDE `__wrap_sleh_abort` - the positional half of (0p).
#      This matters: the AT pair must be issued BEFORE the handler runs, in the faulting thread's own
#      context (TTBR0/ASID still live), or it answers about a different translation.  `_ab_body` is
#      the wrapper's disassembly (computed just above).
grep -qE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wd>' <<<"$_ab_body" || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_wd - the AT pair would not describe the faulting context (a recorder called from anywhere else answers about a different translation) - rebuild"; exit 1; }
echo "   (0p) __wrap_sleh_abort calls entry_note_abt_wd (the recorder runs BEFORE the handler, so the AT pair sees the faulting thread's live TTBR0/ASID)"
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
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the image - the 1009 recorder was dropped (1015 keeps 1014's set verbatim) - rebuild"; exit 1; }
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
  echo "REFUSING: the 1006 freeze store is NOT in the image (literals x$_frz_lit want 6) - 1015's set is 1014's verbatim, so it must be present - rebuild"; exit 1; }
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
  echo "REFUSING: the 1005 cadence store is NOT in the image (literals x$_cad_lit want 4) - 1015's set is 1014's verbatim, so it must be present - rebuild"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the image - the 1005 cadence witness regressed - rebuild"; exit 1; }
done
echo "   (0g) the four cadence symbols are DEFINED T/t in the image"
# (0h) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal x1.
_d4_upc=$(strings out/stage90/xnu_arm_entry.elf | grep -c '^xnu_live_sleh_upc$' || true)
echo "   (0h) xnu_live_sleh_upc literal x$_d4_upc (want 1)"
[[ "$_d4_upc" == 1 ]] || {
  echo "REFUSING: the 1004 abort trace is NOT in the image (upc x$_d4_upc want 1) - 1015's set is 1014's verbatim, so it must be present - rebuild"; exit 1; }
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
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1015.sh."
  exit 1; }
echo "   (0i) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1015 AT/PAR PERMISSION WITNESS (the MMU's own answer for the storm VA, and the permission inputs the walker reads)"

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
# 1015 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1015-$(date -u +%Y%m%d-%H%M%S).txt"
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
echo "    and the 1004..1013 clauses are kept as regressions; the [WIT] and flush sections are 1013/1014's"
echo "    own readings - and python3 tools/check_1014_capture.py <capture> for the TLB-FLUSH verdict."
echo "    *** THE HEADLINE THIS RUNG TESTS IS 1015b'S OWN CELL, AND IT IS READ FIRST: ***"
echo "    python3 tools/check_1015b_capture.py <capture>"
echo "      -> 'G-CONFIRMED'  THE STORM STOPPED  => H-G CONFIRMED IN BEHAVIOUR."
echo "      -> 'G-REFUTED'    THE STORM PERSISTED => H-G REFUTED IN BEHAVIOUR (a POSITIVE reading: it"
echo "         points at a storm ROW, never at an absence).  Next: 1015's passive ladder P-X1 (every"
echo "         input correct and it STILL faults), then H-X - the CPU's own state (_wdsctlr), not"
echo "         another table read."
echo "      -> 'G-WITHHOLD'   names the exact missing/contradictory key.  NEVER a negative."
echo "    READ FOUR THINGS BEFORE BELIEVING A G-CONFIRMED:"
echo "      (i)   THE TRUNCATION GUARD.  A log that just STOPS is what a stopped storm looks like in"
echo "            bytes, so the reader requires the capture to be structurally complete before it will"
echo "            call a missing storm a stopped one.  Do not re-read a WITHHOLD as a CONFIRMED."
echo "      (ii)  THE ATTRIBUTION GUARD.  1015b CHANGES KERNEL BEHAVIOUR, so the stop is evidence about"
echo "            THIS arm only if a \`_wfkind=1\` row (1014's W-FLUSH: 0 = Apple's shared arm, 2 ="
echo "            Apple's ASID arm, 1 = the WIDENED arm) has \`_wfva\` or \`_pcfar\` EQUAL to the storm VA."
echo "            Without it the stop is UNATTRIBUTED - the widened arm may never have been reached by"
echo "            the faulting pmap, and a different dyld or a shorter run produces the same absent"
echo "            rows.  (1014's capture carries \`_wfkind=1\` rows on OTHER VAs: that is the trap.)"
echo "      (iii) THE STORM VA IS RECOVERED BY VALUE, NOT MATCHED TO A LITERAL.  The storm's \`far\`"
echo "            moves per run - 1014 read 0xe6000, 1015 read 0x27000, 1011 read 0x61000 - so a reader"
echo "            keyed to a literal would answer about the wrong address.  If you see a verdict that"
echo "            names the VA, check it against the capture's own tail rows."
echo "      (iv)  THE LADDER'S REACH.  If the cap was hit BEFORE the storm's band, that is a READING"
echo "            (the reader reports MAX \`_pcn\`), not a withhold - and it is NOT a G-CONFIRMED, because"
echo "            a ladder that never reached the band never saw the storm either way.  A full TLBIALL"
echo "            per flush is strictly more work than a TLBIASID: watch this."
echo "    AND 1015's AT/PAR READING IS RE-RUN OVER THE SAME CAPTURE, so the two can be held together:"
echo "    V-AT1 plus a STOPPED storm is the strongest pair this rung can produce."
echo "    THE INHERITED CELL, unchanged: 1015's AT/PAR reading, python3 tools/check_1015_capture.py"
echo "    <capture>.  Read it from the TAIL rows' OWN xnu_live_abt_wdatpar / wdatparpr / wdctlat / wdatok"
echo "    (ordinals _wdn >= 0x400 with _wdva == 0xe6000), NEVER a whole-run aggregate (the 1008/1009"
echo "    measurement defect, not repeated).  THE GATE IS READ FIRST - _wdatok is the leg's gate (1 iff"
echo "    the three AT/PAR pairs ran AND the control passed T-CTL); _wdatok == 0 => V-AT0/P-AT and NO"
echo "    V-AT VERDICT, whatever else is in the capture.  THE CONTROL IS _wdctlat, the T-CTL NULL"
echo "    CONTROL - it is the AT pair aimed at the W-SYNC control VA (PA 0x80080000), NOT the storm's"
echo "    VA, and it must come back par&1 == 0 with par&0xfffff000 == 0x80080000; if it does not, or it"
echo "    is unpublished, the AT instrument itself is broken and NOTHING is concluded.  THE V-AT CELLS:"
echo "      V-AT0  _wdctlat not as above, OR unpublished, OR _wdatok == 0  =>  AT_INSTRUMENT_BROKEN:"
echo "        WITHHOLD V-AT1..4 (and the whole passive ladder)."
echo "      V-AT1  _wdatpar bit0 == 1 (the USER-WRITE AT FAULTS) while _wdatparpr bit0 == 0  =>"
echo "        H-G CONFIRMED AT HARDWARE - the CPU itself faults a write the walked leaf PERMITS"
echo "        (ap=0b11, apx=0, domain=0, nG=0); the privileged view succeeds, so the translation in use"
echo "        is NOT the leaf the walk reads.  With _leafng == 0 (GLOBAL) the culprit is an entry"
echo "        surviving TLBIASID (1014's F2)."
echo "      V-AT2  _wdatpar bit0 == 0 AND (_wdatpar & 0xfffff000) == 0x841e9000 (with _wdatparpr bit0 =="
echo "        0)  =>  TRANSLATION IS CORRECT YET THE STORE FAULTED: H-G REFUTED.  The hardware"
echo "        translates the write to EXACTLY the leaf's PA (1014's _wwleaf=0x841e91bb) => the FS=15 is"
echo "        not produced by this translation; the memo/keying (g_abtpte_curva) or the DFSR/DFAR pair"
echo "        describes a different access.  A PROBE DEFECT - re-rank to the SCTLR cause (_wdsctlr),"
echo "        do not read it as a page-table defect."
echo "      V-AT3  _wdatpar bit0 == 0 AND (_wdatpar & 0xfffff000) != 0x841e9000 (with _wdatparpr bit0 =="
echo "        0)  =>  WRONG TRANSLATION IN THE TLB: the VA translates successfully but to a PA that is"
echo "        not the leaf's.  READ THE PA AND COMPARE IT BY VALUE - it names the STALE PA.  Same"
echo "        conclusion as V-AT1, from the PA side."
echo "      V-AT4  BOTH _wdatpar and _wdatparpr fault (bit0 == 1)  =>  BOTH VIEWS FAULT: the user-AP"
echo "        story is not the whole story (the privileged view is refused too) - a domain/invalid"
echo "        mechanism.  RE-RANK TO THE SCTLR CAUSE before any fix."
echo "    AND THE PERMISSION-INPUTS LADDER (passive, same press), withheld unless _wdctlok == 1 and"
echo "    _wdseen == 1 (and _wdmapok == 1 for the lineage cells): _leafng == 0 is the H-G PRECONDITION"
echo "    (published so the leaf need not be decoded; _leafng == 1 KILLS H-G by value - an ASID-tagged"
echo "    leaf WOULD be evicted by TLBIASID); _wddacr & 3 == 1 (DACR_CLIENT - P-D3 kills H-P's domain"
echo "    form) vs != 1 (P-D2 DACR_NOT_CLIENT); _wddom == 0 (P-D1 fires on nonzero); _wdttbcr & 7 == 2"
echo "    (P-T1 TTBCR_NOT_N2) with _wdsplit (P-T2 ROUTED_TO_TTBR1); _wdttbr0 masked 2^(14-n) =="
echo "    0x81a86000 (P-T3 TTBR0_MOVED_AT_ABORT); _wdthrmap == _wdttbr0 (P-M1 THREAD_PMAP_NE_TTBR0);"
echo "    _wdap == 0b11 and _wdapx == 0 and _wdsmall == 1 (P-L1 LEAF_NOT_USER_WRITABLE / P-L3"
echo "    NOT_A_SMALL_PAGE); _wdsctlr bits TRE(28)/AFE(1)/M(0) vs the console-install reading (P-S1"
echo "    SCTLR_MOVED - H-X is live).  P-X1 INPUTS_ALL_CORRECT_STILL_FAULTS needs every one of those"
echo "    killed AND V-AT2 - then the next rung is the OBSERVER, not the pmap."
echo "    _wdatparpr is the STORM VA's privileged read and _wdctlat is the W-SYNC control VA's - if a"
echo "    row's _wdatparpr carries PA 0x80080000 the recorder reused one key for two readings (one key,"
echo "    two definitions): withhold, it is a build defect, not a reading."
echo "    ALSO read wwpre/wwpost/wwmoved (1014-§6, free here): the pre- and post-call pm_l1_phys bases,"
echo "    so 1014's _wwnoproof is a reading rather than a bare 1."
echo "== DONE =="
