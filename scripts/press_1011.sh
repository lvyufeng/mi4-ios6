#!/usr/bin/env bash
# ====================================================================================================
# press_1011.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1010.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1011 = 1010c's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC, SUBMAP_PMAP_SKIP, CADENCE, STALL, ABORT_TRACE, FREEZE_WITNESS,
# ABT_SPAN, ABT_FAR, ABT_PC and ABT_PTE stay 1) plus ONE new switch STAGE90_XNU_VME_PROT.  1011 is a
# MEASUREMENT rung (no kernel behaviour change): it reads the ONE CELL 1010c could not - the
# vm_map_entry's EFFECTIVE PROTECTION for the storm's fault (cell (E')), beside the faulting thread's
# OWN L1 base (cell (A)).  (E') `__wrap_vm_map_lookup_locked` publishes the out-param `out_prot` - the
# entry's effective protection `vm_map_lookup_locked` computes (vm_map.c:9061 `prot = entry->protection`,
# :9139 demotes it for a read fault on a `needs_copy` entry, :9167 `*out_prot = prot`) - under
# `xnu_live_abt_{vmen,vmekr,vmeprot,vmefault,vmeva}` plus the change-count `xnu_live_abt_vmenx`.  (A)
# `stage90_abt_thread_l1_base()` publishes the faulting thread's own L1 physical base (via
# `current_thread()` -> `thread->map` -> `map->pmap` -> `pm_l1_phys`) under `xnu_live_abt_ptethr`, beside
# `xnu_live_abt_ptettbr` (the live TTBR0 the walk sampled).
#
# WHY (the 1010c rung, the baseline, and the frontier 1010c named).  1010c's press read
# (records/1010c-press-read.txt, arm armed-d13-002b0683) found the walk WINDOW-BOUNDED but reading the
# WRONG TABLE at the storm: with the mask fixed to 0xffffc000 it reads the TRUE L1 base 0x81a84000,
# whose slot for far=0x5d000 holds a SECTION descriptor (`ptel1=0xfeedface`, bits[1:0]=0b10).  FS=15 (a
# level-2 permission fault) requires a COARSE L1, so the walk returns early (`entry_stubs.c:2291`,
# `(l1e & 3) != 1`) and the L2 LEAF (its APX bit) is NEVER READ - F1a (the entry grants write but the
# write is lost prot->PTE) and F1b (the accept path's UNCONDITIONAL `kr = KERN_SUCCESS`, vm_fault.c:2478,
# is the wrong condition) are UNDECIDABLE from 1010c.  1011 decides them WITHOUT a tree patch and
# WITHOUT a struct offset: `prot & VM_PROT_WRITE` => F1a; `!(prot & VM_PROT_WRITE)` => F1b.
# The 1011 change is SOURCE-ONLY, across THREE files plus ONE conditional `--wrap` (NO tree edit, NO
# stage driver): the (A) reader + the (E') recorder `entry_note_abt_vme` + the `g_abtpte_vmelatch` in
# src/entry/entry_stubs.c (args BY VALUE - they forward to `entry_write_kv`, the DIRECT cap-exempt
# channel); the declaration + the NEW `__wrap_vm_map_lookup_locked` (pure pass-through except behind the
# latch) in src/entry/entry_trace.c; `STAGE90_PMAP_L1_PHYS 0` in src/entry/entry_saved_state.h (D13-only;
# 4570's `struct pmap` leads with `tte`); and `--wrap=vm_map_lookup_locked` added to `TRACE_LDFLAGS` ONLY
# when `VME_PROT=1` in src/entry/build_entry.sh (so the OFF image carries no whole-kernel-lookup wrap -
# the §8.1 wrap surface).  All gated `#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR &&
# defined(STAGE90_XNU_ABT_PC) && STAGE90_XNU_ABT_PC && defined(STAGE90_XNU_ABT_PTE) &&
# STAGE90_XNU_ABT_PTE && defined(STAGE90_XNU_VME_PROT) && STAGE90_XNU_VME_PROT` so OFF is byte-identical
# (1010c's arm stays reproducible).  STAGE90_XNU_VME_PROT is in NEITHER $EXTRA NOR the exported pool
# defines: only the TWO entry-toolchain objects read it.  build_entry.sh REFUSES VME_PROT=1 unless
# ABT_FAR=1 AND ABT_PC=1 AND ABT_PTE=1.  Design: docs/1011-design-v2.md.
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
# 1011 PRESS PRE-REGISTRATION
# ====================================================================================================
# SWITCH SET.  1010c's verbatim (STAGE90_XNU_ABT_PC=1, STAGE90_XNU_ABT_FAR=1, STAGE90_XNU_ABT_SPAN=1,
# STAGE90_XNU_FREEZE_WITNESS=1, STAGE90_XNU_CADENCE=1, STAGE90_XNU_STALL=1, STAGE90_XNU_DYLD_WITNESS=1,
# STAGE90_XNU_SHREG_PROBE=1, STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1,
# STAGE90_XNU_USVC=1, STAGE90_XNU_SUBMAP_PMAP_SKIP=1, STAGE90_XNU_ABORT_TRACE=1, USB ladder
# PROBE/DEV/ENUM/STREAM=1, ABT_PTE=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0).  NEW
# switch STAGE90_XNU_VME_PROT=1 (the (E') vm_map_entry effective protection + the (A) faulting thread's
# own L1 base).  CADENCE=1, STALL=1, USVC=1, SUBMAP_PMAP_SKIP=1, ABORT_TRACE=1, FREEZE_WITNESS=1,
# STAGE90_XNU_ABT_SPAN=1, STAGE90_XNU_ABT_FAR=1, STAGE90_XNU_ABT_PC=1, STAGE90_XNU_ABT_PTE=1 AND
# STAGE90_XNU_VME_PROT=1 must ALL be in the record.
#
# (0) THE 1011 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the FORTY-THREE (43)
#     `xnu_live_abt_` key literals (1010c's 36: the 6 span: n/seq/gap/call/maxcall/maxgap; the 5 far:
#     far/type/farn/maxfar/maxtype; the 7 pc: pcn/pc/fsr/abtcpsr/pcfar/nfar/nfsr; the 12 pte/vmf:
#     pten/pteva/ptel2/ptel1/ptettbr/pteu/ptenx/vmfn/vmfret/vmfva/vmfprot/vmfnret; the 6 1010c walk-window:
#     pteoow/ptealias/ptemapok/pteslot/ptewbase/ptewlen; PLUS the 6 1011 vme:
#     vmen/vmekr/vmeprot/vmefault/vmeva/vmenx; PLUS the 1 1011 ptethr) embedded in
#     out/stage90/xnu_arm_entry.elf, AND `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked` a
#     DEFINED text symbol each (a generated stand-in would show 0), AND exactly ONE `b|bl
#     <entry_note_abt_pte>` INSIDE `__wrap_sleh_abort` ([[mi4-hooks-live-at-calls]] - inlined code
#     between calls is unreachable; the call is the wrapper's LAST statement, so a TAIL `b` is the
#     expected spelling and a `bl`-only reader false-refuses), exactly as build_1011.sh's delta clause and
#     scripts/park_1011.sh assert.  If the change is NOT in the image, NOTHING below may be read as a
#     test - rebuild.
# (0a) THE 1009 FAULTING-PC IDENTITY IS A REGRESSION: the seven pc `xnu_live_abt_` key literals (inside
#     the (0) FORTY-THREE (43)) AND `entry_note_abt_pc` a DEFINED text symbol AND exactly ONE `b|bl
#     <entry_note_abt_pc>` inside `__wrap_sleh_abort` (after the far call) must STILL be present (1011's
#     set is 1010c's verbatim).  A x0 means the pc witness was dropped - rebuild.
# (0b) THE 1008 ABORT-FAULT IDENTITY IS A REGRESSION: the five far `xnu_live_abt_` key literals (inside
#     the (0) FORTY-THREE (43)) AND `entry_note_abt_far` a DEFINED text symbol AND exactly ONE `b|bl
#     <entry_note_abt_far>` inside `__wrap_sleh_abort` must STILL be present.  PLUS the §4.3 fix
#     ([[mi4-1007-abort-span-rung]]): out/stage90/xnu_arm_entry_stubs.o must STILL have NO
#     `U entry_counter` (the §2.3 terminal stand-in that would END THE RUN on the abort path stays gone).
# (0c) THE 1007 SPAN WITNESS IS A REGRESSION: the SIX span `xnu_live_abt_` key literals (inside the (0)
#     FORTY-THREE (43)) AND `entry_note_abt_span` a DEFINED text symbol AND exactly ONE `b|bl
#     <entry_note_abt_span>` inside `__wrap_sleh_abort` must STILL be present.
# (0d) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals (n/d/cpsr/ien/
#     pend/ctlr) and the two `xnu_live_frzbase_` key literals (n/d) AND the two symbols
#     `entry_freeze_sample` / `entry_gic_freeze_state` must STILL be in the image x0 each.
# (0e) THE 1005 CADENCE WITNESS IS A REGRESSION: the four `xnu_live_cadence_` key literals (t/sync/tick/
#     irq) AND the four symbols `entry_board_tick_count` / `entry_board_irq_count` / `entry_cadence_sample`
#     / `entry_cadence_summary` must STILL be in the image.
# (0f) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal must STILL be in the
#     image x1.
# (0g) THE 1002 r3-SAVE FIX IS A REGRESSION: locore_fleh_swi's prologue must STILL be the six-word window
#     `push {r0, r1, r2, r3, r8, lr}` / `bl <entry_note_user_svc>` / `pop {r0, r1, r2, r3, r8, lr}`.  A
#     FOUR-word window means the fix regressed - rebuild.
# (F) THE FRONTIER UNDER TEST - WHICH SIDE OF THE vm_map-vs-PTE DISAGREEMENT IS WRONG.  records/
#     1009-press-read.txt, BY VALUE: the storm is a USER-mode WRITE permission fault, CONSTANT from
#     ordinal 0x400 through 0x200000 (~2.09 M aborts): `pc=0x2be8a1a0` (in the dyld shared cache),
#     `fsr=0x0000080f` (WnR=1 (a write) | FS=0xf "permission fault, level 2"), `cpsr=0x60000030` (USER
#     mode, IRQ on), `far=0x9a000` (a LOW user VA, below the TTBR0 split 0x40000000, in one of launchd's
#     own COW/data segments).  The handler chose the SILENT retry branch (the failure branch prints), so
#     `vm_fault` returned KERN_SUCCESS || KERN_ABORTED and the retry is forever.  The mechanism the
#     coordinator NAMED: `vm_fault` returns KERN_SUCCESS while the L2 PTE was made READ-ONLY inside the
#     code-signing CS-taint branch (osfmk/vm/vm_fault.c:2476-2485, `reject_page==FALSE => kr=KERN_SUCCESS;
#     must_disconnect=!m->cs_tainted; m->cs_tainted=TRUE;`), then `pmap_enter_options` writes APX=1
#     (arm/pmap.c:1110-1113, :3280).  THE DECISIVE CELL IS THE (A) L2 APX.
#     THE READING (keys (A) xnu_live_abt_pten / _pteva / _ptel2 / _ptel1 / _ptettbr / _pteu and the
#     change-count _ptenx; (B) xnu_live_abt_vmfn / _vmfret / _vmfva / _vmfprot and the change-count
#     _vmfnret).  Pair each _ptel2 row to the pc/far rows by KEY EQUALITY (pten == pcn == farn at a
#     paired row - the recorders advance one per abort).  THE HEADLINE (A) is the L2 descriptor at the
#     TAIL ordinals: DECIDE CONSTANT-vs-CYCLING ON THE TAIL ROWS' OWN _ptel2 (ordinals >= 0x400), NEVER
#     on the whole-run change-count _ptenx (the 1008/1009 measurement defect, NOT repeated).  Decode
#     _ptel2: bits[1:0] type (0b10 = small page), bit 9 = APX (1 = user READ-ONLY), bit 5 = USER, bit 0 =
#     NX, bits[31:12] = PA.
#       (A) APX=1  - the page the write faulted on is USER READ-ONLY (bit 9 set) => "the PTE is
#                    read-only though vm_fault said success" - the cs-taint/prot-downgrade path.  THE
#                    EXPECTED ROW.
#       (A) APX=0  - the PTE is WRITABLE => the disagreement is NOT the PTE (vm_fault itself returned a
#                    false success, or the retry is elsewhere) - the falsifier.
#       (B) _vmfret - KERN_SUCCESS=0 vs KERN_ABORTED=14: splits "vm_fault said success" (the retry is on a
#                    success return) from "the handler retried an ABORTED return".  The only source of
#                    that distinction.
#     This is a pure MEASUREMENT: the run is EXPECTED to still freeze (1011 changes no kernel behaviour,
#     absent the (B) surface - see §8.1) - the PTE's identity is the finding.
#     FALSIFIERS (design §6): (a) NO `xnu_live_abt_pte*` keys at all while `xnu_live_abt_n` is present =>
#     the ABT_PTE half did not reach the image (rebuild); (b) `xnu_live_abt_pten` GAPS from
#     `xnu_live_abt_n` at a paired row => the pte recorder did not run once per abort - an INSTRUMENT
#     failure; (c) the walk returns 0 (no `_ptel2` row) while `xnu_live_abt_n` is present => the address
#     was not a small-page L2 entry (a section, or TTBR1 was used) - read `_pteu`/`_ptettbr` before
#     judging; (d) the tail `_ptel2` NOT constant => different pages were faulted (NOT the single-address
#     loop 1009 saw) - the strongest refutation of the constant-fault reading; (e) tail `_ptel2` APX=0 =>
#     the PTE is writable (the falsifier of the coordinator's cell); (f) `_vmfnret` > 1 with mixed
#     `_vmfret` => vm_fault returned different codes (the retry is not on one code); (g) `_ptel2` a
#     value whose type bits are not 0b10 => the walk's descriptor read is mismatched
#     ([[mi4-measurement-defects]]).
# (0h) THE 1010c WALK WINDOW IS A REGRESSION-AND-A-WITNESS.  The walk is now WINDOW-BOUNDED: both
#     derefs (the L1 slot and the L2 leaf) are bounded to [gPhysBase, gPhysBase + mem_size) and FAIL
#     OPEN; an out-of-window L2 leaf is reached through a temporary 1 MB section installed by
#     `entry_mmio_section` and RESTORED into `g_live_mmio_l1`.  The SIX new key literals
#     `xnu_live_abt_{pteoow,ptealias,ptemapok,pteslot,ptewbase,ptewlen}` MUST be in the image (part of
#     the (0) FORTY-THREE (43)).  `_pteoow`=1 on a tail row means the L2 table lay OUTSIDE the window (the
#     1010b finding, now a READING not a fault): `_ptemapok`=1 => the leaf was reached through the alias
#     at `_ptealias`; `_ptemapok`=0 => BOTH alias slots were occupied and the leaf is UNREAD there (a
#     COVERED refusal, NOT a fault and NOT a truncation).  The walk must NOT truncate (a `_pcn` ordinal
#     with NO `_pten` row) - under 1010c that would mean the window bound itself did not hold.
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np ret=12`,
#     the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the whole capture
#     must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr>
#     size=<ptr> lr=<ptr>`, BOUNDED (max 8).  EXPECTED >= 1; most likely EXACTLY 2, obj = 999's two
#     VMHINT991 objects 0xc0005b48 / 0xc0005ab0.  ABSENCE while the (4) gate holds = rebuild.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1011_capture.py carries the gate: its [0] DEATH GATE + the kept
#     1004/1005/1006/1007/1008/1009 clauses): no panic + the exec-done trio + the five probe keys +
#     records after the exec-returned point.  An L2-descriptor verdict is a READING taken from a live
#     image, so it counts only if the boot reached the print site (the [ABT4] section never certifies on
#     a dead run).
# (5) THE 1006 FREEZE WITNESS (kept a regression; keys xnu_live_freeze_* / xnu_live_frzbase_*).  1006
#     read it as a VALID SILENCE (zero freeze records, the idle path never entered) - 1011 expects the
#     SAME silence (a pure measurement changes nothing).
# (6) THE 1005 CADENCE WITNESS (kept a regression AND a second reading).  Keys xnu_live_cadence_t / _sync
#     / _tick / _irq plus the entry_cadence_summary() triples.  The ratio ds/max(dt,1) is the (b1)/(b2)/
#     full-stop partition; 1011 expects the SAME (b2) shape (a pure measurement changes nothing).
# (7) THE ABORT TRACE (1004, kept).  The full ordered sleh sequence (keys xnu_live_sleh_seq / _type /
#     _fsr / _far / _thr / _upc): a `far` that REPEATS = LOOP; a `far` that strictly ADVANCES = WALK.
# (8) THE USER-SVC FRONTIER (kept; keys xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr / _r0).
#     `_pid` = 1; `_pc` a user VA; `_cpsr` a USER-mode CPSR.
# (9) THE PET (kept).  The feed gate is `count >= bark/2` = 14.5 s.  If 1011 lives past ~14.5 s with the
#     read door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` MUST appear.
# (10) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
#     thinning ordinal stays 64.  The sleh caps stay RAISED to 512 under 1004's switch.
# (11) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) the (0) change is NOT in the image =>
#     REBUILD; (b) the 230x `usermode data abort at 0x4` recurs past `check_np ret=12` => the 1002
#     r3-save fix REGRESSED - rebuild; (c) `xnu_live_abt_pte*` x0 in the capture while `xnu_live_abt_n`
#     is present => the ABT_PTE half did not reach the image (the (0) rebuild signal); (d)
#     `xnu_live_abt_pten` GAPS from `xnu_live_abt_n` at a paired row => an INSTRUMENT failure; (e) a
#     NON-MONOTONE `xnu_live_abt_n` => a MEASUREMENT defect; (f) death ELSEWHERE, later => the frontier
#     ADVANCED - record by value.  The FROZEN REGRESSION GUARDS - the geometry line exactly once,
#     `IOS7LAB cache mapping` x4, the shared region x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`,
#     `IOS7SHREG map_slide ret=0` - are re-verified only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent - read the
# skipped-block shape IN THE IMAGE before any judgement; (c) the log ends with a capped key and no
# direct key after it = rebuild; (d) the log ends in a direct record = that record's moment IS the
# frontier finding this rung was built to produce - read the last direct keys (abt-pte last, then
# abt-pc, then abt-far, then abt-span, then freeze, then cadence, then sleh, then usvc, then pet) as the
# frontier, and classify the PTE by the (A)/APX/(B) partition from the pten/ptel2 ladder and the
# vmfnret change-count; (e) the (0) change is not in the image = the witness is not in the image -
# rebuild, nothing else is a test.
# ====================================================================================================
if ! grep -q '^# 1011 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1011 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003/1004/1005/1006/1007/1008/"
  echo "          1009/1010 pattern: switch set / THE POINTS / failure signatures / falsification / regression"
  echo "          guards). The build side: scripts/build_1011.sh + scripts/park_1011.sh + the 1011"
  echo "          clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-002b0683 ]] && {
  echo "REFUSING: out/stage90 holds 1010c's arm (002b0683) - the PRESSED predecessor (VERDICT: the walk is"
  echo "WINDOW-BOUNDED and reaches an OUT-OF-WINDOW L2 table; with the mask fixed to 0xffffc000 it reads the"
  echo "TRUE L1 base 0x81a84000, but the storm's L1 slot holds a SECTION descriptor ptel1=0xfeedface"
  echo "(bits[1:0]=0b10), so the walk returns early (entry_stubs.c:2291, (l1e&3)!=1) and the L2 LEAF (and"
  echo "its APX bit) is NEVER read - F1a/F1b UNDECIDABLE; adjudicated in records/1010c-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-64391319 ]] && {
  echo "REFUSING: out/stage90 holds 1010b's arm (64391319) - the PRESSED predecessor (VERDICT: the TTBR0"
  echo "mask fix is CORRECT but it exposed a SECOND defect in the SAME walk - the corrected base reads the"
  echo "TRUE L1, whose L2 table is at PA 0x46300000, BELOW the entry image's identity window"
  echo "[0x80000000,0x9e400000); the walk dereferences it with NO window bound, so the L2 leaf load faults"
  echo "FS=5 (pc=0x80002644 = its own ldr) and the run DIES at launchd-start BEFORE the storm; the leaf is"
  echo "STILL unread; adjudicated in records/1010b-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-0f39207c ]] && {
  echo "REFUSING: out/stage90 holds 1010's arm (0f39207c) - the PRESSED predecessor (VERDICT: the storm"
  echo "reproduced, but the (A) L2 walk is INVALID - it read ptel2=0/pteu=0 at the tail (impossible under"
  echo "fsr=0x80f, FS=15 level-2 permission fault) because the TTBR0 base was masked with the L1->L2 mask"
  echo "0xfffffc00 instead of the base mask 0xffffc000; adjudicated in records/1010-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-24997fc5 ]] && {
  echo "REFUSING: out/stage90 holds 1009's arm (24997fc5) - the PRESSED predecessor (frontier: the storm"
  echo "is a USER-mode WRITE permission fault - pc=0x2be8a1a0 in the dyld shared cache, fsr=0x0000080f ="
  echo "WnR=1|FS=0xf, cpsr=0x60000030, far=0x9a000 a LOW user VA, constant from ordinal 0x400 through"
  echo "0x200000 - the handler chose the SILENT retry branch, so vm_fault returned KERN_SUCCESS||"
  echo "KERN_ABORTED; adjudicated in records/1009-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-0343ef45 ]] && {
  echo "REFUSING: out/stage90 holds 1008's arm (0343ef45) - the PRESSED predecessor (frontier: the storm"
  echo "is a REPEAT at the CONSTANT far=0x0007b000, type=4 DATA abort, from ordinal 0x400 through"
  echo "0x200000 - ~2.09 M retries of one fault address, riding the ~18.7 s handler call; adjudicated in"
  echo "records/1008-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-88074ab3 ]] && {
  echo "REFUSING: out/stage90 holds 1007's arm (88074ab3) - the PRESSED predecessor (frontier: the"
  echo "~24.4 s freeze is INSIDE ONE sleh_abort - maxcall 18.735 s = 0.768 x M - riding a FAULT STORM"
  echo "of ~2.1 M aborts whose fault ADDRESS is in no capture; adjudicated in"
  echo "records/1007-press-read.txt). Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-31bdebf3 ]] && {
  echo "REFUSING: out/stage90 holds 1006's arm (31bdebf3) - the PREVIOUS predecessor (frontier: the"
  echo "~24.4 s freeze does NOT overlap the idle path - __wrap_machine_idle is never entered;"
  echo "adjudicated in records/1006-press-read.txt). Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-ff937225 ]] && {
  echo "REFUSING: out/stage90 holds 1005's arm (ff937225) - the PRESSED predecessor (frontier: the STALL"
  echo "is (b2) a whole-interrupt-path freeze ~24.4 s; adjudicated in records/1005-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-582a19c8 ]] && {
  echo "REFUSING: out/stage90 holds 1004's arm (582a19c8) - the PRESSED predecessor (frontier: the"
  echo "STALL after dyld first touches a shared-cache page; adjudicated in records/1004-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - already pressed 2026-10-13 (pid 1 in the"
  echo "kernel's data-abort return path; adjudicated in records/1003-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt). Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1011 (scripts/build_1011.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1011 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1011 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip, 1003's stall + 1004's abort-trace instrument + 1005's cadence witness + 1006's freeze witness + 1007's abort-span witness + 1008's abort-fault identity + 1009's faulting-pc identity AND 1011's (E-prime) entry-protection + (A) thread-L1-base identity =="
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
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004/1005/1006/1007/1008/1009/1010)
STAGE90_XNU_ABORT_TRACE the abort trace - raised sleh caps + the frame's user pc (1004's; kept verbatim)
STAGE90_XNU_CADENCE the cadence witness - a timer-independent (syscall) sample + the all-interrupt counter (1005's; kept verbatim)
STAGE90_XNU_FREEZE_WITNESS the idle-path freeze witness - the dwell sampler + the GIC/CPSR triple (1006's; kept verbatim)
STAGE90_XNU_ABT_SPAN the abort-span witness - the gap + handler duration per sleh_abort (1007's; kept verbatim)
STAGE90_XNU_ABT_FAR the abort-fault identity - the fault ADDRESS + TYPE of the storm's aborts on the DIRECT channel (1008's; kept verbatim)
STAGE90_XNU_ABT_PC the faulting-pc identity - WHICH instruction faults + the DFSR + the change-counts on the DIRECT channel (1009's; kept verbatim)
STAGE90_XNU_ABT_PTE the L2-descriptor + vm_fault-return identity - walks TTBR0 to the L2 small-page descriptor for the fault VA and reads what vm_fault returned (1010c's; kept verbatim)
STAGE90_XNU_VME_PROT the vm_map_entry effective protection + the faulting thread's own L1 base - the (E') __wrap_vm_map_lookup_locked publishes out_prot; the (A) reader publishes pm_l1_phys (1011's; a pure measurement rung)
EOF
echo "   (record carries 1010c's verbatim switch set PLUS the new STAGE90_XNU_VME_PROT; USVC, SUBMAP_PMAP_SKIP, STALL, ABORT_TRACE, CADENCE, FREEZE_WITNESS, ABT_SPAN, ABT_FAR, ABT_PC and ABT_PTE all on)"

echo "== 2-0. the 1011 (E') vm_map_entry-protection + (A) thread-L1-base in the image (read by value BEFORE the press) + the 1010c L2-descriptor/walk-window, the 1009 pc identity, the 1008 far identity, the 1007 span witness, the 1006 freeze witness, the 1005 cadence witness, the 1004 abort trace and the 1002 r3-save fix as regressions =="
# (0) THE 1011 CHANGE IS IN THE IMAGE: the FORTY-THREE (43) `xnu_live_abt_` key literals embedded (the 6 span
#     + the 5 far + the 7 pc + the 12 pte/vmf + the 6 1010c walk-window + the 6 1011 vme + the 1 1011
#     ptethr) AND the `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked` DEFINED text symbols AND exactly
#     ONE `bl <entry_note_abt_vme>` INSIDE `__wrap_vm_map_lookup_locked` ([[mi4-hooks-live-at-calls]]).
_abt_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_abt_' || true)
echo "   (0) xnu_live_abt_* literals x$_abt_lit (want 43: 1010c's 36 + 6 vme vmen/vmekr/vmeprot/vmefault/vmeva/vmenx + 1 ptethr)"
[[ "$_abt_lit" == 43 ]] || {
  echo "REFUSING: the 1011 (E')/(A) store or the 1010c L2-descriptor + walk-window store is NOT in the image (literals x$_abt_lit want 43) - rebuild"; exit 1; }
# NOTE: `nm | grep -q` under `set -o pipefail` is a trap - `grep -q` exits on the first match, SIGPIPEs
# `nm`, and the pipeline's status becomes 141, vetoing a match that exists (the 487/998 SIGPIPE lesson).
# Capture `nm` ONCE into a variable and grep the variable: no pipeline, no SIGPIPE.
_abt_nm=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf)
for _abt_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_abt_1010sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_abt_1010sym is not a DEFINED text symbol in the image - an object did not rebuild or a stand-in was linked - rebuild"; exit 1; }
done
echo "   (0) entry_note_abt_pte / entry_note_abt_vmf / __wrap_vm_fault are DEFINED T/t in the image"
# The witness's ONE call site: `__wrap_sleh_abort` carries exactly one entry_note_abt_pte call (the
# abort path IS the witness; a second site would publish a fault the classifier is not about).  The call
# is the wrapper's LAST statement, so a TAIL `b` is the expected spelling - accept `b` OR `bl`.
_ab_a=$(awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_sleh_abort is not in the image - the abort path the pte witness rides is gone"; exit 1; }
_ab_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ab_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_ab_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ab_a" --stop-address="${_ab_x:-0xffffffff}")
_ab_pte=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' <<<"$_ab_body" || true)
echo "   (0) __wrap_sleh_abort calls entry_note_abt_pte: $_ab_pte (want 1)"
[[ "$_ab_pte" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_pte exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# The vm_fault wrapper's PASS-THROUGH is the §8.1 rule made a check: it must call BOTH the real
# vm_fault body (the pass-through) AND entry_note_abt_vmf (the (B) record).
#     NOTE: `--wrap=vm_fault` renames `__real_vm_fault` -> `vm_fault` for the linked reference, so the
#     disassembly has `bl <vm_fault>`, never `<__real_vm_fault>` (MEASURED this build).
_wfa=$(awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_vm_fault is not in the image - the vm_fault-return witness is gone"; exit 1; }
_wfx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_wfa#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
_wfb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_wfa" --stop-address="${_wfx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$_wfb" || {
  echo "REFUSING: __wrap_vm_fault does not call the real vm_fault body - it is not a pass-through (docs/1010-design.md §8.1) - rebuild"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$_wfb" || {
  echo "REFUSING: __wrap_vm_fault does not call entry_note_abt_vmf - the (B) record never fires - rebuild"; exit 1; }
echo "   (0) __wrap_vm_fault calls the real vm_fault AND entry_note_abt_vmf (the §8.1 pass-through made a check)"
# (0x) THE 1011 (E') CHANGE IS IN THE IMAGE: the `entry_note_abt_vme`/`__wrap_vm_map_lookup_locked`
#      DEFINED text symbols AND exactly ONE `bl <entry_note_abt_vme>` INSIDE __wrap_vm_map_lookup_locked,
#      and its PASS-THROUGH calling the real `vm_map_lookup_locked` (the §8.1 rule: the wrapper MUST
#      return the real lookup's value on the non-abort case).
for _vme_11sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  grep -qE "^[0-9a-f]+ [Tt] $_vme_11sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_vme_11sym is not a DEFINED text symbol in the image - the 1011 (E') instrument is gone (an object did not rebuild or a stand-in was linked) - rebuild"; exit 1; }
done
# NOTE: `--wrap=vm_map_lookup_locked` renames `__real_vm_map_lookup_locked` -> `vm_map_lookup_locked` for
# the linked reference, so the disassembly has `bl <vm_map_lookup_locked>` (MEASURED this build).
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
echo "   (0x) the 1011 (E')/(A) witness is in the image: entry_note_abt_vme/__wrap_vm_map_lookup_locked DEFINED T/t, ONE bl to entry_note_abt_vme, the wrapper calling vm_map_lookup_locked AND entry_note_abt_vme, and the (A) STAGE90_ACT_MAP immediate (956) present (helper symbol: ${_l1_adr:-inlined})"
# (0y) THE 1010c L2-DESCRIPTOR IS A REGRESSION: the three 1010 symbols are DEFINED T/t and the ONE
#      b|bl to entry_note_abt_pte rides __wrap_sleh_abort (its 36 literals are inside the (0) 43).
for _ab_1010sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  grep -qE "^[0-9a-f]+ [Tt] $_ab_1010sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_ab_1010sym is not a DEFINED text symbol in the image - the 1010c instrument regressed (1011 keeps 1010c's set verbatim) - rebuild"; exit 1; }
done
echo "   (0y) the 1010c L2-descriptor/walk-window witness is still DEFINED T/t (regression)"
# (0a) THE 1009 FAULTING-PC IDENTITY IS A REGRESSION: the `entry_note_abt_pc` DEFINED text symbol AND
#      exactly one `b|bl <entry_note_abt_pc>` inside `__wrap_sleh_abort` (its seven pc key literals are
#      inside the (0) FORTY-THREE (43)).  1011's set is 1010c's verbatim.
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_pc\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_pc is not a DEFINED text symbol in the image - the 1009 recorder was dropped (1011 keeps 1010c's set verbatim) - rebuild"; exit 1; }
_ab_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' <<<"$_ab_body" || true)
echo "   (0a) __wrap_sleh_abort calls entry_note_abt_pc: $_ab_pc (want 1)"
[[ "$_ab_pc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_pc exactly once (0 = the call site moved; >1 = it runs twice) - the 1009 faulting-pc identity regressed - rebuild"; exit 1; }
# (0b) THE 1008 ABORT-FAULT IDENTITY IS A REGRESSION: the `entry_note_abt_far` DEFINED text symbol AND
#      exactly one `b|bl <entry_note_abt_far>` inside `__wrap_sleh_abort` (its five far key literals are
#      inside the (0) FORTY-THREE (43)).
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_far\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_far is not a DEFINED text symbol in the image - the 1008 recorder was dropped (1011 keeps 1010c's set verbatim) - rebuild"; exit 1; }
_ab_fc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' <<<"$_ab_body" || true)
echo "   (0b) __wrap_sleh_abort calls entry_note_abt_far: $_ab_fc (want 1)"
[[ "$_ab_fc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_far exactly once (0 = the call site moved; >1 = it runs twice) - the 1008 abort-fault identity regressed - rebuild"; exit 1; }
# (0c) THE 1007 SPAN WITNESS IS A REGRESSION: the `entry_note_abt_span` DEFINED text symbol AND exactly
#      one `b|bl <entry_note_abt_span>` inside `__wrap_sleh_abort` (its six key literals are inside the
#      (0) FORTY-THREE (43)).
grep -qE "^[0-9a-f]+ [Tt] entry_note_abt_span\$" <<<"$_abt_nm" || {
  echo "REFUSING: entry_note_abt_span is not a DEFINED text symbol in the image - the 1007 recorder was dropped (1011 keeps 1010c's set verbatim) - rebuild"; exit 1; }
_ab_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$_ab_body" || true)
echo "   (0c) __wrap_sleh_abort calls entry_note_abt_span: $_ab_sc (want 1)"
[[ "$_ab_sc" == 1 ]] || {
  echo "REFUSING: __wrap_sleh_abort does not call entry_note_abt_span exactly once (0 = the call site moved; >1 = it runs twice) - the 1007 span witness regressed - rebuild"; exit 1; }
# §4.3 THE ARGUMENT-PASSING FIX (1007, kept a regression): entry_stubs.o must NOT carry an UNDEFINED
# `entry_counter` (the §2.3 terminal stand-in).  If a recorder still calls entry_counter(), the stand-in
# can END THE RUN.
_ab_uc=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o | grep -cE '^[[:space:]]*U entry_counter$' || true)
echo "   (0c) xnu_arm_entry_stubs.o U entry_counter: $_ab_uc (want 0 - the §4.3 fix, kept as a regression)"
[[ "$_ab_uc" == 0 ]] || {
  echo "REFUSING: entry_stubs.o still references entry_counter (U x$_ab_uc want 0) - a recorder calls the clock directly; the §4.3 argument-passing fix regressed - rebuild"; exit 1; }

# (0d) THE 1006 FREEZE WITNESS IS A REGRESSION: the six `xnu_live_freeze_` key literals + the two
#      `xnu_live_frzbase_` key literals embedded AND the two DEFINED text symbols.
_frz_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_freeze_' || true)
echo "   (0d) xnu_live_freeze_* literals x$_frz_lit (want 6: n/d/cpsr/ien/pend/ctlr)"
[[ "$_frz_lit" == 6 ]] || {
  echo "REFUSING: the 1006 freeze store is NOT in the image (literals x$_frz_lit want 6) - 1011's set is 1010c's verbatim, so it must be present - rebuild"; exit 1; }
_idl_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_frzbase_' || true)
echo "   (0d) xnu_live_frzbase_* literals x$_idl_lit (want 2: n/d)"
[[ "$_idl_lit" == 2 ]] || {
  echo "REFUSING: the 1006 idle-baseline store is NOT in the image (literals x$_idl_lit want 2) - rebuild"; exit 1; }
for _frz_sym in entry_freeze_sample entry_gic_freeze_state; do
  grep -qE "^[0-9a-f]+ [Tt] $_frz_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_frz_sym is not a DEFINED text symbol in the image - the 1006 freeze witness regressed - rebuild"; exit 1; }
done
echo "   (0d) the two freeze symbols are DEFINED T/t in the image"
# The sampler's ONE call site: `__wrap_machine_idle` carries exactly one entry_freeze_sample call (the
# idle path IS the witness; a second site would time a pass the classifier is not about).
_idl_a=$(awk '$3=="__wrap_machine_idle"{print "0x" $1; found=1} END{exit(found?0:1)}' <<<"$_abt_nm") || {
  echo "REFUSING: __wrap_machine_idle is not in the image - the idle path the freeze sample rides is gone"; exit 1; }
_idl_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_idl_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
_idl_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_idl_a" --stop-address="${_idl_x:-0xffffffff}")
_frz_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' <<<"$_idl_body" || true)
echo "   (0d) __wrap_machine_idle calls entry_freeze_sample: $_frz_sc (want 1)"
[[ "$_frz_sc" == 1 ]] || {
  echo "REFUSING: __wrap_machine_idle does not call entry_freeze_sample exactly once (0 = the call site moved; >1 = it runs twice) - rebuild"; exit 1; }
# (0e) THE 1005 CADENCE WITNESS IS A REGRESSION: the four key literals AND the four DEFINED symbols.
_cad_lit=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_cadence_' || true)
echo "   (0e) xnu_live_cadence_* literals x$_cad_lit (want 4: t/sync/tick/irq)"
[[ "$_cad_lit" == 4 ]] || {
  echo "REFUSING: the 1005 cadence store is NOT in the image (literals x$_cad_lit want 4) - 1011's set is 1010c's verbatim, so it must be present - rebuild"; exit 1; }
for _cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  grep -qE "^[0-9a-f]+ [Tt] $_cad_sym\$" <<<"$_abt_nm" || {
    echo "REFUSING: $_cad_sym is not a DEFINED text symbol in the image - the 1005 cadence witness regressed - rebuild"; exit 1; }
done
echo "   (0e) the four cadence symbols are DEFINED T/t in the image"
# (0f) THE 1004 ABORT TRACE IS A REGRESSION: the `xnu_live_sleh_upc` key literal x1.
_d4_upc=$(strings out/stage90/xnu_arm_entry.elf | grep -c '^xnu_live_sleh_upc$' || true)
echo "   (0f) xnu_live_sleh_upc literal x$_d4_upc (want 1)"
[[ "$_d4_upc" == 1 ]] || {
  echo "REFUSING: the 1004 abort trace is NOT in the image (upc x$_d4_upc want 1) - 1011's set is 1010c's verbatim, so it must be present - rebuild"; exit 1; }
# (0g) THE 1002 r3-SAVE FIX IS A REGRESSION. The prologue of locore_fleh_swi must STILL be six-word.
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
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1011.sh."
  exit 1; }
echo "   (0g) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1011 (E-prime) entry-protection + (A) thread-L1-base identity"

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
# 1011 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1011-$(date -u +%Y%m%d-%H%M%S).txt"
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
echo "   (adjudicate the recovered capture with: python3 tools/check_1011_capture.py <capture>"
echo "    - the [0] DEATH GATE, the [ABT4] L2-descriptor/vm_fault-return section and the 1004..1010"
echo "    clauses are kept as regressions; the NEW [CFFIX] section is 1011's own reading.  THE HEADLINE is"
echo "    the F1a/F1b DECISION from the TAIL rows' OWN xnu_live_abt_vmeprot (ordinals >= 0x400), NEVER a"
echo "    whole-run max/change-count (the 1008/1009 measurement defect, not repeated): vmeprot & 0x2"
echo "    (VM_PROT_WRITE) != 0 => F1a (the entry GRANTS write, so the write is lost entry->PTE); == 0 =>"
echo "    F1b (the entry is read-only, so the accept path's UNCONDITIONAL kr=KERN_SUCCESS, vm_fault.c:2478,"
echo "    is the wrong condition).  Read xnu_live_abt_vmekr (KERN_SUCCESS=0), _vmefault (the VM_PROT_* the"
echo "    fault asked for) and _vmeva (must equal the pte/vmf VA).  THE (A) CELL: compare _ptethr (the"
echo "    faulting thread's own L1 base) with _ptettbr (the live TTBR0 the walk sampled) - AGREE => the walk"
echo "    read the right table; DIFFER => the walk and the handler look at different tables.  Also read the"
echo "    tail _ptel1 (still the 0xfeedface CLASS descriptor = the 1010c frontier) and _ptel2 (readable"
echo "    leaf).  xnu_live_stub_hit_seq==0 proves the §2.3 stand-in did not fire)"
echo "== DONE =="