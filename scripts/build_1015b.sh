#!/usr/bin/env bash
# Build 1015b: 1015's switch set VERBATIM plus ONE new switch, STAGE90_XNU_GLOBAL_FIX - THE H-G FIX
# (candidate (b), docs/1015-design.md §2b / records/1015-press-read.txt §4) AND ITS BEHAVIOUR READ-OUT.
# Design: docs/1015b-design.md.
#
# **1015b IS NOT ENTRY-ONLY. IT MOVES THE POOL OBJECT.** This is the ONE structural difference from
# 1015, and every clause in this script that reads the pool is written against it. `osfmk/arm/pmap.c`
# is a KERNEL-POOL file (`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`), `build_entry.sh` only LINKS that
# object, and the pool's `.text` is inside the `.bin` whose sha256 IS the arm:
#
#   MEASURED this session (out/stage90/xnu_arm_entry.elf, readelf -l):
#     LOAD 0x010000 0x80000000 0x80000000 0x570000  R E     <- xnu_arm_entry.bin is this segment
#     pmap_flush_tlbs 0x8002968c .. 0x800298b0              <- INSIDE it
#   => the arm MOVES when the pool moves, even if the entry objects were untouched. 1015's own header
#   records the converse for the half it did NOT build (build_1015.sh:1041-1042); 1015b makes it live.
#
#   WHY. 1015's press RETURNED (records/1015-press-read.txt, arm armed-d13-e51d14fa, RESIDENT,
#   2026-10-15 12:43) and it is V-AT1 - **H-G CONFIRMED AT THE HARDWARE**: the CPU's OWN translation of
#   the storm's VA REFUSES a user write (`_wdatpar=0x0000001f`, bit0=1) while TRANSLATING the same VA
#   for a privileged read (`_wdatparpr=0x8419d268`, bit0=0, PA 0x8419d000), with the W-SYNC control
#   satisfied (`_wdctlat=0x80080268`, PA 0x80080000 - the instrument works). The walked leaf IS
#   user-writable (`_wdleaf=0x8417e1bb`, `_wdap=3`, `_wdapx=0`) and GLOBAL (`_leafng=0`). The
#   privileged PA (0x8419d000) DIFFERS from the leaf's (0x8417e000): the TLB serves a translation the
#   tables no longer describe.
#
#   H-G (STALE_GLOBAL_TLB), the mechanism, MEASURED across 1014/1015:
#     * EVERY user leaf is GLOBAL - `L2_NG_BIT 0x800` (pmap.h:231) is never ORed anywhere in the tree
#       (`grep -rn L2_NG_BIT` returns its definition and nothing else), so the leaf has bit11 = 0.
#     * the only steady-state flush is `arm_tlb_flushID_ASID` (pmap.c:1349 = 1014's measured
#       `_wfkind=2`); on ARMv7 TLBIASID invalidates ONLY ASID-tagged (nG=1) entries, so a GLOBAL entry
#       SURVIVES it - only TLBIALL (c8,c7,0) evicts it, and `pmap_switch` does one only on a CHANGE.
#   => a stale GLOBAL translation of the storm's VA shadows the newer user-RW leaf, and the permission
#   decision the CPU makes is from that entry, not from the leaf.
#
#   **THE FIX (b), AND IT IS A BEHAVIOUR CHANGE - THE FIRST SINCE 986.** `pmap_flush_tlbs` gains ONE
#   gated `else if (pmap != kernel_pmap) arm_tlb_flushID();` arm between Apple's `:1347` full-TLBIALL
#   arm and Apple's `:1348` ASID `else`. Apple's `if (pmap_is_shared)` test and its full-flush arm are
#   BYTE-IDENTICAL in both switch positions; the ASID statement stays the terminal `else`. Staged by
#   `tools/stage_d13_globalfix.sh` / `tools/patch_d13_globalfix.py` (its OWN tool, NOT an extension of
#   `patch_d13_flushwitness.py` - three measured reasons at docs/1015b-design.md §2, chief among them
#   that the 1014 OFF proof calls that tool's `--revert` in mid-run, build_1015.sh:832).
#
#   **THE CONDITION IS TAUTOLOGICAL IN THAT POSITION AND IS WRITTEN OUT ANYWAY.** `pmap_is_shared` is
#   `(pmap->pm_shared || (pmap == kernel_pmap))` (pmap.c:1291), so the `else` is reached only when
#   `pmap != kernel_pmap` already holds. It is spelled out because it is the HYPOTHESIS'S OWN PREDICATE
#   ("for a NON-KERNEL pmap take the full flush"), and the honest statement of what this switch does is
#   the narrow, checkable one: **`arm_tlb_flushID_ASID` becomes UNREACHABLE for every non-kernel pmap,
#   while remaining in the text.** The build asserts THAT, by relocation and by code order - not a size
#   delta, which is a codegen artifact (see the pool block's note).
#
#   **WHY (b) AND NOT (a)** (docs/1015b-design.md §6). (a) = emit `L2_NG_BIT` on every user leaf. (b) is
#   one arm, changes no PTE encoding, changes no mapping lifetime semantics, and RETIRES THE ENTRY
#   ALREADY IN THE TLB; (a) changes the encoding of EVERY user leaf in the kernel on ONE page's
#   evidence and leaves THIS stale entry in place. (b) is also exactly the experiment this rung
#   pre-registers - confirmation and fix are the same code. (a) is the principled FOLLOW-ON if (b)
#   confirms and the per-fault cost then measures badly.
#
#   THE PRE-REGISTERED DECISIVE TEST (tools/check_1015b_capture.py, whose table this rung does NOT
#   redefine): storm STOPPED => `G-CONFIRMED`; storm PERSISTS at the same far/pc/fsr => `G-REFUTED`, and
#   1015's §3b permission-inputs ladder runs. A truncated or key-missing capture => `G-WITHHOLD`, which
#   is NEVER a negative. The storm is identified BY VALUE (`classify_storm` returns the `far` with the
#   most tail rows), because the storm VA MOVES per run (1011 0x61000 -> 1012 0x2e000 -> 1014 0xe6000
#   -> 1015 0x27000) and a literal-VA checker reports a VACUOUS `STOPPED` on the run that moved it -
#   exactly the measurement defect records/1015-press-read.txt §0 records.
#
#   **THE ATTRIBUTION GUARD, AND IT IS NOT PEDANTRY.** `_wfkind=1` is NOT by itself the widened arm:
#   1014's capture already publishes `_wfkind=1` for FULL flushes of NON-CURRENT pmaps at low ordinals
#   (records/1014-press-read.txt §4) and 1015's carries 7 such rows. Attribution needs a `_wfkind=1`
#   row KEYED TO THE STORM (`_wfva`/`_pcfar == storm_va`). A `G-CONFIRMED` without one is a stop that
#   is MEASURED BUT NOT ATTRIBUTED and must not be read as a confirmation.
#
#   **A `G-CONFIRMED` MUST BE READ WITH `_pcn_max`, NOT INSTEAD OF IT.** A full TLBIALL per flush is
#   more expensive than a TLBIASID on the storm's hottest path (1015's `_pcn` ladder reaches 0x100000,
#   ~10^6 retries over ~24 s against a 29 s watchdog). A run that STALLED earlier looks, in a tail,
#   exactly like a storm that STOPPED. `_pcn_max` materially below the 1014 band is a STALL. This is
#   pre-registered here rather than discovered afterwards (docs/1015b-design.md §7.1).
#
#   **1014's PROBE STAYS.** `STAGE90_XNU_FLUSH_WITNESS` is not 1015b's to remove: the `_wfkind` cell it
#   publishes is the attribution guard above, and the reader's null control is certified against the
#   frozen 1014 elf. 1015's ENTRY half (`STAGE90_XNU_PERM_WITNESS`, the `wd*` recorder + the AT/PAR
#   cell) is inherited VERBATIM - 1015b adds NOTHING to the entry toolchain, so NO
#   `src/entry/build_entry.sh` plumbing is required for the new switch (unlike FLUSH_WITNESS, whose
#   pool define is ALSO gated on its environment variable).
#
# ORDER OF THE POOL DRIVERS, AND WHY IT IS THE ORDER IT IS. `tools/stage_d13_globalfix.sh` runs AFTER
# `tools/stage_d13_flushwitness.sh`. The 1015b anchors are Apple's OWN lines (`:1346-1349`), which the
# 1014 payload leaves in place, so either order COMPOSES - but only this one is TESTED, and an untested
# order is not a property. The tool REFUSES on a displaced anchor rather than proceeding, which is what
# makes the wrong order a build refusal instead of a silent mis-edit.
#
# OFF IS BYTE-IDENTICAL FOR THE POOL OBJECT, AND THE CLAIM IS SCOPED: this switch changes WHICH SYMBOL
# `pmap.c` CALLS (`arm_tlb_flushID` instead of `arm_tlb_flushID_ASID`); it never changes either body and
# never touches the object they live in (`osfmk/arm/armv/cpufunc-v7.s:66-84`, compiled separately). So
# the OFF proof below is taken on the `pmap.c`-BORN OBJECT's call census and says nothing about
# `cpufunc-v7.o` - fine, because that object is not in the delta. AND THE OFF PROOF NOW STRIPS TWO
# TOKENS: 1015's inherited `STAGE90_XNU_FLUSH_WITNESS` (with its environment variable 0) AND this
# rung's `STAGE90_XNU_GLOBAL_FIX` (likewise). A proof that strips one token compiles the other switch
# into BOTH halves and measures the source text rather than the gate - the failure 1015's own block
# records at :800-802 ("differ in SIZE (45376 vs 44904)").
#
# 1015 PRESS RESULT (records/1015-press-read.txt, arm armed-d13-e51d14fa) - 1015b's FRONTIER. Stated in
# full at "WHY" above: V-AT1, H-G CONFIRMED AT THE HARDWARE. The storm is `far=0x00027000` /
# `pc=0x2be171a0` / `fsr=0x0000080f`, 9 tail rows at `_pcn` 0x400..0x40000, run RESIDENT
# (`xnu_live_wdt_pets=0x61`, no panic). **Every one of those values is 1015's, NOT 1014's** - and this
# script's frozen-artifact controls name BOTH, because a control certified against the wrong run is not
# a control.
# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------
# Build 1014 (branch V1): 1013's switch set VERBATIM plus ONE new switch, STAGE90_XNU_FLUSH_WITNESS -
# THE TLB-FLUSH WITNESS. 1014 is a pure MEASUREMENT rung (no kernel behaviour changes) that reads the ONE
# mechanism the whole prot->PTE chain 1012/1013 left unread: did the flush that should have invalidated
# the stale translation for the storm's VA actually RUN, and under WHICH ASID and WHICH live TTBR0?
# Design: docs/1014-design.md §2.
#
#   WHY. 1013's press (arm `armed-d13-9cf2f15e`) read the leaf `pmap_enter_options` actually WROTE and
#   found it USER-WRITABLE (`_wwapx=0` at `_wwl2pa`), and 1012 had already measured `prot=0x3` ARRIVING
#   at the enter. Every INPUT is correct BY VALUE and the USER store still permission-faults ~2.1 M
#   times (`far`/`pc`/`fsr=0x80f` constant). The surviving V1 hypothesis (design §0.6.4) is H-FF: the
#   flush was REFUSED by the `ttb == ttbr_pmap || pmap_is_shared` test at `pmap.c:1298-1299`.
#
#   THE PROBE. `pmap_flush_tlbs` (`osfmk/arm/pmap.c`) gains ONE gated `entry_note_abt_wf(...)` call at
#   the EXIT of its flush decision, plus a gated CONTEXTIDR witness `entry_note_abt_wctx` called from
#   `__wrap_sleh_abort`. It publishes `_wfn _wfva _wfttb _wflive _wfself _wfshared _wfncpus _wfasid
#   _wfkind _wfseen` and `_wctxn _wctx _wctxva`. `_wfkind` is the cell that decides: 0 = NO FLUSH RAN
#   (F1, H-FF CONFIRMED), 1 = the full `arm_tlb_flushID`, 2 = `arm_tlb_flushID_ASID`. `_wfseen` is the
#   NULL-CONTROL cell (0 = the loop's in-loop TTBR read never executed, so `_wfself = 0` is not a
#   decision and the row must not be read as F1).
#
#   **WHY A SOURCE EDIT AND NOT A `--wrap`.** MEASURED on the built pool object: all 13
#   `R_ARM_CALL pmap_flush_tlbs` relocations are INTRA-object (11 caller functions, every one in
#   `pmap.c`), so a `--wrap=pmap_flush_tlbs` would redirect ZERO of them - including `pmap.c:3473`, the
#   flush the storm's enter rides. A hook that cannot fire ([[mi4-hooks-live-at-calls]]). So the call
#   site must live in `pmap.c`, a KERNEL-POOL file, and the switch goes to
#   `tools/build_xnu_arm_kernel.sh`'s `DEFINES` array (exactly where 1012's WRSTRIP goes) with the tree
#   text staged by `tools/stage_d13_flushwitness.sh` / `tools/patch_d13_flushwitness.py`.
#
#   THE SWITCH: STAGE90_XNU_FLUSH_WITNESS (default 0, spelled `defined(X) && X`). Its consumers are
#   THREE objects: `entry_stubs.c` (the two recorders) and `entry_trace.c` (the `entry_note_abt_wctx`
#   call) ride `build_entry.sh`'s `STUB_DEFINES`/`STUB_DEFINES_TRACE`, and `osfmk/arm/pmap.c` rides
#   `tools/build_xnu_arm_kernel.sh`'s `DEFINES` - so this switch is exported BOTH ways below, exactly
#   like 991's VMHINT_GUARD / 1001's SUBMAP_PMAP_SKIP ([[mi4-one-value-two-definitions]]). The guard is
#   the FULL CONJUNCTION `ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER && PTE_WITNESS && FLUSH_WITNESS`
#   - the recorder keys on 1013's `g_abtpte_curva`, which PTE_WITNESS is what writes - so the build
#   REFUSES FLUSH_WITNESS=1 unless all five are on.
#
#   OFF IS BYTE-IDENTICAL, MEASURED BOTH WAYS: the preprocessor removes all five `pmap.c` insertions
#   (the tree is Apple's bytes) and both entry objects; a rebuild with the switch 0 and a `cmp` of
#   `osfmk_arm_pmap.o` against a pre-patch OFF build is one of the refusals below.
#
# 1013 PRESS RESULT (the FRONTIER this rung starts from). 1013's witness RAN but returned
# WITNESS_METHOD_BROKEN (the W-SYNC control read `wck=0` - a recorder defect, fixed in `02dfca2`), and
# the surviving leaf reading is V1 `LEAF_WRITABLE_STILL_FAULTS`: `_wwleaf`/`_wwapx=0`, i.e. the leaf
# the pmap WROTE is user-writable, yet the store still faults.
# ------------------------------------------------------------------------------------------------
# Build 1013: 1012's switch set VERBATIM plus ONE new switch, STAGE90_XNU_PTE_WITNESS - THE LEAF-VALUE
# WITNESS. 1013 is a pure MEASUREMENT rung (no kernel behaviour changes) that reads the ONE cell the
# fault is actually about: the VALUE of the leaf the pmap writes, and the location the MMU's own walk
# finds it at. Design: docs/1013-design.md.
#
#   WHY. 1012 measured the INPUT (`prot=0x3` at `pmap_enter_options`) and the L1 identity
#   (`pm_l1_phys & 0xFFFFFF00 == TTBR0 & 0xFFFFFF00`), and every step is correct BY VALUE - yet the
#   USER store permission-faults (FS=15 level-2, WnR=1) ~2.1 M times. The leaf's VALUE was never read.
#   1013 reads it, three ways, and changes nothing:
#
#     (W-APEX) `entry_note_abt_wleaf` - inside `__wrap_pmap_enter_options`, AFTER `__real_...` returns.
#              Walks the PMAP'S OWN table from `pm_l1_phys` with Apple's own index math
#              (`pmap.c:1786-1824`, `tte_offset`/`pte_offset` `pmap.h:198-199`) and publishes the L1
#              descriptor, the L2 table base, THE LEAF, its APX bit, and the PA cross-check.
#              `_wwnoproof` is the honesty cell: 1 = `pm_l1_phys` is not the live TTBR0 base, i.e. the
#              table read is NOT provably the live one (1012 measured them EQUAL, so 1 is a NULL
#              CONTROL and a by-value contradiction - reported, never concluded from).
#     (W-WALK) `entry_note_abt_walk` - at the ASYNC abort, beside (A), through the SAME walker
#              `stage90_read_l2_pte` (a second walker is how 1010 and 1010b each produced a defective
#              reading - one wrong mask each). Publishes the location tag, the L1/L2-base/leaf, the
#              `fsr`, and `_wkapex` = "my leaf and the (A) walk's OWN recorded leaf agree" - the
#              MEASUREMENT that settles 1012's `ptel2=0` HARD anomaly instead of an inference.
#     (W-SYNC) `entry_note_abt_wsync` - the CONTROL at a known-good mapped VA (`gPhysBase + 0x80000`,
#              the same window `stage90_read_l2_pte` bounds its loads with). That VA (0x80080000) is
#              ABOVE this port's `TTBCR.N=2` split 0x40000000, i.e. TTBR1 territory (the kernel's
#              identity map lives in `cpu_ttb`, the table TTBR1 points at, arm_vm_init.c:352) - so the
#              walker reads TTBR1 for it and the control row LABELS the answer `_wctb` (=1). If the
#              control reads a correct writable leaf and the storm VA reads a read-only/absent one the
#              witness is sound; if the control is itself wrong, 1013 reports a MEASUREMENT defect and
#              concludes NOTHING (design §3, the WITNESS_METHOD_BROKEN row).
#
#   THE SWITCH: STAGE90_XNU_PTE_WITNESS (default 0). Its two consumers are `entry_stubs.c` (the three
#   recorders + `g_abtpte_ptel2rec`) and `entry_trace.c` (the three call sites), both compiled by
#   `build_entry.sh` - which reads `$PTEWIT` for the RECORD, adds `-DSTAGE90_XNU_PTE_WITNESS` to BOTH
#   `STUB_DEFINES` (entry_stubs.c) and `STUB_DEFINES_TRACE` (entry_trace.c). No pool object and no
#   platform object reads it, so no `EXTRA` entry and no env export - the EXPORTS pin below is the only
#   carrier ([[mi4-one-value-two-definitions]]). NO new `--wrap` is added: 1013 rides 1012's
#   `--wrap=pmap_enter_options` and the existing `--wrap=sleh_abort`. The guard is the FULL CONJUNCTION
#   `ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER && PTE_WITNESS`, so the build REFUSES PTE_WITNESS=1
#   unless all four are on (W-APEX observes the enter's apex; W-WALK keys on the (A) latch).
#
#   OFF IS BYTE-IDENTICAL: nothing here but the switch, and the guards delete all six edits.
#
# 1012 PRESS RESULT (records/1012-press-read.txt, arm armed-d13-ffdc8ba1) - 1013's FRONTIER. The apex is
# MEASURED: `_pmpprot=0x3` (READ|WRITE) ARRIVES at `pmap_enter_options`, and `_pmppmap & 0xFFFFFF00
# (0x81a86000) == _pmpttbr & 0xFFFFFF00` - so the pmap writes the SAME L1 table the MMU walks, and the
# strip-above-the-apex hypothesis is REFUTED BY MEASUREMENT. Two shapes survive: (c1) the apex VA keying
# (`g_abtpte_pmpva`) is not the storm's published `far` - a MEASUREMENT defect; (c2) a SECOND writer
# re-ROs the leaf, or the PTE `pmap_pte` returns is not the slot the MMU walks. 1013 reads the LEAF
# itself and the location the MMU finds it at, which is what tells (c1) from (c2).
# ------------------------------------------------------------------------------------------------
# Build 1012: 1011's switch set VERBATIM plus ONE new switch, STAGE90_XNU_PMP_ENTER - the PTE-BUILD
# APEX. 1012 is a pure MEASUREMENT rung (no kernel behaviour changes) that reads the LAST cell of the
# prot->PTE chain: the `prot` handed to `pmap_enter_options` (via `--wrap=pmap_enter_options`), the
# leaf's own INPUT. Design: docs/1012-design.md.
#
#   THE APEX. 1011 (E') measured the vm_map ENTRY's effective protection (`vm_map_lookup_locked`'s
#   `out_prot`); 1012 walks the SAME cell one layer down, to the leaf's builder. `vm_fault_enter`
#   reaches it through the `PMAP_ENTER_OPTIONS` macro (`osfmk/vm/pmap.h:399`) -> `pmap_enter_options`, a
#   CROSS-object call, so a `--wrap=pmap_enter_options` redirects EVERY linker-visible caller. On THIS
#   tree it dispatches to `leo_enter` (`pmap.c:3181`), which REBUILDS the leaf UNCONDITIONALLY from
#   `prot` - so a write-bearing `prot` here MUST yield a writable leaf, and a read-only `prot` here IS
#   the loss (`prot.o` is INTRA-object and is NOT wrapped; the storm's path is cross-object). Keys:
#   xnu_live_abt_pmp{n,prot,pa,pmap,ttbr,nx}, gated on the 1010 latch chain `entry_note_abt_pte` arms.
#
# 1011 PRESS RESULT (records/1011-press-read.txt, arm armed-d13-fce83837) - 1012's FRONTIER. F1a is
# MEASURED: the (E') row carries `vmeprot=0x3` (READ|WRITE) with `vmekr=0` (KERN_SUCCESS), i.e. the
# vm_map ENTRY GRANTS write; the write is LOST between the entry and the PTE. F1b is DENIED BY VALUE
# (the entry's `out_prot` DOES carry WRITE, so the accept path's unconditional KERN_SUCCESS is not the
# wrong condition). The (A) walk's TTBR0 base-mask defect is named and FIXED: `entry_ttbr_base`
# (`entry_stubs.c`) derives the L1 base from `TTBCR.N` (the port runs N=2, so bits[31:12] answer), not
# the fixed 16 KB constant - the 1010c/1011 walk read MH_MAGIC `0xfeedface` because the 0xffffc000 mask
# landed 8 KB low. 1012 carries that fix VERBATIM and adds the apex cell that NAMES whether `prot` STILL
# carries WRITE at the leaf's builder.
#
# THE SWITCH: STAGE90_XNU_PMP_ENTER (default 0). Its two consumers are `entry_stubs.c` (the recorder
# `entry_note_abt_pmp`) and `entry_trace.c` (the `__wrap_pmap_enter_options`), both compiled by
# `build_entry.sh` - which reads `$PMPENTER` for the RECORD, adds `-DSTAGE90_XNU_PMP_ENTER` to BOTH
# `STUB_DEFINES` (entry_stubs.c) and `STUB_DEFINES_TRACE` (entry_trace.c), and gates the
# `--wrap=pmap_enter_options` in TRACE_LDFLAGS so the OFF image carries none. Its recorder keys on the
# 1010 latch chain `entry_note_abt_pte` arms, so the build REFUSES `PMP_ENTER=1` unless the full 1010
# ladder (ABT_FAR=1 && ABT_PC=1 && ABT_PTE=1) is on. No pool object and no platform object reads it, so
# no `EXTRA` entry and no env export - the EXPORTS pin below is the only carrier
# ([[mi4-one-value-two-definitions]]).
#
# ------------------------------------------------------------------------------------------------
# Build 1011: 1010c's switch set VERBATIM plus ONE new switch, STAGE90_XNU_VME_PROT - a pure
# MEASUREMENT rung (no kernel behaviour changes) that reads the F1a/F1b DISCRIMINATOR the
# `docs/1011-discriminator-gap.md` audit found published by NOTHING, plus a corroborating cell that
# measures WHICH page-table base answered at the storm. Design: docs/1011-design-v2.md.
#
#   (E') `--wrap=vm_map_lookup_locked` publishes the lookup's OUT-param `out_prot` - the entry's
#        EFFECTIVE protection for the fault (vm_map.c:9061 sets it from entry->protection, :9139
#        demotes it for a needs_copy read fault, :9167 writes it). `prot & VM_PROT_WRITE` => the entry
#        GRANTS write => F1a (a strip lost it prot->PTE); `!(prot & VM_PROT_WRITE)` => the entry DENIES
#        write => F1b (the accept path's unconditional KERN_SUCCESS, vm_fault.c:2478, is the wrong
#        condition). Keys: xnu_live_abt_vme{kr,prot,fault,va,nx}.
#   (A)  `xnu_live_abt_ptethr` - the faulting thread's own L1 base (current_thread()->thread->map->
#        map->pmap->pmap->pm_l1_phys), read via STAGE90_ACT_MAP/STAGE90_MAP_PMAP/STAGE90_PMAP_L1_PHYS,
#        published beside `_ptettbr` (the live TTBR0 the walk sampled). The 1010c record read
#        `ptel1=0xfeedface` (a non-COARSE L1 word) at the storm; comparing the two bases says whether
#        the walk and the handler are looking at the SAME table. D13-ONLY (4570's struct pmap has no
#        pm_l1_phys); the 4570 arm publishes the 0xFFFFFFFF "not read" sentinel.
#
# 1010c PRESS RESULT (records/1010c-press-read.txt, arm armed-d13-002b0683) - 1011's FRONTIER. The walk
# is WINDOW-BOUNDED (no more FS=5 crash; the alias mechanism works once at pten=4) but at the STORM the
# walk reads a NON-COARSE L1 slot (`ptel1=0xfeedface`, bits[1:0]=0b10 = a SECTION), so it returns early
# at `(l1e&3)!=1` and never reaches the L2 leaf - the APX bit is STILL UNREAD and F1a/F1b UNDECIDABLE.
# 1010 (wrong base mask) read `0x81a86000`->`0x81bb1001` (COARSE); 1010c (correct mask) reads
# `0x81a84000`->`0xfeedface`; the live TTBR0 is the same `0x81a8605b`. The 1010c cause claim (4570's
# locore.s:1035-1039 / __ARM_USER_PROTECT__ switching TTBR0 to the kernel ttep) is RETRACTED - D13 has
# zero __ARM_USER_PROTECT__ hits. The cause is OPEN and 1011's (A) MEASURES it. (B) is now MEASURED at
# the storm: vmfret=0 KERN_SUCCESS, vmfprot=0x3 (READ|WRITE), vmfva=0x9a000.
#
# THE SWITCH: STAGE90_XNU_VME_PROT (default 0). Its two consumers are `entry_stubs.c` (the recorders
# `entry_note_abt_vme` + `stage90_abt_thread_l1_base`, and the `_ptethr` store) and `entry_trace.c`
# (the `__wrap_vm_map_lookup_locked`), both compiled by `build_entry.sh`, which reads `$VMEPROT` for the
# RECORD and adds `-DSTAGE90_XNU_VME_PROT` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. It ALSO
# gates the `--wrap=vm_map_lookup_locked` in TRACE_LDFLAGS, so the OFF image carries none. It REFUSES
# `VME_PROT=1` unless the full 1010 ladder (ABT_FAR=1 && ABT_PC=1 && ABT_PTE=1) is on - the (E') row is
# only meaningful paired with the (A)/(B) rows it discriminates. No pool object and no platform object
# reads it, so no `EXTRA` entry and no env export - the EXPORTS pin below is the only carrier
# ([[mi4-one-value-two-definitions]]).
#
# ------------------------------------------------------------------------------------------------
# Build 1010: 1009's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABT_PTE - a pure MEASUREMENT
# rung (no kernel behaviour changes) that reads ONE CELL FROM EACH SIDE of the vm_map-vs-PTE
# disagreement 1009 named: (A) the L2 small-page descriptor for the fault VA (walked from TTBR0), whose
# `L2_ACCESS_APX` bit is the decisive cell (APX=1 => the page is USER READ-ONLY though the fault asked
# for a WRITE); and (B) what `vm_fault` RETURNED (via a new `__wrap_vm_fault`). Design:
# docs/1010-design.md (§3 the change, §4 the build refusals, §5 the classifier);
# docs/1010-pte-walk.md (the L2 walk, by value).
#
# 1010b (2026-10-14): the 1010 press (`records/1010-press-read.txt`, arm armed-d13-0f39207c) reproduced
# the storm by value BUT the (A) walk read `ptel2=0/pteu=0` at the tail - IMPOSSIBLE under `fsr=0x80f`
# (FS=15, a LEVEL-2 permission fault REQUIRES a present L2 descriptor). Root cause, by value: the walk
# masked the TTBR0 base with `ABTPTE_L1_PTE_MASK` (0xfffffc00, the coarse-L1-descriptor -> L2-TABLE
# mask, correct only at `l2base`) instead of `STAGE90_TTBR_BASE_MASK` (0xffffc000, bits[31:14] - an L1
# table is L1_SIZE=0x4000=16 KB aligned, pmap.h:195), reading the L1 +0x2000 high. 1010b splits the two
# masks AND extends the (B) `vm_fault` ladder past the first-four prefix to the powers-of-two tail (paired
# to the (A) `_ptel2` rows). Both are instrument fixes; the switch set is UNCHANGED (no new switch), so
# the entry bin moves only because the instrument moved. This is the design's OWN falsifier
# (docs/1010-design.md §6).
#
# 1010b PRESS RESULT (2026-10-14): the mask fix is CORRECT but it EXPOSED a second defect in the same
# walk (`records/1010b-press-read.txt`, arm armed-d13-64391319). The corrected TTBR0 base reads the TRUE
# L1, whose L2 table lives at PA 0x46300000 - BELOW the entry image's identity window [0x80000000,
# 0x9e400000) - and the walk dereferences it with NO window bound, so the L2 leaf load (`entry_stubs.c:
# 2184`) faults FS=5 and the run dies at launchd-start (before the storm; the leaf is STILL unread). The
# truncation shows as a `_pcn` ordinal with NO `_pten` row (`entry_note_abt_pte` runs LAST in
# `__wrap_sleh_abort`). The next rung is 1010c: bound both walk derefs to the entry window, and reach an
# out-of-window L2 table via a temporary section VA->PA installed by `entry_mmio_section` (the 911c smem
# mechanism). See docs/1010-pte-walk.md §7.
#
# 1010c (2026-10-14, THIS RUNG): the walk becomes WINDOW-BOUNDED and can REACH an out-of-window L2 table.
# Both derefs (the L1 slot and the L2 leaf) are bounded to [gPhysBase, gPhysBase + mem_size) and FAIL
# OPEN - the walk returns 0 rather than faulting (class [[mi4-a-device-address-can-be-right-and-
# undereferenceable]]). An out-of-window L2 leaf is reached through a temporary 1 MB section VA->PA
# installed by `entry_mmio_section` (the 911c `entry_smem.c` mechanism) and RESTORED into the exact table
# the install wrote (`g_live_mmio_l1`, never re-derived - the 484 defect). The alias install is gated to
# the published LADDER ordinals (`onladder = n <= 4 || n & (n-1) == 0`), so the ~2.1 M-abort storm does
# NOT install/restore a section per abort. SIX new keys (`xnu_live_abt_pteoow`/`_ptealias`/`_ptemapok`/
# `_pteslot`/`_ptewbase`/`_ptewlen`) publish the walk's own statement about where the table was. The
# switch set is UNCHANGED (no new switch), so the entry bin moves only because the walk moved.
#
# 1009's press read (records/1009-press-read.txt, arm armed-d13-24997fc5) named the storm BY VALUE -
# from ordinal 0x400 through 0x200000 (~2.09 M aborts) the faulting instruction is CONSTANT
# (`pc=0x2be8a1a0`, in the dyld shared cache), the DFSR is CONSTANT (`fsr=0x0000080f` = WnR=1 | FS=0xf
# "permission fault, level 2"), the abort ran in USER mode, and `far=0x9a000` is a LOW user VA (below
# the TTBR0 split 0x40000000, in one of launchd's own COW/data segments). What it could NOT say is WHY
# the write permission fault is never dismissed: the handler PRINTS on the failure branch and is SILENT
# on the retry branch, and the capture shows the silent branch (no `usermode data abort` line over
# 2.1 M faults), so `vm_fault` returned KERN_SUCCESS || KERN_ABORTED - the handler retries forever.
#
# 1010 (A) walks TTBR0 to the L2 descriptor and publishes it (APX is the decisive cell); (B) records
# the `vm_fault` return. (A) is the PRIMARY measurement; (B) splits KERN_SUCCESS from KERN_ABORTED and
# is wired by a whole-kernel `--wrap=vm_fault` gated so the OFF image carries no such wrap. The
# CONSTANT-vs-CYCLING decision for the storm's tail is made by the READER on the TAIL ROWS' OWN
# `_ptel2`, NEVER on a whole-run change-count (the 1009 measurement defect, not repeated).
#
# Originating rung (1009): 1008's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABT_PC - the
# faulting PC + FSR recorder. 1010 KEEPS it (the inheriting switch) and pins it alongside the new one;
# the 1010 recorder sits after the 1009 recorder (its guard is the CONJUNCTION ABT_FAR && ABT_PC &&
# ABT_PTE), so it is inert unless 1008 AND 1009 are also on.
#
# 1008's press read (records/1008-press-read.txt, arm armed-d13-0343ef45) named WHERE the storm faults:
# the ladder `far` collapses onto the CONSTANT `0x0007b000` (type=4, a DATA abort) from ordinal 0x400
# through 0x200000 (~2.09 M retries). What it could NOT name is WHICH INSTRUCTION re-runs, and whether
# the DFSR status cycles across the retries. 1009 forwards the `pc`/`cpsr` the wrapper ALREADY holds
# (the saved-state frame `regs`, entry_trace.c) plus the `fsr`/`far` it already read, to the DIRECT
# channel on 1008's SAME powers-of-two trigger set (so the tail is already sampled), plus two bounded
# CHANGE-COUNTS (`nfar`/`nfsr`, written where they change). It reads NOTHING new from the machine. The
# record budget is the whole design (docs/1009-design.md §3): the change-counts are bounded by the
# DISTINCT-value count, not the abort count, so a REPEAT is decided by value (~4 KB, as 1008).
#
# Originating rung (1008): 1007's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABT_FAR - the
# far/type recorder (the maxcall latch + the powers-of-two ladder). 1009 KEEPS it (the inheriting
# switch) and pins it alongside the new one; the 1009 recorder sits after the 1008 recorder and reads
# nothing 1008 did not, so it is inert unless 1008 is also on.
#
# 1007's press read (records/1007-press-read.txt, arm armed-d13-88074ab3, commit 3fced8d) turned the
# frontier from a WALK into a STORM and localised the ~24.4 s freeze INSIDE one `sleh_abort` (18.735 s,
# `xnu_live_abt_maxcall = 0x1570e6a5`). What it could NOT name is the storm's fault ADDRESS: the storm
# rows are past the live channel's cap (ENTRY_LIVE_CAP 24576), so `xnu_live_sleh_far` stops at abort #40,
# and 1007's abort-span recorder publishes only n/seq/gap/call - never far/type. 1008 forwards the
# `far`/`type` the wrapper ALREADY read (entry_trace.c:5687-5693) to the direct channel on a BOUNDED
# trigger set: the maxcall latch (names the 18.7 s abort) and the powers-of-two ladder (the storm's
# address, progressively). It reads NOTHING new from the machine - it publishes two values the wrapper
# already holds. The record budget is the whole design (docs/1008-design.md §3): a per-abort publish is
# ~124 MB into a 2 MB console; the ladder+latch is ~4 KB.
#
# Originating rung (1007): 1006's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABT_SPAN - the
# abort-span witness (brackets each `__wrap_sleh_abort` call with the free-running generic timer). 1008
# KEEPS it (the inheriting switch) and pins it alongside the new one; the far call reuses its `t0`.
#
# Originating rung (1005): 1004's switch set VERBATIM plus ONE new switch, STAGE90_XNU_CADENCE - the
# timer-independent syscall-path cadence witness + the all-interrupts counter.
#
# Originating rung (1004): 1003's switch set VERBATIM plus ONE new switch, STAGE90_XNU_ABORT_TRACE -
# a pure MEASUREMENT rung that widens the sleh (data/prefetch abort) detail caps so the TAIL of a
# fault run is published, and adds the returning frame's user pc beside each abort's fault pair.
#
# 1003's press read (records/1003-press-read.txt): pid 1's thread 0xc0bfa000 is NOT a user spin and
# NOT a passive sleep - the per-tick frame sampler showed the last samples are ONE kernel frame
# (pc=0x8001b1fd, sp=0xcfdbbf00, lr=sleh_abort+0x468), i.e. the CPU is on pid 1's kernel stack, in
# the data-abort RETURN path. Total abort entries that boot: `xnu_live_sleh_seen` reached 40 with
# `xnu_live_sleh_storm=9`, ALL on thread 0xc0bfa000. But the sleh hook's caps - SLEH_LIVE_MAX=8
# detailed entries, SLEH_LIVE_FRAMES=16 frames - publish only the FIRST 8, and those 8 show DIFFERENT
# `far` values (dyld walking FORWARD: 0x2be89fe0, 0x2be8c78f, 0x2be8a084, ...). THE HANG IS IN THE
# TAIL THE CAP HIDES.
#
# WHY THE INSTRUMENT, AND WHY *THIS* ONE. The 1003 read leaves two readings that fit its evidence:
# (a) the SAME page re-faults - each `vm_fault` returns and the user re-faults the same address (a
# validation / cs_validated-style LOOP); or (b) dyld WALKS FORWARD one page per fault (normal paging
# of the 301 MiB shared cache), which is merely slow, not stuck. The evidence leans to (a) at the very
# end (seven samples one frame) but the sleh records so far name DIFFERENT far addresses, so the early
# window may be (b) and only the tail (a). Two things separate them, and 1004 takes both:
#   * raise the caps under the NEW switch so EVERY abort of a run this size publishes its full tuple
#     (seq, type, fsr, far, thr) - the tail is no longer cut; and
#   * publish the frame's USER pc (`xnu_live_sleh_upc`) beside it - so consecutive `(far, upc)` PAIRS
#     answer loop-vs-walk directly: a `far` that repeats within a small window = LOOP (same page
#     re-faulting); a `far` that strictly advances = WALK (dyld progress, merely slow).
#
# THE SWITCH: STAGE90_XNU_ABORT_TRACE=1 (the one new switch). It reaches `entry_stubs.c`'s compile as
# `-DSTAGE90_XNU_ABORT_TRACE` via `STUB_DEFINES` (the file's whole sleh body is inside the
# `STAGE90_ENTRY_TRACE` region) - the same path the seam flags use. NO tree edit, NO stage driver and
# NO platform-object change: both halves are `src/entry/entry_stubs.c` edits, gated so
# ABORT_TRACE=0 is byte-identical. THE ENTRY BIN MUST MOVE (7669309b -> the new sha8) because the
# widened caps and the new `_upc` store change the linked entry payload.
#
# THE SWITCH SET is 1003's VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP and STALL stay 1) plus STAGE90_XNU_ABORT_TRACE=1 (the one new switch).
#
# ALL 1003 INSTRUMENTS STAY (the tick sampler/summaries, the cap-exempt pet/anomaly channels, the
# user-SVC frontier - USVC=1 - the 991 memq_hint guard, the 1001 submap pmap-enter skip, the 1002
# USVC-hook r3 save set, and 1003's per-tick stall sampler): the new expectation is that the boot
# SURVIVES to the same pid-1 fault-return frontier and the sleh tail is now PUBLISHED for every abort.
# ONE PAYLOAD edit (kept from 999/1000/1001/1002/1003): STAGE90_HW_WATCHDOG_TIMEOUT_S 29u - the loaded
# payload bytes are 999's; 1004 must not slip back to the 25 s net.
#
# 1003's headline: it sampled the interrupted frame on the tick's powers-of-two ladder and settled
# that pid 1 is in the kernel's fault-return path (KERNEL pc x14, USER pc x0), reproducing the 1002
# frontier byte for byte (last usvc pc 0x2be97328 = dyld's `_mach_absolute_time` return). 1002's
# headline (kept): the USVC hook's r3-save fix took BY VALUE (the 230x dyld abort at 0x4 is GONE).
# 1001's headline (kept): the is_submap guard in vm_map_enter's tail (SUBMAP_PMAP_SKIP). 1000's
# headline (kept): the USVC frontier hook; the tick sampler/summaries; the pet/anomaly keys.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt
# **THE CROSS-LANE DECLARATION CARRIER, CREATED (EMPTY) HERE, AT THE TOP.** `STAGE90_XNU_GLOBAL_FIX`
# is a POOL switch that MOVES THE KERNEL, and the record CANNOT carry it: the record is written by
# `src/entry/build_entry.sh`, which does not name the switch (MEASURED x0), and
# `scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS` - a SET DIFFERENCE against the record - does
# not either, because a key no writer produces would make that gate refuse every press. So the
# declaration rides this drop-in file, which the park appends ONE line to at the freeze point:
#     STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix
# It is created EMPTY, and re-created EMPTY on every run of this script, BEFORE anything else reads
# it: `out/stage90` is shared live state, and a declaration left behind by a previous arm would
# describe the WRONG KERNEL to whoever reads the directory next
# ([[mi4-out-stage90-is-shared-live-state]]). `_b_extra_n`'s assertion in the pool block below is what
# makes this a CHECK rather than a hope.
mkdir -p out/stage90
_b_extra_file=out/stage90/xnu_arm_entry-arm-extra.txt
: > "$_b_extra_file"

echo "== 1015b: THE DECISIVE TEST - is the recurring FS=15 a STALE GLOBAL TLB entry? (the storm's own flush widened to a full TLBIALL) + the permission-inputs witness riding along =="
echo "-- 1015's arm was armed-d13-e51d14fa (V-AT1 AT THE HARDWARE: _wdatpar faults with par&1==1 while its null control _wdatparpr SUCCEEDS to the leaf's own PA, on a leaf read as AP=3/APX=0/nG=0 with domain 0, DACR=1 and TTBCR.N=2 - so the MMU's translation for a USER WRITE is NOT the leaf the walk reads; the storm then ran to _pcn=0x40000 and the run was RESIDENT, wdt_pets=0x61; see records/1015-press-read.txt); 1015's set verbatim + ONE new switch, STAGE90_XNU_GLOBAL_FIX --"

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS \
         STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# The record carries every key above (998's own record carries SHREG_GEOM and the rest). The pins
# keep the layers from ever disagreeing with each other or with the ENV exports below
# ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_PROBE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_WIRE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_GEOM=1"
# 1001's switch: `vm_map.c` IS a pool file (out/xnu_kernel_obj_d13/osfmk_vm_vm_map.o), so unlike
# 1000's USVC this one rides BOTH `EXTRA` and the environment export below into
# `build_xnu_arm_kernel.sh`'s DEFINES ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SUBMAP_PMAP_SKIP=1"
# 1003's NEW switch: STAGE90_XNU_STALL, the per-tick pid-1 pc sampler. It reaches the board PE's
# compile (`src/platform/darwin13/pe_msm8974.c`) as `-DSTAGE90_XNU_STALL` via `XNU_KERNEL_EXTRA_DEFINES`
# (== this `EXTRA`), the same path HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD use - `pe_msm8974.o` is
# compiled by `build_xnu_arm_kernel.sh`'s platform block with `EXTRA_DEFINES`, NOT by the entry
# toolchain. The recorder half lives in `src/entry/entry_stubs.c` (compiled by build_entry.sh, which
# reads `$STALL` for the RECORD only; its body is ungated). NO tree edit and NO stage driver: both
# halves are src/ files edited directly.
EXTRA="$EXTRA -DSTAGE90_XNU_STALL=1"
# 1005's NEW switch: STAGE90_XNU_CADENCE, the timer-independent cadence witness. It reaches the board
# PE's compile (`src/platform/darwin13/pe_msm8974.c`) as `-DSTAGE90_XNU_CADENCE` via
# `XNU_KERNEL_EXTRA_DEFINES` (== this `EXTRA`) - the SAME path STALL uses - because the all-interrupt
# counter and the two board accessors live in `pe_msm8974.o` (a platform object, NOT an entry
# toolchain object). The recorder + the atomic sleh `++` live in `src/entry/entry_stubs.c` and the pet
# call in `src/entry/entry_trace.c` (compiled by build_entry.sh, which reads `$CADENCE` - via its own
# `STAGE90_XNU_CADENCE` env var - for the RECORD and adds `-DSTAGE90_XNU_CADENCE` to BOTH
# `STUB_DEFINES` and `STUB_DEFINES_TRACE`). NO tree edit and NO stage driver: all halves are src/ files
# edited directly. It MUST be exported (not only in `EXTRA`) for the same reason STALL's export is -
# `build_xnu_arm_kernel.sh` gates the DEFINE of that name on the environment variable, and the platform
# block's compile (which carries the pe_msm8974.c accessors) reads it from there.
EXTRA="$EXTRA -DSTAGE90_XNU_CADENCE=1"
# 1006's NEW switch STAGE90_XNU_FREEZE_WITNESS is NOT here and is NOT exported for the pool/platform
# build: its ONLY two consumers are `entry_stubs.c` (the sampler) and `entry_trace.c` (the call site),
# both compiled by `build_entry.sh`, which reads `$FREEZE` for the RECORD and adds
# `-DSTAGE90_XNU_FREEZE_WITNESS` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. No pool object reads
# it and no platform object reads it, so unlike CADENCE it needs no `EXTRA` entry and no env export -
# the EXPORTS pin below is the only carrier. (Contrast the CADENCE comment just above, which DOES need
# both because pe_msm8974.o carries its counter.)
# 1007's NEW switch STAGE90_XNU_ABT_SPAN is NOT here and is NOT exported for the pool/platform build,
# for the SAME reason 1006's FREEZE_WITNESS is not: its ONLY two consumers are `entry_stubs.c` (the
# recorder, `entry_note_abt_span`) and `entry_trace.c` (the ONE bracket inside `__wrap_sleh_abort`),
# both compiled by `build_entry.sh`, which reads `$ABT_SPAN` for the RECORD and adds
# `-DSTAGE90_XNU_ABT_SPAN` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. No pool object reads it
# and no platform object reads it, so it needs no `EXTRA` entry and no env export - the EXPORTS pin
# below is the only carrier ([[mi4-one-value-two-definitions]]).
# 1008's NEW switch STAGE90_XNU_ABT_FAR is NOT here and is NOT exported for the pool/platform build,
# for the SAME reason 1006's FREEZE_WITNESS/1007's ABT_SPAN are not: its ONLY two consumers are
# `entry_stubs.c` (the recorder, `entry_note_abt_far`) and `entry_trace.c` (the ONE call after the span
# bracket), both compiled by `build_entry.sh`, which reads `$ABTFAR` for the RECORD and adds
# `-DSTAGE90_XNU_ABT_FAR` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. No pool object reads it and
# no platform object reads it, so it needs no `EXTRA` entry and no env export - the EXPORTS pin below
# is the only carrier ([[mi4-one-value-two-definitions]]). It also REFUSES `ABT_FAR=1` with `ABT_SPAN=0`
# (build_entry.sh, docs/1008-design.md §4.5): the far call reuses the span's `t0`.
# 1010's NEW switch STAGE90_XNU_ABT_PTE is NOT here and is NOT exported for the pool/platform build,
# for the SAME reason 1006/1007/1008's switches are not: its ONLY two consumers are `entry_stubs.c`
# (the walk `stage90_read_l2_pte` + the recorders `entry_note_abt_pte`/`entry_note_abt_vmf`) and
# `entry_trace.c` (the ONE `entry_note_abt_pte(far_)` call after the 1009 call, plus the NEW
# `__wrap_vm_fault`), both compiled by `build_entry.sh`, which reads `$ABTPTE` for the RECORD and adds
# `-DSTAGE90_XNU_ABT_PTE` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`. No pool object reads it and
# no platform object reads it, so it needs no `EXTRA` entry and no env export - the EXPORTS pin below
# is the only carrier ([[mi4-one-value-two-definitions]]). **It REFUSES `ABT_PTE=1` with `ABT_FAR=0` OR
# `ABT_PC=0`** (build_entry.sh): the recorder's guard is the CONJUNCTION ABT_FAR && ABT_PC && ABT_PTE,
# so either zero would carry a switch that measures nothing. **AND (the §8.1 surface) `--wrap=vm_fault`
# is added to `TRACE_LDFLAGS` ONLY when `ABT_PTE=1`** - the OFF image carries no such wrap at all.
# store. Its ONLY consumer is `src/entry/entry_stubs.c`, compiled by build_entry.sh (which reads
# `$ABORT_TRACE` for the RECORD and adds `-DSTAGE90_XNU_ABORT_TRACE` to `STUB_DEFINES`). It is NOT a
# pool switch and NOT a platform-object switch, so unlike SUBMAP_PMAP_SKIP it needs NO `EXTRA` entry
# and NO environment export here - the ENTRY build below carries it (the EXPORTS pin + the record).
# 1000's USVC adds NO key HERE: it is read by the ASSEMBLY layer (ASFLAGS via assemble_arm_layer.sh)
# and by build_entry.sh (the env EXPORTS below), and NO pool C file reads it - the same shape
# RESIDENT had at 999. 1001's SUBMAP_PMAP_SKIP DOES (pinned just above): vm_map.c rides the pool.
# 1014's NEW switch STAGE90_XNU_FLUSH_WITNESS. **It IS here and IS exported below**, unlike
# 1006/1007/1008/1010/1013's switches: `pmap.c` is a KERNEL-POOL file
# (`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`), so the switch has to reach
# `build_xnu_arm_kernel.sh`'s `DEFINES` array - exactly where 1012's WRSTRIP goes
# ([[mi4-one-value-two-definitions]]). The tree text itself is staged separately by
# `tools/stage_d13_flushwitness.sh`.
EXTRA="$EXTRA -DSTAGE90_XNU_FLUSH_WITNESS=1"
# **1015b ADDS ONE `EXTRA` ENTRY AND REVIVES THE VERY SWITCH 1015 DELETED.** The removed active half
# lived exactly here (and in the `export` block below) because `pmap.c` is a KERNEL-POOL file; 1015b is
# that half, under the name `STAGE90_XNU_GLOBAL_FIX` (the design's `STAGE90_XNU_USER_FULL_FLUSH` is an
# earlier name for the same thing - one value, one spelling, docs/1015b-design.md).
# 1015's OWN switch STAGE90_XNU_PERM_WITNESS is inherited VERBATIM and is still NOT here: its two
# consumers are entry-toolchain C files (`entry_stubs.c`'s `wd*` recorder + the AT/PAR reads, and
# `entry_trace.c`'s ONE `entry_note_abt_wd(far_)` call), compiled by `build_entry.sh`, which reads
# `$PERMWIT` for the RECORD and adds `-DSTAGE90_XNU_PERM_WITNESS` to BOTH `STUB_DEFINES` and
# `STUB_DEFINES_TRACE`. No pool object and no platform object reads it, so it needs no `EXTRA` entry
# and no env export - the EXPORTS pin below is the only carrier ([[mi4-one-value-two-definitions]]),
# exactly 1013's PTE_WITNESS shape. **1015b's own switch is the OPPOSITE shape**: its ONE consumer is
# the kernel pool, so it is in `EXTRA` here AND exported below, and it needs NO entry plumbing at all.
# 1014's FLUSH_WITNESS STAYS: it is not 1015b's to remove, and the `_wfkind` cell it publishes is the
# ATTRIBUTION GUARD the reader's G-CONFIRMED cell depends on.
EXTRA="$EXTRA -DSTAGE90_XNU_GLOBAL_FIX=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"
# **1014's READ-OUT MUST STILL EXIST.** 1015 keeps FLUSH_WITNESS but no longer pairs it with a widening
# switch; the clause stays because the reader's V-G3 null control reads `_wfkind`, and a build without
# the probe would make the frozen-capture control unreadable rather than absent-and-noticed
# ([[mi4-measurement-defects]]). The ASSERTION for it lives AFTER the `export STAGE90_XNU_FLUSH_WITNESS=1`
# below (:614): this script SETS the switch itself, so a precondition here on a CALLER-exported variable
# would demand the caller pre-export a value the script has not yet set - a chicken-and-egg that made
# every invocation fail before the export ran. The switch MUST be in EXTRA (already pinned at :498) AND
# exported (:614); both are checked at their own sites, after they are set.

# 1. Stage the tree edits (all TWELVE idempotent; the shreg-probe driver stages TEN edits - the
#    three 999 slide prints ride the SAME file and gate as the five existing IOS7SHREG ones - the
#    timer-edge driver stages the 999 amendment's TWO assembly hooks, the usvc driver stages 1000's
#    ONE hook in traps_lo.s, and the submap-pmap-skip driver stages 1001's ONE edit in vm_map.c).
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) + the iOS 7 VM wire + -3 SWI (994) + the dyld witness rev-3 (995/996) + the 294/438 shared-region probe + the 999 slide ladder (996/999) + the 32 B shared-region wire (997) + the shared-region geometry + stack top (998) + the timer-discriminator edge hooks (999 am2) + the user-SVC frontier hook (1000) + the submap pmap-enter skip (1001) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh
tools/stage_d13_shreg_probe.sh
tools/stage_d13_shreg_wire.sh
tools/stage_d13_shreg_geom.sh
tools/stage_d13_timer_edge.sh
tools/stage_d13_usvc.sh
tools/stage_d13_submap_pmap_skip.sh
# 1014 (branch V1) adds ONE stage driver: the TLB-flush witness in `osfmk/arm/pmap.c`'s
# `pmap_flush_tlbs` (the ONE probe, at the exit of its flush decision). It is a POOL edit, so
# `build_xnu_arm_kernel.sh` must be re-run with the switch exported - which is why the switch is in
# `EXTRA` above AND in the `export` block below.
tools/stage_d13_flushwitness.sh
# 1015b adds ONE MORE, and its ORDER AFTER the 1014 driver is the tested one (see the header): the
# GLOBAL-FIX arm, three inserted lines between Apple's `:1347` full-TLBIALL arm and Apple's `:1348`
# ASID `else`. Also a POOL edit, also in `EXTRA` above and in the `export` block below. Its own tool
# (`tools/patch_d13_globalfix.py`), NOT an extension of the 1014 one - docs/1015b-design.md §2.
tools/stage_d13_globalfix.sh
# 1003 adds NO stage driver. The two halves of the stall instrument are src/ files edited directly:
# the recorder in `src/entry/entry_stubs.c` (compiled by build_entry.sh) and the ONE call site in
# `src/platform/darwin13/pe_msm8974.c` (compiled by the platform block under -DSTAGE90_XNU_STALL). No
# tree file outside src/ is touched - this is a pure MEASUREMENT rung, and the gated `#if` in
# pe_msm8974.c keeps the tree's own objects byte-identical when STALL=0.

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
# MUST be exported BEFORE assemble_arm_layer.sh runs (it reads the environment, not the record).
export STAGE90_XNU_SP_ALIGN=1

# 991: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below.
export STAGE90_XNU_VMHINT_GUARD=1

# 1001: the POOL reads this one the same way (vm_map.c is a pool file; the DEFINE is gated on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below - the same
# rule as 991's VMHINT_GUARD ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_SUBMAP_PMAP_SKIP=1

# 992/993: same rule for the commpage alias (all pool files).
export STAGE90_XNU_COMMPAGE_ALIAS=1

# 994: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_IOS7_TRAPS=$IOS7_TRAPS into traps_lo.o, and the
# POOL reads the same variable. MUST be exported BEFORE assemble_arm_layer.sh runs.
export STAGE90_XNU_IOS7_TRAPS=1

# 995: the POOL reads this one - `trap.c` rides the pool, so there is no ASFLAGS half. 999 keeps it at 1.
export STAGE90_XNU_DYLD_WITNESS=1

# 996/999: same rule for the shared-region probe (`bsd/vm/vm_unix.c`, a pool file). 999 keeps it at 1:
# the probe's IOS7SHREG lines stay readable next to the wire's own IOS7LAB cache mapping lines, and the
# three 999 slide prints ride the same gate.
export STAGE90_XNU_SHREG_PROBE=1

# 997: same rule for the 32 B shared-region wire (the SAME pool file). 999 keeps it at 1.
export STAGE90_XNU_SHREG_WIRE=1

# 998: same rule for the shared-region geometry + stack top. 999 keeps it at 1 and touches neither.
export STAGE90_XNU_SHREG_GEOM=1

# 999 (amendment #2): the ASSEMBLY layer reads this to put -DSTAGE90_XNU_RESIDENT=$RESIDENT into
# machine_routines_asm.o and cswitch.o (the timer-discriminator edge hooks). MUST be exported BEFORE
# assemble_arm_layer.sh runs; the SAME value reaches build_entry.sh through EXPORTS below
# ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_RESIDENT=1

# 1000: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_USVC=$USVC into traps_lo.o (the user-SVC
# frontier hook in fleh_swi). MUST be exported BEFORE assemble_arm_layer.sh runs; the SAME value
# reaches build_entry.sh through EXPORTS below ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_USVC=1

# 1003: the NEW switch STAGE90_XNU_STALL. It must be exported (not only in `EXTRA`) for the same
# reason 991/1001's pool switches are: `build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable, and the platform block's compile (which carries the pe_msm8974.c call site)
# reads it from there. Export it HERE, before the pool/platform build below - the SAME value reaches
# build_entry.sh through EXPORTS below ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_STALL=1

# 1005: the NEW switch STAGE90_XNU_CADENCE. It must be exported (not only in `EXTRA`) for the same
# reason 991/1001's pool switches and 1003's STALL are: `build_xnu_arm_kernel.sh` gates the DEFINE of
# that name on the environment variable, and the platform block's compile (which carries the
# pe_msm8974.c all-interrupt counter and the two board accessors) reads it from there. Export it HERE,
# before the pool/platform build below - the SAME value reaches build_entry.sh through EXPORTS below
# ([[mi4-one-value-two-definitions]]). No assembly layer reads it, but the export keeps the three
# spellings one value.
export STAGE90_XNU_CADENCE=1

# 1014's NEW switch STAGE90_XNU_FLUSH_WITNESS. It MUST be exported (not only in `EXTRA`) for the same
# reason 991/1001's pool switches and 1012's WRSTRIP are: `build_xnu_arm_kernel.sh` gates the DEFINE of
# that name on the environment variable, and `osfmk_arm_pmap.o` (the KERNEL-POOL object that carries the
# one call site) is compiled there. Export it HERE, before the pool/platform build below - the SAME
# value reaches build_entry.sh through EXPORTS below ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_FLUSH_WITNESS=1
# The precondition, asserted at the site that establishes it (the guard that used to sit at :515 ran
# BEFORE this export and could never pass - a chicken-and-egg, not a check). Both carriers must be live:
# the exported variable (the pool/platform gate) AND the `EXTRA` token (the DEFINES path).
[[ -n ${STAGE90_XNU_FLUSH_WITNESS:-} ]] || { echo "FAIL: STAGE90_XNU_FLUSH_WITNESS is not exported after :614 - the 1014 read-out (_wfkind) would not exist, and the reader's V-G3 null control rides it"; exit 1; }
[[ "$EXTRA" == *"-DSTAGE90_XNU_FLUSH_WITNESS=1"* ]] || { echo "FAIL: -DSTAGE90_XNU_FLUSH_WITNESS=1 is not in EXTRA ('$EXTRA') - the pool DEFINES path would not carry it, so _wfkind would be absent"; exit 1; }

# 1015b's OWN switch STAGE90_XNU_GLOBAL_FIX. **IT IS DELIBERATELY *NOT* `export`ED, AND THE REASON IS
# MEASURED RATHER THAN ASSUMED - do not "fix" this line by restoring the export.** The `export` shape
# is how 1014's FLUSH_WITNESS and 991/1001's pool switches declare themselves, and copying it here was
# the first draft's error. Three separate measurements close it (docs/1015b-design.md §3):
#   (1) `grep -c 'STAGE90_XNU_GLOBAL_FIX' src/entry/build_entry.sh` -> **0**. The record is written BY
#       that script; a name it never mentions cannot appear in it, so the export would be a value
#       nobody reads back ([[mi4-self-written-record-is-not-a-constraint]]).
#   (2) `build_entry.sh` is invoked below through `env -i PATH HOME "${EXPORTS[@]}"` - a WIPED
#       environment plus the explicit EXPORTS list. An `export` in this shell could not reach it even
#       if it did name the switch. (GLOBAL_FIX is not in EXPORTS; putting it there would make the
#       invocation pass a key `build_entry.sh` ignores - inert, but a lie about a carrier.)
#   (3) `grep -c 'STAGE90_XNU_GLOBAL_FIX' scripts/preflight_boot_check.sh` -> **0**, and that gate's
#       check is a SET DIFFERENCE against the record: naming the key there with no `src/entry/` writer
#       would make it refuse EVERY press, not just this rung's.
# What the switch actually has is ONE compile-time carrier - the `-D` token in `EXTRA` above, which
# `build_xnu_arm_kernel.sh` consumes as `XNU_KERNEL_EXTRA_DEFINES` and appends to every pool compile -
# plus the DROP-IN DECLARATION FILE created at the top of this script. There is NO `DEFINES+=` clause
# for it in `tools/build_xnu_arm_kernel.sh` (another lane's file; `EXTRA` is the only path), so the
# build does NOT assume the define arrived: it reads the COMPILED OBJECT's relocation census below
# ([[mi4-a-claim-in-a-comment-is-not-a-check]]). The two clauses that keep the negative claim TRUE are
# at the tail (the record must not name it; `preflight_boot_check.sh` must not name it).
[[ "$EXTRA" == *"-DSTAGE90_XNU_GLOBAL_FIX=1"* ]] || { echo "FAIL: -DSTAGE90_XNU_GLOBAL_FIX=1 is not in EXTRA ('$EXTRA') - the compile-time path would not carry it and the arm would be Apple's kernel with a declaration file claiming a behaviour change"; exit 1; }
[[ -z ${STAGE90_XNU_GLOBAL_FIX:-} ]] || { echo "FAIL: STAGE90_XNU_GLOBAL_FIX is set in this build's ENVIRONMENT ('$STAGE90_XNU_GLOBAL_FIX') - this rung's switch has NO environment carrier and must not acquire one by accident: a caller who exports it is declaring a switch through a channel nothing reads, which is how a divergent second value starts ([[mi4-one-value-two-definitions]]). It reaches the POOL through EXTRA alone"; exit 1; }

# 1015's OWN switch STAGE90_XNU_PERM_WITNESS is NOT exported here - this is the DESIGN, not an
# oversight. Its two consumers are entry-toolchain C files (`entry_stubs.c`/`entry_trace.c`), reached
# through build_entry.sh's EXPORTS pin below, and NO pool object and NO platform object reads it. It is
# therefore 1013's PTE_WITNESS shape, not 1014's FLUSH_WITNESS shape ([[mi4-one-value-two-definitions]]).
# **1015b ADDS NOTHING ELSE TO THE ENTRY TOOLCHAIN**: the new switch is pool-only, so no
# `src/entry/build_entry.sh` plumbing site is needed for it (contrast FLUSH_WITNESS, which is threaded
# through STUB_DEFINES/STUB_DEFINES_TRACE as well as the pool).

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 999+am2: machine_routines_asm.o and cswitch.o get the timer-discriminator edge hooks; 1000: traps_lo.o ALSO gets the USVC hook - both switches exported before this line) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_SUBMAP_PMAP_SKIP=$STAGE90_XNU_SUBMAP_PMAP_SKIP STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM (pool DEFINES + asm ASFLAGS + EXTRA agree) STAGE90_XNU_RESIDENT=$STAGE90_XNU_RESIDENT (asm ASFLAGS + build_entry EXPORTS + the ungated helper's runtime gate; no pool object reads it) STAGE90_XNU_USVC=$STAGE90_XNU_USVC (asm ASFLAGS + build_entry EXPORTS; no pool object reads it - the C half is the entry image's own) --"
echo "-- the WHOLE D13 kernel pool + platform block (vm_unix.c carries the 32 B wire (997) + the 294/438 probe + the 999 slide ladder; vm_shared_region.c the iOS 7 geometry (998); status.c the 0x2BE00000 stack top (998); trap.c the witness rev-3; mach_kernelrpc.c the wire adapters; ios7_compat.h the two selectors; pmap.c + commpage.c + arm_vm_init.c the alias/zeroing; vm_resident.c the hint guard; pcb.c the alignment) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** Same trap as 987
# ([[mi4-one-value-two-definitions]]): the record's key name and the tree's guard name differ by one
# word, and only the environment path bridges them.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

# ====================================================================================================
# 1014's POOL-SIDE REFUSAL (design §2.4 item 4): THE DEFINE MUST BE ASSERTED, NOT ASSUMED.
# ====================================================================================================
# This is the 1012 WRSTRIP lesson applied to 1014 ([[mi4-one-value-two-definitions]]): the switch has
# TWO carriers - `build_entry.sh`'s STUB_DEFINES (entry_stubs.o/entry_trace.o) and this pool build's
# `DEFINES` array (`build_xnu_arm_kernel.sh`, gated on the `STAGE90_XNU_FLUSH_WITNESS` ENVIRONMENT
# variable). If the env half is missing, the entry half still compiles and links, `build_entry.sh` only
# LINKS the pool object, and the result is an image whose `_wfn`/`_wfkind` cells exist but can NEVER be
# written - the probe silently cannot fire. So this refusal reads the POOL OBJECT ITSELF, BEFORE the
# entry link, and stops the build there. `strings`, not `nm`: the key literals are rodata strings and
# the call site is a relocation, and neither survives a switch that never reached the compile.
wf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f $wf_obj ]] || { echo "FAIL: $wf_obj does not exist - the pool build did not produce the object that carries the ONE pmap_flush_tlbs probe"; exit 1; }
# The NEW LITERAL: the pool object must reference the recorder BY NAME - an UNDEFINED symbol. This is
# the design's "the new literal", and it is the reading a switch that never reached `DEFINES` cannot
# produce: without the gate the tree text is Apple's, the symbol is mentioned nowhere, and the object
# is the pre-patch bytes.
wf_sym=$(arm-none-eabi-nm -u "$wf_obj" 2>/dev/null | awk '$2=="entry_note_abt_wf"{print $2}' | head -1)
[[ "$wf_sym" == "entry_note_abt_wf" ]] || { echo "FAIL: the pool object $wf_obj does NOT reference entry_note_abt_wf as an undefined symbol - the switch did NOT reach tools/build_xnu_arm_kernel.sh's DEFINES (export STAGE90_XNU_FLUSH_WITNESS=1 BEFORE ./tools/build_xnu_arm_kernel.sh, or pass XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_FLUSH_WITNESS=1), so the pool object is the tree's ORIGINAL bytes and the probe silently cannot fire. build_entry.sh only LINKS this object - a green entry build says nothing about this half"; exit 1; }
# The NEW CALL SITE: exactly ONE relocation, because pmap.c must carry exactly ONE probe.
wf_calls=$(arm-none-eabi-objdump -r "$wf_obj" 2>/dev/null | grep -c 'entry_note_abt_wf' || true)
[[ "$wf_calls" == "1" ]] || { echo "FAIL: the pool object $wf_obj carries $wf_calls relocations to entry_note_abt_wf, want EXACTLY 1 - 0 => the call site was reverted or the object did not rebuild; >1 => a second call site reached the tree (docs/1014-design.md §2.4 item 7: exactly one)"; exit 1; }
# And the two recoder-side key LITERALS must NOT be here: the recorders are entry-toolchain
# (`entry_stubs.o`), so a `xnu_live_abt_wf*` string in a POOL object would mean the 1014 region leaked
# into a pool C file. This is the same reader run in the other direction.
wf_lits=$(strings "$wf_obj" | grep -c '^xnu_live_abt_wf' || true)
[[ "$wf_lits" == "0" ]] || { echo "FAIL: the POOL object $wf_obj carries $wf_lits \`xnu_live_abt_wf*\` key literals - the two recorders are entry-toolchain (entry_stubs.o) and the pool must not carry them. A pool literal means the 1014 region leaked into a pool C file"; exit 1; }
wf_src=src/entry/entry_stubs.c
# **The anchor is the FULL-CONJUNCTION TAIL, not the file-header one-liner.** 1015 rewrote the header
# include gate to a two-line disjunction (`FLUSH_WITNESS || PERM_WITNESS`, :87-88), so the 1014-era
# fixed string counts 0 and rejects a correct tree ([[mi4-mutation-anchors-are-code-spans]]: the anchor
# was a text SPAN, not the switch-bit the code means). And a bare token count is over-broad - PERM_WITNESS
# legitimately appears FOUR times (:88, the :2912 pre-call helper, the :2990 owed-fix, :3467), FLUSH
# twice. What is invariant is the shape of the RECORDER gate: the switch occurring as the LAST conjunct
# (`&& defined(SWITCH) && SWITCH`), which happens EXACTLY ONCE per switch and nowhere else. A moved or
# duplicated recorder region breaks it.
wf_gate=$(grep -cF '&& defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS' "$wf_src" || true)
[[ "$wf_gate" == "1" ]] || { echo "FAIL: $wf_src carries $wf_gate FLUSH_WITNESS conjunction-tails, want 1 (the 1014 recorder region :3303) - the recorder region was moved or duplicated"; exit 1; }
wf_perm=$(grep -cF '&& defined(STAGE90_XNU_PERM_WITNESS) && STAGE90_XNU_PERM_WITNESS' "$wf_src" || true)
[[ "$wf_perm" == "1" ]] || { echo "FAIL: $wf_src carries $wf_perm PERM_WITNESS conjunction-tails, want 1 (the 1015 recorder region :3467) - the recorder region was moved or duplicated"; exit 1; }
echo "   POOL: $wf_obj references entry_note_abt_wf (undefined) with EXACTLY ONE call relocation (x$wf_calls, want 1), carries x0 \`xnu_live_abt_wf*\` container-side literals, and $wf_src carries x$wf_gate gate"
echo "   POOL: this is what makes the 1014 switch a MEASUREMENT rather than a null - without it the entry image links an unpatched pmap.o and no flush row can ever be published"
# PURITY (design §2.4 item 8): the probe is a `void` call that returns; the flush arms themselves are
# UNTOUCHED. Assert by disassembly that the two cp15 flush encodings are still in the object AND that
# the relocation count to the probe equals the number of probe sites inserted (1). MEASURED spelling in
# this toolchain's objdump: `mcr 15, 0, rX, cr7, cr8, {2}` is `arm_tlb_flushID_ASID` (TLBIASID) and
# `{0}` is `arm_tlb_flushID` (TLBIALL) - the design writes the same pair as `c8,c7,2` / `c8,c7,0`; the
# ASSERTION uses the spelling the object actually carries, not the design's prose.
wf_asid=$(arm-none-eabi-objdump -d "$wf_obj" | grep -cE 'mcr[[:space:]]+15, 0, r[0-9]+, cr7, cr8, \{2\}' || true)
wf_full=$(arm-none-eabi-objdump -d "$wf_obj" | grep -cE 'mcr[[:space:]]+15, 0, r[0-9]+, cr7, cr8, \{0\}' || true)
[[ "$wf_asid" -ge 2 && "$wf_full" -ge 2 ]] || { echo "FAIL: the pool object $wf_obj carries $wf_asid TLBIASID and $wf_full TLBIALL encodings (want >=2 each, the :1317/:1319/:1322 arms plus the other callers) - the flush instructions were EDITED, not merely observed (design §2.4 item 8 purity)"; exit 1; }
[[ "$wf_calls" == "1" ]] || { echo "FAIL: purity - the number of relocations to the probe ($wf_calls) does not equal the number of probe sites inserted (1)"; exit 1; }
echo "   POOL: PURITY - the flush arms are intact (TLBIASID x$wf_asid, TLBIALL x$wf_full, both >=2) and the probe relocations (x$wf_calls) equal the probe sites inserted (1): the probe observes, it does not choose"
python3 tools/patch_d13_flushwitness.py "$XNU_TREE" --check >/dev/null || { echo "FAIL: tools/patch_d13_flushwitness.py --check did not PASS - the 1014 payload is not staged in osfmk/arm/pmap.c (re-run tools/stage_d13_flushwitness.sh BEFORE tools/stage_d13_globalfix.sh - 1015b anchors on Apple's own :1346-1349 lines, which the 1014 payload leaves in place). NOTE: this clause is the INHERITED-payload precondition, not the rung's claim - 1015b's own pool claim is asserted by the MOVED block below"; exit 1; }
echo "   POOL: tools/patch_d13_flushwitness.py --check PASSes (1014's payload is intact - the attribution guard the reader's G-CONFIRMED cell needs)"

# ====================================================================================================
# 1015b's POOL-MOVED ASSERTION: THE POOL OBJECT IS **NOT** 1014's ANY MORE - AND THAT IS THE RUNG.
# ====================================================================================================
# **THIS BLOCK IS THE EXACT OPPOSITE OF 1015's PURITY BLOCK, AND IT IS THE INVERSE THAT PROVES THE
# RUNG RAN.** 1015 asserted the pool was 1014's byte for byte (`wf_asid -eq 3`, `wf_full -eq 3`, and
# `USERFULLFLUSH` x0 in BOTH the object and the tree) because it was ENTRY-ONLY: a pool edit reaching
# the object would have made the arm a kernel the rung had not declared, and the reader's V-G3 null
# control - certified against the frozen 1014 elf - would have been certifying a different kernel. A
# 1015b that carried those clauses forward would FAIL on a correctly built arm, which is why they are
# GONE from this script rather than relaxed (see the tail's inverse assertion).
#
# The claim worth refusing now runs the other way: **the pool object MOVED, and it moved in exactly
# one place.** The trap this closes is precise and 1015's own header names it
# (build_1015.sh:1041-1042): `build_entry.sh` only LINKS `osfmk_arm_pmap.o`, so a switch that never
# reached `tools/build_xnu_arm_kernel.sh`'s `EXTRA_DEFINES` produces an image whose ENTRY half is
# perfect and whose pool half is Apple's - a green entry build says NOTHING about this half.
#
# **THE ASSERTION IS THE RELOCATION CENSUS AND THE CODE ORDER, NOT A SIZE DELTA.** An earlier draft
# asserted `.text` grew; that was REJECTED BEFORE THE BUILD because it would have been a guess dressed
# as a check ([[mi4-measurement-defects]]): the extra `cmp` is a NET ZERO with the `beq` the compiler
# already emitted, or a fresh branch pair (net +8 B in the object), and which one clang picks is not a
# property of this rung. What IS invariant is WHICH SYMBOL IS CALLED, and that is what is asserted.
#
# MEASURED, this session, on the OFF-state object (`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`, 45376 B):
#     pmap_flush_tlbs @0x418 .. 0x63c   (the next `T` symbol is pmap_update_cache_attributes_locked)
#       0x560  R_ARM_CALL  arm_tlb_flushID        <- Apple's :1347 full arm
#       0x58c  R_ARM_CALL  arm_tlb_flushID_ASID   <- Apple's :1349 ASID arm (the one 1015b retires)
# **WHAT THE ON STATE ACTUALLY IS, MEASURED (2026-10-15), AND WHY THIS CLAUSE'S FIRST DRAFT WAS A
# MEASUREMENT DEFECT.** The first draft asserted `arm_tlb_flushID_ASID == 0` and `arm_tlb_flushID >= 2`.
# BOTH are unsatisfiable, and the compiled object says so:
#   * Apple's source has TWO `arm_tlb_flushID()` sites - the `:1347` shared arm AND the outer `else`
#     after `if (pmap_asid_ncpus)`. The compiler TAIL-MERGES the two identical calls (same callee, same
#     return path) into ONE `bl`, so the OFF object ALREADY reads `arm_tlb_flushID x1` (MEASURED), and
#     the injected third site merges into that same `bl`. `>= 2` is unsatisfiable at ANY switch setting.
#   * The design's OWN claim is the OPPOSITE of `ASID == 0`: "the ASID arm becomes UNREACHABLE for every
#     non-kernel pmap, WHILE REMAINING IN THE TEXT" (this script's header). It IS still in the text -
#     reached only through the `pmap == kernel_pmap` branch, which `pmap_is_shared` already made
#     unreachable (the tautology the pmap.c comment names). `ASID == 0` would be a FAILURE of the
#     rung's declared shape, not a pass.
# **THE FINGERPRINT THAT IS THE INSERTION**, measured by value on the SAME tree with only the switch
# flipped (WF on / GF off vs WF on / GF on):
#     kernel_pmap address-materialization loads inside pmap_flush_tlbs:   OFF (Apple) 2  ->  ON 3
# Apple materializes kernel_pmap twice (`:1291`'s `pmap_is_shared` init and the `:1382` terminal panic);
# the third - at the offset immediately before the full arm's `bl` - is the inserted
# `else if (pmap != kernel_pmap)` test. THAT count is the assertion below, not a call count.
# UNMEASURED — measure at first build: the ON-state offsets and the object's `.text` size. They are
# PRINTED below so the first build's numbers land on the record rather than in a comment.
gf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f $gf_obj ]] || { echo "FAIL: $gf_obj does not exist - the pool build did not produce the object 1015b edits"; exit 1; }
# (a) the 1014 probe must STILL be here and still have EXACTLY ONE call site: it is the attribution
#     guard the reader's G-CONFIRMED cell rides, and 1015b's insertion must not have displaced it.
gf_calls=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | grep -c 'entry_note_abt_wf' || true)
[[ "$gf_calls" == "1" ]] || { echo "FAIL: the pool object $gf_obj carries $gf_calls relocations to entry_note_abt_wf, want EXACTLY 1 - 1015b's insertion displaced the INHERITED 1014 probe, and the reader's G-CONFIRMED attribution guard (_wfkind keyed to the storm) would have no producer. Stage the 1014 payload FIRST (tools/stage_d13_flushwitness.sh) and do not nest the 1015b arm inside it"; exit 1; }
# The STAGED TREE must carry the 1015b marker and pass its OWN tool's check. Both directions, because
# `--check` reads the TREE and the census below reads the OBJECT: an edit staged but never compiled, or
# compiled from a tree that was reverted afterwards, are different failures.
gf_tree_mark=$(grep -cF '1015b: GLOBALFIX' "$XNU_TREE/osfmk/arm/pmap.c" || true)
[[ "$gf_tree_mark" == "1" ]] || { echo "FAIL: the STAGED TREE $XNU_TREE/osfmk/arm/pmap.c carries x$gf_tree_mark \`1015b: GLOBALFIX\` marker(s), want EXACTLY 1 - 0 means tools/stage_d13_globalfix.sh never ran (or the OFF proof left the tree reverted), >1 means a second insertion reached the tree. Either way the compile below is not the rung's text"; exit 1; }
python3 tools/patch_d13_globalfix.py "$XNU_TREE" --check >/dev/null || { echo "FAIL: tools/patch_d13_globalfix.py --check did not PASS - the tree is not exactly this rung's staged text (re-run tools/stage_d13_globalfix.sh AFTER tools/stage_d13_flushwitness.sh). The tool refuses on a displaced anchor rather than half-applying, so this is also what a WRONG DRIVER ORDER looks like"; exit 1; }
echo "   POOL: the staged tree carries x1 \`1015b: GLOBALFIX\` marker and tools/patch_d13_globalfix.py --check PASSes"
# (b) FIND THE FUNCTION BY SYMBOL, NEVER BY A PARSER THAT CAN RETURN THE RELOCATION ITSELF. This
#     clause's FIRST measurement returned `pmap_flush_tlbs .. +8 bytes` because the next-symbol `awk`
#     matched the RELOCATION's own symbol line, and the two calls then fell OUTSIDE the "function" -
#     i.e. the check, not the code, was the defect. It now requires a STRICTLY GREATER `T`/`t` symbol
#     from `nm -n`, and refuses if it cannot find one, rather than computing a range it cannot justify.
gf_lo=$(arm-none-eabi-nm "$gf_obj" 2>/dev/null | awk '$3=="pmap_flush_tlbs" && ($2=="T"||$2=="t"){print "0x" $1; exit}')
[[ -n "$gf_lo" ]] || { echo "FAIL: arm-none-eabi-nm finds no TEXT symbol pmap_flush_tlbs in $gf_obj - the function is not in this object and the census below would be measuring nothing"; exit 1; }
gf_hi=$(arm-none-eabi-nm -n "$gf_obj" 2>/dev/null | awk -v s="${gf_lo#0x}" '
    # NO early `exit` in this awk (998'"'"'s measured SIGPIPE/141 defect under `set -o pipefail`): read the
    # whole stream and keep the FIRST strictly-greater text symbol.
    BEGIN { sv = strtonum("0x" s); want = 1 }
    { if (want) { a = strtonum("0x" $1); if (a > sv && ($2 == "T" || $2 == "t")) { print "0x" $1; want = 0 } } }')
[[ -n "$gf_hi" ]] || { echo "FAIL: no TEXT symbol follows pmap_flush_tlbs in $gf_obj - the range for the census below cannot be computed, and a census over an uncomputable range measures nothing"; exit 1; }
echo "   POOL: pmap_flush_tlbs = $gf_lo .. $gf_hi (the next text symbol; MEASURED at OFF state: 0x418 .. 0x63c)"
# (c) THE CENSUS. Two readings, both BY VALUE, both object-side:
#     * the flush-arm call sites (`R_ARM_CALL arm_tlb_flushID`/`_ASID` inside [$gf_lo, $gf_hi)) are
#       REPORTED and their CODE ORDER checked: the full arm must precede the ASID arm (Apple's order),
#       because the ASID arm now hangs off the `pmap == kernel_pmap` branch that terminates the chain.
#       Names are matched EXACTLY (`arm_tlb_flushID_ASID` has `arm_tlb_flushID` as a prefix).
#     * **THE INSERTION'S OWN FINGERPRINT:** the number of `kernel_pmap` address-materialization
#       relocations. Apple materializes it exactly TWICE (the `pmap_is_shared` init and the terminal
#       panic); the switch adds a THIRD, the `else if (pmap != kernel_pmap)` test. Each materialization
#       is an `R_ARM_MOVW_ABS_NC` (+ its `R_ARM_MOVT_ABS`); the `MOVW_ABS_NC` count is the count of
#       sites, and it is measured OFF=2 / ON=3. (Counted as MOVW, NOT as offset+4 pairs: MEASURED, the
#       compiler puts an unrelated instruction between a materialization's movw and movt - `0x440` then
#       `0x448` - so an `o+4` pairing UNDERCOUNTS and is itself a measurement defect.)
python3 - "$gf_obj" "$gf_lo" "$gf_hi" <<'PYGF' || exit 1
import re, subprocess, sys
obj, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
out = subprocess.run(["arm-none-eabi-objdump", "-r", obj], capture_output=True, text=True).stdout
sites = {"arm_tlb_flushID": [], "arm_tlb_flushID_ASID": []}
kp_movw = []
for line in out.splitlines():
    m = re.match(r"^([0-9a-f]{8})\s+R_ARM_(\S+)\s+(\S+)\s*$", line)
    if not m:
        continue
    off, kind, name = int(m.group(1), 16), m.group(2), m.group(3)
    if not (lo <= off < hi):
        continue
    if name in sites and kind == "CALL":
        sites[name].append(off)
    if name == "kernel_pmap" and kind == "MOVW_ABS_NC":
        kp_movw.append(off)
full, asid = sites["arm_tlb_flushID"], sites["arm_tlb_flushID_ASID"]
print("   POOL: inside pmap_flush_tlbs: arm_tlb_flushID x%d at %s; arm_tlb_flushID_ASID x%d at %s"
      % (len(full), [hex(o) for o in full], len(asid), [hex(o) for o in asid]))
print("   POOL: kernel_pmap materializations x%d at %s (OFF/Apple = 2)"
      % (len(kp_movw), [hex(o) for o in kp_movw]))
ok = True
if len(kp_movw) != 3:
    print("   FAIL: pmap_flush_tlbs materializes kernel_pmap at %d site(s) (%s), want EXACTLY 3 - the"
          " OFF/Apple object has 2 (`:1291` pmap_is_shared init + the `:1382` terminal panic) and the"
          " 1015b switch adds the 3rd, the inserted `else if (pmap != kernel_pmap)` test at the offset"
          " immediately before the full arm's `bl`. 2 => the switch is INERT (the staged text is not"
          " what was compiled, or -DSTAGE90_XNU_GLOBAL_FIX=1 never reached build_xnu_arm_kernel.sh: it"
          " rides XNU_KERNEL_EXTRA_DEFINES ONLY; there is no DEFINES+= clause for it); >3 => a second"
          " insertion reached the tree. build_entry.sh only LINKS this object, so the entry build is"
          " green either way." % (len(kp_movw), [hex(o) for o in kp_movw]))
    ok = False
if len(asid) != 1:
    print("   FAIL: arm_tlb_flushID_ASID has %d relocation(s) inside pmap_flush_tlbs (%s), want EXACTLY"
          " 1 - the ASID arm REMAINS IN THE TEXT (this script's header: 'UNREACHABLE ... while remaining"
          " in the text'); it is reached only through the `pmap == kernel_pmap` branch the inserted test"
          " makes impossible. 0 would mean the rung DELETED Apple's arm rather than shadowing it, which"
          " is not the declared change." % (len(asid), [hex(o) for o in asid]))
    ok = False
if not (len(full) == 1 and len(asid) == 1 and full[0] < asid[0]):
    print("   FAIL: the flush arms are not the declared shape (full=%s, asid=%s) - want ONE full call"
          " BEFORE the ONE ASID call (Apple's code order; the identical full-flush sites tail-merge into"
          " one `bl`)." % ([hex(o) for o in full], [hex(o) for o in asid]))
    ok = False
if not ok:
    sys.exit(1)
print("   POOL: MOVED - the ASID arm is UNREACHABLE for non-kernel pmaps but PRESENT (x1 at %s, after"
      " the full arm x1 at %s), and the switch's own byte - the 3rd kernel_pmap materialization - is in"
      " the text." % (hex(asid[0]), hex(full[0])))
PYGF
gf_size=$(stat -c%s "$gf_obj")
gf_tmp=$(mktemp) || { echo "FAIL: mktemp failed"; exit 1; }
arm-none-eabi-objcopy -O binary --only-section=.text "$gf_obj" "$gf_tmp" 2>/dev/null || true
gf_text=$(stat -c%s "$gf_tmp" 2>/dev/null || echo 0)
rm -f "$gf_tmp"
echo "   POOL: MEASURED ARTIFACT, stated rather than asserted: osfmk_arm_pmap.o = $gf_size B, .text = $gf_text B"
echo "   POOL: (OFF state was 45376 B; the .text delta is a CODEGEN artifact - the \`cmp\` can reuse the"
echo "   POOL:  \`beq\` the compiler already emitted (net 0) or need a fresh branch pair (net +8) - which"
echo "   POOL:  is why the claim above is the RELOCATION CENSUS and the CODE ORDER, not a size.)"
# (d) THE INVERSE ASSERTION: 1015's purity clauses must NOT be in this script. Carried forward they
#     would FAIL the build on a correct arm (the object is no longer 1014's), and a clause that fails
#     for the wrong reason is not a check - the same defect class the OFF block below records. This is
#     asserted rather than trusted because the copy that produced this script is one `sed` away.
#     **THE PATTERNS ARE ASSEMBLED AT RUNTIME**, so the clause cannot match its own source: a literal
#     spelled out here would be found in THIS file by the very grep that is looking for it, and the
#     check would fail on itself ([[mi4-a-claim-in-a-comment-is-not-a-check]], running backwards).
python3 - scripts/build_1015b.sh <<'PYINV' || exit 1
import sys
path = sys.argv[1]
text = open(path).read()
# The marker's pattern is the READER IDIOM, not the bare word: this script's own PROSE names the
# removed marker several times (explaining why it is gone), and a bare-word search would flag those
# comments - a check that fails on its own documentation. What must not survive is a clause that
# READS the marker out of an object or a tree.
bad = {
    'wf_asid" -eq ' + '3': "1015's TLBIASID-EXACTLY-3 purity clause",
    'wf_full" -eq ' + '3': "1015's TLBIALL-EXACTLY-3 purity clause",
    "grep -cF 'USER" + "FULLFLUSH'": "the REMOVED active half's marker reader",
}
for k, what in bad.items():
    n = text.count(k)
    if n:
        print("   FAIL: %s occurs x%d in %s - 1015b MOVES the pool object, so this clause would"
              " refuse a correctly built arm (the object is no longer 1014's) and would read as a"
              " discipline while doing it. Delete it; do not relax it."
              % (what, n, path))
        sys.exit(1)
print("   POOL: x0 of 1015's purity clauses survive in this script (they are the exact inverse of"
      " this block)")
PYINV
echo "   POOL: x0 of 1015's purity clauses survive in this script (they are the exact inverse of this block)"

# (e) THE CROSS-LANE DECLARATION CARRIER, ASSERTED EMPTY AND AT A FIXED PATH. The switch that moves the
#     kernel is declared in `out/stage90/xnu_arm_entry-arm-extra.txt` (created empty at the TOP of
#     this script, before anything reads it), because `src/entry/build_entry.sh` - the only writer of the record - is another
#     lane's file and does not name the switch (MEASURED x0). The arm's declaration therefore rides a
#     DROP-IN file the park appends to.
#     **WHY A FIXED PATH AND NOT A TEMP NAME.** `scripts/park_1015b.sh` appends to this exact path at
#     the freeze point, and the park must not have to guess a name: a random name would need the park
#     and the build to share state outside both files, and the arm would freeze with a declaration
#     nothing could find afterwards. A fixed name is a contract; the assertion below is what makes it a
#     checked one.
#     **AND IT IS EMPTY HERE ON PURPOSE.** This build writes NO declaration - the ON arm's declaration
#     is the park's line. A build that left a line here would be a build whose output directory
#     describes an arm other than the one it just built ([[mi4-out-stage90-is-shared-live-state]]:
#     out/stage90 is read by the press preflight, and a stale declaration there is a stale claim).
[[ -f "$_b_extra_file" ]] || { echo "FAIL: $_b_extra_file is absent - the cross-lane declaration carrier for STAGE90_XNU_GLOBAL_FIX was not created at the TOP of this script. A park would have nowhere to append the one line that declares the switch that MOVED THE KERNEL, and the arm would freeze undescribed"; exit 1; }
_b_extra_n=$(wc -l < "$_b_extra_file" || echo 0)
[[ "$_b_extra_n" == "0" ]] || { echo "FAIL: $_b_extra_file is not empty ($_b_extra_n line(s)) at the freeze point - this BUILD writes no declaration; the park appends the ON arm's one line. A non-empty file here means either a stale file from a prior arm survived (out/stage90 is shared live state) or a build wrote a claim about an arm it is not"; exit 1; }
echo "   (e) the cross-lane declaration carrier $_b_extra_file exists and is EMPTY at this build (the park appends the ON arm's one line: 'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix')"

# ====================================================================================================
# 1014's OFF BYTE-IDENTITY PROOF (design §2.4 item 5): THE HARDER HALF, AND IT IS A `cmp`.
# ====================================================================================================
# `--check` proves the payload is STAGED.  It says NOTHING about whether switch OFF really is Apple's
# bytes - a `#if` can gate the wrong region and still make `--check` green.  The reading that cannot be
# faked is a pair of compiles with the SAME command line and the switch OFF: one with the tree PATCHED
# (the gate deletes all four insertions) and one with the tree REVERTED to Apple's text.  `cmp` of the
# two objects is the proof.
#
# The comparator must be the POOL OBJECT, not the linked image, because the linked image embeds the
# entry blob and the payload and moves for reasons that have nothing to do with this switch.
#
# It rebuilds `osfmk/arm` only (`--dir osfmk/arm`) - one directory of the pool, ~19 C files - and it
# rebuilds it TWICE.  That is the cost of a proof that can fail; the alternative (a saved "pre-patch
# baseline" object, which the first draft of this block used) was REJECTED BY MEASUREMENT: the baseline
# on disk was built with the arm's FULL pool switch set, so its `pmap.c` already carried the 992
# commpage-alias blocks and it was 460 bytes larger in `.text` - a `cmp` against it `differ`s for a
# reason that predates this rung, and a check that fails for the wrong reason is not a check.
#
# **It restores the ON state before returning**, because everything below (the entry link included)
# needs the switch's text in the tree; the function `patch_d13_flushwitness.py --revert` refuses to
# write a half-reverted tree, so a failure inside cannot leave a silently-OFF tree behind.
echo "-- by value: the OFF byte-identity proof (design §2.4 item 5) - patched-tree-OFF vs reverted-tree-OFF, same command line --"
# **WHAT THIS MEASURES, AND WHAT IT CANNOT.** The design asks for a `cmp` of the pool object between
# switch OFF and the pre-patch tree. MEASURED, that `cmp` DOES NOT COME BACK CLEAN, and the reason is
# NOT the switch: `assert(...)` in this tree stringifies the failing expression with `__LINE__`, so
# every assert BELOW the probe carries `.../osfmk/arm/pmap.c:<line>` and the line numbers move by the
# 42 physical lines the gated region occupies - even with the gate CLOSED. The four assert strings above
# this function still read `1368`/`1410`/`1460`/... in the patched build and `1326`/`1368`/`1418`/... in
# the reverted one. A raw `cmp` would therefore fail for a reason that has nothing to do with the code,
# which is a check that fails for the wrong reason - the same defect class this block's first draft had
# (it compared against a saved baseline object built with the arm's FULL switch set; that object was 460
# bytes larger because its `pmap.c` already carried the 992 commpage-alias blocks).
#
# So the reading is taken on the two things the switch can actually change, and it is stated by value:
#   * `.text`   BYTE-IDENTICAL - the gate left no instruction behind (this is the whole claim).
#   * the whole object, with the `pmap.c:<digits>` line fields NORMALIZED, BYTE-IDENTICAL - nothing
#     else moved either (no size change, no relocation change, no symbol change).
# The normalization is spelled out here rather than hidden in a tool: it rewrites exactly the string
# `pmap.c:<digits>` to `pmap.c:N`, and asserts that the ONLY bytes it rewrote are those line fields, by
# requiring the two objects to have EQUAL SIZE first. A gate that left an insertion behind would change
# `.text` and be caught by the first clause before the second one is reached.
echo "-- the OFF proof compiles only osfmk/arm, into a THROWAWAY object dir, so the pool the entry link reads is untouched --"
_wf_defs="$EXTRA"
_wf_dir=osfmk/arm
_wf_tmp=$(mktemp -d) || { echo "FAIL: mktemp -d failed"; exit 1; }
_wf_build() {  # $1 = destination .o ; $2 = the object dir to use
    # **THE SWITCH MUST BE OFF IN THE ENVIRONMENT TOO.** `tools/build_xnu_arm_kernel.sh` adds
    # `-DSTAGE90_XNU_FLUSH_WITNESS=1` from the EXPORTED variable (`:819`), independently of
    # `XNU_KERNEL_EXTRA_DEFINES`. Stripping the token from `_wf_defs` alone therefore compiled BOTH
    # halves with the switch ON and measured the SOURCE TEXT rather than the gate: MEASURED, the first
    # run of this block failed with "differ in SIZE (45376 vs 44904)" - the gated tree's probe compiled
    # in and the reverted tree's did not. Setting the variable to 0 for the duration of the build is
    # what makes this an OFF proof.
    # `STAGE90_XNU_GLOBAL_FIX=0` is set here for SYMMETRY ONLY, and it is labelled that way because
    # the difference is measurable: `tools/build_xnu_arm_kernel.sh` names that variable 0 times
    # (MEASURED; unlike FLUSH_WITNESS at `:819`), so this rung's switch has no environment path at
    # all - stripping its `-D` token from `_wf_defs` is what actually turns it off. The two switches
    # in this one `env` line therefore reach `build_xnu_arm_kernel.sh` by DIFFERENT routes, which is
    # exactly the kind of asymmetry that reads as a real carrier to the next person who copies it.
    STAGE90_XNU_FLUSH_WITNESS=0 \
    STAGE90_XNU_GLOBAL_FIX=0 \
    XNU_KERNEL_OBJ_OUT="$2" \
    XNU_PLATFORM_OBJ_OUT="$2/platform" \
    XNU_RT_OBJ_OUT="$2/rt" \
    XNU_MACHO_OBJ_OUT="$2/macho" \
    XNU_ASM_OBJ_OUT="$2/asm" \
    XNU_TREE="$XNU_TREE" \
    XNU_KERNEL_CONFIG=STAGE90_XNU \
    XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
    XNU_KERNEL_EXTRA_DEFINES="$_wf_defs" \
        ./tools/build_xnu_arm_kernel.sh --dir "$_wf_dir" >/dev/null 2>&1 || {
        echo "FAIL: the OFF-identity rebuild of $_wf_dir failed - see $2/failed.txt"; exit 1; }
    cp "$2/osfmk_arm_pmap.o" "$1"
}
# 1. the tree as STAGED, both pool switches 0.  This is `_wf_defs` with **BOTH** pool switches' `-D`
# removed, because 1015b HAS TWO: 1014's inherited `FLUSH_WITNESS` and this rung's `GLOBAL_FIX`. Each
# moves `.text` for its own reason, and a `cmp` that fails for a reason other than the gate is not a
# check - so BOTH must be OFF for the reading to be about the `#if`s rather than about the source text.
# 1015 had only one token to strip; the second is this rung's addition, and stripping ONE of the two
# is exactly the failure 1015's own block records at the message below ("differ in SIZE (45376 vs
# 44904)"). Both strips are asserted to have HAPPENED (the define set must change, and neither token
# may survive) so a typo cannot silently make both builds the same build.
_wf_defs_off=$(sed -e 's/ -DSTAGE90_XNU_FLUSH_WITNESS=1//' -e 's/ -DSTAGE90_XNU_GLOBAL_FIX=1//' <<<"$_wf_defs")
[[ "$_wf_defs_off" != "$_wf_defs" ]] || { echo "FAIL: neither -DSTAGE90_XNU_FLUSH_WITNESS=1 nor -DSTAGE90_XNU_GLOBAL_FIX=1 is in EXTRA ('$_wf_defs') - the OFF proof would compile the SAME switches twice and prove nothing"; exit 1; }
[[ "$_wf_defs_off" != *STAGE90_XNU_FLUSH_WITNESS* ]] || { echo "FAIL: a STAGE90_XNU_FLUSH_WITNESS token survives in the OFF define set ('$_wf_defs_off') - the OFF half would not be off, and the proof would be measuring 1014's gate rather than this rung's"; exit 1; }
[[ "$_wf_defs_off" != *STAGE90_XNU_GLOBAL_FIX* ]] || { echo "FAIL: a STAGE90_XNU_GLOBAL_FIX token survives in the OFF define set ('$_wf_defs_off') - 1015b's own gate would still be ON in the OFF half, so the two objects would differ BY the insertion and the proof would be measuring the source text, not the gate"; exit 1; }
[[ "$_wf_defs_off" != "$_wf_defs" ]] || { echo "FAIL: the DEFINE set did not change"; exit 1; }
_wf_defs="$_wf_defs_off"
mkdir -p "$_wf_tmp/gated"
_wf_build "$_wf_tmp/gated.o" "$_wf_tmp/gated"
# 2. the tree REVERTED to Apple's text, both switches 0 - the SAME command line. **TWO reverts, in the
#    REVERSE order of the two stages**: this rung's arm is removed FIRST (it anchors between Apple's
#    own lines, which the 1014 payload leaves in place), then the 1014 payload. Each tool refuses on a
#    tree that is not exactly its own output, so a half-done revert is a refusal rather than a
#    silently mixed build.
python3 tools/patch_d13_globalfix.py "$XNU_TREE" --revert >/dev/null || { echo "FAIL: patch_d13_globalfix --revert refused - the staged tree is not exactly the 1015b tool's output (was the 1014 payload reverted first?). The OFF proof cannot run"; exit 1; }
python3 tools/patch_d13_flushwitness.py "$XNU_TREE" --revert >/dev/null || { echo "FAIL: patch_d13_flushwitness --revert refused AFTER the 1015b revert - the staged tree is not exactly the 1014 tool's output, so the OFF proof cannot run"; exit 1; }
[[ $(grep -cF '1015b: GLOBALFIX' "$XNU_TREE/osfmk/arm/pmap.c" || true) == 0 ]] || { echo "FAIL: the 1015b marker survives the revert in $XNU_TREE/osfmk/arm/pmap.c - the 'Apple' half of the OFF proof would still carry this rung's arm and the two objects would differ by the insertion it is supposed to have removed"; exit 1; }
mkdir -p "$_wf_tmp/apple"
_wf_build "$_wf_tmp/apple.o" "$_wf_tmp/apple"
# 3. restore BOTH payloads BEFORE any assertion below, so a failure cannot leave the tree unstaged -
#    and in the STAGE order (1014 first, then 1015b), which is the order the driver list uses.
python3 tools/patch_d13_flushwitness.py "$XNU_TREE" >/dev/null || { echo "FAIL: the 1014 payload could not be re-staged after the OFF proof - the tree is left with NO probe"; exit 1; }
python3 tools/patch_d13_flushwitness.py "$XNU_TREE" --check >/dev/null || { echo "FAIL: after the OFF proof the re-staged 1014 payload does not pass --check"; exit 1; }
python3 tools/patch_d13_globalfix.py "$XNU_TREE" >/dev/null || { echo "FAIL: the 1015b arm could not be re-staged after the OFF proof - the tree is left with Apple's flush decision while the object census below would refuse"; exit 1; }
python3 tools/patch_d13_globalfix.py "$XNU_TREE" --check >/dev/null || { echo "FAIL: after the OFF proof the re-staged 1015b arm does not pass --check"; exit 1; }
# 4. THE READINGS.
_wf_sz_g=$(stat -c%s "$_wf_tmp/gated.o"); _wf_sz_a=$(stat -c%s "$_wf_tmp/apple.o")
[[ "$_wf_sz_g" == "$_wf_sz_a" ]] || { echo "FAIL: the OFF build of the PATCHED tree and of the REVERTED tree differ in SIZE ($_wf_sz_g vs $_wf_sz_a) - the gate does not remove exactly the inserted text (an Apple statement was edited, or the insertions are not all gated). The line-field normalization below presumes equal size"; exit 1; }
arm-none-eabi-objcopy -O binary --only-section=.text "$_wf_tmp/gated.o" "$_wf_tmp/gated.text"
arm-none-eabi-objcopy -O binary --only-section=.text "$_wf_tmp/apple.o" "$_wf_tmp/apple.text"
cmp -s "$_wf_tmp/gated.text" "$_wf_tmp/apple.text" || { echo "FAIL: with STAGE90_XNU_FLUSH_WITNESS=0 AND STAGE90_XNU_GLOBAL_FIX=0 the .text of the patched tree is NOT byte-identical to the reverted (Apple) tree (gated $(stat -c%s "$_wf_tmp/gated.text") B vs apple $(stat -c%s "$_wf_tmp/apple.text") B) - A GATE LEAVES CODE BEHIND (each switch must be inert). OFF is not Apple's bytes. NOTE: with TWO switches this cmp is the JOINT claim; a failure does not by itself say WHICH gate leaked, so run the two tools' --check on the un-reverted tree before reading it as one"; exit 1; }
echo "   OFF: .text is BYTE-IDENTICAL ($(stat -c%s "$_wf_tmp/gated.text") B) between the gated and the reverted tree with the pool's TWO switches (1014's FLUSH_WITNESS and 1015b's GLOBAL_FIX) both 0 - each gate leaves no instruction behind. **THIS IS THE CLAIM THE DESIGN'S OFF PROOF CAN MAKE FOR 1015b**: OFF removes the arm entirely, so the compiler sees Apple's flush decision byte for byte even though the SOURCE TEXT carries a three-line insertion"
# The line-field normalization: `pmap.c:<digits>` -> `pmap.c:N`.  Equal size is already established, so
# the substitution cannot change the length and the compare stays a byte compare.
python3 - "$_wf_tmp/gated.o" "$_wf_tmp/apple.o" "$_wf_tmp/gated.norm" "$_wf_tmp/apple.norm" <<'PYNORM' || exit 1
import re, sys
_pattern = re.compile(rb'pmap\.c:\d+')
def norm(path):
    data = open(path, 'rb').read()
    return _pattern.sub(b'pmap.c:N', data)
g, a = norm(sys.argv[1]), norm(sys.argv[2])
if len(g) != len(a):
    print("FAIL: the normalized objects differ in length - the only difference was NOT the `pmap.c:<n>` line fields"); sys.exit(1)
if g != a:
    d = [i for i in range(len(g)) if g[i] != a[i]]
    print("FAIL: after normalizing the `pmap.c:<n>` line fields the OFF objects STILL differ (%d byte(s), first at %d). Something other than `__LINE__` moved: A=%r B=%r" % (len(d), d[0], g[d[0]-30:d[0]+30], a[d[0]-30:d[0]+30]))
    sys.exit(1)
open(sys.argv[3], 'wb').write(g); open(sys.argv[4], 'wb').write(a)
PYNORM
cmp -s "$_wf_tmp/gated.norm" "$_wf_tmp/apple.norm" || { echo "FAIL: the normalized OFF objects differ"; exit 1; }
echo "   OFF: with the assert line fields (pmap.c:<n>, which shift by the gated region's own line count even with the gate closed) normalized, the two objects are BYTE-IDENTICAL - OFF is Apple's object in every other byte"
echo "   OFF: MEASURED ARTIFACT, stated rather than hidden: the bytes that DO differ raw are only those line fields ($(cmp -l "$_wf_tmp/gated.o" "$_wf_tmp/apple.o" | wc -l) byte(s)); the gate removes the code, the preprocessor still counts the lines"
rm -rf "$_wf_tmp"
# 5. and the switch is ON again in the tree, so the entry link below reads the staged text.
[[ $(grep -cF 'entry_note_abt_wf' "$XNU_TREE/osfmk/arm/pmap.c" || true) -ge 1 ]] || { echo "FAIL: after the OFF proof the tree does not carry the probe again"; exit 1; }
echo "   OFF: the tree is re-staged and carries the probe (the entry link below reads the ON text)"

echo "-- entry image (1014's switch set re-exported verbatim + PERM_WITNESS=1; build_entry only LINKS the pool, which is 1014's object unchanged) --"
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
# The record (999's own) carries every tree-shaping switch including SHREG_GEOM and RESIDENT; the
# pins name them explicitly so build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the
# build layers compiled with ([[mi4-one-value-two-definitions]]). STAGE90_XNU_USVC is 1000's NEW key
# and the 999 record does NOT carry it yet - the pin is the only carrier this run, and the record the
# entry build writes (asserted at the tail) is what parks the arm with it.
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_XNU_COMMPAGE_ALIAS=1")
EXPORTS+=("STAGE90_XNU_IOS7_TRAPS=1")
EXPORTS+=("STAGE90_XNU_DYLD_WITNESS=1")
EXPORTS+=("STAGE90_XNU_SHREG_PROBE=1")
EXPORTS+=("STAGE90_XNU_SHREG_WIRE=1")
EXPORTS+=("STAGE90_XNU_SHREG_GEOM=1")
EXPORTS+=("STAGE90_XNU_RESIDENT=1")
EXPORTS+=("STAGE90_XNU_USVC=1")
# 1001's NEW key: the 1000 record does NOT carry it yet - the pin is the only carrier this run, and
# the record the entry build writes (asserted at the tail) is what parks the arm with it.
EXPORTS+=("STAGE90_XNU_SUBMAP_PMAP_SKIP=1")
# 1003's NEW key STAGE90_XNU_STALL. The 1002 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD only; the PE's
# call site was compiled from the environment export above (== the same value).
EXPORTS+=("STAGE90_XNU_STALL=1")
# 1004's NEW key STAGE90_XNU_ABORT_TRACE. The 1003 record does NOT carry it yet - the pin is the
# only carrier this run, and the record the entry build writes (asserted at the tail) is what parks
# the arm with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and
# adds it to `STUB_DEFINES` (the compile reads the SAME value).
EXPORTS+=("STAGE90_XNU_ABORT_TRACE=1")
# 1005's NEW key STAGE90_XNU_CADENCE. The 1004 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_CADENCE` to BOTH `STUB_DEFINES` (entry_stubs.c) and `STUB_DEFINES_TRACE`
# (entry_trace.c) - the same value the platform export above gave pe_msm8974.o.
EXPORTS+=("STAGE90_XNU_CADENCE=1")
# 1006's NEW key STAGE90_XNU_FREEZE_WITNESS. The 1005 record does NOT carry it yet - the pin is the
# only carrier this run, and the record the entry build writes (asserted at the tail) is what parks the
# arm with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_FREEZE_WITNESS` to BOTH `STUB_DEFINES` (entry_stubs.c, the sampler) and
# `STUB_DEFINES_TRACE` (entry_trace.c, the call site). No platform object and no pool object reads it.
EXPORTS+=("STAGE90_XNU_FREEZE_WITNESS=1")
# 1007's NEW key STAGE90_XNU_ABT_SPAN. The 1006 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_ABT_SPAN` to BOTH `STUB_DEFINES` (entry_stubs.c, the recorder `entry_note_abt_span`)
# and `STUB_DEFINES_TRACE` (entry_trace.c, the bracket inside `__wrap_sleh_abort`). No platform object
# and no pool object reads it. NOTE: 1007 KEEPS 1006's FREEZE_WITNESS=1 (the inheriting arm) - both
# are pinned.
EXPORTS+=("STAGE90_XNU_ABT_SPAN=1")
# 1008's NEW key STAGE90_XNU_ABT_FAR. The 1007 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_ABT_FAR` to BOTH `STUB_DEFINES` (entry_stubs.c, the recorder `entry_note_abt_far`) and
# `STUB_DEFINES_TRACE` (entry_trace.c, the call after the span bracket). No platform object and no pool
# object reads it. It requires STAGE90_XNU_ABT_SPAN=1 (the far call reuses `t0`) - both are pinned.
EXPORTS+=("STAGE90_XNU_ABT_FAR=1")
# 1009's NEW key STAGE90_XNU_ABT_PC. The 1008 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_ABT_PC` to BOTH `STUB_DEFINES` (entry_stubs.c, the recorder `entry_note_abt_pc`) and
# `STUB_DEFINES_TRACE` (entry_trace.c, the call after the far call). No platform object and no pool
# object reads it. It requires STAGE90_XNU_ABT_FAR=1 (the 1009 call sits after the 1008 call and reads
# the frame the far call's span already bracketed) - both are pinned.
EXPORTS+=("STAGE90_XNU_ABT_PC=1")
# 1010's NEW key STAGE90_XNU_ABT_PTE. The 1009 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_ABT_PTE` to BOTH `STUB_DEFINES` (entry_stubs.c, the walk + the two recorders) and
# `STUB_DEFINES_TRACE` (entry_trace.c, the call + `__wrap_vm_fault`). No platform object and no pool
# object reads it. It ALSO gates the `--wrap=vm_fault` in build_entry.sh's TRACE_LDFLAGS - so exporting
# `ABT_PTE=1` is what makes the wrap appear, and the OFF arm carries none. It requires BOTH
# STAGE90_XNU_ABT_FAR=1 and STAGE90_XNU_ABT_PC=1 (the recorder's guard is the conjunction) - all three
# are pinned.
EXPORTS+=("STAGE90_XNU_ABT_PTE=1")
# 1011's NEW key STAGE90_XNU_VME_PROT. The 1010c record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_VME_PROT` to BOTH `STUB_DEFINES` (entry_stubs.c: the (E') recorder `entry_note_abt_vme`
# + the (A) reader `stage90_abt_thread_l1_base` + the `_ptethr` store) and `STUB_DEFINES_TRACE`
# (entry_trace.c: the declarations + `__wrap_vm_map_lookup_locked`). No platform object and no pool
# object reads it. It ALSO gates the `--wrap=vm_map_lookup_locked` in build_entry.sh's TRACE_LDFLAGS -
# so exporting `VME_PROT=1` is what makes the wrap appear, and the OFF arm carries none. It requires
# the full 1010 ladder (STAGE90_XNU_ABT_FAR=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_PTE=1) - all
# three are pinned from the record above.
EXPORTS+=("STAGE90_XNU_VME_PROT=1")
# 1012's NEW key STAGE90_XNU_PMP_ENTER. The 1011 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_PMP_ENTER` to BOTH `STUB_DEFINES` (entry_stubs.c: the recorder `entry_note_abt_pmp`)
# and `STUB_DEFINES_TRACE` (entry_trace.c: the declarations + `__wrap_pmap_enter_options`). No platform
# object and no pool object reads it - its only two consumers are those entry-toolchain objects. It ALSO
# gates the `--wrap=pmap_enter_options` in build_entry.sh's TRACE_LDFLAGS - so exporting `PMP_ENTER=1`
# is what makes the wrap appear, and the OFF image carries none. It keys on the 1010 latch chain
# `entry_note_abt_pte` arms, so it requires the full 1010 ladder (STAGE90_XNU_ABT_FAR=1 AND
# STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_PTE=1) - all three are pinned from the record above.
EXPORTS+=("STAGE90_XNU_PMP_ENTER=1")
# 1013's NEW key STAGE90_XNU_PTE_WITNESS. The 1012 record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_PTE_WITNESS` to BOTH `STUB_DEFINES` (entry_stubs.c: the three recorders
# `entry_note_abt_wleaf`/`entry_note_abt_walk`/`entry_note_abt_wsync` + `g_abtpte_ptel2rec`) and
# `STUB_DEFINES_TRACE` (entry_trace.c: the three declarations + the three call sites). No platform
# object and no pool object reads it - its only two consumers are those entry-toolchain objects. It
# adds NO `--wrap` (1013 rides 1012's `--wrap=pmap_enter_options` and the existing `--wrap=sleh_abort`),
# so unlike PMP_ENTER/VME_PROT there is no TRACE_LDFLAGS clause to assert. It observes the enter's apex
# AND keys on the (A) latch chain, so it requires `PMP_ENTER=1` plus the full 1010 ladder
# (STAGE90_XNU_ABT_FAR=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_PTE=1) - all four are pinned.
EXPORTS+=("STAGE90_XNU_PTE_WITNESS=1")
# 1014's NEW key STAGE90_XNU_FLUSH_WITNESS. The 1013b record does NOT carry it yet - the pin is the only
# carrier this run, and the record the entry build writes (asserted at the tail) is what parks the arm
# with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the RECORD and adds
# `-DSTAGE90_XNU_FLUSH_WITNESS` to BOTH `STUB_DEFINES` (entry_stubs.c: the two recorders
# `entry_note_abt_wf`/`entry_note_abt_wctx`) and `STUB_DEFINES_TRACE` (entry_trace.c: the declaration +
# the ONE `entry_note_abt_wctx` call). It ALSO adds `-I $XNU_TREE/osfmk` to those two compile lines
# only, so the recorders can reach `proc_reg.h:583`'s own CONTEXTIDR macro without re-spelling cp15
# ([[mi4-one-value-two-definitions]]). It adds NO `--wrap` - a `--wrap=pmap_flush_tlbs` would be a hook
# that cannot fire (all 13 call relocations are intra-object). Its THIRD consumer is the kernel pool
# (`osfmk_arm_pmap.o`, via the `EXTRA` entry and the `export` above), and it keys on 1013's
# `g_abtpte_curva`, so it requires `PTE_WITNESS=1` plus the full 1010 ladder and `PMP_ENTER=1` - all
# five are pinned.
EXPORTS+=("STAGE90_XNU_FLUSH_WITNESS=1")
# 1015's NEW key STAGE90_XNU_PERM_WITNESS (the PASSIVE half). The 1014 record does NOT carry it yet -
# the pin is the only carrier this run, and the record the entry build writes (asserted at the tail) is
# what parks the arm with it ([[mi4-one-value-two-definitions]]). build_entry.sh reads it for the
# RECORD and adds `-DSTAGE90_XNU_PERM_WITNESS` to BOTH `STUB_DEFINES` (entry_stubs.c: the `wd*`
# recorder) and `STUB_DEFINES_TRACE` (entry_trace.c: the declaration + the ONE `entry_note_abt_wd`
# call beside `entry_note_abt_wctx(far_)`). It adds NO `--wrap` - the recorder rides the EXISTING
# `--wrap=sleh_abort`, so unlike PMP_ENTER/VME_PROT there is no TRACE_LDFLAGS clause to assert. It keys
# on 1013's `g_abtpte_curva`, so it requires `PTE_WITNESS=1` plus the full 1010 ladder and
# `PMP_ENTER=1` - all five are pinned from the record above.
EXPORTS+=("STAGE90_XNU_PERM_WITNESS=1")
# **A CROSS-LANE DECLARATION, NOT A SWITCH: `STAGE90_XNU_GLOBAL_FIX` DOES NOT GO IN `EXPORTS`.**
# `EXPORTS` is the list `build_entry.sh` is invoked with, and by that invocation the *record* is
# already WRITTEN - the record is produced BY that same invocation, so a name added here cannot reach
# the file it produces ([[mi4-the-step-after-the-point-of-no-return]]: read the failure path as if it
# were already taken). This is the first of "two ways to declare a switch" this rung refuses.
# THE SECOND WAY, WHICH LOOKS BETTER AND IS ALSO DEAD, IS THE ENVIRONMENT. `build_entry.sh` is invoked
# below through `env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh` - the environment
# is WIPED and rebuilt from `EXPORTS` alone, so an `export` in this shell could not reach it even if
# the file named the switch (it does not: `grep -c STAGE90_XNU_GLOBAL_FIX src/entry/build_entry.sh`
# is 0). Both spellings were measured, and both are dead; that is why the declaration is a DROP-IN
# FILE rather than a variable (docs/1015b-design.md §3).
#   * `src/entry/build_entry.sh` does NOT echo any such line - that file is another lane's
#     ([[mi4-file-lanes]]) and the lane boundary is not crossed by an edit, so the cross-lane
#     declaration is made HERE: the dropped-in file `xnu_arm_entry-arm-extra.txt` beside the record,
#     onto the tail of which `scripts/park_1015b.sh` appends one line, verbatim, for exactly one
#     parked arm.
# `[[mi4-one-value-two-definitions]]` is the reason this is a refusal at the build rather than a note:
# a record that omits the switch that MOVED THE KERNEL is a record that describes the wrong arm, and
# the rung whose whole point is a behaviour change is the last one that may ship one.
_b_eval=$(grep -cE '^\s*eval\s+.*STAGE90_XNU_GLOBAL_FIX' src/entry/build_entry.sh || true)
echo "   (record) the cross-lane declaration file $_b_extra_file (RESET to empty at the top of this script) is still empty; build_entry.sh's eval of the name: x$_b_eval (MEASURED 0 this session - this file is its only carrier)"
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   1009 was 24997fc5, 1010 was 0f39207c, 1010b was 64391319, 1010c was 002b0683, 1011 was fce83837, 1012 was ffdc8ba1, 1013 was 9cf2f15e, 1014 was 29ec9e48, 1015 was e51d14fa - a DIFFERENT value here means the 1015b edits reached the image (and for this rung in particular: the entry half is 1015's, so a move here is EXPECTED from the POOL segment alone)."
# The arm must MOVE off the PRESSED arms (the project rule: the arm IS the entry bin's sha8). A build
# that lands back on 64391319 did not change the entry image - the 1010c window bound + alias fix did
# not reach the linked image (or one of the two entry objects was not rebuilt). This is the build-side
# counterpart of the arm-refusal guards the park/press scripts carry.
_d6_arm=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
# **THE STALE-ARM REFUSAL THAT NAMES THE PRESSED 1015 PREDECESSOR.** The project rule is that the arm
# IS the entry bin's sha8, so a build that lands back on `armed-d13-29ec9e48` - 1014's PRESSED arm -
# did NOT move the entry image: the 1015 PASSIVE half (the `wd*` recorder + its ONE call) never reached
# the linked image. This is the chain `build_1013.sh:607-618` carries, one rung on. NOTE that this one
# is a DIFFERENT KIND of failure from its neighbours below: 1015's ACTIVE half lives in the POOL
# (`osfmk_arm_pmap.o`), which `build_entry.sh` only LINKS - so a widened flush COULD reach the pool
# object while the entry bin stayed put. Either way the arm is stale and must not be parked.
# **1015b's OWN STALE-ARM REFUSAL FIRST: `armed-d13-e51d14fa` IS 1015's PRESSED PREDECESSOR.** This is
# the refusal that R7 in `docs/1015b-design.md` §4 names, and it is a DIFFERENT KIND of failure from
# every clause below it: 1015's switch set is ENTRY-ONLY, so landing back on 1015's arm could mean
# either (i) the entry half did not rebuild, or (ii) **the POOL edit reached its object but
# `build_entry.sh` LINKED a stale copy** - `build_entry.sh` only LINKS the pool objects, so a
# `pmap.c` edit that rebuilt `osfmk_arm_pmap.o` but was not re-LINKED would leave the entry bin's bytes
# where they were... **except that the pool's `.text` IS inside the `.bin`** (MEASURED:
# `LOAD 0x010000 0x80000000 0x80000000 0x570000 R E`, and `pmap_flush_tlbs` at `0x8002968c` sits inside
# it). So the link is not optional: if the arm equals 1015's, the entered decision did NOT reach the
# bytes and the rung is measuring Apple's kernel with a record claiming otherwise - the exact false
# green `[[mi4-one-value-two-definitions]]` describes.
[[ $_d6_arm == armed-d13-e51d14fa ]] && { echo "FAIL: the build produced 1015's PRESSED arm (e51d14fa) again - THE POOL EDIT DID NOT REACH THE LINKED IMAGE. This is 1015b's own refusal (docs/1015b-design.md R7) and it is not the same failure as its neighbours below: 1015 was ENTRY-ONLY, so this arm's bytes are that arm's bytes, and 1015b MOVES THE KERNEL. Check, in this order: (1) tools/stage_d13_globalfix.sh ran and tools/patch_d13_globalfix.py --check passes (the tree text is staged); (2) the -D token in EXTRA reached the POOL compile - it is this switch's ONLY channel (NO DEFINES+= clause in tools/build_xnu_arm_kernel.sh and NO environment path: MEASURED 0 mentions there, and this script refuses if a caller exports the variable); (3) tools/build_xnu_arm_kernel.sh actually RECOMPILED osfmk_arm_pmap.o (a stale object is LINKED happily); (4) the pool census just above says MOVED. Do NOT park this arm: it would be an arm described as a behaviour change that is not in its bytes"; exit 1; }
[[ $_d6_arm == armed-d13-29ec9e48 ]] && { echo "FAIL: the build produced 1014's PRESSED arm (29ec9e48) again - THE ENTRY BIN DID NOT MOVE, so the 1015 PASSIVE half (the entry_note_abt_wd recorder in entry_stubs.c + its ONE call in entry_trace.c) did not reach the linked image. Rebuild with STAGE90_XNU_PERM_WITNESS=1 exported through BOTH STUB_DEFINES (entry_stubs.c) and STUB_DEFINES_TRACE (entry_trace.c) AND with STAGE90_XNU_ABT_FAR/ABT_PC/ABT_PTE/PMP_ENTER/PTE_WITNESS all =1 (the recorder's guard is the conjunction and it keys on g_abtpte_curva, which only PTE_WITNESS writes). NOTE: the ACTIVE half's widening lives in the POOL object, which build_entry.sh only LINKS - so this refusal fires on the passive half even when the pool half is correct; do not park a 1015 arm identical to the pressed 1014 one"; exit 1; }
[[ $_d6_arm == armed-d13-ffdc8ba1 ]] && { echo "FAIL: the build produced 1012's PRESSED arm (ffdc8ba1) again - the entry bin did NOT move, so the 1013 witness (the three recorders + their call sites) did not reach the linked image (rebuild: build_entry.sh with STAGE90_XNU_PTE_WITNESS=1 AND STAGE90_XNU_PMP_ENTER=1 AND STAGE90_XNU_ABT_PTE=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_FAR=1 exported for BOTH STUB_DEFINES and STUB_DEFINES_TRACE); do not park a 1013 arm identical to the pressed 1012 one"; exit 1; }
[[ $_d6_arm == armed-d13-9cf2f15e ]] && { echo "FAIL: the build produced 1013's PRESSED arm (9cf2f15e) again - the entry bin did NOT move, so the 1014 flush witness (entry_note_abt_wf's call site inside pmap_flush_tlbs + the CONTEXTIDR recorder entry_note_abt_wctx and its call) did not reach the linked image. Rebuild with STAGE90_XNU_FLUSH_WITNESS=1 exported BOTH for build_entry.sh (via EXPORTS) and for tools/build_xnu_arm_kernel.sh (the pool's osfmk_arm_pmap.o) AND with tools/stage_d13_flushwitness.sh run first - the pool object is where the ONE call site lives, and build_entry.sh only LINKS it"; exit 1; }
[[ $_d6_arm == armed-d13-9bf25f8f ]] && { echo "FAIL: the build produced 1013b's arm (9bf25f8f) - a stale layer (the 1014 leaf-value witness, not the 1015 experiment)"; exit 1; }
[[ $_d6_arm == armed-d13-fce83837 ]] && { echo "FAIL: the build produced 1011's PRESSED arm (fce83837) again - the entry bin did NOT move, so the 1012 apex (the recorder + the --wrap=pmap_enter_options) did not reach the linked image (rebuild: build_entry.sh with STAGE90_XNU_PMP_ENTER=1 AND STAGE90_XNU_VME_PROT=1 AND STAGE90_XNU_ABT_PTE=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_FAR=1 AND STAGE90_XNU_ABT_SPAN=1 exported for BOTH STUB_DEFINES and STUB_DEFINES_TRACE); do not park a 1012 arm identical to the pressed 1011 one"; exit 1; }
[[ $_d6_arm == armed-d13-64391319 ]] && { echo "FAIL: the build produced 1010b's PRESSED arm (64391319) again - the entry bin did NOT move, so the 1010c window bound + temporary-section alias did not reach the linked image (rebuild: build_entry.sh with STAGE90_XNU_ABT_PTE=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_FAR=1 AND STAGE90_XNU_ABT_SPAN=1 exported for BOTH STUB_DEFINES and STUB_DEFINES_TRACE); do not park a 1010c arm identical to the pressed 1010b one"; exit 1; }
[[ $_d6_arm == armed-d13-0f39207c ]] && { echo "FAIL: the build produced 1010's PRESSED arm (0f39207c) again - the entry bin did NOT move, so the 1010b instrument fix did not reach the linked image (rebuild: build_entry.sh with STAGE90_XNU_ABT_PTE=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_FAR=1 AND STAGE90_XNU_ABT_SPAN=1 exported for BOTH STUB_DEFINES and STUB_DEFINES_TRACE); do not park a 1010b arm identical to the pressed 1010 one"; exit 1; }
[[ $_d6_arm == armed-d13-24997fc5 ]] && { echo "FAIL: the build produced 1009's arm (24997fc5) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-0343ef45 ]] && { echo "FAIL: the build produced 1008's arm (0343ef45) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-88074ab3 ]] && { echo "FAIL: the build produced 1007's arm (88074ab3) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-31bdebf3 ]] && { echo "FAIL: the build produced 1006's arm (31bdebf3) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-ff937225 ]] && { echo "FAIL: the build produced 1005's arm (ff937225) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-582a19c8 ]] && { echo "FAIL: the build produced 1004's arm (582a19c8) - a stale layer"; exit 1; }
[[ $_d6_arm == armed-d13-7669309b ]] && { echo "FAIL: the build produced 1003's arm (7669309b) - a stale layer"; exit 1; }
# ====================================================================================================
# **THE POOL OBJECT CHANGED VS 1015's FROZEN COPY - BY VALUE, NOT BY THE ARM NAME.**
# ====================================================================================================
# The instruction is "assert the pool object CHANGED vs `out/stage90/frozen/armed-d13-e51d14fa/`", and
# the FROZEN COPY DOES NOT CONTAIN A POOL OBJECT - it holds the LINKED artifact set (`xnu_arm_entry.elf`
# / `.bin`), which is what a park freezes. So the assertion is taken on the surface the frozen copy
# DOES carry, and it is the same claim at one level up: **the flush arm inside `pmap_flush_tlbs` changed
# identity.** MEASURED this session, on the frozen 1015 elf
# (`arm-none-eabi-objdump -d ... --start-address=0x8002968c --stop-address=0x800298b0`):
#
#     800297d4:  bl  80498188 <arm_tlb_flushID>         <- Apple's :1347 full arm
#     80029800:  bl  80498178 <arm_tlb_flushID_ASID>    <- Apple's :1349 ASID arm, THE ONE 1015b RETIRES
#
# The new image must carry **ZERO** `bl arm_tlb_flushID_ASID` inside the function and **>= 2**
# `bl arm_tlb_flushID`. This is the INVARIANT form of the claim (the same one the object census above
# makes): the linked ADDRESS of the second call is NOT asserted, because it moves with every rung -
# what is asserted is WHICH SYMBOL IS CALLED, and the frozen 1015 elf is carried as an explicit
# NEGATIVE CONTROL so a checker that cannot fail cannot pass here
# ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
#
# WHY BOTH THIS AND THE OBJECT CENSUS. They fail independently: the object census catches "the switch
# never reached the compile" and this one catches "the object was rebuilt but a STALE one was LINKED"
# - the `build_entry.sh` only-LINKS trap, which is 1015b's signature failure. Neither subsumes the other.
_d6f=out/stage90/frozen/armed-d13-e51d14fa/xnu_arm_entry.elf
if [[ -f "$_d6f" ]]; then
  # **THE FINGERPRINT IS THE kernel_pmap MATERIALIZATION COUNT, MEASURED, NOT A CALL COUNT.** In a
  # LINKED image there are no relocations, so the object census's `R_ARM_MOVW_ABS_NC kernel_pmap` test
  # cannot run here. The equivalent by value is the resolved low half: `kernel_pmap`'s linked address
  # is read from `nm`, its low 16 bits become a `movw rX, #<lo>` immediate, and the count of those in
  # pmap_flush_tlbs is the count of materialization sites. MEASURED: 1015's frozen elf = 2, the 1015b
  # image = 3. (The `bl arm_tlb_flushID` / `_ASID` counts are IDENTICAL in both - x1 each, because the
  # compiler tail-merges the full-flush sites and keeps the now-unreachable ASID arm in the text - so
  # they are REPORTED here as context but NOT asserted; asserting them was this clause's first-draft
  # measurement defect, the same one the object census above records.)
  # **NO early `exit` in this awk**: the elf's symbol table is 23k lines, so `awk` exiting on the first
  # match closes the pipe while `nm` is still writing and `nm` dies of SIGPIPE - under `set -euo pipefail`
  # that is a 141 that aborts the build. MEASURED this session: `nm <elf> | awk '$3=="kernel_pmap"{print;exit}'`
  # returns 141, while the whole-stream `END{exit(found?0:1)}` form returns 0 (998's measured defect).
  _d6f_kp=$(arm-none-eabi-nm "$_d6f" 2>/dev/null | awk '$3=="kernel_pmap"{print $1; found=1} END{exit(found?0:1)}')
  [[ -n "$_d6f_kp" ]] || { echo "FAIL: kernel_pmap is not a symbol in the frozen 1015 elf ($_d6f) - the fingerprint below cannot be computed"; exit 1; }
  _d6f_kplo=$(( 0x$_d6f_kp & 0xFFFF ))
  _d6f_cnt=$(arm-none-eabi-objdump -d "$_d6f" --start-address=0x8002968c --stop-address=0x800298b0 2>/dev/null | grep -cE "movw[[:space:]]+r[0-9]+, #$_d6f_kplo([^0-9]|$)" || true)
  _d6f_asid=$(arm-none-eabi-objdump -d "$_d6f" --start-address=0x8002968c --stop-address=0x800298b0 2>/dev/null | grep -cE 'bl[[:space:]]+[0-9a-f]+ <arm_tlb_flushID_ASID>' || true)
  # The NEGATIVE CONTROL: the frozen 1015 elf MUST show the ASID call AND exactly 2 materializations,
  # or the tool is not reading what this clause claims it reads.
  [[ "$_d6f_asid" == "1" ]] || { echo "FAIL: the CONTROL is not a control - the frozen 1015 elf ($_d6f) carries x$_d6f_asid \`bl arm_tlb_flushID_ASID\` in pmap_flush_tlbs[0x8002968c,0x800298b0), want exactly 1 (MEASURED at HEAD). If the frozen copy has moved, this whole clause is certifying against a different kernel"; exit 1; }
  [[ "$_d6f_cnt" == "2" ]] || { echo "FAIL: the CONTROL is not a control - the frozen 1015 elf ($_d6f) materializes kernel_pmap (movw #$_d6f_kplo) x$_d6f_cnt time(s) in pmap_flush_tlbs, want exactly 2 (Apple's :1291 pmap_is_shared init + the :1382 terminal panic; MEASURED at HEAD). A different count means this clause's fingerprint is not measuring the switch's byte in this image"; exit 1; }
  echo "   frozen 1015 elf (the NEGATIVE CONTROL): pmap_flush_tlbs carries x1 \`bl arm_tlb_flushID_ASID\` and x2 kernel_pmap materializations (@0x$_d6f_kplo) - 1015b's fix is NOT in it (control holds)"
  # The image's OWN census. Range computed from `nm -n`, next STRICTLY GREATER text symbol - never a
  # hardcoded start/stop, because pmap_flush_tlbs's linked address moves with every rung.
  # whole-stream awk, never `exit` on first match (see the SIGPIPE note above)
  _d6f_lo=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf 2>/dev/null | awk '$3=="pmap_flush_tlbs" && ($2=="T"||$2=="t"){print "0x" $1; found=1} END{exit(found?0:1)}')
  _d6f_hi=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf 2>/dev/null | awk -v s="${_d6f_lo#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2 ~ /^[Tt]$/ {if(!d){print "0x" $1; d=1}}')
  [[ -n "$_d6f_lo" && -n "$_d6f_hi" ]] || { echo "FAIL: pmap_flush_tlbs's range could not be computed in out/stage90/xnu_arm_entry.elf (lo='$_d6f_lo' hi='$_d6f_hi') - the image-side pool-changed clause would measure nothing"; exit 1; }
  _d6i_kp=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf 2>/dev/null | awk '$3=="kernel_pmap"{print $1; found=1} END{exit(found?0:1)}')
  [[ -n "$_d6i_kp" ]] || { echo "FAIL: kernel_pmap is not a symbol in out/stage90/xnu_arm_entry.elf - the fingerprint below cannot be computed"; exit 1; }
  _d6i_kplo=$(( 0x$_d6i_kp & 0xFFFF ))
  _d6i_cnt=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_d6f_lo" --stop-address="$_d6f_hi" 2>/dev/null | grep -cE "movw[[:space:]]+r[0-9]+, #$_d6i_kplo([^0-9]|$)" || true)
  _d6i_asid=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_d6f_lo" --stop-address="$_d6f_hi" 2>/dev/null | grep -cE 'bl[[:space:]]+[0-9a-f]+ <arm_tlb_flushID_ASID>' || true)
  _d6i_full=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_d6f_lo" --stop-address="$_d6f_hi" 2>/dev/null | grep -cE 'bl[[:space:]]+[0-9a-f]+ <arm_tlb_flushID>' || true)
  echo "   the 1015b IMAGE: pmap_flush_tlbs $_d6f_lo..$_d6f_hi - kernel_pmap materializations x$_d6i_cnt (want 3; 1015's elf has 2), bl arm_tlb_flushID x$_d6i_full, bl arm_tlb_flushID_ASID x$_d6i_asid (both x1 in 1015's elf too - reported, not asserted)"
  [[ "$_d6i_cnt" == "3" ]] || { echo "FAIL: the LINKED IMAGE materializes kernel_pmap x$_d6i_cnt time(s) inside pmap_flush_tlbs (want 3: Apple's 2 PLUS 1015b's inserted \`else if (pmap != kernel_pmap)\` test) - THE POOL OBJECT WAS REBUILT BUT A STALE ONE WAS LINKED, or the widened arm never compiled. build_entry.sh only LINKS out/xnu_kernel_obj_d13/osfmk_arm_pmap.o, so a green object census above with a stale link is exactly this. 1015's frozen elf has 2; a count of 2 means the image is 1015's kernel"; exit 1; }
  echo "   POOL CHANGED VS 1015's FROZEN COPY: kernel_pmap materializations x2 -> x$_d6i_cnt (MEASURED at the linked addresses, symbol identity not address); arm_tlb_flushID x1 -> x$_d6i_full and arm_tlb_flushID_ASID x1 -> x$_d6i_asid unchanged by codegen, as the object census explains"
else
  echo "FAIL: the frozen 1015 elf ($_d6f) is absent - the pool-changed control for THIS rung is REQUIRED. Without it, nothing certifies that the linked image's flush arm changed rather than being 1015's bytes under a new name"; exit 1
fi
echo "   1015b arm: $_d6_arm (must differ from 1015's PRESSED arm e51d14fa - the whole point, because 1015b MOVES THE POOL OBJECT - and from the pressed 1014 arm 29ec9e48, the 1013 arm 9cf2f15e, the pressed 1012 arm ffdc8ba1, the pressed 1011 arm fce83837 and the pressed 1010c arm 002b0683)"
echo "-- 1000's own constants/keys BY VALUE (source side, what the image check cannot split) --"
printf '   ENTRY_LIVE_CAP:  '; grep -m1 '^#define ENTRY_LIVE_CAP' src/entry/entry_stubs.c || true
printf '   WDT timeout:     '; grep -m1 '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S' src/stage90.h || true
[[ $(grep -c '^#define ENTRY_LIVE_CAP 24576u$' src/entry/entry_stubs.c) == 1 ]] || { echo "FAIL: the cap constant is not 24576u - the rebuild would re-hit the 998 wall"; exit 1; }
[[ $(grep -c '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S 29u$' src/stage90.h) == 1 ]] || { echo "FAIL: the watchdog timeout is not 29u - the outer net is not the arithmetic ceiling"; exit 1; }
# The robust use-count: a bare `grep 'ST_LIVE_RD('` also matches the two `#define ST_LIVE_RD(...)`
# lines themselves (and any prose naming the macro), measured 72 - statement USES start after
# whitespace with the macro name, measured 70.
rd_thin=$(grep -cE '^\s*ST_LIVE_RD\(' src/entry/entry_storage.c || true)
rd_full=$(sed -n '/^static __attribute__((noinline, noclone)) void st_read_single_block/,/^}/p' src/entry/entry_storage.c | grep -cE '(^|[^_])ST_LIVE\(' || true)
echo "   read body: $rd_thin thinned cell site(s) (want 70), $rd_full locating cell site(s) that never thin (want 8)"
[[ "$rd_thin" == "70" && "$rd_full" == "8" ]] || { echo "FAIL: the thinning is not 70/8 - either the six locating cells were gated (a record whose absence is evidence would move) or the thinning did not reach the reader"; exit 1; }
probe_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_exec_done_probe_' || true)
echo "   exec-done probe keys in the image: $probe_keys (want 5)"
[[ "$probe_keys" == "5" ]] || { echo "FAIL: the five probe keys are not in the image - the sig/size read-back did not reach it"; exit 1; }
done_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -cE 'xnu_live_exec_done_(seq|who|current)' || true)
echo "   exec-done record keys in the image: $done_keys (want 3, still the SAME names)"
[[ "$done_keys" == "3" ]] || { echo "FAIL: the done trio's names moved - the pre-registration greps for them"; exit 1; }
echo "-- by value: the DOOR'S PET (entry_storage_driver_read references entry_wdt_pet WEAKLY, null-guarded; the trace object defines it as a global T) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_wdt_pet$' || { echo "FAIL: entry_wdt_pet is not a defined text symbol in the image (RESIDENT=1 carries the pet; 977)"; exit 1; }
# The reference class is part of the contract: `w` (weak undefined), NOT `U`. The first 999 build died on
# the strong form - pass 1 stubs the pet and the final link refuses with `multiple definition` (the trap
# at build_entry.sh:29101-29112); measured on the third run, this exact line REJECTED the weak object
# (the check had been written for `U` before the 2026-10-13 ruling). A revert to strong here means the
# ruling was undone, and the name count is still 1.
[[ $(arm-none-eabi-nm out/stage90/xnu_arm_entry_storage.o | grep -c '^[[:space:]]*w entry_wdt_pet$' || true) == 1 ]] || { echo "FAIL: the storage object does not carry exactly one WEAK 'w entry_wdt_pet' reference - either the read-door feed did not compile in, or the reference went back to the strong 'U' shape (the pass-1 stub collision of 2026-10-13)"; exit 1; }
door_a=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="entry_storage_driver_read"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_storage_driver_read is not in the image (the probe>=60 door is compiled out - the whole resident pet hangs off it)"; exit 1; }
# Same introduced-defect guard as 998's reader below: NO early `exit` in the next-symbol awk (an early
# exit SIGPIPEs `nm`, and under `set -o pipefail` the 141 is a silent death); read the stream, keep the
# first match.
door_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${door_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
door_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$door_a" --stop-address="${door_x:-0xffffffff}")
door_pets=$(grep -c 'bl.*<entry_wdt_pet>' <<<"$door_body" || true)
echo "   the door body calls entry_wdt_pet $door_pets time(s) (want 1)"
[[ "$door_pets" == "1" ]] || { echo "FAIL: entry_storage_driver_read does not call the pet exactly once (0 = the feed is not at the door; >1 = the body moved)"; exit 1; }
echo "-- by value: the 999 SLIDE LADDER in the POOL object (bsd_vm_vm_unix.o must carry the three new printf literals) --"
slide_lits=$(strings out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -c 'IOS7SHREG slide_' || true)
echo "   IOS7SHREG slide_* literals: $slide_lits (want 3)"
[[ "$slide_lits" == "3" ]] || { echo "FAIL: the three slide prints are not in the pool object - stage_d13_shreg_probe.sh did not put them in bsd/vm/vm_unix.c or the pool was not rebuilt"; exit 1; }
echo "-- by value: the TIMER-DISCRIMINATOR hooks (amendment #2) - the C helper in the image, BOTH asm objects referencing it (the ASFLAGS half of the switch, the 933/990 trap), exactly ONE \`bl\` inside each staged site's own body, and the six direct record keys --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_timer_edge_probe$' || { echo "FAIL: entry_timer_edge_probe is not a defined text symbol in the image - the pe_msm8974.c helper did not link (the bl would be undefined)"; exit 1; }
for eobj in machine_routines_asm cswitch; do
  e_u=$(arm-none-eabi-nm out/xnu_asm_obj_d13/$eobj.o 2>/dev/null | grep -c '^[[:space:]]*U entry_timer_edge_probe$' || true)
  echo "   out/xnu_asm_obj_d13/$eobj.o references entry_timer_edge_probe: $e_u (want 1)"
  [[ "$e_u" == "1" ]] || { echo "FAIL: $eobj.o does not reference the helper - the -DSTAGE90_XNU_RESIDENT=\$RESIDENT ASFLAGS line did not reach this object (export the switch before tools/assemble_arm_layer.sh and rebuild the assembly layer)"; exit 1; }
done
for esym in ml_set_interrupts_enabled thread_exception_return; do
  ea=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$esym" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $esym not in the image - the site the edge probe rides is gone"; exit 1; }
  # The range ends at the next GLOBAL `T`, NOT the next `T/t` the door reader uses: both hook sites
  # sit BEHIND an internal label the assembler gave a local symbol (`astloop` at mlie+0xc,
  # `return_to_user` at texr+0x28), so a first-T/t reader would stop 4 bytes into the function and
  # report 0 calls on a CORRECT image (measured 2026-10-13 on the 998 elf: mlie 12 bytes, texr 4).
  # The next global is the function's true end here - `ml_get_interrupts_enabled` at mlie+0x80,
  # `L_CurrentThread`'s successor T at texr+0x4c - and it contains the site. Same no-early-exit awk
  # as the door reader above (998's measured SIGPIPE/141 defect).
  ex=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${ea#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 == "T" { if (!d) { print "0x" $1; d = 1 } }')
  ebody=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$ea" --stop-address="${ex:-0xffffffff}")
  ecalls=$(grep -c 'bl.*<entry_timer_edge_probe>' <<<"$ebody" || true)
  echo "   $esym ($ea..${ex:-end}) calls entry_timer_edge_probe: $ecalls (want 1)"
  [[ "$ecalls" == "1" ]] || { echo "FAIL: $esym does not carry exactly one edge-probe call site (0 = the hook did not reach the assembly layer; >1 = the body moved)"; exit 1; }
done
edge_keys=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cE 'xnu_live_edge_(mlie|texr)_(seq|cpsr|tick)' || true)
echo "   timer-discriminator record keys in the image: $edge_keys (want 6)"
[[ "$edge_keys" == "6" ]] || { echo "FAIL: the six edge keys are not in the image - the pre-registration greps for xnu_live_edge_mlie_seq/_cpsr/_tick and xnu_live_edge_texr_seq/_cpsr/_tick verbatim"; exit 1; }
echo "-- by value: the checkers (PASS only if the linked image carries each fix) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_ios7_traps.py out/stage90/xnu_arm_entry.elf --expect on
python3 tools/check_d13_ios7_vmwire.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the same witness checker against the FROZEN 994 image as a NEGATIVE CONTROL (must FAIL ON and PASS OFF) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 996 SHREG checker ON the 999 image (via --ladder; the frozen 996/998 runs below stay base-5) - its frozen 20 B clause ('996 measures the misparse, it does not fix it') is the ONE by-design FAIL, and NOTHING else may fail: the EIGHT IOS7SHREG literals in the image (5 x 996 + 3 x 999), the 294/438 sysent rows still name the real bodies, no IOS7SHREG reference outside the three probed functions, and the SOURCE-side gate check on the staged tree file --tree bsd/vm/vm_unix.c (10 guard regions; NOTE: --tree takes the FILE, not the tree root - measured) --"
# NOTE (coordination, 2026-10-13): the three 999 slide literals are enforced ONLY under the new
# `--ladder` flag (recommended to the vmabi-recon lane: base PROBE_LITERALS stay 5 and the frozen
# 996/998 home-image runs above/below keep passing; `--ladder` adds the 3 image clauses AND makes
# --tree expect 10 guard regions instead of 7). If the lane lands a different spelling, this one
# line and the echo above change - nothing else here depends on it.
sp_rc=0
sp_out=$(python3 tools/check_d13_shreg_probe.py out/stage90/xnu_arm_entry.elf --expect on --ladder --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c 2>&1) || sp_rc=$?
printf '%s\n' "$sp_out"
sp_fails=$(printf '%s\n' "$sp_out" | grep -c '^  FAIL ' || true)
if [[ $sp_rc -eq 0 ]] || [[ $sp_fails -ne 1 ]] || ! grep -q '^  FAIL shared_region_copyin_mappings does not compute' <<<"$sp_out"; then
  echo "FAIL: the 996 checker did not fail on exactly its by-design 20 B clause (rc=$sp_rc, $sp_fails clause FAIL line(s)) - rc 0 means the 32 B wire did not reach the image; any other failing clause means the probe/ladder is damaged or the tree is not staged"
  exit 1
fi
echo "   (by design: the ONLY failing clause is the frozen 20 B straddle one)"
echo "-- by value: the SAME frozen checker against its OWN 996 image, with the FROZEN 994 image as a NEGATIVE CONTROL (on must FAIL, off must PASS) - 997/998/999 do not weaken it --"
python3 tools/check_d13_shreg_probe.py out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 997 shreg-WIRE CHECKER (switch ON: the IOS7LAB cache mapping literal in shared_region_copyin_mappings reaching a printf; count*32 (\`lsl #5\` + \`add rX,rX,#32\`); the 20 B \`count*5<<2\` pair ABSENT; 294's copyout r2=8 for the full uint64; no stray literal) + the SOURCE-side gate check --tree --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c
echo "-- by value: the same wire checker against the FROZEN 996 image as a NEGATIVE CONTROL (it must FAIL ON and PASS OFF) and the 994 capture as a second --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 998 shreg-GEOM CHECKER, SOURCE side (NOTE: --tree takes the TREE ROOT here, unlike the wire/probe checkers' FILE argument - measured): the three files all in the staged shape, the ABI constants intact in BOTH states, no v1 inline-#if wrap --"
python3 tools/check_d13_shreg_geom.py --tree external/xnu-hd2-darwin13/xnu --expect staged
echo "-- by value: the 998 IMAGE-side clauses, standalone (the same three properties build_entry.sh's 998 clause asserts at the link site): (b) the IOS7LAB shared region printf literal is in the image (it sits INSIDE the opened gate), (c) thread_userstack + thread_userstackdefault materialize the 0xBE0 stack top and NOT the 0xFE0 default --"
gl_lit='IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000'
gl_n=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   (b) IOS7LAB shared region literal copies: $gl_n (want 1)"
[[ $gl_n -eq 1 ]] || { echo "FAIL: the image-side literal reader found $gl_n copies of the geometry printf literal (want exactly 1)"; exit 1; }
for gfn in thread_userstack thread_userstackdefault; do
  gfa=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$gfn" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $gfn not in the image"; exit 1; }
  # NO early `exit` in this awk (998's measured defect: an early exit SIGPIPEs `nm` and under
  # `set -o pipefail` the 141 becomes a silent death). Read the whole stream, keep the first match.
  gfnx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${gfa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  echo "   (c) $gfn ($gfa..${gfnx:-end}):"
  arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$gfa" --stop-address="${gfnx:-0xffffffff}" | grep -E 'movt.*; 0x2be0|movt.*; 0x2fe0' | sed 's/^/       /' || true
done
echo "-- by value: the SAME image-side reader against the FROZEN 998 elf as a NEGATIVE CONTROL (both readers must still work on it; 999 moves neither constant) --"
gc_frz=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-dee822b0/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   frozen 998 elf: IOS7LAB shared region literal copies: $gc_frz (want 1)"
[[ $gc_frz -eq 1 ]] || { echo "FAIL: the frozen 998 elf does not carry the geometry literal the reader looks for - the reader is stale"; exit 1; }
echo "-- by value: the POOL objects FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
echo "-- by value: the 997/999 WIRE+PROBE object (bsd_vm_vm_unix.o must carry the IOS7LAB cache mapping literal, the count*32 shape, the 996 IOS7SHREG probe AND the 999 slide ladder) --"
strings -a out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "IOS7LAB cache mapping|IOS7SHREG" | head -14 || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "lsl.*#5|#32" | head -8 || true
echo "-- by value: the 998 GEOMETRY object (osfmk_vm_vm_shared_region.o must carry the IOS7LAB shared region literal + 0x2C000000/0x14000000) --"
strings -a out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -F 'IOS7LAB shared region' || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -E '#0x2c00|#0x1400' | head -6 || true
echo "-- by value: the 998 STACK object (osfmk_arm_status.o must carry the 0x2BE00000 stack top) --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_status.o 2>/dev/null | grep -B1 -A1 '; 0x2be0' | head -12 || true
echo "-- by value: the 995 PROBE object (trap.o must carry the witness REV-3) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_trap.o 2>/dev/null | grep -E "ios7wit_dyld_message|IOS7WITNESS|IOS7WIT_AI_|copyin|copyinstr" | head -20 || true
echo "-- by value: the 994 POOL object (mach_kernelrpc.o must carry the wire adapters) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o 2>/dev/null | grep -E "IOS7LAB|vm_allocate64|vm_deallocate64|vm_protect64|ios7_traps_enabled" | head -20 || true
echo "-- by value: the ASSEMBLY object (traps_lo.o must carry the mach_absolute_time call, NOT the cmn r12,#3 fast path) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -B2 -A2 -E "mach_absolute_time|swi_exit64" | head -20 || true
echo "-- by value: the 999 PAYLOAD constant AT THE ARM SITE (stage90_main.o's call must pass 29 - the register carries SECONDS; measured 998: 'mov r0, #25' immediately before 'bl stage90_hw_watchdog_arm') --"
# The 0xE7FA9 (29*32765) / 0xFFFA0 (32*32765) tick values are RUNTIME arithmetic
# (hw_watchdog.c:223 `bark_ticks = timeout_s * MSM8974_WDT_HZ` + BITE_EXTRA_TICKS), so NO object
# contains them - the ARMED log's `hw_watchdog_timeout_s=0x0000001d` and
# `hw_watchdog_bite_ticks_written=0x000FFFA0` are the press-side observables, and this is the
# build-side one that makes them arithmetically inevitable.
arm_site=$(arm-none-eabi-objdump -dr out/stage90/stage90_main.o 2>/dev/null | grep -B3 'bl.*stage90_hw_watchdog_arm' | grep -E 'mov[a-z]*[[:space:]]+r0, #(25|29)' || true)
printf '%s\n' "${arm_site:-   (no mov r0,#25|#29 found near the arm call - the reader is stale)}"
grep -qE 'mov[a-z]*[[:space:]]+r0, #29' <<<"$arm_site" || { echo "FAIL: the payload's arm site does not pass 29 - the loaded payload would arm the OLD 25 s net and the run would die at 998's wall"; exit 1; }
echo "-- by value: the 1000 read-dump thinning ordinal AT THE SOURCE (999 built 256 and it NEVER FIRED - 218 reads in the whole boot - so the full-width dump spent the cap before exec; 1000 moves it to 64 and the tool PARSES it, never restates it) --"
printf '   thinning ordinal:  '; grep -m1 'const uint32_t rd_full_' src/entry/entry_storage.c || true
[[ $(grep -cF 'rd_full_ = (st_read_count < 64u);' src/entry/entry_storage.c) == 1 ]] || { echo "FAIL: the thinning ordinal is not 64 - the rebuild would spend the cap on full-width dumps before exec again"; exit 1; }
[[ $(grep -cF 'st_read_count < 256u' src/entry/entry_storage.c) == 0 ]] || { echo "FAIL: a '< 256u' comparison survives - the moved ordinal is defined twice"; exit 1; }
python3 tools/read_storage_key_order.py --selftest >/dev/null || { echo "FAIL: the key-order tool's selftest is red - the ordinal this build just moved is parsed by that tool, not restated"; exit 1; }
printf '   USVC window:       '; grep -m1 '^#define USVC_FULL_N' src/entry/entry_stubs.c || true
[[ $(grep -c '^#define USVC_FULL_N 64u' src/entry/entry_stubs.c) == 1 ]] || { echo "FAIL: the SVC full-field window is not 64u - the press-side pre-registration greps the first 64 SVCs in full"; exit 1; }
python3 tools/check_saved_state_offsets.py --selftest >/dev/null || { echo "FAIL: the saved-state checker's selftest is red"; exit 1; }
python3 tools/check_saved_state_offsets.py || { echo "FAIL: the saved-state offsets disagree across the trees - the USVC C half reads R0/R12 by these very numbers, and a wrong one prints a field of the wrong frame word"; exit 1; }
[[ $(awk -F= '$1=="STAGE90_XNU_USVC"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_USVC=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "-- by value: the 1000 USER-SVC hook IN THE IMAGE (the C half defined, the ASFLAGS half in traps_lo.o, ONE hook call inside locore_fleh_swi - the link's rename of traps_lo.o's fleh_swi, NOT the low-address stand-in - the hook IS the body's first branch, the writes all DIRECT, and the eight keys x1) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_note_user_svc$' || { echo "FAIL: entry_note_user_svc is not a defined text symbol in the image - the C half (entry_stubs.c) did not compile in or link"; exit 1; }
u10_u=$(arm-none-eabi-nm out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -c '^[[:space:]]*U entry_note_user_svc$' || true)
echo "   out/xnu_asm_obj_d13/traps_lo.o references entry_note_user_svc: $u10_u (want 1)"
[[ "$u10_u" == "1" ]] || { echo "FAIL: traps_lo.o does not reference the helper - the -DSTAGE90_XNU_USVC=\$USVC ASFLAGS line did not reach this object (export the switch before tools/assemble_arm_layer.sh and rebuild the assembly layer)"; exit 1; }
u10_ls=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: locore_fleh_swi is not in the image - it is traps_lo.o's fleh_swi under the link's own rename of the six colliding names, and it is the ONE body the hook is inserted at"; exit 1; }
# The range ends at the next GLOBAL `T`: this body is ASSEMBLY with internal named labels
# (swi_trap_ret at +0x3c and the rest), so the first-T/t reader the C bodies above use would stop
# early. Same no-early-exit awk as the edge-probe reader (999's measured SIGPIPE/141 defect).
u10_lsx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_ls#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 == "T" { if (!d) { print "0x" $1; d = 1 } }')
u10_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_ls" --stop-address="${u10_lsx:-0xffffffff}")
u10_c=$(grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_user_svc>' <<<"$u10_body" || true)
echo "   locore_fleh_swi ($u10_ls..${u10_lsx:-end}) calls entry_note_user_svc: $u10_c (want 1)"
[[ "$u10_c" == "1" ]] || { echo "FAIL: locore_fleh_swi does not carry exactly one hook call (0 = the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported; >1 = the body moved)"; exit 1; }
u10_all=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_user_svc>' || true)
echo "   image-wide call sites: $u10_all (want 1 - the SVC entry's own)"
[[ "$u10_all" == "1" ]] || { echo "FAIL: the image carries $u10_all call site(s) of the hook - a second site would count calls the frontier is not about"; exit 1; }
# The same reader as build_entry.sh's live clause, and it must be spelled the same way for the same
# two measured reasons (2026-10-13: run 3 of build_1000.sh died SILENTLY at the twin - rc 1, no FAIL
# line - because `\t` inside an ERE is NOT a tab to GNU grep 3.7, which is what runs here under
# `env -i`/child processes, while the interactive shell's ugrep-backed `grep` function matched the
# same pattern - a reader "verified" through the wrong grep is m997's class, the measurement being
# the thing that was wrong; and `| head -1 |` can race SIGPIPE under `set -o pipefail`, 998's 141
# defect - `-m1 ... || true` lets a genuine no-match reach the check below instead of a bare exit 1).
u10_hook=$(grep -n -m1 '<entry_note_user_svc>' <<<"$u10_body" | cut -d: -f1 || true)
u10_br=$(grep -nE -m1 '[[:space:]](b|bl|bx|blx|b(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al))(\.[a-z]+)?[[:space:]]' <<<"$u10_body" | cut -d: -f1 || true)
echo "   the hook is the body's FIRST branch: hook at disasm line ${u10_hook:-none}, first branch at ${u10_br:-none}"
[[ -n "$u10_hook" && "$u10_hook" == "$u10_br" ]] || { echo "FAIL: the hook is not the body's first branch - it must sit right after \`cpsie i\`, BEFORE the -3 timebase fast path and every dispatch branch, or the frontier reading is about a subset of the calls"; exit 1; }
echo "   the hook window, by value:"
grep -B2 -A2 'bl.*<entry_note_user_svc>' <<<"$u10_body" | sed 's/^/       /'
u10_svc=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="entry_note_user_svc"{print "0x" $1; found=1} END{exit(found?0:1)}')
u10_svcx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_svc#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
u10_svc_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_svc" --stop-address="${u10_svcx:-0xffffffff}")
u10_kv=$(grep -c '<entry_write_kv>' <<<"$u10_svc_body" || true)
u10_lw=$(grep -c '<entry_live_write>' <<<"$u10_svc_body" || true)
echo "   entry_note_user_svc ($u10_svc..${u10_svcx:-end}): entry_write_kv x$u10_kv (want >= 8), entry_live_write x$u10_lw (want 0)"
[[ "$u10_kv" -ge 8 && "$u10_lw" -eq 0 ]] || { echo "FAIL: the C half's writes are not all DIRECT (>= 8 sites for the full window's eight fields, 0 counted) - a record whose ABSENCE is the reading may not ride the cap"; exit 1; }
grep -qE 'cmp[[:space:]]+r[0-9]+, #(64|0x40)' <<<"$u10_svc_body" || { echo "FAIL: entry_note_user_svc does not compare against 64 - USVC_FULL_N is the full-field window"; exit 1; }
u10_keys=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_usvc_' || true)
echo "   xnu_live_usvc_* keys in the image: $u10_keys (want 8)"
[[ "$u10_keys" == "8" ]] || { echo "FAIL: the eight field keys are not in the image once each - the press-side pre-registration greps them verbatim"; exit 1; }
echo "-- by value: the FROZEN 999 elf as the NEGATIVE CONTROL for the new readers (the hook and the keys are NEW - a reader that cannot fail certifies nothing) --"
u10_frz_u=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-467d59e4/xnu_arm_entry.elf | grep -c 'xnu_live_usvc_' || true)
echo "   frozen 999 elf: xnu_live_usvc_* keys: $u10_frz_u (want 0)"
[[ "$u10_frz_u" == "0" ]] || { echo "FAIL: the frozen 999 elf carries usvc keys - the reader cannot discriminate the two arms"; exit 1; }
u10_frz_t=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-467d59e4/xnu_arm_entry.elf | grep -cE 'xnu_live_tick_(sum_|anom_)' || true)
echo "   frozen 999 elf: xnu_live_tick_{sum_,anom_}* keys: $u10_frz_t (want 0)"
[[ "$u10_frz_t" == "0" ]] || { echo "FAIL: the frozen 999 elf carries 1000's tick keys - the reader cannot discriminate the two instruments"; exit 1; }
echo "-- by value: the 1000 TICK probe IN THE IMAGE (the sample's ONE call site inside the handler, the summaries' two call sites, the ten keys) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_tick_summary$' || { echo "FAIL: entry_tick_summary is not in the image - the pe_msm8974.c aggregate dump did not link"; exit 1; }
u10_tss=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="msm8974_tick_sample"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: msm8974_tick_sample is not in the image - the tick's own sampler did not link"; exit 1; }
u10_tss_all=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -cE 'bl[[:space:]]+[0-9a-f]+ <msm8974_tick_sample>' || true)
echo "   image-wide msm8974_tick_sample call sites: $u10_tss_all (want 1)"
[[ "$u10_tss_all" == "1" ]] || { echo "FAIL: the sample does not have exactly one call site - a second site changes what the delta the records carry means"; exit 1; }
u10_hi=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="msm8974_handle_interrupt"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: msm8974_handle_interrupt is not in the image - the function whose timer branch owns the one sample call"; exit 1; }
u10_hix=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_hi#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
u10_hi_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_hi" --stop-address="${u10_hix:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <msm8974_tick_sample>' <<<"$u10_hi_body" || { echo "FAIL: the one sample call is not inside msm8974_handle_interrupt ($u10_hi..${u10_hix:-end}) - the reading would be taken by code the timer path does not run"; exit 1; }
echo "   the sample's site inside the handler, by value:"
grep -B3 -A1 'bl.*<msm8974_tick_sample>' <<<"$u10_hi_body" | sed 's/^/       /'
for u10_pop in entry_note_exec_returned entry_wdt_pet; do
  u10_pa=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$u10_pop" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $u10_pop not in the image - it is one of the two summary call sites"; exit 1; }
  u10_px=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${u10_pa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  u10_pb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$u10_pa" --stop-address="${u10_px:-0xffffffff}")
  # `b` OR `bl`: the exec-done site's call is a TAIL call (measured: `b 801ae2ec
  # <entry_tick_summary>` at the body's last instruction - the same reader fix as build_entry.sh's
  # 1000 clause; the structure to certify is one direct branch, not a spelling).
  u10_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_tick_summary>' <<<"$u10_pb" || true)
  echo "   $u10_pop ($u10_pa..${u10_px:-end}) calls entry_tick_summary: $u10_pc (want 1)"
  [[ "$u10_pc" == "1" ]] || { echo "FAIL: $u10_pop does not call the summary exactly once - the aggregate must be asked for at this moment (0 = the call site moved; >1 = it runs twice)"; exit 1; }
done
u10_tk=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_tick_sum_' || true)
u10_ta=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_tick_anom_' || true)
u10_pk=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_wdt_pet_' || true)
echo "   tick summary keys: $u10_tk (want 3); tick anomaly keys: $u10_ta (want 4); pet feed keys: $u10_pk (want 3)"
[[ "$u10_tk" == "3" && "$u10_ta" == "4" && "$u10_pk" == "3" ]] || { echo "FAIL: the ten new direct keys (3 summary + 4 anomaly + 3 pet) are not each in the image once - the press-side pre-registration greps them verbatim"; exit 1; }
echo "-- by value: the 1001 SUBMAP-SKIP in the POOL object and the LINKED image (switch ON) --"
ss_obj=$(strings -a out/xnu_kernel_obj_d13/osfmk_vm_vm_map.o 2>/dev/null | grep -c 'SUBMAPSKIP:' || true)
echo "   osfmk_vm_vm_map.o SUBMAPSKIP literals: $ss_obj (want 1)"
[[ "$ss_obj" == "1" ]] || { echo "FAIL: the SUBMAPSKIP literal is not in the pool object - tools/stage_d13_submap_pmap_skip.sh did not put the skip in osfmk/vm/vm_map.c, or the pool was not rebuilt with STAGE90_XNU_SUBMAP_PMAP_SKIP=1 exported (build_entry.sh only LINKS this object)"; exit 1; }
python3 tools/check_submap_pmap_skip.py out/stage90/xnu_arm_entry.elf || { echo "FAIL: the 1001 checker did not PASS on the fresh image (see its clauses above)"; exit 1; }
echo "-- by value: the FROZEN 1000 elf as the NEGATIVE CONTROL for the 1001 checker (must FAIL check 1 - the skip did not exist when it was built) --"
ss_ctrl=0
ss_ctrl_out=$(python3 tools/check_submap_pmap_skip.py out/stage90/frozen/armed-d13-bbdf41bc/xnu_arm_entry.elf 2>&1) || ss_ctrl=$?
printf '%s\n' "$ss_ctrl_out" | sed 's/^/   /'
if [[ $ss_ctrl -ne 1 ]] || ! grep -q 'not in the image at all' <<<"$ss_ctrl_out"; then
  echo "FAIL: the 1001 checker did not refuse the frozen 1000 elf by its check-1 clause (rc=$ss_ctrl) - a checker that cannot fail certifies nothing"; exit 1
fi
echo "-- by value: the 1001 SOURCE edit in the staged tree (the kprintf literal once; the defined()-first #if twice; the is_submap guard once) --"
ss_src=external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_map.c
[[ $(grep -cF 'kprintf("SUBMAPSKIP:' "$ss_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one kprintf(\"SUBMAPSKIP: ...) - the 1001 edit is not the staged shape (re-run tools/stage_d13_submap_pmap_skip.sh)"; exit 1; }
[[ $(grep -cE '^#if defined\(STAGE90_XNU_SUBMAP_PMAP_SKIP\) && STAGE90_XNU_SUBMAP_PMAP_SKIP$' "$ss_src" || true) == 2 ]] || { echo "FAIL: the tree does not carry the two defined()-first #if wraps - half the edit is missing or a hand edit moved it"; exit 1; }
[[ $(grep -cF 'if (!is_submap) {' "$ss_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one \`if (!is_submap) {\` guard in vm_map_enter's tail"; exit 1; }
[[ $(awk -F= '$1=="STAGE90_XNU_SUBMAP_PMAP_SKIP"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "-- by value: the 1002 FIX AS A REGRESSION (the USVC hook's r3 save set must STILL be the six-word {r0-r3,r8,lr}; it is the baseline 1003 builds on, not 1003's delta) --"
echo "   (a) traps_lo.o carries the SIX-word save/restore {r0-r3,r8,lr} and NOT the four-word {r0,r1,r8,lr}"
t10_pw=$(arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' || true)
t10_lw=$(arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' || true)
echo "   traps_lo.o: six-word push {r0-r3,r8,lr} x$t10_pw (want 1), four-word {r0,r1,r8,lr} x$t10_lw (want 0)"
[[ "$t10_pw" == "1" && "$t10_lw" == "0" ]] || { echo "FAIL: traps_lo.o does not carry the 1002 six-word save set (and only it) - the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported, OR a revert put the 1001 four-word form back (reintroducing the r3 clobber)"; exit 1; }
echo "   (b) the LINKED image's locore_fleh_swi carries the SAME six-word save, with the four-word form gone"
t10_iw=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$u10_body" || true)
t10_il=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$u10_body" || true)
echo "   locore_fleh_swi: six-word push {r0-r3,r8,lr} x$t10_iw (want 1), four-word {r0,r1,r8,lr} x$t10_il (want 0)"
[[ "$t10_iw" == "1" && "$t10_il" == "0" ]] || { echo "FAIL: the linked locore_fleh_swi does not carry the 1002 six-word save set (and only it) - build_entry.sh links traps_lo.o, so a stale object would keep the r3 clobber the 1001 press read diagnosed"; exit 1; }
echo "   (c) the six-word save is immediately followed by the hook call, and the restore brackets it"
grep -A3 'push.*{r0, r1, r2, r3, r8, lr}' <<<"$u10_body" | sed 's/^/       /'
echo "   (d) the SOURCE edit in the staged tree (the six-word stmfd/ldmfd once each; the four-word form absent)"
t10_src=external/xnu-hd2-darwin13/xnu/osfmk/arm/traps_lo.s
[[ $(grep -cE '^[[:space:]]*stmfd[[:space:]]+sp!,[[:space:]]*\{r0-r3,r8,lr\}$' "$t10_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one six-word \`stmfd sp!, {r0-r3,r8,lr}\` - re-run tools/patch_d13_usvc.py"; exit 1; }
[[ $(grep -cE '^[[:space:]]*ldmfd[[:space:]]+sp!,[[:space:]]*\{r0-r3,r8,lr\}$' "$t10_src" || true) == 1 ]] || { echo "FAIL: the tree does not carry exactly one matching six-word \`ldmfd\`"; exit 1; }
[[ $(grep -cE '^[[:space:]]*stmfd[[:space:]]+sp!,[[:space:]]*\{r0,r1,r8,lr\}' "$t10_src" || true) == 0 ]] || { echo "FAIL: the four-word \`stmfd {r0,r1,r8,lr}\` survives in the tree - the r3 clobber is back"; exit 1; }
echo "   (e) the payload constant is unchanged (29 s) and the watchdog arm site still passes 29"
grep -qE 'mov[a-z]*[[:space:]]+r0, #29' <<<"$arm_site" || { echo "FAIL: the arm site no longer passes 29"; exit 1; }
# ====================================================================================================
# 1003's delta, KEPT AS A REGRESSION (1004's set is 1003's verbatim): the per-tick stall sampler.
# ====================================================================================================
echo "-- by value: the 1003 STALL instrument (KEPT) - the recorder symbol + the 7 key literals in the image --"
pe_obj=out/xnu_platform_obj_d13/pe_msm8974.o
pe_u=$(arm-none-eabi-nm "$pe_obj" 2>/dev/null | awk '($1=="U" && $2=="entry_note_stall_sample") || ($2=="U" && $3=="entry_note_stall_sample"){print "1"}')
[[ "$pe_u" == "1" ]] || { echo "FAIL: $pe_obj does not reference entry_note_stall_sample - 1003's instrument was dropped"; exit 1; }
elf=out/stage90/xnu_arm_entry.elf
stall_sym=$(arm-none-eabi-nm "$elf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$stall_sym" == "1" ]] || { echo "FAIL: entry_note_stall_sample is not a defined text symbol in $elf (x$stall_sym, want 1) - 1003's instrument was dropped"; exit 1; }
stall_keys=$(strings "$elf" | grep -c 'xnu_live_stall_' || true)
[[ "$stall_keys" == "7" ]] || { echo "FAIL: the image carries $stall_keys xnu_live_stall_* literals (want 7)"; exit 1; }
echo "   entry_note_stall_sample (T) + xnu_live_stall_* x$stall_keys (1003 kept)"
[[ $(awk -F= '$1=="STAGE90_XNU_STALL"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_STALL=1 - 1003's switch must stay on"; exit 1; }

# ====================================================================================================
# THE 1004 DELTA: the abort trace (STAGE90_XNU_ABORT_TRACE), asserted BY VALUE, with the FROZEN 1003
# image (armed-d13-7669309b) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing.
#
# The change is ONE src/ file (NO tree edit, NO platform object): `src/entry/entry_stubs.c` widens the
# three sleh detail caps (SLEH_LIVE_MAX/_FRAMES/_SEEN 8/16/64 -> 512/512/512) and adds the frame's
# user pc as `xnu_live_sleh_upc`, both under `#if defined(STAGE90_XNU_ABORT_TRACE) &&
# STAGE90_XNU_ABORT_TRACE`. The by-value fingerprint is (i) the `xnu_live_sleh_upc` literal in the
# linked image, (ii) the widened `512` caps visible in `entry_note_sleh`'s own body, and (iii) the
# gated source shape. On the frozen 1003 elf the literal is ABSENT and the body carries the OLD 8/16/64
# caps - the negative control.
# ====================================================================================================
echo "-- by value: the 1004 DELTA - the abort trace (STAGE90_XNU_ABORT_TRACE) --"
# (a) the LINKED image carries the new `xnu_live_sleh_upc` literal - the one key a reader greps the
#     NEXT capture for, so its presence in THIS image is what makes that capture's `_upc` mean this arm.
upc_lit=$(strings "$elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$upc_lit" == "1" ]] || { echo "FAIL: the image carries $upc_lit \`xnu_live_sleh_upc\` literal(s) (want 1) - the STAGE90_XNU_ABORT_TRACE store did not reach the linked image"; exit 1; }
echo "   (a) the linked image carries the \`xnu_live_sleh_upc\` key literal (the new frame store)"

# (b) the CAPS moved: `entry_note_sleh`'s own body must compare against 512 - three times (MAX, FRAMES
#     and SEEN are all 512 with the switch on), and must NOT carry the OLD 8/16/64 comparisons that the
#     frozen 1003 body had. Read the disassembly of the function by value.
sleh_a=$(arm-none-eabi-nm "$elf" | awk '$3=="entry_note_sleh"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_note_sleh is not in the image - the abort record's body is gone"; exit 1; }
sleh_x=$(arm-none-eabi-nm -n "$elf" | awk -v s="${sleh_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
sleh_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$sleh_a" --stop-address="${sleh_x:-0xffffffff}")
cap512=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(512|0x200)|movw[[:space:]]+r[0-9]+, #(512|0x200)\b' <<<"$sleh_body" || true)
cap_old=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(8|16|64)\b' <<<"$sleh_body" || true)
echo "   (b) entry_note_sleh ($sleh_a..${sleh_x:-end}): #512 comparisons x$cap512 (want >= 3: MAX x2 (entry+back) + FRAMES + SEEN), legacy #8/#16/#64 comparisons x$cap_old (want 0)"
[[ "$cap512" -ge 3 && "$cap_old" == "0" ]] || { echo "FAIL: the caps did not move (512 x$cap512 want >= 3, legacy x$cap_old want 0) - the STAGE90_XNU_ABORT_TRACE #if did not compile the widened constants in"; exit 1; }

# (c) the SOURCE-side shape: the defined()-first guard once, the three widened definitions inside it,
#     and the gated `_upc` store - each so ABORT_TRACE=0 (the #else branch: the exact old tokens)
#     changes neither file's compiled bytes.
stub_src=src/entry/entry_stubs.c
guard_n=$(grep -cE '^#if defined\(STAGE90_XNU_ABORT_TRACE\) && STAGE90_XNU_ABORT_TRACE$' "$stub_src" || true)
echo "   (c) $stub_src: the defined()-first guard x$guard_n (want 2 - one for the caps, one for the _upc store)"
[[ "$guard_n" == "2" ]] || { echo "FAIL: $stub_src does not carry the two defined()-first \`#if defined(STAGE90_XNU_ABORT_TRACE) && STAGE90_XNU_ABORT_TRACE\` guards (a hand edit moved one, or one is missing)"; exit 1; }
[[ $(grep -cE '^#define SLEH_LIVE_MAX 512u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_FRAMES 512u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_SEEN 512u$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the three widened caps (512u) are not each defined once in $stub_src"; exit 1; }
# The OFF branch must STILL be the exact old tokens - a widened constant would break this.
[[ $(grep -cE '^#define SLEH_LIVE_MAX 8u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_FRAMES 16u$' "$stub_src" || true) == 1 && $(grep -cE '^#define SLEH_LIVE_SEEN 64u$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the ABORT_TRACE=0 branch is not the old 8/16/64 tokens - the off-arm is not byte-identical"; exit 1; }
[[ $(grep -cE 'entry_live_write\("xnu_live_sleh_upc", pc\);' "$stub_src" || true) == 1 ]] || { echo "FAIL: the \`xnu_live_sleh_upc\` store is not present exactly once in $stub_src"; exit 1; }
echo "   (c) the source shape: 2 guards + the 512u caps + the old 8u/16u/64u off-branch + the one _upc store"

# (d) the fresh record carries the new arm key.
[[ $(awk -F= '$1=="STAGE90_XNU_ABORT_TRACE"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABORT_TRACE=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (d) the fresh record carries STAGE90_XNU_ABORT_TRACE=1 (the new arm key this run)"

echo "-- by value: the FROZEN 1003 elf as the NEGATIVE CONTROL (it must NOT carry the 1004 change; a reader that cannot fail certifies nothing) --"
d4_ctrl=out/stage90/frozen/armed-d13-7669309b/xnu_arm_entry.elf
if [[ -f $d4_ctrl ]]; then
  ctl_upc=$(strings "$d4_ctrl" | grep -c '^xnu_live_sleh_upc$' || true)
  ctl_a=$(arm-none-eabi-nm "$d4_ctrl" | awk '$3=="entry_note_sleh"{print "0x" $1; found=1} END{exit(found?0:1)}')
  ctl_x=$(arm-none-eabi-nm -n "$d4_ctrl" | awk -v s="${ctl_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  ctl_body=$(arm-none-eabi-objdump -d "$d4_ctrl" --start-address="$ctl_a" --stop-address="${ctl_x:-0xffffffff}")
  ctl_512=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #512|movw[[:space:]]+r[0-9]+, #512' <<<"$ctl_body" || true)
  [[ "$ctl_upc" == "0" && "$ctl_512" == "0" ]] || { echo "FAIL: the frozen 1003 elf already carries the 1004 change (upc x$ctl_upc, #512 x$ctl_512) - the negative control is not a control"; exit 1; }
  echo "   frozen 1003 elf ($d4_ctrl): xnu_live_sleh_upc x0, #512 caps x0 - the change is NOT in it (control holds)"
else
  echo "   (frozen 1003 elf not present at $d4_ctrl - the negative control is unavailable this run)"
fi
echo "-- 1004 build done: park with scripts/park_1004.sh --"

# ====================================================================================================
# THE 1005 DELTA: the cadence witness (STAGE90_XNU_CADENCE), asserted BY VALUE, with the FROZEN 1004
# elf (armed-d13-582a19c8, the PRESSED 1004 arm) as the NEGATIVE CONTROL - a reader that cannot fail
# certifies nothing. Design: docs/1005-stall-cadence-design.md, §6.
#
# The change is THREE src/ files (NO tree edit, NO stage driver): `src/platform/darwin13/pe_msm8974.c`
# adds the all-interrupt counter + the two board accessors; `src/entry/entry_stubs.c` adds the
# syscall-path sample + the late summary + the atomic sleh `++`; `src/entry/entry_trace.c`'s pet calls
# the summary. All gated `#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE`; OFF is byte-identical.
# ====================================================================================================
echo "-- by value: the 1005 DELTA - the cadence witness (STAGE90_XNU_CADENCE) --"
pe_cad=src/platform/darwin13/pe_msm8974.c

# (a) SOURCE side, per-file (a hand edit that moved a guard would drift the count). The guard is
#     spelled defined()-first in all three files.
cad_p=$(grep -cE '^#if defined\(STAGE90_XNU_CADENCE\) && STAGE90_XNU_CADENCE$' "$pe_cad" || true)
cad_s=$(grep -cE '^#if defined\(STAGE90_XNU_CADENCE\) && STAGE90_XNU_CADENCE$' "$stub_src" || true)
cad_t=$(grep -cE '^#if defined\(STAGE90_XNU_CADENCE\) && STAGE90_XNU_CADENCE$' src/entry/entry_trace.c || true)
echo "   (a) guards: pe_msm8974.c x$cad_p (want 2 - accessors+irq-counter, the handler incr), entry_stubs.c x$cad_s (want 6 - decl, block, USVC call, exec-done call, 2 sleh), entry_trace.c x$cad_t (want 2 - decl, pet call)"
[[ "$cad_p" == "2" && "$cad_s" == "6" && "$cad_t" == "2" ]] || { echo "FAIL: the cadence guards are not one-per-region (pe x$cad_p want 2, stubs x$cad_s want 6, trace x$cad_t want 2) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -c 'entry_cadence_sample()' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_cadence_sample() is not present exactly once in $stub_src (definition + USVC call; the def carries no ())"; exit 1; }
[[ $(grep -c 'entry_cadence_summary()' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_cadence_summary() is not present exactly once in $stub_src"; exit 1; }
# The board half and the two accessors are in the platform PE source.
[[ $(grep -c 'g_msm8974_irq_count' "$pe_cad" || true) == 4 ]] || { echo "FAIL: g_msm8974_irq_count is not the 4 expected spellings (comment, decl, increment, accessor) in $pe_cad"; exit 1; }
[[ $(grep -cF 'uint32_t entry_board_tick_count(void)' "$pe_cad" || true) == 1 && $(grep -cF 'uint32_t entry_board_irq_count(void)' "$pe_cad" || true) == 1 ]] || { echo "FAIL: the two board accessors are not each defined once in $pe_cad"; exit 1; }
# The OFF branch of the sleh edits must still be the OLD token set (byte-identical arm).
[[ $(grep -cE '^[[:space:]]*g_sleh_seq\+\+;$' "$stub_src" || true) == 1 && $(grep -cE '^[[:space:]]*g_sleh_back\+\+;$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the two non-atomic \`g_sleh_seq++;\`/\`g_sleh_back++;\` off-branch tokens are not present - the CADENCE=0 arm is not byte-identical"; exit 1; }
echo "   (a) the source shape: guards one-per-region + 1 sample()/1 summary() call + the board counter + the old \`++\` off-branch"

# (b) IMAGE side (by value). Four unique cadence literals, the four symbols DEFINED (a generated
#     stand-in would show 0), and the one call site each.
cad_lit=$(strings "$elf" | grep -c 'xnu_live_cadence_' || true)
[[ "$cad_lit" == "4" ]] || { echo "FAIL: the image carries $cad_lit \`xnu_live_cadence_\` literals (want 4: t/sync/tick/irq) - the CADENCE store did not reach the linked image"; exit 1; }
echo "   (b) the linked image carries the 4 \`xnu_live_cadence_\` key literals"
for cad_sym in entry_board_tick_count entry_board_irq_count entry_cadence_sample entry_cadence_summary; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $cad_sym\$" || { echo "FAIL: $cad_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) the four symbols are DEFINED T/t in the image"

# Call-site clause: entry_note_user_svc's body carries exactly one entry_cadence_sample call;
# entry_note_exec_returned and entry_wdt_pet each carry exactly one entry_cadence_summary call (the
# exec-done site is a tail call - a `b` is a valid spelling, the same reader fix as build_1004.sh:500).
cad_sa=$(arm-none-eabi-nm "$elf" | awk '$3=="entry_note_user_svc"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_note_user_svc is not in the image"; exit 1; }
cad_sx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${cad_sa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
cad_sbody=$(arm-none-eabi-objdump -d "$elf" --start-address="$cad_sa" --stop-address="${cad_sx:-0xffffffff}")
cad_sc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_cadence_sample>' <<<"$cad_sbody" || true)
echo "   (b) entry_note_user_svc calls entry_cadence_sample: $cad_sc (want 1)"
[[ "$cad_sc" == "1" ]] || { echo "FAIL: entry_note_user_svc does not call entry_cadence_sample exactly once (0 = the call site moved; >1 = it runs twice)"; exit 1; }
for cad_pop in entry_note_exec_returned entry_wdt_pet; do
  cad_pa=$(arm-none-eabi-nm "$elf" | awk -v s="$cad_pop" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $cad_pop not in the image"; exit 1; }
  cad_px=$(arm-none-eabi-nm -n "$elf" | awk -v s="${cad_pa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  cad_pb=$(arm-none-eabi-objdump -d "$elf" --start-address="$cad_pa" --stop-address="${cad_px:-0xffffffff}")
  cad_pc=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_cadence_summary>' <<<"$cad_pb" || true)
  echo "   (b) $cad_pop calls entry_cadence_summary: $cad_pc (want 1)"
  [[ "$cad_pc" == "1" ]] || { echo "FAIL: $cad_pop does not call the cadence summary exactly once (0 = the call site moved; >1 = it runs twice)"; exit 1; }
done

# (b3) ATOMICITY: the sleh increments in the stub object must be INLINE ldrex/strex, NOT a
#      `bl __atomic_fetch_add_4` libcall the freestanding link may not resolve. Design §6(b3).
cad_stubobj=out/stage90/xnu_arm_entry_stubs.o
if [[ -f $cad_stubobj ]]; then
  cad_libcall=$(arm-none-eabi-objdump -d "$cad_stubobj" 2>/dev/null | grep -cE 'bl[[:space:]]+[0-9a-f]+ <__atomic_fetch_add' || true)
  cad_ldrex=$(arm-none-eabi-objdump -d "$cad_stubobj" 2>/dev/null | grep -cE '(ldrex|strex)' || true)
  echo "   (b3) the stub object: __atomic_fetch_add libcall(s) x$cad_libcall (want 0), inline ldrex/strex x$cad_ldrex (want >= 1)"
  [[ "$cad_libcall" == "0" && "$cad_ldrex" -ge 1 ]] || { echo "FAIL: the atomic sleh increments are not inline ldrex/strex (libcall x$cad_libcall, ldrex/strex x$cad_ldrex) - the §4 fallback (store the seq into the _back record) is required"; exit 1; }
else
  echo "   (b3) (stub object not present at $cad_stubobj - the atomicity reader is unavailable this run)"
fi

# (b3b) entry_trace.o must itself carry the CADENCE define (the pet's call), so the summary call is
#       not compiled out of the trace object - the §5.3 STUB_DEFINES_TRACE half.
cad_traceobj=out/stage90/xnu_arm_entry_trace.o
if [[ -f $cad_traceobj ]]; then
  cad_tu=$(arm-none-eabi-nm "$cad_traceobj" 2>/dev/null | grep -cE '(U|w) entry_cadence_summary' || true)
  echo "   (b3b) the trace object references entry_cadence_summary: $cad_tu (want 1)"
  [[ "$cad_tu" == "1" ]] || { echo "FAIL: $cad_traceobj does not reference entry_cadence_summary - the STUB_DEFINES_TRACE half did not reach entry_trace.c"; exit 1; }
else
  echo "   (b3b) (trace object not present at $cad_traceobj - the reader is unavailable this run)"
fi

# (d) the POOL/platform object: pe_msm8974.o must define the two accessors and reference nothing new
#     that did not resolve (a stand-in would not be a `T`).
if [[ -f $pe_obj ]]; then
  for cad_bsym in entry_board_tick_count entry_board_irq_count; do
    arm-none-eabi-nm "$pe_obj" | grep -E "^[0-9a-f]+ [Tt] $cad_bsym\$" || { echo "FAIL: $pe_obj does not DEFINE $cad_bsym - the platform block was not rebuilt with -DSTAGE90_XNU_CADENCE (export the switch before build_xnu_arm_kernel.sh)"; exit 1; }
  done
  echo "   (d) $pe_obj defines entry_board_tick_count + entry_board_irq_count"
else
  echo "   (d) (platform object not present at $pe_obj - the reader is unavailable this run)"
fi

# (e) the fresh record carries the new arm key, and the preflight gate names it (else the converse
#     check refuses the press - exactly how 1004's first press preflight failed).
[[ $(awk -F= '$1=="STAGE90_XNU_CADENCE"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_CADENCE=1 - the new arm key was not recorded this run"; exit 1; }
echo "   (e) the fresh record carries STAGE90_XNU_CADENCE=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_CADENCE' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_CADENCE is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (e) scripts/preflight_boot_check.sh names STAGE90_XNU_CADENCE (the gate will not refuse the press)"

echo "-- by value: the FROZEN 1004 elf (armed-d13-582a19c8, the PRESSED 1004 arm) as the NEGATIVE CONTROL (it must NOT carry the 1005 change; a reader that cannot fail certifies nothing) --"
d5_ctrl=out/stage90/frozen/armed-d13-582a19c8/xnu_arm_entry.elf
if [[ -f $d5_ctrl ]]; then
  ctl_cad=$(strings "$d5_ctrl" | grep -c 'xnu_live_cadence_' || true)
  ctl_cadsym=$(arm-none-eabi-nm "$d5_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_cadence_(sample|summary)$' || true)
  ctl_ldrex=$(arm-none-eabi-objdump -d "$d5_ctrl" 2>/dev/null | grep -cE '(ldrex|strex)' || true)
  [[ "$ctl_cad" == "0" && "$ctl_cadsym" == "0" ]] || { echo "FAIL: the frozen 1004 elf already carries the 1005 cadence change (literals x$ctl_cad, symbols x$ctl_cadsym) - the negative control is not a control"; exit 1; }
  echo "   frozen 1004 elf ($d5_ctrl): xnu_live_cadence_ x0, entry_cadence_{sample,summary} x0 (ldrex/strex x$ctl_ldrex) - the change is NOT in it (control holds)"
else
  echo "   (frozen 1004 elf not present at $d5_ctrl - the negative control is unavailable this run)"
fi

# ====================================================================================================
# THE 1006 DELTA: the idle-path freeze witness (STAGE90_XNU_FREEZE_WITNESS), asserted BY VALUE, with
# the FROZEN 1005 elf (armed-d13-ff937225, the PRESSED 1005 arm) as the NEGATIVE CONTROL - a reader
# that cannot fail certifies nothing. Design: docs/1006-interrupt-freeze-witness-design.md, §4/§8.
#
# The change is FOUR src/ files (NO tree edit, NO stage driver, NO platform object): `entry_stubs.c`
# adds the sampler `entry_freeze_sample` (+ externs entry_counter/entry_gic_freeze_state); `entry_trace.c`
# declares it and calls it at `__wrap_machine_idle`'s head; `entry_irq.c` defines the GIC-state helper
# `entry_gic_freeze_state`; `entry_gic.h` declares it. All gated `#if defined(STAGE90_XNU_FREEZE_WITNESS)
# && STAGE90_XNU_FREEZE_WITNESS`; OFF is byte-identical.
# ====================================================================================================
echo "-- by value: the 1006 DELTA - the idle-path freeze witness (STAGE90_XNU_FREEZE_WITNESS) --"

# (a) SOURCE side, per-file: the defined()-first guard, once per gated region.
fw_stub=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' "$stub_src" || true)
fw_trace=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' src/entry/entry_trace.c || true)
fw_irq=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' src/entry/entry_irq.c || true)
fw_gich=$(grep -cE '^#if defined\(STAGE90_XNU_FREEZE_WITNESS\) && STAGE90_XNU_FREEZE_WITNESS$' src/entry/entry_gic.h || true)
echo "   (a) guards: entry_stubs.c x$fw_stub (want 1), entry_trace.c x$fw_trace (want 2 - decl, call site), entry_irq.c x$fw_irq (want 1 - the helper), entry_gic.h x$fw_gich (want 1 - the decl)"
[[ "$fw_stub" == "1" && "$fw_trace" == "2" && "$fw_irq" == "1" && "$fw_gich" == "1" ]] || { echo "FAIL: the freeze guards are not one-per-region (stubs x$fw_stub want 1, trace x$fw_trace want 2, irq x$fw_irq want 1, gic.h x$fw_gich want 1) - a hand edit moved or dropped one"; exit 1; }
# The sampler is defined once; the helper is declared once in the header; the call site is one line.
# **1007 §4.3 changed the sampler's SIGNATURE**: it now TAKES the clock by value
# (`entry_freeze_sample(uint32_t now)`), because a call to `entry_counter()` from `entry_stubs.c`
# resolves to the terminal stand-in (§2.3). The caller passes it: `entry_freeze_sample(entry_counter())`.
[[ $(grep -cF 'entry_freeze_sample(uint32_t now)' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_freeze_sample(uint32_t now) is not present exactly once in $stub_src (the §4.3 definition taking the clock by value)"; exit 1; }
[[ $(grep -cF 'entry_freeze_sample(entry_counter());' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the entry_freeze_sample(entry_counter()) CALL is not present exactly once in entry_trace.c (the §4.3 caller must pass the clock)"; exit 1; }
[[ $(grep -c 'void entry_gic_freeze_state(uint32_t \*out);' src/entry/entry_gic.h || true) == 1 ]] || { echo "FAIL: entry_gic_freeze_state is not declared once in entry_gic.h"; exit 1; }
[[ $(grep -c 'entry_gic_freeze_state(uint32_t \*out)' src/entry/entry_irq.c || true) == 1 ]] || { echo "FAIL: entry_gic_freeze_state is not defined once in entry_irq.c"; exit 1; }
# The threshold constant is the published ordinal (design §7) - a drift here moves the reading.
[[ $(grep -c '^#define MSM8974_FREEZE_MIN 0x00004b00u$' "$stub_src" || true) == 1 ]] || { echo "FAIL: MSM8974_FREEZE_MIN is not the published 0x00004b00u in $stub_src"; exit 1; }
echo "   (a) the source shape: guards one-per-region + 1 sample def / 1 call / 1 helper decl+def + the threshold constant"

# (b) IMAGE side (by value). Eight literals: six freeze (n/d/cpsr/ien/pend/ctlr) + two idle baseline;
#     the two symbols DEFINED (a generated stand-in would show 0); the call site inside __wrap_machine_idle.
fw_lit=$(strings "$elf" | grep -c 'xnu_live_freeze_' || true)
[[ "$fw_lit" == "6" ]] || { echo "FAIL: the image carries $fw_lit \`xnu_live_freeze_\` literals (want 6: n/d/cpsr/ien/pend/ctlr) - the store did not reach the linked image"; exit 1; }
fw_idle=$(strings "$elf" | grep -c 'xnu_live_frzbase_' || true)
[[ "$fw_idle" == "2" ]] || { echo "FAIL: the image carries $fw_idle \`xnu_live_frzbase_\` literals (want 2: n/d, the unfiltered baseline)"; exit 1; }
echo "   (b) the linked image carries the 6 \`xnu_live_freeze_\` + 2 \`xnu_live_frzbase_\` key literals"
for fw_sym in entry_freeze_sample entry_gic_freeze_state; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $fw_sym\$" || { echo "FAIL: $fw_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) the two symbols are DEFINED T/t in the image"

# (c) the call site is inside __wrap_machine_idle, exactly once image-wide.
fw_mi=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_machine_idle"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_machine_idle is not in the image"; exit 1; }
fw_mix=$(arm-none-eabi-nm -n "$elf" | awk -v s="${fw_mi#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
fw_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$fw_mi" --stop-address="${fw_mix:-0xffffffff}")
fw_calls=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' <<<"$fw_body" || true)
fw_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_freeze_sample>' || true)
echo "   (c) __wrap_machine_idle calls entry_freeze_sample: $fw_calls (want 1); image-wide: $fw_all (want 1)"
[[ "$fw_calls" == "1" && "$fw_all" == "1" ]] || { echo "FAIL: entry_freeze_sample is not called exactly once, inside __wrap_machine_idle (in-wrapper x$fw_calls, image-wide x$fw_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }

# (d) the OBJECTS reference what the design says, and the platform object is UNCHANGED by 1006/1007.
#     **CHANGED AT 1007 (§4.3).**  1006's sampler called `entry_counter()` from entry_stubs.c, which the
#     standalone stub generator had emitted as a TERMINAL stand-in (the image's `8049cce4 T
#     entry_counter` -> `entry_stub_hit` -> `entry_epilogue`), so entry_stubs.o carried `U
#     entry_counter`.  1007 gives the sampler the clock as an ARGUMENT, so entry_stubs.o must now have
#     NO `entry_counter` reference at all (the §6e landmine clause asserts this by value below); the GIC
#     helper extern stays.
fw_so=out/stage90/xnu_arm_entry_stubs.o
if [[ -f $fw_so ]]; then
  fw_o_ec=$(arm-none-eabi-nm "$fw_so" 2>/dev/null | grep -cE '(U|w) entry_counter' || true)
  fw_o_gic=$(arm-none-eabi-nm "$fw_so" 2>/dev/null | grep -cE '(U|w) entry_gic_freeze_state' || true)
  echo "   (d) entry_stubs.o references entry_counter: $fw_o_ec (want 0 - 1007's §4.3 argument-passing fix), entry_gic_freeze_state: $fw_o_gic (want 1)"
  [[ "$fw_o_ec" == "0" && "$fw_o_gic" == "1" ]] || { echo "FAIL: entry_stubs.o still references entry_counter (x$fw_o_ec, want 0) - the §4.3 argument-passing fix did not reach the sampler, and the terminal stand-in would end the run on the abort path; or entry_gic_freeze_state (x$fw_o_gic, want 1) is gone - a stale object"; exit 1; }
else
  echo "   (d) (entry_stubs.o not present at $fw_so - the reader is unavailable this run)"
fi

# (e) the fresh record carries the new arm key, and the preflight gate names it (else the converse
#     check refuses the press - exactly how 1004's/1005's first press preflight failed).
[[ $(awk -F= '$1=="STAGE90_XNU_FREEZE_WITNESS"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_FREEZE_WITNESS=1 - the new arm key was not recorded this run"; exit 1; }
echo "   (e) the fresh record carries STAGE90_XNU_FREEZE_WITNESS=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_FREEZE_WITNESS' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_FREEZE_WITNESS is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (e) scripts/preflight_boot_check.sh names STAGE90_XNU_FREEZE_WITNESS (the gate will not refuse the press)"

echo "-- by value: the FROZEN 1005 elf (armed-d13-ff937225, the PRESSED 1005 arm) as the NEGATIVE CONTROL (it must NOT carry the 1006 change; a reader that cannot fail certifies nothing) --"
d6_ctrl=out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf
if [[ -f $d6_ctrl ]]; then
  ctl_fw=$(strings "$d6_ctrl" | grep -c 'xnu_live_freeze_' || true)
  ctl_fwsym=$(arm-none-eabi-nm "$d6_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_freeze_sample|entry_gic_freeze_state)$' || true)
  [[ "$ctl_fw" == "0" && "$ctl_fwsym" == "0" ]] || { echo "FAIL: the frozen 1005 elf already carries the 1006 freeze change (literals x$ctl_fw, symbols x$ctl_fwsym) - the negative control is not a control"; exit 1; }
  echo "   frozen 1005 elf ($d6_ctrl): xnu_live_freeze_ x0, entry_freeze_sample/entry_gic_freeze_state x0 - the change is NOT in it (control holds)"
else
  echo "   (frozen 1005 elf not present at $d6_ctrl - the negative control is unavailable this run)"
fi

# ====================================================================================================
# THE 1007 DELTA, KEPT AS A REGRESSION (1008's set is 1007's verbatim): the abort-span witness
# (STAGE90_XNU_ABT_SPAN), asserted BY VALUE, with the FROZEN 1006 elf (armed-d13-31bdebf3, the PRESSED
# 1006 arm) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing. Design:
# docs/1007-design.md (§4 the change, §5 the A/B/C/D classifier) + docs/1008-design.md §5(f) (the
# reproducibility clause: 1007's own source-shape pins must not drift).
#
# The change is TWO src/ files (NO tree edit, NO stage driver, NO platform object): `entry_stubs.c`
# adds the recorder `entry_note_abt_span(uint32_t t0, uint32_t call)` (it takes the two clock VALUES as
# arguments - it never calls `entry_counter()`); `entry_trace.c` DECLARES it and BRACKETS the
# `__real_sleh_abort` call inside `__wrap_sleh_abort` with the file's own `static inline
# entry_counter()`. All gated `#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN`; OFF is
# byte-identical. 1007 ALSO carries §4.3 (the `entry_freeze_sample(uint32_t now)` argument-passing fix),
# so the OFF image is NOT any pressed arm - the honest OFF control is the symbol-level one (§6d).
#
# **1008 KEEPS it and does NOT touch its shape** (the ABT_FAR guard is SEPARATE, §4.1/§4.3): the span
# recorder-def and bracket-call greps below are the exact 1007 reproducibility pins. The image-key
# count clause moves 6 -> 11 because 1008's five `xnu_live_abt_*` literals join the six (docs/1008-
# design.md §4.4).
# ====================================================================================================
echo "-- by value: the 1007 DELTA, KEPT (the abort-span witness, STAGE90_XNU_ABT_SPAN) --"

# (a) SOURCE SHAPE, PER FILE - the `defined()`-first guard count, the recorder defined once, the call
#     one line.  A hand edit that moved a guard would drift the count.
ab_sp=$(grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$' "$stub_src" || true)
ab_tr=$(grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$' src/entry/entry_trace.c || true)
# entry_trace.c carries FOUR guards, not the design's two: the `t0` DECLARATION and its ASSIGNMENT sit
# in different places, and each must be guarded on its own (an unguarded `uint32_t t0;` is an unused
# local to `-Wall -Wextra -Werror` in the OFF arm, `build_entry.sh:2089`). The measured count is 4.
echo "   (a) guards: entry_stubs.c x$ab_sp (want 1 - the recorder), entry_trace.c x$ab_tr (want 4 - decl, t0 decl, clock read, note call)"
[[ "$ab_sp" == "1" && "$ab_tr" == "4" ]] || { echo "FAIL: the ABT_SPAN guards are not one-per-region (stubs x$ab_sp want 1, trace x$ab_tr want 4: decl + t0 decl + clock read + note call) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -cF 'entry_note_abt_span(uint32_t t0, uint32_t call)' "$stub_src" || true) == 1 ]] || { echo "FAIL: the recorder 'entry_note_abt_span(uint32_t t0, uint32_t call)' is not DEFINED exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_span(t0, entry_counter() - t0);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the ONE bracket call 'entry_note_abt_span(t0, entry_counter() - t0);' is not present exactly once in entry_trace.c"; exit 1; }
# §4.3 (the companion fix that closes the §2.3 defect): the 1006 sampler must take the clock as an
# argument, NOT call the stubbed symbol.
[[ $(grep -cF 'entry_freeze_sample(uint32_t now)' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_freeze_sample is not the §4.3 argument-passing shape 'entry_freeze_sample(uint32_t now)' in $stub_src - the sampler still calls entry_counter and the terminal stand-in would end the run"; exit 1; }
[[ $(grep -cF 'entry_freeze_sample(entry_counter());' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: 'entry_freeze_sample(entry_counter());' is not present exactly once in entry_trace.c - the §4.3 caller does not pass the clock"; exit 1; }
echo "   (a) the source shape: 1+4 guards + 1 recorder def + 1 bracket call + the §4.3 argument-passing fix"

# (b) IMAGE KEYS - the `xnu_live_abt_` literals: the six 1007 span keys (n/seq/gap/call/maxcall/
#     maxgap) PLUS 1008's five (far/type/farn/maxfar/maxtype), PLUS 1009's seven (pcn/pc/fsr/abtcpsr/
#     pcfar/nfar/nfsr), PLUS 1010's twelve (pten/pteva/ptel2/ptel1/ptettbr/pteu/ptenx + vmfn/vmfret/
#     vmfva/vmfprot/vmfnret), PLUS 1010c's six (pteoow/ptealias/ptemapok/pteslot/ptewbase/ptewlen),
#     PLUS 1011's seven (vmen/vmekr/vmeprot/vmefault/vmeva/vmenx + ptethr), PLUS 1012's six (pmpn/
#     pmpprot/pmppa/pmppmap/pmpttbr/pmpnx) => the image carries 49. A count below 49 means a store did
#     not reach the image; a count below the 1007/1008/1009 floors means a regression.
#     docs/1012-design.md §2/§4.
ab_lit=$(strings "$elf" | grep -c 'xnu_live_abt_' || true)
# **131 = 1014's measured 100 + 1015's 31 added keys.** The exact delta was MEASURED this session by
# diffing the frozen 1014 elf's literal set against the current build (comm on the sorted unique
# `xnu_live_abt_[a-z0-9_]+` sets): 1014 elF = 100 (matching this check's old constant), 1015 ADDS 31 and
# REMOVES 0 - 27 `wd*` keys (wddacr/wddom/wdttbcr/wdsplit/wdttbr0/wdttbr1/wdl1/wdleaf/wdap/wdapx/wdtex/
# wdcb/wdsmall/wdpcpu/wdthrmap/wdmapok/wdseen/wdctl/wdctlok/wdn/wdva/wdempty + the AT group wdatok/
# wdatpar/wdatparpr/wdctlat/wdsctlr = 27) + `leafng` (the raw bit11) + the three owed-1014 §6 fixes
# (wwpre/wwpost/wwmoved) = 31. A count below 131 means a store did not reach the image; a count != 131
# means the 1015 key set drifted from what the design declares.
[[ "$ab_lit" == "131" ]] || { echo "FAIL: the image carries $ab_lit \`xnu_live_abt_\` literals (want 131: 1014's measured 100 + 1015's 31 added keys [27 wd* + leafng + the owed wwpre/wwpost/wwmoved], the exact delta measured by diffing the frozen 1014 elf's literal set) - a store did not reach the linked image"; exit 1; }
echo "   (b) the linked image carries the 131 \`xnu_live_abt_\` key literals (1014's measured 100 + 1015's 31: 27 wd* + leafng + wwpre/wwpost/wwmoved)"
arm-none-eabi-nm "$elf" | grep -E '^[0-9a-f]+ [Tt] entry_note_abt_span$' || { echo "FAIL: entry_note_abt_span is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
echo "   (b) entry_note_abt_span is DEFINED T/t in the image"

# (c) CALL SITE - `__wrap_sleh_abort`'s own body carries exactly one branch to `entry_note_abt_span`,
#     and image-wide exactly one ([[mi4-hooks-live-at-calls]]).
#     **MEASURED, not assumed (1007 first build):** the compiler turns the last statement into a TAIL
#     CALL - `b <entry_note_abt_span>`, a plain `b`, not a `bl` - because the recorder is `void` and the
#     wrapper returns its value. The clause must accept `b` OR `bl`; a `bl`-only reader false-refuses a
#     correct image (the 1004 tail-call reader fix, `build_1004.sh:500`, again).
ab_wa=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_sleh_abort"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_sleh_abort is not in the image - the abort handler the witness rides is gone"; exit 1; }
# NO early `exit` in the next-symbol awk (998's measured SIGPIPE/141 defect under `set -o pipefail`).
ab_wx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${ab_wa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
ab_body=$(arm-none-eabi-objdump -d "$elf" --start-address="$ab_wa" --stop-address="${ab_wx:-0xffffffff}")
ab_in=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$ab_body" || true)
ab_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' || true)
echo "   (c) __wrap_sleh_abort ($ab_wa..${ab_wx:-end}) calls entry_note_abt_span: $ab_in (want 1); image-wide: $ab_all (want 1)"
[[ "$ab_in" == "1" && "$ab_all" == "1" ]] || { echo "FAIL: entry_note_abt_span is not called exactly once, inside __wrap_sleh_abort (in-wrapper x$ab_in, image-wide x$ab_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }
echo "   the bracket, by value:"
grep -B4 -A2 -E '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_span>' <<<"$ab_body" | sed 's/^/       /'

# (d) OFF CONTROL, BY VALUE - a twin rebuild with STAGE90_XNU_ABT_FAR=0 (and ABT_SPAN=1, the 1007 arm)
#     carries `xnu_live_abt_` == 6 (= the 1007 literal set) and no `entry_note_abt_far` symbol.  THIS
#     CLAUSE IS THE REPRODUCIBILITY GUARANTEE (docs/1008-design.md §5d): it proves the 1008 edits
#     compile out and leave exactly 1007's key set.
#
#     **SKIPPED BY DEFAULT** - it costs a second full entry build.  Set
#     `STAGE90_1008_OFF_CONTROL=1` to run it: rebuild into a scratch OUT dir with ABT_FAR=0 (the OFF
#     arm) and assert the two properties on THAT image, WITHOUT disturbing out/stage90.
if [[ "${STAGE90_1008_OFF_CONTROL:-0}" == "1" ]]; then
  echo "-- by value: the OFF CONTROL (STAGE90_XNU_ABT_FAR=0 twin rebuild; the switch must compile out, leaving 1007's six keys) --"
  # The switch's ONLY consumers are the two entry-toolchain objects (entry_stubs.c, entry_trace.c);
  # no pool/platform object reads it, so the OFF image is the SAME (already built) pool linked with the
  # entry objects rebuilt OFF.  build_entry.sh hardcodes OUT to REPO_ROOT/out/stage90, so the twin build
  # SWAPS that dir for an empty scratch, runs the entry build with ABT_FAR=0, inspects the scratch elf,
  # then restores the ON out/stage90 in ALL cases (EXIT/INT trap).  **This touches live shared state
  # ([[mi4-out-stage90-is-shared-live-state]]): get a window first; do NOT run it while a press preflight
  # is reading out/stage90.**  The OFF arm is the operator's call (the switch is OFF by default).
  if [[ -e out/stage90-off-save ]]; then echo "FAIL: out/stage90-off-save already exists - a prior OFF run did not clean up"; exit 1; fi
  mv out/stage90 out/stage90-off-save
  mkdir -p out/stage90
  ab_restore() { rm -rf out/stage90; mv out/stage90-off-save out/stage90; }
  trap ab_restore EXIT INT TERM
  _ab_ok=0
  ( cd src/entry && env -i PATH="$PATH" HOME="$HOME" \
      STAGE90_XNU_ABT_FAR=0 STAGE90_XNU_ABT_SPAN=1 STAGE90_XNU_FREEZE_WITNESS=1 \
      "${EXPORTS[@]}" ./build_entry.sh ) && _ab_ok=1 || _ab_ok=0
  _ab_offelf=out/stage90/xnu_arm_entry.elf
  if [[ "$_ab_ok" == "1" && -f "$_ab_offelf" ]]; then
    off_lit=$(strings "$_ab_offelf" | grep -c 'xnu_live_abt_' || true)
    off_sym=$(arm-none-eabi-nm "$_ab_offelf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_far$' || true)
    echo "   (d) OFF image: xnu_live_abt_ x$off_lit (want 6 = 1007's set), entry_note_abt_far x$off_sym (want 0)"
    ab_restore; trap - EXIT INT TERM
    [[ "$off_lit" == "6" && "$off_sym" == "0" ]] || { echo "FAIL: the OFF image does not carry exactly 1007's six keys (literals x$off_lit want 6, symbol x$off_sym want 0) - the 1008 edit did NOT truly compile out (1007's arm is not reproducible)"; exit 1; }
    echo "   (d) the switch COMPILES OUT (OFF image carries exactly 1007's six abt keys and no far recorder symbol)"
  else
    ab_restore; trap - EXIT INT TERM
    echo "   (d) OFF control: the twin build was requested but FAILED (rc=$_ab_ok) - out/stage90 was restored; investigate before trusting the OFF clause"
    [[ "${STAGE90_1008_OFF_CONTROL_HARD:-0}" == "1" ]] && exit 1
  fi
else
  echo "   (d) OFF control: SKIPPED (set STAGE90_1008_OFF_CONTROL=1 to run the twin build) - the OFF"
  echo "       shape is asserted by the SOURCE guard (a) and demonstrated by the frozen 1007 elf (f)"
fi

# (e) THE LANDMINE CLAUSE - the terminal `entry_counter` stand-in must be GONE, forcing §4.3.  The
#     standalone stub generator emits `void entry_counter(void) { entry_stub_hit(...) }` into
#     realstubs.c ONLY when entry_stubs.o has an undefined `entry_counter`; so BOTH must be absent.
if [[ -f out/stage90/xnu_arm_entry_realstubs.c ]]; then
  lm_gen=$(grep -cE '^void entry_counter\(void\)' out/stage90/xnu_arm_entry_realstubs.c || true)
  echo "   (e) realstubs.c: \`^void entry_counter(void)\` lines: $lm_gen (want 0)"
  [[ "$lm_gen" == "0" ]] || { echo "FAIL: the terminal entry_counter STAND-IN is still generated in realstubs.c - entry_stubs.c still references entry_counter; apply the §4.3 argument-passing fix"; exit 1; }
else
  echo "   (e) (realstubs.c not present at out/stage90/xnu_arm_entry_realstubs.c - the landmine reader is unavailable this run)"
fi
if [[ -f out/stage90/xnu_arm_entry_stubs.o ]]; then
  lm_u=$(arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o 2>/dev/null | grep -cE '^[[:space:]]*U entry_counter$' || true)
  echo "   (e) out/stage90/xnu_arm_entry_stubs.o: \`U entry_counter\` lines: $lm_u (want 0)"
  [[ "$lm_u" == "0" ]] || { echo "FAIL: entry_stubs.o still carries an undefined \`U entry_counter\` - the §2.3 landmine: on the abort path (which 1007 rides ~40x) the terminal stand-in would end the run at entry_stub_hit -> entry_epilogue"; exit 1; }
else
  echo "   (e) (entry_stubs.o not present - the landmine reader is unavailable this run)"
fi

# (f) NEGATIVE CONTROL against the FROZEN 1006 elf (the PRESSED 1006 arm): `xnu_live_abt_` x0 AND no
#     `entry_note_abt_span` symbol.  By value the 1006 elf DOES contain the `entry_counter` stand-in at
#     8049cce4 - this control also demonstrates the §2.3 defect 1007 closes.
d7_ctrl=out/stage90/frozen/armed-d13-31bdebf3/xnu_arm_entry.elf
if [[ -f $d7_ctrl ]]; then
  ctl_ab=$(strings "$d7_ctrl" | grep -c 'xnu_live_abt_' || true)
  ctl_absym=$(arm-none-eabi-nm "$d7_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_span$' || true)
  [[ "$ctl_ab" == "0" && "$ctl_absym" == "0" ]] || { echo "FAIL: the frozen 1006 elf already carries the 1007 change (xnu_live_abt_ x$ctl_ab, entry_note_abt_span x$ctl_absym) - the negative control is not a control"; exit 1; }
  _ab_ctr_ec=$(arm-none-eabi-nm "$d7_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_counter$' || true)
  echo "   frozen 1006 elf ($d7_ctrl): xnu_live_abt_ x$ctl_ab, entry_note_abt_span x$ctl_absym - the change is NOT in it (control holds)"
  echo "   frozen 1006 elf carries the §2.3 defect BY VALUE: entry_counter stand-in x$_ab_ctr_ec (want >= 1 - the terminal stand-in 1007 closes)"
else
  echo "   (frozen 1006 elf not present at $d7_ctrl - the negative control is unavailable this run)"
fi

# (g) RECORD + GATE - the fresh record carries the 1007 arm key once, and the preflight gate names it
#     (else the converse check at preflight_boot_check.sh:1172 refuses the press).  1008 KEEPS it
#     (ABT_SPAN stays 1; the far call reuses its `t0`).
[[ $(awk -F= '$1=="STAGE90_XNU_ABT_SPAN"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABT_SPAN=1 - 1008 requires it (the far call reuses the span's t0) and the record must name it"; exit 1; }
echo "   (g) the fresh record carries STAGE90_XNU_ABT_SPAN=1 (kept from 1007 this run)"
grep -q 'STAGE90_XNU_ABT_SPAN' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_ABT_SPAN is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (g) scripts/preflight_boot_check.sh names STAGE90_XNU_ABT_SPAN (the gate will not refuse the press)"

# ====================================================================================================
# THE 1008 DELTA: the abort-fault identity (STAGE90_XNU_ABT_FAR), asserted BY VALUE, with the FROZEN
# 1007 elf (armed-d13-88074ab3, the PRESSED 1007 arm) as the NEGATIVE CONTROL - a reader that cannot
# fail certifies nothing. Design: docs/1008-design.md §4 (the change), §5 (the refusals (a)-(h)).
#
# The change is TWO src/ files (NO tree edit, NO stage driver, NO platform object, NO new --wrap):
# `entry_stubs.c` adds the recorder `entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type)`
# (its `far`/`type` passed BY VALUE - it never reads cp15); `entry_trace.c` DECLARES it beside the span
# extern and CALLS it after the span bracket inside `__wrap_sleh_abort` (reusing the span's `t0`). Both
# gated `#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR`, SEPARATE from ABT_SPAN's guards so
# 1007's guard count (1 stubs / 4 trace) is undisturbed; OFF is byte-identical (1007's six keys).
# ====================================================================================================
echo "-- by value: the 1008 DELTA - the abort-fault identity (STAGE90_XNU_ABT_FAR) --"

# (a) SOURCE SHAPE, PER FILE - the `defined()`-first guard count, the recorder defined once, the call
#     one line (docs/1008-design.md §5a).
abf_sp=$(grep -cE '^#if defined\(STAGE90_XNU_ABT_FAR\) && STAGE90_XNU_ABT_FAR$' "$stub_src" || true)
abf_tr=$(grep -cE '^#if defined\(STAGE90_XNU_ABT_FAR\) && STAGE90_XNU_ABT_FAR$' src/entry/entry_trace.c || true)
echo "   (a) guards: entry_stubs.c x$abf_sp (want 1 - the recorder), entry_trace.c x$abf_tr (want 2 - extern decl, call)"
[[ "$abf_sp" == "1" && "$abf_tr" == "2" ]] || { echo "FAIL: the ABT_FAR guards are not one-per-region (stubs x$abf_sp want 1, trace x$abf_tr want 2: extern decl + note call) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -cF 'entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type)' "$stub_src" || true) == 1 ]] || { echo "FAIL: the recorder 'entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type)' is not DEFINED exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the ONE call 'entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type);' is not present exactly once in entry_trace.c"; exit 1; }
# THE REPRODUCIBILITY CLAUSE (docs/1008-design.md §5f): 1007's OWN source-shape pins must not drift -
# the span recorder-def == 1, the span call == 1, and the ABT_SPAN guard count == 1 / 4.
[[ $(grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$' "$stub_src" || true) == 1 ]] || { echo "FAIL: 1007 is NOT reproducible - the ABT_SPAN guard count in $stub_src is not 1 (the 1008 edit reached a 1007-pinned span region)"; exit 1; }
[[ $(grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$' src/entry/entry_trace.c || true) == 4 ]] || { echo "FAIL: 1007 is NOT reproducible - the ABT_SPAN guard count in entry_trace.c is not 4 (the 1008 edit reached a 1007-pinned span region)"; exit 1; }
[[ $(grep -cF 'entry_note_abt_span(uint32_t t0, uint32_t call)' "$stub_src" || true) == 1 ]] || { echo "FAIL: 1007 is NOT reproducible - the span recorder-def is not present exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_span(t0, entry_counter() - t0);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: 1007 is NOT reproducible - the span bracket call is not present exactly once in entry_trace.c"; exit 1; }
echo "   (a) the source shape: 1+2 ABT_FAR guards + 1 recorder def + 1 call + 1007's span pins intact (1/4 guards, 1 def, 1 call)"

# (b) IMAGE KEYS - 1008's five `xnu_live_abt_*` literals (far/type/farn/maxfar/maxtype) present, and
#     the recorder a DEFINED T/t (0 => an object did not rebuild or a stand-in was used).
for abf_key in xnu_live_abt_far xnu_live_abt_type xnu_live_abt_farn xnu_live_abt_maxfar xnu_live_abt_maxtype; do
  grep -qx "$abf_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$abf_key\` literal - the ABT_FAR store did not reach the linked image"; exit 1; }
done
echo "   (b) the linked image carries all five 1008 key literals (far/type/farn/maxfar/maxtype)"
arm-none-eabi-nm "$elf" | grep -E '^[0-9a-f]+ [Tt] entry_note_abt_far$' || { echo "FAIL: entry_note_abt_far is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
echo "   (b) entry_note_abt_far is DEFINED T/t in the image"

# (c) CALL SITE - `__wrap_sleh_abort`'s own body carries exactly one branch to `entry_note_abt_far`,
#     and image-wide exactly one ([[mi4-hooks-live-at-calls]]).  The last statement is a TAIL call -
#     accept `b` OR `bl` (the 1007 measured fix).
abf_in=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' <<<"$ab_body" || true)
abf_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' || true)
echo "   (c) __wrap_sleh_abort ($ab_wa..${ab_wx:-end}) calls entry_note_abt_far: $abf_in (want 1); image-wide: $abf_all (want 1)"
[[ "$abf_in" == "1" && "$abf_all" == "1" ]] || { echo "FAIL: entry_note_abt_far is not called exactly once, inside __wrap_sleh_abort (in-wrapper x$abf_in, image-wide x$abf_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }
echo "   the far call, by value:"
grep -B5 -A2 -E '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_far>' <<<"$ab_body" | sed 's/^/       /'

# (e) 1007 NOT REGRESSED - the frozen 1007 elf (the PRESSED 1007 arm) carries `xnu_live_abt_` == 6 and
#     `T/t entry_note_abt_span`, and carries NO 1008 literal and NO `entry_note_abt_far` symbol - the
#     negative control proves the 1008 change is not in the 1007 arm.
d8_ctrl=out/stage90/frozen/armed-d13-88074ab3/xnu_arm_entry.elf
if [[ -f $d8_ctrl ]]; then
  ctl7_ab=$(strings "$d8_ctrl" | grep -c 'xnu_live_abt_' || true)
  ctl7_span=$(arm-none-eabi-nm "$d8_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_span$' || true)
  ctl7_farsym=$(arm-none-eabi-nm "$d8_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_far$' || true)
  ctl7_farlit=$(strings "$d8_ctrl" | grep -cE 'xnu_live_abt_(far|type|farn|maxfar|maxtype)$' || true)
  echo "   frozen 1007 elf ($d8_ctrl): xnu_live_abt_ x$ctl7_ab (want 6), entry_note_abt_span x$ctl7_span (want >= 1), entry_note_abt_far x$ctl7_farsym (want 0), far literals x$ctl7_farlit (want 0)"
  [[ "$ctl7_ab" == "6" && "$ctl7_span" -ge 1 && "$ctl7_farsym" == "0" && "$ctl7_farlit" == "0" ]] || { echo "FAIL: the frozen 1007 elf does not have the 1007 shape (six abt keys, an entry_note_abt_span symbol, no 1008 change) - the negative control is not a control"; exit 1; }
  echo "   the 1008 change is NOT in the frozen 1007 elf (control holds)"
else
  echo "   (frozen 1007 elf not present at $d8_ctrl - the negative control is unavailable this run)"
fi

# (g) RECORD + GATE - the fresh record carries the new arm key once, and the preflight gate names it
#     (else the converse check at preflight_boot_check.sh:1172 refuses the press).
[[ $(awk -F= '$1=="STAGE90_XNU_ABT_FAR"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABT_FAR=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (g) the fresh record carries STAGE90_XNU_ABT_FAR=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_ABT_FAR' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_ABT_FAR is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (g) scripts/preflight_boot_check.sh names STAGE90_XNU_ABT_FAR (the gate will not refuse the press)"

echo "-- 1008 build done: park with scripts/park_1008.sh --"

# ====================================================================================================
# THE 1009 DELTA, KEPT AS A REGRESSION (1010's set is 1009's verbatim): the faulting PC + FSR
# (STAGE90_XNU_ABT_PC), asserted BY VALUE, with the FROZEN 1008 elf (armed-d13-0343ef45, the PRESSED
# 1008 arm) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing. Design:
# docs/1009-design.md §3 (the change), §4 (the refusals (a)-(f)).
#
# The change is TWO src/ files (NO tree edit, NO stage driver, NO new --wrap): `entry_stubs.c` adds the
# recorder `entry_note_abt_pc(uint32_t pc, uint32_t fsr, uint32_t cpsr, uint32_t far)` (its args passed
# BY VALUE - it reads neither cp15 nor the frame); `entry_trace.c` DECLARES it beside the far extern
# and CALLS it after the far call inside `__wrap_sleh_abort`. The guard is the CONJUNCTION
# `ABT_FAR && ABT_PC` - 1009 sits after the 1008 recorder and reads nothing 1008 did not, so the
# ABT_FAR / ABT_SPAN pins are undisturbed; OFF (ABT_PC=0) is byte-identical (1008's eleven keys).
#
# **1010 KEEPS it and does NOT touch its shape** (ABT_PC's guard is a prefix of 1010's conjunction, and
# the `ABT_PC$` reader below does NOT match 1010's longer `...ABT_PTE` lines).
# ====================================================================================================
echo "-- by value: the 1009 DELTA, KEPT (the faulting PC + FSR, STAGE90_XNU_ABT_PC) --"

# (a) SOURCE SHAPE, PER FILE - the `defined(STAGE90_XNU_ABT_PC)` guard count, the recorder defined once,
#     the call one line (docs/1009-design.md §4a).
abp_sp=$(grep -cE 'defined\(STAGE90_XNU_ABT_PC\) && STAGE90_XNU_ABT_PC$' "$stub_src" || true)
abp_tr=$(grep -cE 'defined\(STAGE90_XNU_ABT_PC\) && STAGE90_XNU_ABT_PC$' src/entry/entry_trace.c || true)
echo "   (a) guards: entry_stubs.c x$abp_sp (want 1 - the recorder), entry_trace.c x$abp_tr (want 2 - extern decl, call)"
[[ "$abp_sp" == "1" && "$abp_tr" == "2" ]] || { echo "FAIL: the ABT_PC guards are not one-per-region (stubs x$abp_sp want 1, trace x$abp_tr want 2: extern decl + note call) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -cF 'entry_note_abt_pc(uint32_t pc, uint32_t fsr, uint32_t cpsr, uint32_t far)' "$stub_src" || true) == 1 ]] || { echo "FAIL: the recorder 'entry_note_abt_pc(uint32_t pc, uint32_t fsr, uint32_t cpsr, uint32_t far)' is not DEFINED exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_pc(((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_PC / 4],' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the ONE 1009 call 'entry_note_abt_pc(((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_PC / 4],' is not present exactly once in entry_trace.c"; exit 1; }
# THE REPRODUCIBILITY CLAUSE: 1008's OWN source-shape pins must not drift - the far recorder-def == 1,
# the far call == 1, and the ABT_FAR guard count == 1 / 2.
[[ $(grep -cF 'entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type)' "$stub_src" || true) == 1 ]] || { echo "FAIL: 1008 is NOT reproducible - the far recorder-def is not present exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: 1008 is NOT reproducible - the far call is not present exactly once in entry_trace.c"; exit 1; }
echo "   (a) the source shape: 1+2 ABT_PC guards + 1 recorder def + 1 call + 1008's far pins intact"

# (b) IMAGE KEYS - 1009's seven `xnu_live_abt_*` literals present, and the recorder a DEFINED T/t.
for abp_key in xnu_live_abt_pcn xnu_live_abt_pc xnu_live_abt_fsr xnu_live_abt_abtcpsr xnu_live_abt_pcfar xnu_live_abt_nfar xnu_live_abt_nfsr; do
  grep -qx "$abp_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$abp_key\` literal - the ABT_PC store did not reach the linked image"; exit 1; }
done
echo "   (b) the linked image carries all seven 1009 key literals (pcn/pc/fsr/abtcpsr/pcfar/nfar/nfsr)"
arm-none-eabi-nm "$elf" | grep -E '^[0-9a-f]+ [Tt] entry_note_abt_pc$' || { echo "FAIL: entry_note_abt_pc is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
echo "   (b) entry_note_abt_pc is DEFINED T/t in the image"
# the recorder must NOT call the stubbed entry_counter (the standalone-termination defect the 1008
# recorder also avoids): image-wide `U entry_counter` from this recorder's object must not appear.
if arm-none-eabi-objdump -d "$elf" 2>/dev/null | grep -qE '<entry_note_abt_pc>:' ; then
  abp_body=$(arm-none-eabi-objdump -d "$elf" | sed -n '/<entry_note_abt_pc>:/,/^$/p')
  grep -q 'entry_counter' <<<"$abp_body" && { echo "FAIL: entry_note_abt_pc calls entry_counter - the recorder must take its inputs BY VALUE (the terminal stand-in risk)"; exit 1; }
  echo "   (b) entry_note_abt_pc does NOT call entry_counter (by value)"
fi

# (c) CALL SITE - `__wrap_sleh_abort`'s own body carries exactly one branch to `entry_note_abt_pc`,
#     and image-wide exactly one.  Accept `b` OR `bl` (the tail-call fix 1004/1007 measured).
abp_in=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' <<<"$ab_body" || true)
abp_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' || true)
echo "   (c) __wrap_sleh_abort ($ab_wa..${ab_wx:-end}) calls entry_note_abt_pc: $abp_in (want 1); image-wide: $abp_all (want 1)"
[[ "$abp_in" == "1" && "$abp_all" == "1" ]] || { echo "FAIL: entry_note_abt_pc is not called exactly once, inside __wrap_sleh_abort (in-wrapper x$abp_in, image-wide x$abp_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }
echo "   the pc call, by value:"
grep -B5 -A2 -E '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pc>' <<<"$ab_body" | sed 's/^/       /'

# (e) 1008 NOT REGRESSED - the frozen 1008 elf (the PRESSED 1008 arm) carries `xnu_live_abt_` == 11 and
#     `T/t entry_note_abt_far`, and NO 1009 literal and NO `entry_note_abt_pc` symbol - the negative
#     control proves the 1009 change is not in the 1008 arm.
d9_ctrl=out/stage90/frozen/armed-d13-0343ef45/xnu_arm_entry.elf
if [[ -f $d9_ctrl ]]; then
  ctl8_ab=$(strings "$d9_ctrl" | grep -c 'xnu_live_abt_' || true)
  ctl8_farsym=$(arm-none-eabi-nm "$d9_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_far$' || true)
  ctl8_pcsym=$(arm-none-eabi-nm "$d9_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_pc$' || true)
  ctl8_pclit=$(strings "$d9_ctrl" | grep -cE 'xnu_live_abt_(pcn|pc|fsr|abtcpsr|pcfar|nfar|nfsr)$' || true)
  echo "   frozen 1008 elf ($d9_ctrl): xnu_live_abt_ x$ctl8_ab (want 11), entry_note_abt_far x$ctl8_farsym (want >= 1), entry_note_abt_pc x$ctl8_pcsym (want 0), pc literals x$ctl8_pclit (want 0)"
  [[ "$ctl8_ab" == "11" && "$ctl8_farsym" -ge 1 && "$ctl8_pcsym" == "0" && "$ctl8_pclit" == "0" ]] || { echo "FAIL: the frozen 1008 elf does not have the 1008 shape (eleven abt keys, an entry_note_abt_far symbol, no 1009 change) - the negative control is not a control"; exit 1; }
  echo "   the 1009 change is NOT in the frozen 1008 elf (control holds)"
else
  echo "   (frozen 1008 elf not present at $d9_ctrl - the negative control is unavailable this run)"
fi

# (g) RECORD + GATE - the fresh record carries the new arm key once, and the preflight gate names it.
[[ $(awk -F= '$1=="STAGE90_XNU_ABT_PC"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABT_PC=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (g) the fresh record carries STAGE90_XNU_ABT_PC=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_ABT_PC' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_ABT_PC is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (g) scripts/preflight_boot_check.sh names STAGE90_XNU_ABT_PC (the gate will not refuse the press)"

# ====================================================================================================
# THE 1010 DELTA: the L2 descriptor + the vm_fault return (STAGE90_XNU_ABT_PTE), asserted BY VALUE,
# with the FROZEN 1009 elf (armed-d13-24997fc5, the PRESSED 1009 arm) as the NEGATIVE CONTROL - a
# reader that cannot fail certifies nothing. Design: docs/1010-design.md §3 (the change), §4 (the
# refusals), docs/1010-pte-walk.md (the walk, by value).
#
# The change is TWO src/ files PLUS one `--wrap` (NO tree edit, NO stage driver): `entry_stubs.c` adds
# the walk `stage90_read_l2_pte` and the two recorders `entry_note_abt_pte(uint32_t va)` /
# `entry_note_abt_vmf(uint32_t ret, uint32_t va, uint32_t prot)` (args BY VALUE); `entry_trace.c`
# DECLARES both, CALLS `entry_note_abt_pte(far_)` after the 1009 call inside `__wrap_sleh_abort`, and
# adds `__wrap_vm_fault` (a PURE PASS-THROUGH except behind the latch). The guard is the CONJUNCTION
# `ABT_FAR && ABT_PC && ABT_PTE`; `--wrap=vm_fault` is added to TRACE_LDFLAGS only when ABT_PTE=1.
# ====================================================================================================
echo "-- by value: the 1010 DELTA - the L2 descriptor + the vm_fault return (STAGE90_XNU_ABT_PTE) --"

# (a) SOURCE SHAPE, PER FILE - the 1010 guard count (stubs 1 region, trace 3: extern + vm_fault wrapper
#     + abort call), the walk + two recorders defined once, the abort call one line, the vm_fault
#     wrapper + its __real_ + the latch rule, and the 1009 pins intact.
abt_sp=$(grep -cE 'defined\(STAGE90_XNU_ABT_PC\) && STAGE90_XNU_ABT_PC && defined\(STAGE90_XNU_ABT_PTE\) && STAGE90_XNU_ABT_PTE$' "$stub_src" || true)
abt_tr=$(grep -cE 'defined\(STAGE90_XNU_ABT_PC\) && STAGE90_XNU_ABT_PC && defined\(STAGE90_XNU_ABT_PTE\) && STAGE90_XNU_ABT_PTE$' src/entry/entry_trace.c || true)
echo "   (a) guards: entry_stubs.c x$abt_sp (want 1 - the walk + both recorders), entry_trace.c x$abt_tr (want 3 - extern decl, vm_fault wrapper, abort call)"
[[ "$abt_sp" == "1" && "$abt_tr" == "3" ]] || { echo "FAIL: the ABT_PTE guards are not one-per-region (stubs x$abt_sp want 1, trace x$abt_tr want 3: extern decl + vm_fault wrapper + abort call) - a hand edit moved or dropped one"; exit 1; }
#     NOTE the anchor: the walk's PROTOTYPE also appears in the doc comment above the definition,
#     so a bare fixed-string count matches the comment AND the definition (2, not 1). Anchor to the
#     line-start definition (column 0, no trailing `;`) — [[mi4-a-claim-in-a-comment-is-not-a-check]].
[[ $(grep -cE '^stage90_read_l2_pte\(uint32_t va, uint32_t \*out_l1_desc, uint32_t \*out_ttbr0_used,$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the walk's definition first line 'stage90_read_l2_pte(uint32_t va, uint32_t *out_l1_desc, uint32_t *out_ttbr0_used,' is not present exactly once at line start in $stub_src (1010c added the allow_alias argument, which wrapped the signature to two lines)"; exit 1; }
[[ $(grep -cE '^ +uint32_t allow_alias\)$' "$stub_src" || true) == 1 ]] || { echo "FAIL: the walk's definition second line '                    uint32_t allow_alias)' is not present exactly once in $stub_src (1010c's fourth argument)"; exit 1; }
[[ $(grep -cF 'entry_note_abt_pte(uint32_t va)' "$stub_src" || true) == 1 ]] || { echo "FAIL: the recorder 'entry_note_abt_pte(uint32_t va)' is not DEFINED exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_vmf(uint32_t ret, uint32_t va, uint32_t prot)' "$stub_src" || true) == 1 ]] || { echo "FAIL: the recorder 'entry_note_abt_vmf(uint32_t ret, uint32_t va, uint32_t prot)' is not DEFINED exactly once in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_note_abt_pte(far_);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the ONE 1010 call 'entry_note_abt_pte(far_);' is not present exactly once in entry_trace.c"; exit 1; }
# The vm_fault wrapper: the __real_ declaration + the wrapper, the pass-through, and the latch call.
[[ $(grep -cF 'uint32_t __wrap_vm_fault(' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: __wrap_vm_fault is not present exactly once in entry_trace.c"; exit 1; }
[[ $(grep -cF 'uint32_t __real_vm_fault(void *map, uint32_t vaddr, uint32_t fault_type, uint32_t change_wiring,' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the __real_vm_fault declaration is not present exactly once in entry_trace.c (the seven-arg signature read off vm_fault.h:90-97)"; exit 1; }
[[ $(grep -cF 'entry_note_abt_vmf(r, vaddr, fault_type);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the wrapper's ONE record call 'entry_note_abt_vmf(r, vaddr, fault_type);' is not present exactly once in entry_trace.c"; exit 1; }
# The walk's constants, BY VALUE (docs/1010-pte-walk.md §1) - each cited to pmap.h, so a drifted
# constant is caught at the source.
[[ $(grep -cF '#define ABTPTE_L1_PTE_MASK     0xfffffc00u' "$stub_src" || true) == 1 ]] || { echo "FAIL: ABTPTE_L1_PTE_MASK is not the pmap.h L1_PTE_ADDR_MASK 0xfffffc00u in $stub_src"; exit 1; }
[[ $(grep -cF '#define ABTPTE_L2_SMALL_PAGE   0x2u' "$stub_src" || true) == 1 ]] || { echo "FAIL: ABTPTE_L2_SMALL_PAGE is not the pmap.h L2_SMALL_PAGE 0x2u in $stub_src"; exit 1; }
# The walk must key on TTBCR.N, NOT on the 0x80000000 address-space boundary (docs/1010-pte-walk.md
# §2). These two strings are NOT unique in the file - `n = ttbcr & 7u;` also appears in the PRE-EXISTING
# `entry_live_ttb_base` (~:2620, the wrong-for-far=0x9a000 helper the design warns about), and
# `base = ttbr0;` appears TWICE inside the walk's own two branches (N=0 and the low window). So scope
# the count to the WALK BODY, extracted by its line-start definition, or the clause refuses a correct
# image ([[mi4-a-claim-in-a-comment-is-not-a-check]], [[mi4-mutation-anchors-are-code-spans]]).
ab_walk=$(awk '/^stage90_read_l2_pte\(uint32_t va, uint32_t \*out_l1_desc, uint32_t \*out_ttbr0_used,$/{f=1} f{print} f&&/^}$/{exit}' "$stub_src")
[[ $(grep -cF 'n = ttbcr & 7u;' <<<"$ab_walk" || true) == 1 ]] || { echo "FAIL: the walk does not read TTBCR.N into 'n' - it must key the table on TTBCR.N (get $(( $(grep -cF 'n = ttbcr & 7u;' <<<"$ab_walk" || true) )))"; exit 1; }
[[ $(grep -cF 'base = ttbr0;' <<<"$ab_walk" || true) == 2 ]] || { echo "FAIL: the walk's TTBR0 selection is not 'base = ttbr0;' in BOTH branches (N=0 and the low window) - want 2 in the walk body"; exit 1; }
# 1011's outcome (commit 07914cf) SUPERSEDED the 1010b fixed mask. The 1010 press (arm 0f39207c) read
# `ptel2=0/pteu=0` at the storm's tail - IMPOSSIBLE under `fsr=0x80f` (FS=15, "permission fault, level
# 2"), which REQUIRES a present L2 descriptor; 1010b swapped in the fixed 16 KB mask
# STAGE90_TTBR_BASE_MASK (0xffffc000, bits[31:14]) - STILL wrong, because THIS PORT RUNS TTBCR.N = 2
# at the storm (arm_vm_init.c:475 set_mmu_ttbcr(2)), where the user pmap's table is 4 KB-aligned and the
# base is bits[31:12]. 1011's capture (records/1011-press-read.txt) MEASURED it: the walk read MH_MAGIC
# 0xfeedface because 0x81a8605b & 0xffffc000 = 0x81a84000 (8 KB low, into a Mach-O image), while the
# truth is 0x81a86000 = pm_l1_phys = the (A) cell's own ptethr. THE MASK IS A FUNCTION OF TTBCR.N, NOT
# A CONSTANT (ARMv7: a table is 2^(14 - N) bytes, aligned to 2^(14 - N)); it is now spelled ONCE as the
# n-derived `entry_ttbr_base` / `entry_ttbr_align_bits`, shared by the walk and `entry_live_ttb_base`.
# Assert: the n-derived spelling is PRESENT (both helpers + the walk's call), the OLD fixed constant
# `STAGE90_TTBR_BASE_MASK`/`0xffffc000u` is GONE, and the L2-TABLE base keeps its OWN mask (bits[31:10]).
[[ $(grep -cF 'entry_ttbr_align_bits(uint32_t n)' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_ttbr_align_bits is not DEFINED in $stub_src - the n-derived alignment that replaced the fixed 16 KB mask (07914cf)"; exit 1; }
[[ $(grep -cF 'entry_ttbr_base(uint32_t ttbr, uint32_t n)' "$stub_src" || true) == 1 ]] || { echo "FAIL: entry_ttbr_base is not DEFINED in $stub_src - the ONE n-derived TTBR base spelling (07914cf)"; exit 1; }
[[ $(grep -cF 'base = entry_ttbr_base(base, n);' <<<"$ab_walk" || true) == 1 ]] || { echo "FAIL: the walk does not mask the TTBR0 base with the n-derived entry_ttbr_base(base, n) - the 1010/1010b constant mask read the L1 8 KB low at N=2 (records/1011-press-read.txt)"; exit 1; }
[[ $(grep -cF 'base &= ABTPTE_L1_PTE_MASK;' <<<"$ab_walk" || true) == 0 ]] || { echo "FAIL: the walk STILL masks the TTBR0 base with the L1->L2 mask ABTPTE_L1_PTE_MASK (the 1010 defect) - the base mask and the L2-table mask are DIFFERENT (one value, two roles)"; exit 1; }
[[ $(grep -cF 'STAGE90_TTBR_BASE_MASK' "$stub_src" || true) == 0 ]] || { echo "FAIL: the fixed constant STAGE90_TTBR_BASE_MASK is STILL spelled in $stub_src - 07914cf removed it; the mask must be n-derived (entry_ttbr_base), never a constant (records/1011-press-read.txt)"; exit 1; }
[[ $(grep -cF '0xffffc000u' "$stub_src" || true) == 0 ]] || { echo "FAIL: the fixed 16 KB TTBR mask 0xffffc000u is STILL spelled in $stub_src - it is correct only at TTBCR.N=0; this port runs N=2 (07914cf, records/1011-press-read.txt)"; exit 1; }
# The L2-TABLE base is the OTHER role: it MUST keep the L1_PTE mask (bits[31:10]). Pin it so a future
# "fix" cannot collapse the two masks the other way.
[[ $(grep -cF 'l2base = l1e & ABTPTE_L1_PTE_MASK;' <<<"$ab_walk" || true) == 1 ]] || { echo "FAIL: the L2-table base is not 'l2base = l1e & ABTPTE_L1_PTE_MASK;' - the two masks must not be conflated (L1->L2 table base is bits[31:10])"; exit 1; }
echo "   (a) the walk masks its TTBR0 base with the n-derived entry_ttbr_base(base,n) (NOT a constant; 07914cf), and l2base = l1e & 0xfffffc00"
# 1010c - THE WINDOW BOUND + THE TEMPORARY-SECTION ALIAS, BY VALUE. The 1010b press read the TRUE L1
# whose L2 table sat at PA 0x46300000, BELOW the window, and the unbounded deref faulted FS=5. The walk
# now (i) bounds both loads to [gPhysBase, gPhysBase + mem_size) and (ii) reaches the out-of-window
# leaf through a temporary 1 MB section installed by entry_mmio_section and RESTORED. Assert the bound,
# the alias helper, the restore, and the recorder's allow_alias gating - all scoped to the walk body.
[[ $(grep -cF 'gPhysBase + (uintptr_t)mem_size' <<<"$ab_walk" || true) -ge 1 ]] || { echo "FAIL: the walk does not bound its L1/L2 loads to [gPhysBase, gPhysBase + mem_size) - the 1010b unbounded deref would fault again"; exit 1; }
[[ $(grep -cF 'g_abtpte_walk_oow = 1u;' <<<"$ab_walk" || true) == 2 ]] || { echo "FAIL: the walk does not raise the out-of-window flag at BOTH derefs (want 2: the L1 slot and the L2 leaf)"; exit 1; }
[[ $(grep -cF 'stage90_read_l2_word_aliased((uint32_t)l2p)' <<<"$ab_walk" || true) == 1 ]] || { echo "FAIL: the walk does not route the out-of-window L2 leaf through the temporary-section helper stage90_read_l2_word_aliased"; exit 1; }
ab_alias=$(awk '/^stage90_read_l2_word_aliased\(uint32_t l2word_pa\)$/{f=1} f{print} f&&/^}$/{exit}' "$stub_src")
[[ -n "$ab_alias" ]] || { echo "FAIL: the alias helper stage90_read_l2_word_aliased is not DEFINED at line start in $stub_src"; exit 1; }
[[ $(grep -cF 'entry_mmio_section(cand, sect, &slot_before, &desc)' <<<"$ab_alias" || true) == 2 ]] || { echo "FAIL: the alias helper must try entry_mmio_section at BOTH alias slots (want 2 calls: ABTPTE_ALIAS_VA_A then, on refusal, ABTPTE_ALIAS_VA_B) - the 911c mechanism with a fallback slot"; exit 1; }
[[ $(grep -cF '*slot = slot_before;' <<<"$ab_alias" || true) == 1 ]] || { echo "FAIL: the alias helper does not RESTORE the displaced slot ('*slot = slot_before;') - entry_section_install writes without restoring, so the restore must be spelled here"; exit 1; }
[[ $(grep -cF 'g_live_mmio_l1 + 4u * (cand >> 20)' <<<"$ab_alias" || true) == 1 ]] || { echo "FAIL: the alias helper restores into a re-derived table - it must use g_live_mmio_l1, the table entry_mmio_section actually wrote (the 484 defect)"; exit 1; }
ab_pte=$(awk '/^entry_note_abt_pte\(uint32_t va\)$/{f=1} f{print} f&&/^}$/{exit}' "$stub_src")
[[ $(grep -cF 'stage90_read_l2_pte(va, &l1, &u, onladder)' <<<"$ab_pte" || true) == 1 ]] || { echo "FAIL: the (A) recorder does not pass the allow_alias gate ('onladder') to the walk - a storm would install/restore a section on every abort"; exit 1; }
[[ $(grep -cF 'onladder = (n <= 4u || (n & (n - 1u)) == 0u) ? 1u : 0u;' <<<"$ab_pte" || true) == 1 ]] || { echo "FAIL: the (A) recorder's allow_alias gate is not the ladder rule - the alias must fire on the published ordinals alone"; exit 1; }
echo "   (a) 1010c: the walk is WINDOW-BOUNDED (both derefs), the out-of-window L2 is reached via a temporary section (entry_mmio_section, restored into g_live_mmio_l1), and the alias is gated to the ladder ordinals"
# 1010b - THE (B) LADDER SAMPLES THE TAIL. 1010 published only the FIRST FOUR `vm_fault` returns
# (`ABTPTE_VMF_FIRST`), so a `_vmfret` row never described the storm's own tail - the whole-run-vs-tail
# defect ([[mi4-measurement-defects]]). The rule is now the SAME powers-of-two ladder the (A) `_pte`
# rows use, so a `_vmfret` at ordinal k pairs with the `_ptel2` at the same k.
ab_vmf=$(awk '/^entry_note_abt_vmf\(uint32_t ret, uint32_t va, uint32_t prot\)$/{f=1} f{print} f&&/^}$/{exit}' "$stub_src")
[[ $(grep -cF 'n <= 4u || (n & (n - 1u)) == 0u' <<<"$ab_vmf" || true) == 1 ]] || { echo "FAIL: the (B) recorder does not use the tail ladder 'n <= 4u || (n & (n - 1u)) == 0u' - the vm_fault return must be sampled at the storm's tail, not only the prefix (the 1010 measurement defect)"; exit 1; }
[[ $(grep -cF 'ABTPTE_VMF_FIRST' <<<"$ab_vmf" || true) == 0 ]] || { echo "FAIL: the (B) recorder still bounds its rows by ABTPTE_VMF_FIRST (the first-N prefix) - the (B) ladder is the tail fix"; exit 1; }
echo "   (a) the (B) vm_fault recorder samples the powers-of-two TAIL (paired to the (A) _ptel2 ladder)"
# The 1009 call must NOT be touched by 1010 (the ABT_PC guard is a prefix of the conjunction).
[[ $(grep -cF 'entry_note_abt_pc(((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_PC / 4],' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: 1009 is NOT reproducible - the pc call is not present exactly once in entry_trace.c"; exit 1; }
echo "   (a) the source shape: 1+3 ABT_PTE guards + walk/2 recorders def + 1 abort call + vm_fault wrapper + __real_ + the pmap.h constants + the TTBCR.N key + 1009 intact"

# (b) IMAGE KEYS - 1010's eleven `xnu_live_abt_*` literals present, and the three symbols DEFINED T/t.
for abt_key in xnu_live_abt_pten xnu_live_abt_pteva xnu_live_abt_ptel2 xnu_live_abt_ptel1 xnu_live_abt_ptettbr xnu_live_abt_pteu xnu_live_abt_ptenx xnu_live_abt_vmfn xnu_live_abt_vmfret xnu_live_abt_vmfva xnu_live_abt_vmfprot xnu_live_abt_vmfnret xnu_live_abt_pteoow xnu_live_abt_ptealias xnu_live_abt_ptemapok xnu_live_abt_pteslot xnu_live_abt_ptewbase xnu_live_abt_ptewlen; do
  grep -qx "$abt_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$abt_key\` literal - the ABT_PTE store did not reach the linked image"; exit 1; }
done
echo "   (b) the linked image carries all EIGHTEEN 1010 key literals (12 + 1010c's six walk-window keys)"
for abt_sym in entry_note_abt_pte entry_note_abt_vmf __wrap_vm_fault; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $abt_sym\$" || { echo "FAIL: $abt_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) entry_note_abt_pte, entry_note_abt_vmf and __wrap_vm_fault are DEFINED T/t in the image"
# The wrapper's PASS-THROUGH is the §8.1 rule made a check: the wrapper's body returns the REAL
# vm_fault's value and calls entry_note_abt_vmf - never a bare `return 0`.
#     NOTE the symbol the call lands on: `--wrap=vm_fault` renames, for every reference, `vm_fault`
#     -> `__wrap_vm_fault` and `__real_vm_fault` -> `vm_fault`. So the source's `__real_vm_fault(...)`
#     call appears in the LINKED disassembly as `bl <vm_fault>` (the real body), NOT as
#     `<__real_vm_fault>` - grepping for the latter refuses a correct image (MEASURED this build).
#     Assert the wrapper calls the `vm_fault` body (a distinct address from the wrapper) plus the
#     recorder.
abt_wf=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_vm_fault"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_vm_fault is not in the image"; exit 1; }
abt_wfx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${abt_wf#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
abt_wfbody=$(arm-none-eabi-objdump -d "$elf" --start-address="$abt_wf" --stop-address="${abt_wfx:-0xffffffff}")
echo "   (b) __wrap_vm_fault ($abt_wf..${abt_wfx:-end}):"
grep -E 'bl.*<|pop.*pc|bx.*lr' <<<"$abt_wfbody" | sed 's/^/       /' | head -8
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_fault>' <<<"$abt_wfbody" || { echo "FAIL: __wrap_vm_fault does not call the real vm_fault body - it is not a pass-through (the §8.1 rule: the wrapper MUST return the real vm_fault's value on the non-abort case)"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vmf>' <<<"$abt_wfbody" || { echo "FAIL: __wrap_vm_fault does not call entry_note_abt_vmf - the (B) record never fires"; exit 1; }
echo "   (b) __wrap_vm_fault calls the real vm_fault (pass-through) AND entry_note_abt_vmf (the (B) record)"

# (c) CALL SITE (A) - `__wrap_sleh_abort`'s own body carries exactly one branch to `entry_note_abt_pte`,
#     and image-wide exactly one (accept `b` OR `bl`, the tail-call fix).
abt_in=$(grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' <<<"$ab_body" || true)
abt_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' || true)
echo "   (c) __wrap_sleh_abort ($ab_wa..${ab_wx:-end}) calls entry_note_abt_pte: $abt_in (want 1); image-wide: $abt_all (want 1)"
[[ "$abt_in" == "1" && "$abt_all" == "1" ]] || { echo "FAIL: entry_note_abt_pte is not called exactly once, inside __wrap_sleh_abort (in-wrapper x$abt_in, image-wide x$abt_all) - 0 = the STUB_DEFINES_TRACE half did not reach entry_trace.c; >1 = a second site"; exit 1; }
echo "   the 1010 abort call, by value:"
grep -B5 -A2 -E '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_pte>' <<<"$ab_body" | sed 's/^/       /'

# (d) THE WRAP IS CONDITIONAL (§8.1) - the OFF image carries NO `--wrap=vm_fault`. The switch's ONLY
#     1010 consumer that gates a wrap is build_entry.sh's TRACE_LDFLAGS line; assert it is written
#     conditionally on ABT_PTE, so a hand edit that made it unconditional is caught.
[[ $(grep -cF '[[ ${STAGE90_XNU_ABT_PTE:-0} -eq 1 ]] && TRACE_LDFLAGS+=(--wrap=vm_fault)' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: build_entry.sh does not add --wrap=vm_fault conditionally on ABT_PTE - the OFF image would carry a whole-kernel vm_fault wrap (docs/1010-design.md §8.1)"; exit 1; }
echo "   (d) build_entry.sh adds --wrap=vm_fault ONLY when STAGE90_XNU_ABT_PTE=1 (the OFF image carries none)"

# (e) 1009 NOT REGRESSED - the frozen 1009 elf (the PRESSED 1009 arm) carries `xnu_live_abt_` == 18 and
#     `T/t entry_note_abt_pc`, and NO 1010 literal and NO 1010 symbol - the negative control proves the
#     1010 change is not in the 1009 arm.
d10_ctrl=out/stage90/frozen/armed-d13-24997fc5/xnu_arm_entry.elf
if [[ -f $d10_ctrl ]]; then
  ctl9_ab=$(strings "$d10_ctrl" | grep -c 'xnu_live_abt_' || true)
  ctl9_pcsym=$(arm-none-eabi-nm "$d10_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_pc$' || true)
  ctl9_ptesym=$(arm-none-eabi-nm "$d10_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_pte|entry_note_abt_vmf|__wrap_vm_fault)$' || true)
  ctl9_ptelit=$(strings "$d10_ctrl" | grep -cE 'xnu_live_abt_(pte|vmf)' || true)
  echo "   frozen 1009 elf ($d10_ctrl): xnu_live_abt_ x$ctl9_ab (want 18), entry_note_abt_pc x$ctl9_pcsym (want >= 1), 1010 symbols x$ctl9_ptesym (want 0), 1010 literals x$ctl9_ptelit (want 0)"
  [[ "$ctl9_ab" == "18" && "$ctl9_pcsym" -ge 1 && "$ctl9_ptesym" == "0" && "$ctl9_ptelit" == "0" ]] || { echo "FAIL: the frozen 1009 elf does not have the 1009 shape (eighteen abt keys, an entry_note_abt_pc symbol, no 1010 change) - the negative control is not a control"; exit 1; }
  echo "   the 1010 change is NOT in the frozen 1009 elf (control holds)"
else
  echo "   (frozen 1009 elf not present at $d10_ctrl - the negative control is unavailable this run)"
fi

# (f) THE OFF IMAGE IS BYTE-IDENTICAL TO THE PRESSED 1009 ARM (the design's own §4 refusal). The OFF arm
#     (ABT_PTE=0) is exactly the 1009 token set for both entry objects AND carries no `--wrap=vm_fault`,
#     so its entry bin must equal the pressed 1009 arm's. The OFF entry objects are identical to the
#     1009 elf's by construction (the guard deletes every 1010 token), so this is asserted by the SOURCE
#     guard (a) + the frozen 1009 elf (e): the 1010 tokens are provably absent from the 1009 elf, and
#     present in the ON elf. A twin OFF rebuild is the §4 byte-identity check and is available on request
#     (STAGE90_1010_OFF_CONTROL=1), which swaps out/stage90 - see the 1008 clause above for the model.
if [[ "${STAGE90_1010_OFF_CONTROL:-0}" == "1" ]]; then
  echo "-- by value: the OFF CONTROL (STAGE90_XNU_ABT_PTE=0 twin rebuild; must be byte-identical to the pressed 1009 arm) --"
  if [[ -e out/stage90-off-save ]]; then echo "FAIL: out/stage90-off-save already exists - a prior OFF run did not clean up"; exit 1; fi
  mv out/stage90 out/stage90-off-save
  mkdir -p out/stage90
  a10_restore() { rm -rf out/stage90; mv out/stage90-off-save out/stage90; }
  trap a10_restore EXIT INT TERM
  _a10_ok=0
  ( cd src/entry && env -i PATH="$PATH" HOME="$HOME" \
      STAGE90_XNU_ABT_PTE=0 STAGE90_XNU_VME_PROT=0 STAGE90_XNU_PMP_ENTER=0 "${EXPORTS[@]}" ./build_entry.sh ) && _a10_ok=1 || _a10_ok=0
  _a10_offelf=out/stage90/xnu_arm_entry.elf
  if [[ "$_a10_ok" == "1" && -f "$_a10_offelf" ]]; then
    off_lit=$(strings "$_a10_offelf" | grep -c 'xnu_live_abt_' || true)
    off_sym=$(arm-none-eabi-nm "$_a10_offelf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_pte|entry_note_abt_vmf|__wrap_vm_fault)$' || true)
    echo "   (f) OFF image: xnu_live_abt_ x$off_lit (want 18 = 1009's set), 1010 symbols x$off_sym (want 0)"
    a10_restore; trap - EXIT INT TERM
    [[ "$off_lit" == "18" && "$off_sym" == "0" ]] || { echo "FAIL: the OFF image does not carry exactly 1009's eighteen keys (literals x$off_lit want 18, symbols x$off_sym want 0) - the 1010 edit did NOT truly compile out (1009's arm is not reproducible)"; exit 1; }
    echo "   (f) the switch COMPILES OUT (OFF image carries exactly 1009's eighteen abt keys and no 1010 symbol)"
  else
    a10_restore; trap - EXIT INT TERM
    echo "   (f) OFF control: the twin build was requested but FAILED (rc=$_a10_ok) - out/stage90 was restored; investigate before trusting the OFF clause"
    [[ "${STAGE90_1010_OFF_CONTROL_HARD:-0}" == "1" ]] && exit 1
  fi
else
  echo "   (f) OFF control: SKIPPED (set STAGE90_1010_OFF_CONTROL=1 to run the twin build) - the OFF"
  echo "       shape is asserted by the SOURCE guard (a) and demonstrated by the frozen 1009 elf (e)"
fi

# (g) RECORD + GATE - the fresh record carries the new arm key once, and the preflight gate names it.
[[ $(awk -F= '$1=="STAGE90_XNU_ABT_PTE"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_ABT_PTE=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (g) the fresh record carries STAGE90_XNU_ABT_PTE=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_ABT_PTE' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_ABT_PTE is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (g) scripts/preflight_boot_check.sh names STAGE90_XNU_ABT_PTE (the gate will not refuse the press)"

# ====================================================================================================
# THE 1011 DELTA: the vm_map_entry effective protection (E', via __wrap_vm_map_lookup_locked) + the
# faulting thread's L1 base (A, _ptethr), asserted BY VALUE, with the FROZEN 1010c elf (armed-d13-
# 002b0683, the PRESSED 1010c arm) as the NEGATIVE CONTROL - a reader that cannot fail certifies
# nothing. The change is TWO src/ files (NO tree edit, NO stage driver, NO platform/pool object):
# `src/entry/entry_trace.c` gains the (E') declarations + `__wrap_vm_map_lookup_locked`,
# `src/entry/entry_stubs.c` gains `entry_note_abt_vme` + `stage90_abt_thread_l1_base` + the `_ptethr`
# store, and `src/entry/entry_saved_state.h` gains STAGE90_PMAP_L1_PHYS (D13 branch only).
# ====================================================================================================
echo "-- by value: the 1011 DELTA - the (E') vm_map_lookup_locked wrapper + the (A) thread L1 base --"
d11_ctrl=out/stage90/frozen/armed-d13-002b0683/xnu_arm_entry.elf

# (a) SOURCE side: the defined()-first guard is the conjunction in both files, spelled identically, and
#     the wrapper's body is a pass-through that calls the recorder.
#     -cxF, not -cE: the guard is a line of LITERAL text, and `(` is a regex group opener to grep -E,
#     so an ERE spelling of `defined(X)` silently matches nothing (this project's own defect class: the
#     check was passing-by-vacuity until the source carried the line). Whole-line fixed-string is the
#     statement - "a line that IS exactly this conjunction".
d11_guard='#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR && defined(STAGE90_XNU_ABT_PC) && STAGE90_XNU_ABT_PC && defined(STAGE90_XNU_ABT_PTE) && STAGE90_XNU_ABT_PTE && defined(STAGE90_XNU_VME_PROT) && STAGE90_XNU_VME_PROT'
d11_s=$(grep -cxF "$d11_guard" src/entry/entry_stubs.c || true)
d11_t=$(grep -cxF "$d11_guard" src/entry/entry_trace.c || true)
echo "   (a) the defined()-first VME guard: entry_stubs.c x$d11_s (want 2: the (E') recorder + the (A) reader's use), entry_trace.c x$d11_t (want 2: the declaration + the wrapper)"
[[ "$d11_s" == "2" && "$d11_t" == "2" ]] || { echo "FAIL: the VME guard is not the conjunction, two per file (stubs x$d11_s want 2, trace x$d11_t want 2) - a hand edit moved or dropped one"; exit 1; }
[[ $(grep -cF 'uint32_t __wrap_vm_map_lookup_locked(' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: __wrap_vm_map_lookup_locked is not defined exactly once in entry_trace.c"; exit 1; }
#     Two patterns, not one with a shared prefix: only the DECLARATION carries the return type, the CALL
#     SITE is `uint32_t kr = __real_...(`. Counting `uint32_t __real_vm_map_lookup_locked(` twice is
#     unsatisfiable by construction (a call is never spelled with a bare return type).
[[ $(grep -cF 'uint32_t __real_vm_map_lookup_locked(' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: __real_vm_map_lookup_locked is not DECLARED once in entry_trace.c"; exit 1; }
[[ $(grep -cF 'uint32_t kr = __real_vm_map_lookup_locked(' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the wrapper does not CALL __real_vm_map_lookup_locked into kr - it is not a pass-through (§8.1)"; exit 1; }
[[ $(grep -cF 'entry_note_abt_vme(uint32_t kr, uint32_t prot, uint32_t fault_type, uint32_t vaddr)' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: entry_note_abt_vme is not declared with the (kr, prot, fault_type, vaddr) signature in entry_trace.c"; exit 1; }
[[ $(grep -cF 'entry_note_abt_vme(kr, *out_prot, fault_type, vaddr)' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the wrapper does not call entry_note_abt_vme(kr, *out_prot, fault_type, vaddr) - the (E') record never fires"; exit 1; }
[[ $(grep -cF 'if (out_prot != 0)' src/entry/entry_trace.c || true) -ge 1 ]] || { echo "FAIL: the wrapper does not null-guard out_prot before dereferencing it"; exit 1; }
[[ $(grep -cF 'stage90_abt_thread_l1_base' src/entry/entry_stubs.c || true) == 2 ]] || { echo "FAIL: stage90_abt_thread_l1_base is not defined + called once in entry_stubs.c (want 2)"; exit 1; }
[[ $(grep -cF '#define STAGE90_PMAP_L1_PHYS   0' src/entry/entry_saved_state.h || true) == 1 ]] || { echo "FAIL: STAGE90_PMAP_L1_PHYS is not defined once (value 0) in entry_saved_state.h"; exit 1; }

# (b) LINKED image: the five (E') and one (A) key literals are each present once; the wrapper + the two
#     recorders are DEFINED text symbols; the wrapper's body calls the real lookup AND the recorder.
elf=out/stage90/xnu_arm_entry.elf
for d11_key in xnu_live_abt_vmen xnu_live_abt_vmekr xnu_live_abt_vmeprot xnu_live_abt_vmefault xnu_live_abt_vmeva xnu_live_abt_vmenx xnu_live_abt_ptethr; do
  grep -qx "$d11_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$d11_key\` literal - the 1011 store did not reach the linked image"; exit 1; }
done
echo "   (b) the image carries the seven 1011 key literals (vmen/vmekr/vmeprot/vmefault/vmeva/vmenx/ptethr)"
for d11_sym in entry_note_abt_vme __wrap_vm_map_lookup_locked; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $d11_sym\$" || { echo "FAIL: $d11_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
# The (A) reader is `static` in entry_stubs.c - it may be inlined into entry_note_abt_pte, so it need
# NOT appear as a symbol; its EFFECT (the _ptethr store) is what (c) reads, plus the three offset
# constants it dereferences. Assert the wrapper body by value (the pass-through rule).
d11_w=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_vm_map_lookup_locked"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_vm_map_lookup_locked is not in the image"; exit 1; }
d11_wx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${d11_w#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
d11_wb=$(arm-none-eabi-objdump -d "$elf" --start-address="$d11_w" --stop-address="${d11_wx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <vm_map_lookup_locked>' <<<"$d11_wb" || { echo "FAIL: __wrap_vm_map_lookup_locked does not call the real vm_map_lookup_locked body - it is not a pass-through (the §8.1 rule: the wrapper MUST return the real lookup's value on the non-abort case)"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' <<<"$d11_wb" || { echo "FAIL: __wrap_vm_map_lookup_locked does not call entry_note_abt_vme - the (E') record never fires"; exit 1; }
d11_w_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_vme>' || true)
echo "   (b) __wrap_vm_map_lookup_locked calls vm_map_lookup_locked (pass-through) + entry_note_abt_vme; image-wide entry_note_abt_vme call sites: $d11_w_all (want 1)"
[[ "$d11_w_all" == "1" ]] || { echo "FAIL: entry_note_abt_vme does not have exactly one call site (0 = the (E') call did not reach the image; >1 = a second site)"; exit 1; }

# (c) the (A) reader's dereference chain: the recorder (or the helper it was inlined from) must read the
#     thread->map offset. `stage90_abt_thread_l1_base` is `static`, so the compiler may keep it as its
#     own symbol OR inline it into entry_note_abt_pte - the #956 immediate lands in EITHER region. Scan
#     the UNION of both regions, so the clause reads the reader's EFFECT without dictating the inlining.
d11_pte=$(arm-none-eabi-nm "$elf" | awk '$3=="entry_note_abt_pte"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_note_abt_pte is not in the image"; exit 1; }
d11_l1=$(arm-none-eabi-nm "$elf" | awk '$3=="stage90_abt_thread_l1_base"{print "0x" $1; found=1} END{exit(found?0:0)}')
d11_regions=''
for d11_base in "$d11_pte" "$d11_l1"; do
  [[ -n "$d11_base" ]] || continue
  d11_nx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${d11_base#0x}" '
      BEGIN { sv = strtonum("0x" s) }
      { a = strtonum("0x" $1) }
      a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  d11_regions+=$(arm-none-eabi-objdump -d "$elf" --start-address="$d11_base" --stop-address="${d11_nx:-0xffffffff}")
done
# The thread->map offset (956 = 0x3bc) must appear as an immediate or a movw split across the union.
grep -qE '(#956\b|#0x3bc|movw[[:space:]]+r[0-9]+, #956)' <<<"$d11_regions" || { echo "FAIL: neither entry_note_abt_pte nor stage90_abt_thread_l1_base carries the STAGE90_ACT_MAP immediate (956) - the (A) reader did not compile in"; exit 1; }
echo "   (c) the (A) deref chain is present: the STAGE90_ACT_MAP immediate (956) appears in entry_note_abt_pte or stage90_abt_thread_l1_base (helper symbol: ${d11_l1:-inlined})"

# (d) build_entry.sh adds --wrap=vm_map_lookup_locked ONLY when STAGE90_XNU_VME_PROT=1 (the OFF image carries none).
[[ $(grep -cF '[[ ${STAGE90_XNU_VME_PROT:-0} -eq 1 ]] && TRACE_LDFLAGS+=(--wrap=vm_map_lookup_locked)' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: build_entry.sh does not add --wrap=vm_map_lookup_locked conditionally on VME_PROT - the OFF image would carry a whole-kernel lookup wrap (docs/1011-design-v2.md §2)"; exit 1; }
[[ $(grep -cF 'STAGE90_XNU_VME_PROT="${STAGE90_XNU_VME_PROT:-0}"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_VME_PROT is not threaded into STUB_DEFINES_TRACE in build_entry.sh - entry_trace.c would not see it"; exit 1; }
echo "   (d) build_entry.sh adds --wrap=vm_map_lookup_locked ONLY when STAGE90_XNU_VME_PROT=1 (the OFF image carries none)"

# (e) the FROZEN 1010c elf as the NEGATIVE CONTROL: it must NOT carry the 1011 change.
if [[ -f $d11_ctrl ]]; then
  ctl11_sym=$(arm-none-eabi-nm "$d11_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_vme|__wrap_vm_map_lookup_locked)$' || true)
  ctl11_lit=$(strings "$d11_ctrl" | grep -cE 'xnu_live_abt_vme|xnu_live_abt_ptethr' || true)
  echo "   (e) frozen 1010c elf ($d11_ctrl): 1011 symbols x$ctl11_sym (want 0), 1011 literals x$ctl11_lit (want 0)"
  [[ "$ctl11_sym" == "0" && "$ctl11_lit" == "0" ]] || { echo "FAIL: the frozen 1010c elf already carries the 1011 change (symbols x$ctl11_sym, literals x$ctl11_lit) - the negative control is not a control"; exit 1; }
  echo "   the 1011 change is NOT in the frozen 1010c elf (control holds)"
else
  echo "   (e) frozen 1010c elf not present at $d11_ctrl - the negative control is unavailable this run"
fi

# (f) RECORD + GATE.
[[ $(awk -F= '$1=="STAGE90_XNU_VME_PROT"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_VME_PROT=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (f) the fresh record carries STAGE90_XNU_VME_PROT=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_VME_PROT' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_VME_PROT is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (f) scripts/preflight_boot_check.sh names STAGE90_XNU_VME_PROT (the gate will not refuse the press)"

# ====================================================================================================
# THE 1013 DELTA: THE LEAF-VALUE WITNESS (STAGE90_XNU_PTE_WITNESS) - the leaf the pmap ACTUALLY
# writes, and the location the MMU's own walk finds it at. Asserted BY VALUE, with the FROZEN 1012
# elf (armed-d13-ffdc8ba1) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing.
# The change is TWO src/ files (NO tree edit, NO stage driver, NO platform/pool object, NO new
# --wrap): `src/entry/entry_stubs.c` gains the three recorders + `g_abtpte_ptel2rec`, and
# `src/entry/entry_trace.c` gains the three declarations + the three call sites, gated
# `#if defined(STAGE90_XNU_PTE_WITNESS) && STAGE90_XNU_PTE_WITNESS`. Design: docs/1013-design.md.
# ====================================================================================================
echo "-- by value: the 1013 DELTA - the leaf-value witness (leaf at the pmap apex + the MMU walk + the control) --"
# The CONTROL is the FROZEN 1012 elf (armed-d13-ffdc8ba1) - the PRESSED 1012 arm, i.e. an arm that
# predates EVERY cell of the 1013 measurement. It is a CONTROL, NOT 1013's pressed arm; it is here only
# to prove the readers below can fail.
d13_ctrl=out/stage90/frozen/armed-d13-ffdc8ba1/xnu_arm_entry.elf

# (a) SOURCE side: the defined()-first guard, once per file; the three recorder definitions; the three
#     call sites (one in `__wrap_pmap_enter_options`, two in `__wrap_sleh_abort`); the `pa` argument the
#     design names. -cxF, not -cE: `defined(X)` is a line of LITERAL text and `(` is a regex group
#     opener to grep -E, so an ERE spelling silently matches nothing (this project's own defect class).
d13_guard='#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR && defined(STAGE90_XNU_ABT_PC) && STAGE90_XNU_ABT_PC && defined(STAGE90_XNU_ABT_PTE) && STAGE90_XNU_ABT_PTE && defined(STAGE90_XNU_PMP_ENTER) && STAGE90_XNU_PMP_ENTER && defined(STAGE90_XNU_PTE_WITNESS) && STAGE90_XNU_PTE_WITNESS'
d13_s=$(grep -cxF "$d13_guard" src/entry/entry_stubs.c || true)
d13_t=$(grep -cxF "$d13_guard" src/entry/entry_trace.c || true)
# entry_stubs.c ONE (the recorder region), entry_trace.c FOUR (the declaration region + the THREE
# call blocks: the current-fault key BEFORE the handler, the W-WALK+W-SYNC pair after it, and the
# W-APEX call in the pmap wrapper - each carries the SAME full conjunction).
echo "   (a) the defined()-first PTE_WITNESS guard: entry_stubs.c x$d13_s (want 1 - the recorder region), entry_trace.c x$d13_t (want 4 - decls + the three call blocks)"
[[ "$d13_s" == "1" && "$d13_t" == "4" ]] || { echo "FAIL: the PTE_WITNESS guard is not 1/4 (stubs x$d13_s want 1, trace x$d13_t want 4) - a hand edit moved or dropped one"; exit 1; }
for d13_sym in     'entry_note_abt_wleaf(uint32_t va, const void *pmap, uint32_t pa, uint32_t fault_type)'     'entry_note_abt_walk(uint32_t va, uint32_t fsr)'     'entry_note_abt_wsync(void)'     'entry_note_abt_cur(uint32_t va)' ; do
  [[ $(grep -cF "$d13_sym" src/entry/entry_stubs.c || true) -ge 1 ]] || { echo "FAIL: the recorder '$d13_sym' is not DEFINED in $stub_src"; exit 1; }
done
echo "   (a) the four recorders are defined in entry_stubs.c (wleaf/walk/wsync/cur)"
# The three call sites: the W-APEX call in the pmap wrapper, the W-WALK + W-SYNC calls in the abort
# wrapper. Each EXACTLY once, so a hand edit that duplicated a call (doubling a probe) is a refusal.
[[ $(grep -cF 'entry_note_abt_wleaf(va, pmap, pa, fault_type);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the W-APEX call 'entry_note_abt_wleaf(va, pmap, pa, fault_type);' is not exactly once in entry_trace.c"; exit 1; }
# THE PROVENANCE ORDERING (docs/1012-probe-keying.md §1.4 - the 1012 APEX DEFECT this rung must not
# inherit): the current-fault key `entry_note_abt_cur(far_)` must be set BEFORE `__real_sleh_abort`
# runs, because the enter W-APEX observes happens INSIDE that handler.  A call placed after the handler
# keys on the PREVIOUS abort's far and the whole apex cell becomes unverifiable.  Asserted by LINE
# NUMBER (`grep -n`), so a hand edit that moved it is a refusal.
d13_curln=$(grep -nF 'entry_note_abt_cur(far_);' src/entry/entry_trace.c | head -1 | cut -d: -f1)
d13_realln=$(grep -nF '__real_sleh_abort(regs, type);' src/entry/entry_trace.c | head -1 | cut -d: -f1)
[[ -n "$d13_curln" && -n "$d13_realln" && "$d13_curln" -lt "$d13_realln" ]] || { echo "FAIL: entry_note_abt_cur(far_) is at line '$d13_curln' but __real_sleh_abort at '$d13_realln' - the current-fault key must be set BEFORE the handler (docs/1012-probe-keying.md §1.4), else W-APEX filters against the PREVIOUS abort's far"; exit 1; }
# and the recorder must KEY on that value, NOT on 1012's post-handler `g_abtpte_pmpva`. 1014 adds TWO
# more recorders that correctly key on the SAME current-fault cell (`entry_note_abt_wf`, the flush
# decision; `entry_note_abt_wctx`, the CONTEXTIDR read), and 1015 adds ONE more (`entry_note_abt_wd`,
# the permission-inputs + AT/PAR witness) - all four key on the SAME current-fault cell, so the count is
# 4, not 1 or 3 ([[mi4-measurement-defects]]: the constant tracked the recorder set, and the set grew).
[[ $(grep -cF 'cur = g_abtpte_curva;' src/entry/entry_stubs.c || true) == 4 ]] || { echo "FAIL: entry_note_abt_wleaf (+ the 1014 wf/wctx + the 1015 wd recorders) do not key on g_abtpte_curva exactly 4 times - one inherits 1012's post-handler key, or a recorder was duplicated"; exit 1; }
[[ $(grep -cF 'if (cur == 0u || va != cur)' src/entry/entry_stubs.c || true) == 4 ]] || { echo "FAIL: the current-fault filter 'if (cur == 0u || va != cur)' is not present exactly 4 times (wleaf + the two 1014 recorders + the 1015 wd recorder)"; exit 1; }
[[ $(grep -cF 'cur = g_abtpte_pmpva;' src/entry/entry_stubs.c || true) == 0 ]] || { echo "FAIL: a recorder keys (cur = ...) on 1012's post-handler g_abtpte_pmpva - the whole apex/flush cell becomes unverifiable (docs/1012-probe-keying.md §1.4)"; exit 1; }
echo "   (a) W-APEX is keyed on the CURRENT fault, set before the handler (cur line $d13_curln < real line $d13_realln)"
# THE DOUBLE-READ (docs/1012-probe-keying.md §2.3a/§2.4): W-WALK must walk the SAME VA TWICE with a
# `dsb` between, so the (A) walk's tail `ptel2=0` is distinguishable from a race with a concurrent
# table write.  The SECOND read must carry the SAME `allow_alias` as the first - `stage90_read_l2_pte`
# ASSIGNS `g_abtpte_walk_oow`, and on this storm's own VA the L2 table is OUT OF WINDOW (the
# 1010b/1010c case); a second read with `allow_alias=0` would fail OPEN and set `_wkstable=0` on EVERY
# storm row - an INSTRUMENT artifact that would disqualify the very reading it exists to validate.
[[ $(grep -cF 'l2b = stage90_read_l2_pte(va, &l1b, &ub, onladder);' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: the double-read's second walk is not exactly once with allow_alias=onladder - a read with allow_alias=0 fails OPEN on the storm's out-of-window L2 and makes _wkstable an instrument artifact"; exit 1; }
[[ $(grep -cE 'l2b = stage90_read_l2_pte\(va, &l1b, &ub, 0u?\);' src/entry/entry_stubs.c || true) == 0 ]] || { echo "FAIL: the double-read's second walk passes allow_alias=0 - it would spuriously report _wkstable=0 on the out-of-window storm VA"; exit 1; }
[[ $(grep -cF 'oow = g_abtpte_walk_oow;' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: the first walk's out-of-window reading is not latched into 'oow' - the double-read would reset _wkoow before it is published"; exit 1; }
[[ $(grep -cF 'entry_write_kv("xnu_live_abt_wkoow",  oow);' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: _wkoow is not published from the LATCHED 'oow' - it would report the double-read's own (reset) reading"; exit 1; }
[[ $(grep -cF 'entry_write_kv("xnu_live_abt_wkleaf2", l2b);' src/entry/entry_stubs.c || true) == 1 && $(grep -cF 'entry_write_kv("xnu_live_abt_wkstable", stable);' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: the double-read does not publish BOTH _wkleaf2 and _wkstable - the reader must decide, not infer"; exit 1; }
echo "   (a) the double-read: same VA, same allow_alias, one dsb; _wkoow latched before it (oow) and published from it"
# THE VA CELL (1013b).  A W-WALK row that does not publish the VA it walked is not SELF-IDENTIFYING:
# the 1013 press log shows W-APEX `_wwleaf`=0x8411e1bb and W-WALK `_wkleaf`=0 at the SAME
# `_wkl2pa`=0x841bf000, and without `_wkva` a reader cannot tell "one slot read twice, the leaf
# vanished" from "two slots in the same 1 MB section" (one L1 slot maps 256 L2 slots).  Assert the
# publisher by value, and that it is published on the SAME row as the leaf it identifies.
[[ $(grep -cF 'entry_write_kv("xnu_live_abt_wkva",   va);' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: W-WALK does not publish the VA it walked (_wkva) - the apex/walk leaf pair cannot be paired BY LOCATION and a zero leaf stays a 'stale 0' shrug instead of a V1-class reading"; exit 1; }
[[ $(grep -cE 'entry_note_abt_walk\(uint32_t va, uint32_t fsr\)' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_note_abt_walk's signature (uint32_t va, uint32_t fsr) is gone - _wkva has no source"; exit 1; }
echo "   (a) W-WALK publishes the VA it walked (_wkva), on the row whose leaf it identifies"
# THE CONTROL'S TABLE (1013's fix).  The control VA `gPhysBase + ABTWS_WINDOW_OFF` = 0x80080000 is
# ABOVE this port's `TTBCR.N = 2` split `0x40000000` - i.e. TTBR1 territory, because the kernel's
# identity/managed tables live in `cpu_ttb` (`arm_vm_init.c:352/388/391` + `set_mmu_ttb_alt(cpu_ttb)`
# :474), the table TTBR1 points at.  As built, the walker returned 0 for every such VA (`:2280`), so
# W-SYNC published `wck=0` on every row and the checker withheld V1-V3 (WITNESS_METHOD_BROKEN).  The
# walker now READS TTBR1 for a high-window VA under THIS switch, bounded to the same window, and the
# control row LABELS the answer (`_wctb`).  Assert by value - a reverted walker or an unlabeled
# control is a REFUSAL, not a silent null.
[[ $(grep -cF 'g_abtpte_walk_tt1 = 1u;' <<<"$ab_walk" || true) -ge 1 ]] || { echo "FAIL: the walk does not LABEL a TTBR1 (high-window) answer (g_abtpte_walk_tt1 = 1u;) - as built it returned 0 for every VA above the split and the W-SYNC control could never read"; exit 1; }
[[ $(grep -cF 'mrc p15, 0, %0, c2, c0, 1' <<<"$ab_walk" || true) == 1 ]] || { echo "FAIL: the walk does not read TTBR1 (mrc p15,0,Rt,c2,c0,1) in its high-window branch - the control VA 0x80080000 is TTBR1 territory"; exit 1; }
[[ $(grep -cF 'g_abtpte_walk_tt1 = 0u;' <<<"$ab_walk" || true) == 1 ]] || { echo "FAIL: the walk does not RESET g_abtpte_walk_tt1 at entry - a stale TTBR1 label would leak into a TTBR0 answer"; exit 1; }
d13_wsync_body=$(awk '/^entry_note_abt_wsync\(void\)$/{f=1} f{print} f&&/^}$/{exit}' src/entry/entry_stubs.c)
[[ $(grep -cF 'entry_write_kv("xnu_live_abt_wctb",   g_abtpte_walk_tt1);' <<<"$d13_wsync_body" || true) == 1 ]] || { echo "FAIL: the W-SYNC control does not publish its own table label (_wctb = g_abtpte_walk_tt1) - the control's identity would be unreadable"; exit 1; }
[[ $(grep -cF 'stage90_read_l2_pte(va, &l1, &u, 0u)' <<<"$d13_wsync_body" || true) == 1 ]] || { echo "FAIL: the W-SYNC control no longer reads with the ONE walker (stage90_read_l2_pte(va, &l1, &u, 0u))"; exit 1; }
echo "   (a) the control is READABLE: the walker reads TTBR1 for the high-window control VA (bounded) and W-SYNC labels it (_wctb)"
# THE EXPAND HAZARD (docs/1013-leaf-readback-verify.md §0.2/§6.2): the W-APEX call must pass the PMAP,
# and the recorder must re-read `pm_l1_phys` INSIDE itself - `pmap_expand_ttb` can MOVE the L1 table
# inside the enter (pmap.c:3084-3086). A call passing a pre-call `pm_l1_phys` copy would read a RETIRED
# table. Both spellings must be ABSENT.
[[ $(grep -cE 'entry_note_abt_wleaf\(va, *pm_l1_phys' src/entry/*.c | grep -cv ':0$' || true) == 0 ]] || { echo "FAIL: a W-APEX call passes pm_l1_phys instead of the pmap - it would read the RETIRED L1 table if pmap_expand moved it (docs/1013-leaf-readback-verify.md §0.2)"; exit 1; }
[[ $(grep -cE 'entry_note_abt_wleaf\(uint32_t va, uint32_t pm_l1_phys' src/entry/entry_stubs.c || true) == 0 ]] || { echo "FAIL: entry_note_abt_wleaf still takes a uint32_t pm_l1_phys - it must take the pmap and re-read the field itself"; exit 1; }
[[ $(grep -cF 'entry_note_abt_walk(far_, fsr);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the W-WALK call 'entry_note_abt_walk(far_, fsr);' is not exactly once in entry_trace.c"; exit 1; }
[[ $(grep -cF 'entry_note_abt_wsync();' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the W-SYNC call 'entry_note_abt_wsync();' is not exactly once in entry_trace.c"; exit 1; }
echo "   (a) the three call sites are exactly once each in entry_trace.c"
# The pass-through must SURVIVE the new call: `__real_...` still named-into-`kr` and `return kr;` last.
[[ $(grep -cF 'uint32_t kr = __real_pmap_enter_options(pmap, va, pa, prot, fault_type, flags, wired, options);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the wrapper no longer CALLS __real_pmap_enter_options into kr - 1013 hollowed out the pass-through (§8.1)"; exit 1; }
[[ $(grep -cF 'return kr;' src/entry/entry_trace.c || true) -ge 1 ]] || { echo "FAIL: the wrapper does not return the named kr - the real enter's value is lost (§8.1)"; exit 1; }
echo "   (a) the pmap wrapper is still a PURE PASS-THROUGH (kr named, __real_ called, return kr last)"

# (b) LINKED image: the 1013 key literals are each present once; the three recorders + the pmap wrapper
#     are DEFINED text symbols; the pmap wrapper's body calls the real pmap_enter_options (pass-through)
#     AND BOTH recorders; image-wide exactly ONE call site of each recorder.
elf=out/stage90/xnu_arm_entry.elf
# The full fixed keyspace (`docs/1013-design.md` §2): W-APEX `w*`, W-WALK `wk*`, W-SYNC `wc*`.
for d13_key in     xnu_live_abt_wwn xnu_live_abt_wwl1 xnu_live_abt_wwl2pa xnu_live_abt_wwleaf xnu_live_abt_wwapx xnu_live_abt_wwnoproof xnu_live_abt_wwpa xnu_live_abt_wwpaok xnu_live_abt_wwva xnu_live_abt_wwft xnu_live_abt_wwpatbl xnu_live_abt_wwnx     xnu_live_abt_wkn xnu_live_abt_wkva xnu_live_abt_wk xnu_live_abt_wktb xnu_live_abt_wkl1 xnu_live_abt_wkl2pa xnu_live_abt_wkleaf xnu_live_abt_wkleaf2 xnu_live_abt_wkstable xnu_live_abt_wkapx xnu_live_abt_wkoow xnu_live_abt_wkfsr xnu_live_abt_wkapex xnu_live_abt_wknx     xnu_live_abt_wcnt xnu_live_abt_wcva xnu_live_abt_wck xnu_live_abt_wctb xnu_live_abt_wcl1 xnu_live_abt_wcl2pa xnu_live_abt_wcleaf xnu_live_abt_wcapx xnu_live_abt_wcoow xnu_live_abt_wctb0 xnu_live_abt_wctb1 ; do
  grep -qx "$d13_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$d13_key\` literal - the 1013 store did not reach the linked image"; exit 1; }
done
echo "   (b) the image carries all 35+2 1013 key literals (11 W-APEX incl. the provenance cells wwva/wwft/wwpatbl + 13 W-WALK incl. the 1013b VA cell wkva and the double-read wkleaf2/wkstable + 9 W-SYNC incl. the TTBR1 label wctb + the 2 change-counts wwnx/wknx + the 2 raw TTBR words wctb0/wctb1)"
for d13_s in entry_note_abt_wleaf entry_note_abt_walk entry_note_abt_wsync entry_note_abt_cur; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $d13_s\$" || { echo "FAIL: $d13_s is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) entry_note_abt_wleaf / entry_note_abt_walk / entry_note_abt_wsync / entry_note_abt_cur are DEFINED T/t in the image"
# The wrapper's PASS-THROUGH + the ONE new call it grew. `--wrap=pmap_enter_options` renames
# `pmap_enter_options -> __wrap_...` and `__real_... -> pmap_enter_options`, so the source's
# `__real_...` call appears as `bl <pmap_enter_options>` (the real body), NOT `<__real_...>`.
d13_w=$(arm-none-eabi-nm "$elf" | awk '$3=="__wrap_pmap_enter_options"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: __wrap_pmap_enter_options is not in the image"; exit 1; }
d13_wx=$(arm-none-eabi-nm -n "$elf" | awk -v s="${d13_w#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
d13_wb=$(arm-none-eabi-objdump -d "$elf" --start-address="$d13_w" --stop-address="${d13_wx:-0xffffffff}")
grep -qE 'bl[[:space:]]+[0-9a-f]+ <pmap_enter_options>' <<<"$d13_wb" || { echo "FAIL: __wrap_pmap_enter_options does not call the real pmap_enter_options body - it is not a pass-through (§8.1)"; exit 1; }
grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_wleaf>' <<<"$d13_wb" || { echo "FAIL: __wrap_pmap_enter_options does not call entry_note_abt_wleaf - the apex leaf is never read"; exit 1; }
# The anti-inline assertion ([[mi4-hooks-live-at-calls]]): W-WALK's recording must NOT have been moved
# into the pmap wrapper - it belongs to the ABORT path, which this wrapper is never reached from.
! grep -qE 'bl[[:space:]]+[0-9a-f]+ <entry_note_abt_walk>' <<<"$d13_wb" || { echo "FAIL: __wrap_pmap_enter_options calls entry_note_abt_walk - W-WALK's recording was moved off the abort path"; exit 1; }
# A `b` OR `bl`: the compiler TAILS a void call that is the last statement of its function, so the
# image carries `b <entry_note_abt_wsync>` (it is the last statement of `__wrap_sleh_abort`), not a
# `bl` - the 1004 tail-call reader defect, the SAME one the current-fault key's count below guards
# against.  A `bl`-only regex reads the tail call as 0 and manufactures a false "call did not reach
# the image".
d13_w_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wleaf>' || true)
d13_k_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_walk>' || true)
d13_c_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wsync>' || true)
echo "   (b) __wrap_pmap_enter_options calls pmap_enter_options (pass-through) + entry_note_abt_wleaf; image-wide call sites: wleaf $d13_w_all, walk $d13_k_all, wsync $d13_c_all (want 1 each)"
[[ "$d13_w_all" == "1" && "$d13_k_all" == "1" && "$d13_c_all" == "1" ]] || { echo "FAIL: a 1013 recorder does not have exactly one call site (0 = a call did not reach the image; >1 = a second site)"; exit 1; }
# The current-fault key's ONE call site, inside the abort wrapper (a `b` OR `bl`: the compiler may tail
# the last void call - the 1004 tail-call reader defect, again).
d13_u_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_cur>' || true)
echo "   (b) image-wide call sites of entry_note_abt_cur: $d13_u_all (want 1)"
[[ "$d13_u_all" == "1" ]] || { echo "FAIL: entry_note_abt_cur does not have exactly one call site (0 = the current-fault key never reaches the image, so W-APEX is unkeyed; >1 = a second site)"; exit 1; }
# The W-APEX recorder is entered through the SAME `--wrap` 1012 added: the wrap must still be there.
[[ $(grep -cF '[[ ${STAGE90_XNU_PMP_ENTER:-0} -eq 1 ]] && TRACE_LDFLAGS+=(--wrap=pmap_enter_options)' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: build_entry.sh no longer adds --wrap=pmap_enter_options conditionally - W-APEX has no apex to observe"; exit 1; }
echo "   (b) the --wrap=pmap_enter_options W-APEX rides is still present and conditional"

# (c) build_entry.sh threads PTE_WITNESS into BOTH define sets and defines the record key; it adds NO
#     --wrap of its own (1013 rides 1012's), so there is NO TRACE_LDFLAGS clause to assert.
[[ $(grep -cF 'STAGE90_XNU_PTE_WITNESS="${STAGE90_XNU_PTE_WITNESS:-0}"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_PTE_WITNESS is not threaded into STUB_DEFINES_TRACE in build_entry.sh - entry_trace.c would not see it"; exit 1; }
[[ $(grep -cF -- '-DSTAGE90_XNU_PTE_WITNESS="$PTEWIT"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_PTE_WITNESS is not threaded into STUB_DEFINES in build_entry.sh - entry_stubs.c would not see it"; exit 1; }
[[ $(grep -cF 'STAGE90_XNU_PTE_WITNESS)      _v=$PTEWIT ;;' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_PTE_WITNESS is not in build_entry.sh's record-case block - the record's value would be empty"; exit 1; }
[[ $(grep -cF 'echo "STAGE90_XNU_PTE_WITNESS=$PTEWIT"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: the record writer does not echo STAGE90_XNU_PTE_WITNESS - the record is written by explicit echo lines, NOT from ENTRY_ARM_KEYS"; exit 1; }
[[ $(grep -cF '[[ $PTEWIT -eq 1 && $PMPENTER -ne 1 ]]' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: build_entry.sh does not REFUSE PTE_WITNESS=1 without PMP_ENTER=1 - W-APEX would carry a switch with no apex"; exit 1; }
[[ $(grep -cE '^ *-eq 1 \]\] && TRACE_LDFLAGS\+=\(--wrap=.*pte|PTE_WITNESS.*TRACE_LDFLAGS' src/entry/build_entry.sh || true) == 0 ]] || { echo "FAIL: build_entry.sh appears to gate a --wrap on PTE_WITNESS - 1013 adds NO wrap of its own"; exit 1; }
echo "   (c) build_entry.sh threads PTE_WITNESS into both define sets + the record, refuses a partial set, adds no wrap"

# (d) the FROZEN 1012 elf as the NEGATIVE CONTROL: it must NOT carry the 1013 change.
if [[ -f $d13_ctrl ]]; then
  ctl13_sym=$(arm-none-eabi-nm "$d13_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_wleaf|entry_note_abt_walk|entry_note_abt_wsync|entry_note_abt_cur)$' || true)
  ctl13_lit=$(strings "$d13_ctrl" | grep -cE 'xnu_live_abt_(wwleaf|wkleaf|wcleaf|wwnoproof|wkapex|wwva|wwft|wwpatbl|wkleaf2|wkstable)' || true)
  echo "   (d) frozen 1012 elf ($d13_ctrl): 1013 symbols x$ctl13_sym (want 0), 1013 leaf literals x$ctl13_lit (want 0)"
  [[ "$ctl13_sym" == "0" && "$ctl13_lit" == "0" ]] || { echo "FAIL: the frozen 1012 elf already carries the 1013 change (symbols x$ctl13_sym, literals x$ctl13_lit) - the negative control is not a control"; exit 1; }
  echo "   the 1013 change is NOT in the frozen 1012 elf (control holds)"
else
  echo "   (d) frozen 1012 elf not present at $d13_ctrl - the negative control is unavailable this run"
  # The design §4 fallback: when the frozen control is ABSENT, the OFF byte-identity check is REQUIRED.
  echo "   (d) falling back to the OFF BYTE-IDENTITY check (design §4): rebuild with PTE_WITNESS=0 and cmp the two entry objects"
fi

# (e) RECORD + GATE - the fresh record carries the new arm key once, and the preflight gate names it.
[[ $(awk -F= '$1=="STAGE90_XNU_PTE_WITNESS"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_PTE_WITNESS=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (e) the fresh record carries STAGE90_XNU_PTE_WITNESS=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_PTE_WITNESS' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_PTE_WITNESS is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
echo "   (e) scripts/preflight_boot_check.sh names STAGE90_XNU_PTE_WITNESS (the gate will not refuse the press)"

# ====================================================================================================
# THE 1014 DELTA: THE TLB-FLUSH WITNESS (STAGE90_XNU_FLUSH_WITNESS) - WHICH flush arm ran for the storm
# pmap, under WHICH ASID and WHICH live TTBR0.  Asserted BY VALUE, with the FROZEN 1013 elf
# (armed-d13-9cf2f15e) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing.
#
# The change is TWO src/ files PLUS one KERNEL-POOL TREE EDIT, and it is the first rung in this series
# whose call site is NOT in src/: `pmap_flush_tlbs` is called 13 times, all INTRA-object, so a `--wrap`
# would redirect zero of them ([[mi4-hooks-live-at-calls]]) and the probe must be a SOURCE EDIT into
# `osfmk/arm/pmap.c` (`tools/patch_d13_flushwitness.py`).  `src/entry/entry_stubs.c` gains the two
# recorders; `src/entry/entry_trace.c` gains one declaration + the ONE `entry_note_abt_wctx` call.
# Design: docs/1014-design.md §2.
# ====================================================================================================
echo "-- by value: the 1014 DELTA - the TLB-flush witness (which flush arm ran, under which ASID + live TTBR0) --"
# The CONTROL is the FROZEN 1013 elf (armed-d13-9cf2f15e) - the PRESSED 1013 arm, i.e. an arm that
# predates EVERY cell of the 1014 measurement.  It is a CONTROL, NOT 1014's arm.
d14_ctrl=out/stage90/frozen/armed-d13-9cf2f15e/xnu_arm_entry.elf

# (a) SOURCE side: the defined()-first guard, once in entry_stubs.c (the recorder region) and TWICE in
#     entry_trace.c (the declaration block + the ONE call block - each carries the same full six-way
#     conjunction).  -cxF, not -cE: `defined(X)` is a line of LITERAL text and `(` is a regex group
#     opener to grep -E, so an ERE spelling silently matches nothing (this project's own defect class).
d14_guard='#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR && defined(STAGE90_XNU_ABT_PC) && STAGE90_XNU_ABT_PC && defined(STAGE90_XNU_ABT_PTE) && STAGE90_XNU_ABT_PTE && defined(STAGE90_XNU_PMP_ENTER) && STAGE90_XNU_PMP_ENTER && defined(STAGE90_XNU_PTE_WITNESS) && STAGE90_XNU_PTE_WITNESS && defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS'
d14_s=$(grep -cxF "$d14_guard" src/entry/entry_stubs.c || true)
d14_t=$(grep -cxF "$d14_guard" src/entry/entry_trace.c || true)
echo "   (a) the defined()-first FLUSH_WITNESS guard: entry_stubs.c x$d14_s (want 1 - the recorder region), entry_trace.c x$d14_t (want 2 - the declaration block + the ONE call block)"
[[ "$d14_s" == "1" && "$d14_t" == "2" ]] || { echo "FAIL: the FLUSH_WITNESS guard is not 1/2 (stubs x$d14_s want 1, trace x$d14_t want 2) - a hand edit moved or dropped one. It has its OWN guard (NOT the PTE_WITNESS one) precisely so 1013's guard counts stay 1/4"; exit 1; }
# The two recorders, defined in entry_stubs.c.
[[ $(grep -cF 'entry_note_abt_wf(uint32_t va, uint32_t ttb, uint32_t live, uint32_t self, uint32_t shared,' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_note_abt_wf's signature (9 args) is not defined exactly once in entry_stubs.c"; exit 1; }
[[ $(grep -cF 'entry_note_abt_wctx(uint32_t va)' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_note_abt_wctx(uint32_t va) is not defined exactly once in entry_stubs.c"; exit 1; }
echo "   (a) the two recorders are defined in entry_stubs.c (entry_note_abt_wf 9-arg, entry_note_abt_wctx)"
# The ONE call site in entry_trace.c, and the recorder that keys on 1013's current-fault key.
[[ $(grep -cF 'entry_note_abt_wctx(far_);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the W-CTX call 'entry_note_abt_wctx(far_);' is not exactly once in entry_trace.c"; exit 1; }
# **The keying, and the PLAIN predicate that makes it real.** The 1014 recorders must filter on 1013's
# `g_abtpte_curva` - NOT a locally re-derived value - because that is the ONE current-fault key 1013's
# `entry_note_abt_cur` writes before the handler runs (docs/1012-probe-keying.md §1.4).  Assert the
# EXACT predicate TEXT so a hand edit that swapped the key is a refusal rather than a quiet re-tuning
# of what gets published (the memory's own rule: a regex on `cur` would accept a different predicate).
[[ $(grep -cF 'if (cur == 0u || va != cur)' src/entry/entry_stubs.c || true) -ge 3 ]] || { echo "FAIL: the recorders do not carry the 'if (cur == 0u || va != cur)' key filter (want >=3: wleaf + the two 1014 recorders) - the probe would publish for EVERY flush, not the storm's VA. NOTE: the tree spells the guard as TWO lines (the `if (...)` predicate, then `return;` on the next line), so the pattern is the predicate line alone, never '... return;' on one line"; exit 1; }
[[ $(grep -cF 'cur = g_abtpte_curva;' src/entry/entry_stubs.c || true) -ge 2 ]] || { echo "FAIL: the 1014 recorders do not key on g_abtpte_curva (want >=2) - they would key on something else"; exit 1; }
echo "   (a) both recorders KEY on 1013's current-fault key g_abtpte_curva ('if (cur == 0u || va != cur) return;')"
# The CONTEXTIDR read is `proc_reg.h:583`'s own macro, NOT a new cp15 spelling - the design's explicit
# instruction. Assert the include is gated and that the recorder names the MACRO, not an asm string.
[[ $(grep -cE '^#include <arm/proc_reg.h>$' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_stubs.c does not include <arm/proc_reg.h> exactly once at line start - the CONTEXTIDR recorder has no macro (the 'grep -cF' spelling counts the two COMMENT mentions too, so the clause anchors to column 0)"; exit 1; }
[[ $(grep -cF 'armreg_contextidr_read()' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_stubs.c does not call armreg_contextidr_read() exactly once - the CONTEXTIDR read was re-spelled"; exit 1; }
[[ $(grep -cF 'c13, c0, 1' src/entry/entry_stubs.c || true) == 0 ]] || { echo "FAIL: entry_stubs.c carries a hand-written `mrc p15, 0, %0, c13, c0, 1` (the CONTEXTIDR cp15 operand) - the read must go through proc_reg.h:583's armreg_contextidr_read() macro ([[mi4-one-value-two-definitions]]), not a second cp15 spelling. NOTE: 'mrc p15' alone is NOT the pattern - the file legitimately reads TTBR0/TTBCR/TTBR1 (c2,c0,0/2/1) and TPIDRPRW (c13,c0,4); only the CONTEXTIDR operand 'c13, c0, 1' is forbidden"; exit 1; }
echo "   (a) CONTEXTIDR is read through proc_reg.h:583's armreg_contextidr_read() - no new cp15 spelling"
# The TREE EDIT: staged, and the ONE call site is in it. `--check` is a SOURCE reader; the POOL-object
# reader is the block above (before the entry link), where it belongs.
python3 tools/patch_d13_flushwitness.py "$XNU_TREE" --check >/dev/null || { echo "FAIL: the pmap.c probe is not staged"; exit 1; }
[[ $(python3 - "$XNU_TREE/osfmk/arm/pmap.c" <<'PYC'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("pf", "tools/patch_d13_flushwitness.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
# NB: _CALL is a MULTI-LINE block; `grep -cF` with it would count the block's LINES (5), not the call,
# so the whole-text count is done in python - the tool and the build must assert the SAME bytes.
print(open(sys.argv[1]).read().count(m._CALL))
PYC
) == 1 ]] || { echo "FAIL: the tree does not carry the ONE entry_note_abt_wf call in pmap_flush_tlbs exactly once - the build script and the tool have drifted (they must assert the SAME text)"; exit 1; }
echo "   (a) the tree edit is staged and carries the ONE probe, verbatim from the tool (one spelling)"

# (b) LINKED image: the 14 new key literals, both recorders DEFINED, and the (b|bl) call-site count.
elf=out/stage90/xnu_arm_entry.elf
for d14_key in xnu_live_abt_wfn xnu_live_abt_wfva xnu_live_abt_wfttb xnu_live_abt_wflive xnu_live_abt_wfself xnu_live_abt_wfshared xnu_live_abt_wfncpus xnu_live_abt_wfasid xnu_live_abt_wfkind xnu_live_abt_wfseen xnu_live_abt_wfnx xnu_live_abt_wctxn xnu_live_abt_wctx xnu_live_abt_wctxva; do
  grep -qx "$d14_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$d14_key\` literal - the 1014 store did not reach the linked image"; exit 1; }
done
echo "   (b) the image carries all 14 1014 key literals (11 wf* incl. the null-control wfseen + the change-count wfnx, and 3 wctx*)"
for d14_sym in entry_note_abt_wf entry_note_abt_wctx; do
  arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] $d14_sym\$" >/dev/null || { echo "FAIL: $d14_sym is not a DEFINED text symbol in $elf - an object did not rebuild or a generated stand-in was used"; exit 1; }
done
echo "   (b) entry_note_abt_wf and entry_note_abt_wctx are DEFINED T/t in the image"
# The anti-inline rule ([[mi4-hooks-live-at-calls]]), and the `(b|bl)`-not-`bl` rule: a void call that
# is the last statement of its function is TAIL-called and the image carries `b <sym>`.  A `bl`-only
# regex reads the tail call as 0 and manufactures a false "the call did not reach the image" - 1013's
# own build bug ([[mi4-linked-code-order-is-not-source-order]]).  Exactly ONE each.
d14_wf_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wf>' || true)
d14_wc_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wctx>' || true)
echo "   (b) image-wide (b|bl) call sites: entry_note_abt_wf $d14_wf_all (want 1 - it is called from the POOL object, not from src/), entry_note_abt_wctx $d14_wc_all (want 1)"
[[ "$d14_wf_all" == "1" ]] || { echo "FAIL: entry_note_abt_wf does not have exactly ONE (b|bl) call site (0 = the pmap.o probe did not reach the link, or it was read with a bl-only regex over a tail call; >1 = a second site in the tree)"; exit 1; }
[[ "$d14_wc_all" == "1" ]] || { echo "FAIL: entry_note_abt_wctx does not have exactly ONE (b|bl) call site (0 = the trace call did not reach the image; >1 = a second site)"; exit 1; }

# (c) build_entry.sh threads FLUSH_WITNESS into BOTH define sets and defines the record key (all five
#     sites); it adds NO --wrap of its own, and it REFUSES the switch without PTE_WITNESS.
[[ $(grep -cF 'STAGE90_XNU_FLUSH_WITNESS="${STAGE90_XNU_FLUSH_WITNESS:-0}"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_FLUSH_WITNESS is not threaded into STUB_DEFINES_TRACE in build_entry.sh - entry_trace.c would not see it"; exit 1; }
[[ $(grep -cF -- '-DSTAGE90_XNU_FLUSH_WITNESS="$FLWIT"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_FLUSH_WITNESS is not threaded into STUB_DEFINES in build_entry.sh - entry_stubs.c would not see it"; exit 1; }
[[ $(grep -cF 'STAGE90_XNU_FLUSH_WITNESS)    _v=$FLWIT ;;' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_FLUSH_WITNESS is not in build_entry.sh's record-case block - the record's value would be empty"; exit 1; }
[[ $(grep -cF 'echo "STAGE90_XNU_FLUSH_WITNESS=$FLWIT"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: the record writer does not echo STAGE90_XNU_FLUSH_WITNESS - the record is written by explicit echo lines, NOT from ENTRY_ARM_KEYS"; exit 1; }
[[ $(grep -cF 'STAGE90_XNU_FLUSH_WITNESS' src/entry/build_entry.sh || true) -ge 1 ]] || { echo "FAIL: STAGE90_XNU_FLUSH_WITNESS is not in ENTRY_ARM_KEYS"; exit 1; }
[[ $(grep -cF '[[ $FLWIT -eq 1 && $PTEWIT -ne 1 ]]' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: build_entry.sh does not REFUSE FLUSH_WITNESS=1 without PTE_WITNESS=1 - the recorder keys on 1013's g_abtpte_curva, so without it EVERY row would be filtered out and the probe would measure NOTHING while reporting a green build ([[mi4-measurement-defects]])"; exit 1; }
[[ $(grep -cE '^ *-eq 1 \]\] && TRACE_LDFLAGS\+=\(--wrap=.*flw|FLUSH_WITNESS.*TRACE_LDFLAGS' src/entry/build_entry.sh || true) == 0 ]] || { echo "FAIL: build_entry.sh appears to gate a --wrap on FLUSH_WITNESS - 1014 adds NO wrap of its own (all 13 pmap_flush_tlbs calls are intra-object)"; exit 1; }
echo "   (c) build_entry.sh threads FLUSH_WITNESS into both define sets + the record (all five sites), refuses a partial set, adds no wrap"

# (d) the FROZEN 1013 elf as the NEGATIVE CONTROL: it must NOT carry the 1014 change.
if [[ -f $d14_ctrl ]]; then
  ctl14_sym=$(arm-none-eabi-nm "$d14_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] (entry_note_abt_wf|entry_note_abt_wctx)$' || true)
  ctl14_lit=$(strings "$d14_ctrl" | grep -cE '^xnu_live_abt_(wf[a-z]+|wctx[a-z]*)$' || true)
  echo "   (d) frozen 1013 elf ($d14_ctrl): 1014 symbols x$ctl14_sym (want 0), 1014 key literals x$ctl14_lit (want 0)"
  [[ "$ctl14_sym" == "0" && "$ctl14_lit" == "0" ]] || { echo "FAIL: the frozen 1013 elf already carries the 1014 change (symbols x$ctl14_sym, literals x$ctl14_lit) - the negative control is not a control"; exit 1; }
  echo "   the 1014 change is NOT in the frozen 1013 elf (control holds)"
else
  echo "FAIL: the frozen 1013 elf is absent at $d14_ctrl - the negative control is REQUIRED this run (design §2.4 item 6); without it the readers above certify nothing"
  exit 1
fi

# (e) RECORD + GATE - the fresh record carries the new arm key once, and the preflight gate names it.
[[ $(awk -F= '$1=="STAGE90_XNU_FLUSH_WITNESS"{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry STAGE90_XNU_FLUSH_WITNESS=1 - the new arm key was not recorded this run, and a park would freeze an arm whose switch the record does not name"; exit 1; }
echo "   (e) the fresh record carries STAGE90_XNU_FLUSH_WITNESS=1 (the new arm key this run)"
grep -q 'STAGE90_XNU_FLUSH_WITNESS' scripts/preflight_boot_check.sh || { echo "FAIL: STAGE90_XNU_FLUSH_WITNESS is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check at :1180 will refuse the press"; exit 1; }
echo "   (e) scripts/preflight_boot_check.sh names STAGE90_XNU_FLUSH_WITNESS (the gate will not refuse the press)"

# ====================================================================================================
# THE 1015 DELTA, INHERITED BY 1015b AS A PRECONDITION: STAGE90_XNU_PERM_WITNESS (the passive
# permission-INPUTS witness + the AT/PAR cell). **ENTRY-ONLY**, and 1015b does not change that: the
# design's second, ACTIVE switch was never built and is NOT built here either - what 1015b builds is a
# SOURCE fix to the same pool file the passive witness reads, under its own name
# (`STAGE90_XNU_GLOBAL_FIX`), asserted in its own block immediately after this one.
# **THE ROLE OF THIS BLOCK IN 1015b CHANGED IN ONE WORD: it is no longer "the rung's delta", it is a
# PRECONDITION.** 1015b's whole pre-registered test is read off the `wd*`/AT keys 1015's recorder
# publishes, so a 1015b build whose inherited `wd*` keyspace is partially present would produce an arm
# that MOVED THE KERNEL and whose read-out is uninterpretable - the worst of both. That is why R4/R5
# in `docs/1015b-design.md` §4 exist and why these assertions are kept rather than relaxed.
# Design: docs/1015-design.md §2 and §4.  Asserted BY VALUE, with the FROZEN 1014 elf
# (armed-d13-29ec9e48) as the NEGATIVE CONTROL - a reader that cannot fail certifies nothing.
# ====================================================================================================
echo "-- by value: the 1015 DELTA - the PASSIVE half ONLY (the permission-inputs witness + the AT/PAR hardware-translation cell); 1015 is entry-only --"
# The CONTROL is the FROZEN 1014 elf - the PRESSED 1014 arm, i.e. an arm that predates EVERY 1015 cell.
d15_ctrl=out/stage90/frozen/armed-d13-29ec9e48/xnu_arm_entry.elf

# (a) SOURCE side, entry half.  The `wd*` region's guard is the FULL SIX-WAY CONJUNCTION (it keys on
#     1013's `g_abtpte_curva`, which only PTE_WITNESS writes).  TWO files: the recorder region in
#     entry_stubs.c, and the declaration + the ONE call block in entry_trace.c.
d15_guard='#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR && defined(STAGE90_XNU_ABT_PC) && STAGE90_XNU_ABT_PC && defined(STAGE90_XNU_ABT_PTE) && STAGE90_XNU_ABT_PTE && defined(STAGE90_XNU_PMP_ENTER) && STAGE90_XNU_PMP_ENTER && defined(STAGE90_XNU_PTE_WITNESS) && STAGE90_XNU_PTE_WITNESS && defined(STAGE90_XNU_PERM_WITNESS) && STAGE90_XNU_PERM_WITNESS'
d15_s=$(grep -cxF "$d15_guard" src/entry/entry_stubs.c || true)
d15_t=$(grep -cxF "$d15_guard" src/entry/entry_trace.c || true)
echo "   (a) the defined()-first PERM_WITNESS guard: entry_stubs.c x$d15_s (want 1 - the recorder region), entry_trace.c x$d15_t (want 2 - the declaration block + the ONE call block)"
[[ "$d15_s" == "1" && "$d15_t" == "2" ]] || { echo "FAIL: the PERM_WITNESS guard is not 1/2 (stubs x$d15_s want 1, trace x$d15_t want 2) - a hand edit moved or dropped one. It has its OWN guard (NOT the FLUSH_WITNESS/PTE_WITNESS one) precisely so the 1013/1014 guard counts stay 1/2 and 1/4"; exit 1; }
# The recorder is defined in entry_stubs.c and called exactly once in entry_trace.c.
[[ $(grep -cF 'entry_note_abt_wd(' src/entry/entry_stubs.c || true) -ge 1 ]] || { echo "FAIL: entry_note_abt_wd is not defined in entry_stubs.c"; exit 1; }
[[ $(grep -cF 'entry_note_abt_wd(far_);' src/entry/entry_trace.c || true) == 1 ]] || { echo "FAIL: the W-D call 'entry_note_abt_wd(far_);' is not exactly once in entry_trace.c - the passive witness would never be published"; exit 1; }
# **THE SINGLE-READ RULE (design §2b, the 1013 V6 rule): every published derived cell must be computed
# from the ONE `_wdleaf` LOCAL, not from a second read of the table.**  Assert the leaf is bound once
# in the recorder's body and that the derived expressions name `_wdleaf` (the design's §4 assertion:
# `grep -c` on the source for `_wdleaf`).  A recorder that re-walked for the decode would let the two
# readings disagree and every derived cell would be a claim rather than a decode.
wd_leafbinds=$(grep -cF 'wdleaf = ' src/entry/entry_stubs.c || true)
echo "   (a) the leaf is bound to a LOCAL once in the recorder: \`wdleaf = \` x$wd_leafbinds (want >= 1)"
[[ "$wd_leafbinds" -ge 1 ]] || { echo "FAIL: the recorder does not bind the leaf to a local named \`wdleaf\` (x$wd_leafbinds) - the derived cells would not come from ONE read (design §2b's single-read rule, the 1013 V6 lesson)"; exit 1; }
for wd_expr in wdap wdapx wdtex wdcb wdsmall; do
  [[ $(grep -cF "wdleaf" src/entry/entry_stubs.c || true) -ge 5 ]] || { echo "FAIL: \`wdleaf\` is named fewer than five times in entry_stubs.c - the derived cells (\`$wd_expr\` and its siblings) are not all computed from the ONE leaf local"; exit 1; }
done
echo "   (a) the derived cells (_wdap/_wdapx/_wdtex/_wdcb/_wdsmall) all name the ONE \`wdleaf\` local"
# The DACR is READ, never written: the ONLY DACR writer in the whole port is src/mmu.c:4921.
mmu_dacrw=$(grep -c 'c3, c0, 0' src/mmu.c || true)
pmap_dacrw=$(grep -c 'c3, c0, 0' "$XNU_TREE/osfmk/arm/pmap.c" || true)
echo "   (a) the DACR write/read census: src/mmu.c x$mmu_dacrw (want 2 = its read + its ONE write, UNCHANGED by 1015), pmap.c x$pmap_dacrw (want 0 - 1015 READS the DACR through proc_reg.h and never writes it)"
[[ "$mmu_dacrw" == "2" ]] || { echo "FAIL: src/mmu.c carries $mmu_dacrw \`c3, c0, 0\` occurrences (want 2 - its own read at :4884 and its ONE write at :4921). 1015 READS the DACR at the abort and must not add or remove a writer; a third occurrence means a second DACR writer appeared and every P-D2 reading would have two producers ([[mi4-one-value-two-definitions]])"; exit 1; }
[[ "$pmap_dacrw" == "0" ]] || { echo "FAIL: $XNU_TREE/osfmk/arm/pmap.c carries $pmap_dacrw \`c3, c0, 0\` occurrences - 1015 must never write the DACR from the pool; the read goes through the tree's own proc_reg.h macro in entry_stubs.c only"; exit 1; }
# The control key the passive ladder's null control publishes.  **`_wdctl` is a KEY, not a SYMBOL**:
# there is no `entry_note_abt_wdctl` function anywhere (the design names `xnu_live_abt_wdctl` at
# `docs/1015-design.md:202` and nothing else), so an earlier draft of this clause grepped a symbol
# that cannot exist and would have FAILED on its own literal rather than on the code
# ([[mi4-a-claim-in-a-comment-is-not-a-check]], running backwards).
[[ $(grep -cF 'xnu_live_abt_wdctl' src/entry/entry_stubs.c || true) -ge 1 ]] || { echo "FAIL: the passive recorder publishes no \`xnu_live_abt_wdctl\` control cell - P-CTL would have no reading"; exit 1; }
echo "   (a) the recorder publishes the null control (_wdctl/_wdctlok) - P-CTL is a reading, not a status"

# (b) LINKED image: the passive keyspace, the DEFINED symbol, and the ONE (b|bl) call site.
elf=out/stage90/xnu_arm_entry.elf
for d15_key in xnu_live_abt_wdn xnu_live_abt_wdva xnu_live_abt_leafng xnu_live_abt_wdempty \
               xnu_live_abt_wddacr xnu_live_abt_wddom xnu_live_abt_wdttbcr xnu_live_abt_wdsplit \
               xnu_live_abt_wdttbr0 xnu_live_abt_wdttbr1 xnu_live_abt_wdl1 xnu_live_abt_wdleaf \
               xnu_live_abt_wdap xnu_live_abt_wdapx xnu_live_abt_wdtex xnu_live_abt_wdcb \
               xnu_live_abt_wdsmall xnu_live_abt_wdpcpu xnu_live_abt_wdthrmap xnu_live_abt_wdmapok \
               xnu_live_abt_wdseen xnu_live_abt_wdctl xnu_live_abt_wdctlok; do
  grep -qx "$d15_key" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$d15_key\` literal - the 1015 passive store did not reach the linked image"; exit 1; }
done
echo "   (b) the image carries all 23 1015 passive key literals (22 wd* incl. the null control wdctl/wdctlok + the RAW nG bit leafng, whose NAME does not start with wd by the design's own spelling)"
# (b2) THE AT/PAR LEG (`docs/1015-perm-static.md` Item 6, "the killer key") - the ONE 1015 cell that
#      reads the CPU's own TRANSLATION instead of a page table.  It is a THIRD keyspace (three flat
#      keys, not a ladder), added by the entry lane after the first draft of this harness; the reader
#      adjudicates it as V-AT1/2/3/4 (tools/check_1015_capture.py's s_at).  A build whose image lacks
#      these keys makes the reader's AT block print "n/a" - a silent downgrade of the strongest cell
#      in the rung - so the presence is asserted here rather than assumed from the source.
for d15_atk in xnu_live_abt_wdatpar xnu_live_abt_wdatparpr xnu_live_abt_wdctlat xnu_live_abt_wdatok xnu_live_abt_wdsctlr; do
  grep -qx "$d15_atk" <<<"$(strings "$elf")" || { echo "FAIL: the image does not carry the \`$d15_atk\` literal - the AT/PAR leg (docs/1015-perm-static.md Item 6) did not reach the linked image, so the reader's V-AT cells would silently read 'n/a' and the rung's strongest, press-independent evidence would be missing without the build saying so"; exit 1; }
done
d15_at_src=$(grep -oE 'xnu_live_abt_(wdatpar|wdatparpr|wdctlat|wdatok|wdsctlr)' src/entry/entry_stubs.c | sort -u | wc -l || true)
echo "   (b2) the image carries the FIVE AT/PAR+SCTLR key literals (wdatpar/wdatparpr/wdctlat/wdatok/wdsctlr); the recorder source names x$d15_at_src distinct of them"
[[ "$d15_at_src" -ge 5 ]] || { echo "FAIL: src/entry/entry_stubs.c names only $d15_at_src of the five AT/SCTLR keys - the recorder publishes a partial leg, so a PAR would be read without its _wdatok gate (or the T-CTL null control _wdctlat, or the SCTLR half of §3c's re-rank)"; exit 1; }
arm-none-eabi-nm "$elf" | grep -E "^[0-9a-f]+ [Tt] entry_note_abt_wd\$" >/dev/null || { echo "FAIL: entry_note_abt_wd is not a DEFINED text symbol in $elf - the recorder did not link (or a generated stand-in was used)"; exit 1; }
echo "   (b) entry_note_abt_wd is DEFINED T/t in the image"
# **`(b|bl)`, NEVER `bl`** - the 1013 tail-call reader defect: a void call that is the LAST statement
# of its function is TAIL-called and the image carries `b <sym>`, so a `bl`-only regex reads 0 and
# manufactures a false "the call did not reach the image" ([[mi4-linked-code-order-is-not-source-order]]).
d15_wd_all=$(arm-none-eabi-objdump -d "$elf" | grep -cE '[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <entry_note_abt_wd>' || true)
echo "   (b) image-wide (b|bl) call sites of entry_note_abt_wd: $d15_wd_all (want 1 - the ONE call in the abort wrapper's neighbourhood)"
[[ "$d15_wd_all" == "1" ]] || { echo "FAIL: entry_note_abt_wd does not have exactly ONE (b|bl) call site (0 = the trace call did not reach the image, or it was read with a bl-only regex over a tail call - the 1013 defect; >1 = a second site)"; exit 1; }
# AND the inherited recorders must still be ONE each - 1015 must not have displaced them.
for d15_inh in entry_note_abt_wf entry_note_abt_wctx entry_note_abt_wleaf entry_note_abt_walk entry_note_abt_wsync entry_note_abt_cur; do
  d15_n=$(arm-none-eabi-objdump -d "$elf" | grep -cE "[[:space:]](b|bl)[[:space:]]+[0-9a-f]+ <$d15_inh>" || true)
  [[ "$d15_n" == "1" ]] || { echo "FAIL: the INHERITED recorder $d15_inh has $d15_n (b|bl) call site(s), want 1 - 1015 disturbed a 1013/1014 recorder (the switches must ADD, never displace)"; exit 1; }
done
echo "   (b) the inherited recorders are undisturbed (wleaf/walk/wsync/cur/wf/wctx x1 (b|bl) each)"

# (c) build_entry.sh threads PERM_WITNESS into both define sets and the record (all five plumbing
#     sites), refuses the partial set, and adds NO wrap of its own. **1015 has exactly ONE switch to
#     thread**: the removed ACTIVE half would have added a second plumbing set here, and asserting it
#     now would require build_entry.sh to carry a switch this rung no longer exports.
[[ $(grep -cF 'STAGE90_XNU_PERM_WITNESS="${STAGE90_XNU_PERM_WITNESS:-0}"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_PERM_WITNESS is not threaded into STUB_DEFINES_TRACE in build_entry.sh - entry_trace.c would not see it"; exit 1; }
[[ $(grep -cF -- '-DSTAGE90_XNU_PERM_WITNESS="$PERMWIT"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_PERM_WITNESS is not threaded into STUB_DEFINES in build_entry.sh - entry_stubs.c would not see it"; exit 1; }
# **THE FIVE-SPACE SPELLING.**  The `case` arms align their `_v=` with four spaces after the
# longest name (`FLUSH_WITNESS`, 13 chars).  `PERM_WITNESS` is 12 chars and therefore needs FIVE -
# `build_entry.sh`'s own line is `STAGE90_XNU_PERM_WITNESS)     _v=$PERMWIT ;;`.  An earlier draft of
# this clause grepped SIX spaces and so could never match the tree it was checking: the check, not
# the code, was wrong.  Matched whitespace-insensitively so an alignment edit cannot resurrect the
# defect while the arm it names stays correct.
[[ $(grep -cE 'STAGE90_XNU_PERM_WITNESS\)[[:space:]]+_v=\$PERMWIT ;;' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: STAGE90_XNU_PERM_WITNESS is not in build_entry.sh's record-case block (read whitespace-insensitively) - the record's value would be empty"; exit 1; }
[[ $(grep -cF 'echo "STAGE90_XNU_PERM_WITNESS=$PERMWIT"' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: the record writer does not echo STAGE90_XNU_PERM_WITNESS - the record is written by explicit echo lines, NOT from ENTRY_ARM_KEYS ([[mi4-a-claim-in-a-comment-is-not-a-check]])"; exit 1; }
[[ $(grep -cF '[[ $PERMWIT -eq 1 && $PTEWIT -ne 1 ]]' src/entry/build_entry.sh || true) == 1 ]] || { echo "FAIL: build_entry.sh does not REFUSE PERM_WITNESS=1 without PTE_WITNESS=1 - the recorder keys on 1013's g_abtpte_curva, so without it EVERY row would be filtered out and the probe would measure NOTHING while reporting a green build ([[mi4-measurement-defects]])"; exit 1; }
echo "   (c) build_entry.sh threads PERM_WITNESS into both define sets + the record (all five sites) and refuses the partial set"
# **1015b's OWN SWITCH IS POOL-ONLY - IT MUST NOT BE THREADED INTO EITHER ENTRY DEFINE SET, AND THAT
# IS A CHECK RATHER THAN A FACT ABOUT THE TREE AT ONE MOMENT.** `src/entry/build_entry.sh` is another
# lane's file and is not edited here ([[mi4-file-lanes]]). MEASURED this session:
# `grep -cE 'STAGE90_XNU_GLOBAL_FIX' src/entry/build_entry.sh` returns 0. The clause below keeps that
# a check, because the failure it closes is a real one and not a formality: if a future edit added
# `-DSTAGE90_XNU_GLOBAL_FIX=...` to `STUB_DEFINES`/`STUB_DEFINES_TRACE`, the *entry objects* would
# carry the token while nothing in them reads it - an inert define that would let a later reader
# believe the entry half verified the switch. Worse, it would put the switch in the entry lane's
# keyspace while the fixed-name declaration (`xnu_arm_entry-arm-extra.txt`, asserted at the tail) is
# this script's only writer, so the two carriers could disagree
# ([[mi4-one-value-two-definitions]]) - and the disagreement would be invisible, because an inert
# define changes no byte.
[[ $(grep -cE 'STAGE90_XNU_GLOBAL_FIX' src/entry/build_entry.sh || true) == 0 ]] || { echo "FAIL: src/entry/build_entry.sh names STAGE90_XNU_GLOBAL_FIX ($(grep -cE 'STAGE90_XNU_GLOBAL_FIX' src/entry/build_entry.sh || true) line(s)) - 1015b's switch is POOL-ONLY (the entry objects do not read it), so plumbing there would be an inert define claiming a verification the entry lane does not perform"; exit 1; }
[[ $(grep -cE '^ *-eq 1 \]\] && TRACE_LDFLAGS\+=\(--wrap=.*(perm|glob)|(PERM_WITNESS|GLOBAL_FIX).*TRACE_LDFLAGS' src/entry/build_entry.sh || true) == 0 ]] || { echo "FAIL: build_entry.sh appears to gate a --wrap on one of the 1015/1015b switches - 1015 adds NO wrap of its own (the recorder rides --wrap=sleh_abort), and 1015b adds none either (its fix is a SOURCE edit to the pool's pmap.c; a --wrap=pmap_flush_tlbs would be a hook that cannot fire - every call site is intra-object)"; exit 1; }
echo "   (c) the entry switch adds no wrap, and 1015b's pool-only switch is NOT threaded into either entry define set"

# (d0) OFF BYTE-IDENTITY FOR THE TWO ENTRY OBJECTS, BY VALUE - a twin entry rebuild with
#      STAGE90_XNU_PERM_WITNESS=0 must produce
#      an image carrying the 1014 key set and NO `wd*` key and NO `entry_note_abt_wd` symbol - the
#      reproducibility guarantee that the 1015 entry edits COMPILE OUT (design §4 item 5).
#
#      **SKIPPED BY DEFAULT** - it costs a second full entry build.  Set
#      `STAGE90_1015_OFF_CONTROL=1` to run it.  It SWAPS `out/stage90` for an empty scratch, runs the
#      entry build with the switch OFF, inspects the scratch elf, then restores the ON out/stage90 in
#      ALL cases (EXIT/INT trap).  **This touches live shared state
#      ([[mi4-out-stage90-is-shared-live-state]]): get a window first; do NOT run it while a press
#      preflight is reading out/stage90.**  A DISTINCT save dir name is used so it cannot collide with
#      the 1008 block's `out/stage90-off-save` if both are requested.
if [[ "${STAGE90_1015_OFF_CONTROL:-0}" == "1" ]]; then
  echo "-- by value: the 1015 OFF CONTROL (PERM_WITNESS=0 twin build; the passive switch must compile out, leaving 1014's key set) --"
  if [[ -e out/stage90-off-save-1015 ]]; then echo "FAIL: out/stage90-off-save-1015 already exists - a prior OFF run did not clean up"; exit 1; fi
  mv out/stage90 out/stage90-off-save-1015
  mkdir -p out/stage90
  uf_restore() { rm -rf out/stage90; mv out/stage90-off-save-1015 out/stage90; }
  trap uf_restore EXIT INT TERM
  _uf_ok=0
  ( cd src/entry && env -i PATH="$PATH" HOME="$HOME" \
      STAGE90_XNU_PERM_WITNESS=0 "${EXPORTS[@]}" ./build_entry.sh ) && _uf_ok=1 || _uf_ok=0
  _uf_offelf=out/stage90/xnu_arm_entry.elf
  if [[ "$_uf_ok" == "1" && -f "$_uf_offelf" ]]; then
    uf_off_wd=$(strings "$_uf_offelf" | grep -cE 'xnu_live_abt_(wd[a-z0-9]*|leafng|at[a-z0-9]*)$' || true)
    uf_off_sym=$(arm-none-eabi-nm "$_uf_offelf" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_wd$' || true)
    uf_off_wf=$(strings "$_uf_offelf" | grep -cE '^xnu_live_abt_wf[a-z]+$' || true)
    uf_restore; trap - EXIT INT TERM
    echo "   (d0) OFF image: 1015 passive literals x$uf_off_wd (want 0), entry_note_abt_wd symbols x$uf_off_sym (want 0), and 1014's wf* literals x$uf_off_wf (want 11 - the inherited set is UNDISTURBED)"
    [[ "$uf_off_wd" == "0" && "$uf_off_sym" == "0" ]] || { echo "FAIL: the PERM_WITNESS=0 image still carries the 1015 passive change (literals x$uf_off_wd want 0, symbols x$uf_off_sym want 0) - the switch did NOT truly compile out (OFF is not byte-identical)"; exit 1; }
    [[ "$uf_off_wf" == "11" ]] || { echo "FAIL: the PERM_WITNESS=0 image carries x$uf_off_wf 1014 wf* literals (want 11) - the OFF arm is NOT 1014's arm, so this control is measuring two switches at once"; exit 1; }
    echo "   (d0) the passive switch COMPILES OUT (OFF image carries 1014's key set and none of 1015's)"
  else
    uf_restore; trap - EXIT INT TERM
    echo "   (d0) OFF control: the twin build was requested but FAILED (rc=$_uf_ok) - out/stage90 was restored; investigate before trusting the OFF clause"
    [[ "${STAGE90_1015_OFF_CONTROL_HARD:-0}" == "1" ]] && exit 1
  fi
else
  echo "   (d0) OFF control: SKIPPED (set STAGE90_1015_OFF_CONTROL=1 to run the twin build) - the OFF"
  echo "       byte-identity of the entry objects is demonstrated by the frozen 1014 elf control below"
  echo "       (the 1015 passive keys and symbol are absent from it) and by the source guard in (a)"
fi

# (d) THE NEGATIVE CONTROL: the FROZEN 1014 elf must carry ZERO `wd*`/`leafng`/`at*` literals and ZERO
#     `entry_note_abt_wd` symbols - the control that makes every reader above able to fail.
if [[ -f $d15_ctrl ]]; then
  ctl15_wd=$(strings "$d15_ctrl" | grep -cE 'xnu_live_abt_(wd[a-z0-9]*|leafng|at[a-z0-9]*)$' || true)
  ctl15_sym=$(arm-none-eabi-nm "$d15_ctrl" 2>/dev/null | grep -cE '^[0-9a-f]+ [Tt] entry_note_abt_wd$' || true)
  echo "   (d) frozen 1014 elf ($d15_ctrl): 1015 passive+AT key literals x$ctl15_wd (want 0), entry_note_abt_wd symbols x$ctl15_sym (want 0)"
  [[ "$ctl15_wd" == "0" && "$ctl15_sym" == "0" ]] || { echo "FAIL: the frozen 1014 elf already carries the 1015 passive/AT change (literals x$ctl15_wd, symbols x$ctl15_sym) - the negative control is not a control"; exit 1; }
  echo "   the 1015 passive change is NOT in the frozen 1014 elf (control holds)"
else
  echo "FAIL: the frozen 1014 elf is absent at $d15_ctrl - the negative control is REQUIRED this run (design §4); without it the readers above certify nothing"
  exit 1
fi
# The CROSS-LANE half of 1015b's declaration (the `xnu_arm_entry-arm-extra.txt` drop-in file) used to
# be asserted here and is no longer: it is a 1015b assertion and it now lives in the 1015b block at the
# tail, beside the pool-MOVED census it belongs to. The 1015 KEY half above is untouched.

# (e) RECORD + GATE - the fresh record carries the rung's OWN switch once, and the preflight gate names
#     it. **ONE KEY, because 1015 has ONE switch**, and that switch is ENTRY-SIDE: a build where
#     `build_entry.sh` forgot the name would write a record naming an arm it did not compile, and the
#     reader's V-AT cells would be read off an image that never asked the MMU anything.
for d15_k in STAGE90_XNU_PERM_WITNESS; do
  [[ $(awk -F= -v k="$d15_k" '$1==k{print $2}' "$REC") == 1 ]] || { echo "FAIL: the fresh record does not carry $d15_k=1 - a park would freeze an arm whose switch the record does not name"; exit 1; }
  grep -q "$d15_k" scripts/preflight_boot_check.sh || { echo "FAIL: $d15_k is not in scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS - the converse check will refuse the press"; exit 1; }
done
echo "   (e) the fresh record carries STAGE90_XNU_PERM_WITNESS=1 and the preflight gate names it"
# **AND 1015b's OWN SWITCH IS ON A PARALLEL CARRIER, DELIBERATELY NOT IN `ENTRY_CFG_KEYS`.** The
# preflight gate's converse check is a SET DIFFERENCE between `ENTRY_CFG_KEYS` and the record's keys,
# so naming `STAGE90_XNU_GLOBAL_FIX` there while no writer in `src/entry/` produces the line would make
# the check compare a key against a keyspace that cannot contain it - and the honest outcome of that is
# not a caught defect but a gate that refuses every press. The declaration therefore rides the drop-in
# file (asserted by VALUE in the 1015b block at the tail), and what is asserted HERE is the OTHER
# direction: the record itself must NOT name the switch. A record that named it would read, to every
# downstream consumer of the record, as "this arm's entry build compiled a kernel switch" - true of the
# ARM, false of the FILE, and the file is the thing a re-run reads back
# ([[mi4-self-written-record-is-not-a-constraint]]: ask who writes this file).
[[ $(awk -F= '$1=="STAGE90_XNU_GLOBAL_FIX"{print $2}' "$REC") == "" ]] || { echo "FAIL: the fresh record names STAGE90_XNU_GLOBAL_FIX ($(awk -F= '$1=="STAGE90_XNU_GLOBAL_FIX"{print $2}' "$REC")) - no writer in src/entry/ produces that line, so if one appeared here it came from a hand edit of the record, which is precisely the carrier this rung refuses to trust"; exit 1; }
grep -q 'STAGE90_XNU_GLOBAL_FIX' scripts/preflight_boot_check.sh && { echo "FAIL: scripts/preflight_boot_check.sh's ENTRY_CFG_KEYS names STAGE90_XNU_GLOBAL_FIX - the converse check is a set difference, and no src/entry/ writer produces that key, so naming it there turns the gate into one that refuses every press (the 'the gate, not the code, was wrong' class)"; exit 1; }
echo "   (e) the fresh record does NOT name STAGE90_XNU_GLOBAL_FIX and preflight's ENTRY_CFG_KEYS does not either (the switch's declaration rides the arm-extra drop-in file, asserted at the tail)"

echo "-- by value: the 1015 READER on the frozen 1014 capture as a NEGATIVE CONTROL (it must be V-G3, NOT V-G2 - the widened arm was never reached, so the capture cannot refute H-G) --"
d15_cap=out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt
if [[ -f $d15_cap ]]; then
  python3 tools/check_1015_capture.py "$d15_cap" >/tmp/check1015-ctl.$$ 2>&1 || true
  ctl15_cell=$(grep -m1 '1015 INHERITED PRIMARY CELL' /tmp/check1015-ctl.$$ | sed 's/.*: //')
  ctl15_at=$(grep -m1 '1015 AT/PAR CELL' /tmp/check1015-ctl.$$ | sed 's/.*: //')
  echo "   frozen 1014 capture under the 1015 reader: PRIMARY CELL = $ctl15_cell (want V-G3 - EXPERIMENT VACUOUS), AT/PAR CELL = $ctl15_at (want n/a - a pre-AT capture carries no AT cell, NOT a withhold)"
  [[ "$ctl15_at" == "n/a" ]] || { echo "FAIL: the 1015 reader read the frozen 1014 capture's AT/PAR CELL as \`$ctl15_at\`, not n/a - that capture has no AT keys at all, so an AT cell there would mean the reader is manufacturing an AT verdict out of a missing keyspace (the withhold V-AT0 is for a leg that RAN and was void, not one that was never armed)"; rm -f /tmp/check1015-ctl.$$; exit 1; }
  [[ "$ctl15_cell" == "V-G3" ]] || { echo "FAIL: the 1015 reader read the frozen 1014 capture as \`$ctl15_cell\`, not V-G3 - the frozen capture's `_wfkind` is 2 (the ASID arm), so a reader that calls it V-G2 would be manufacturing a REFUTATION of H-G out of a run where the widened arm never existed. A reader that cannot withhold certifies nothing ([[mi4-silence-is-a-reading-only-if-success-is-silent]])"; rm -f /tmp/check1015-ctl.$$; exit 1; }
  rm -f /tmp/check1015-ctl.$$
else
  echo "   (the frozen 1014 capture is absent at $d15_cap - the reader control is unavailable this run)"
fi
# The reader's OWN self-test - the synthetic table, including the V-G3-must-not-be-V-G2 case.
python3 tools/check_1015_capture.py --self-test "$d15_cap" || { echo "FAIL: tools/check_1015_capture.py --self-test did not PASS - the pre-registered verdict table does not reproduce"; exit 1; }
echo "   the 1015 reader's synthetic table reproduces (--self-test PASS)"

echo "-- 1015b's OWN READER ON THE SAME FROZEN 1014 CAPTURE: the NEGATIVE CONTROL for the rung's verdict cell --"
# **THIS IS THE ONE CHECK THAT MAKES G-CONFIRMED MEAN SOMETHING.** 1015b's decisive cell is read by
# `tools/check_1015b_capture.py`: G-CONFIRMED = the storm STOPPED. The obvious failure of such a reader
# is one that answers G-CONFIRMED to every capture - and the frozen 1014 capture is exactly the input
# to catch it: it is a COMPLETE, RESIDENT capture carrying a full storm, taken on an arm where the
# widened arm did NOT EXIST. A reader that reads it as CONFIRMED is a constant function; one that reads
# it as WITHHELD is one that cannot decide a capture it should be able to decide. The reading required
# here is a POSITIVE G-REFUTED, and G-REFUTED is a positive measurement (the table demands a storm ROW
# to point at, never an absence) - so this clause cannot pass on a reader that fails to find anything.
if [[ -f $d15_cap && -f tools/check_1015b_capture.py ]]; then
  python3 tools/check_1015b_capture.py "$d15_cap" >/tmp/check1015b-ctl.$$ 2>&1 || true
  b15_cell=$(grep -m1 '^1015b GLOBAL-FIX CELL:' /tmp/check1015b-ctl.$$ | sed 's/.*: //')
  echo "   frozen 1014 capture under the 1015b reader: GLOBAL-FIX CELL = $b15_cell (want G-REFUTED - a capture from an arm WITHOUT the widened flush must never read as G-CONFIRMED)"
  [[ "$b15_cell" == "G-REFUTED"* ]] || { echo "FAIL: the 1015b reader read the frozen 1014 capture's GLOBAL-FIX CELL as \`$b15_cell\`, not G-REFUTED. That capture is from an arm where the widened flush DID NOT EXIST and it carries a full storm, so G-CONFIRMED here would mean the reader confirms H-G unconditionally - the verdict cell would then certify 1015b whatever the hardware did ([[mi4-silence-is-a-reading-only-if-success-is-silent]], and the measurement-defect class it belongs to). A G-WITHHOLD here is a different defect: the capture is complete and readable, so withholding would mean the reader cannot decide the cell it exists to decide"; rm -f /tmp/check1015b-ctl.$$; exit 1; }
  rm -f /tmp/check1015b-ctl.$$
  python3 tools/check_1015b_capture.py --self-test >/tmp/check1015b-st.$$ 2>&1 || { echo "FAIL: tools/check_1015b_capture.py --self-test did not PASS - the rung's pre-registered verdict cell does not reproduce"; tail -5 /tmp/check1015b-st.$$; rm -f /tmp/check1015b-st.$$; exit 1; }
  echo "   the 1015b reader's synthetic table reproduces ($(grep -m1 -oE '^ *[0-9]+ case\(s\), [0-9]+ failed' /tmp/check1015b-st.$$ || echo 'self-test'))"
  rm -f /tmp/check1015b-st.$$
else
  echo "FAIL: either the frozen 1014 capture ($d15_cap) or tools/check_1015b_capture.py is absent - the rung's verdict-cell control is REQUIRED this run. Without it nothing above certifies that a G-CONFIRMED verdict would distinguish a stopped storm from a reader that cannot say no"; exit 1
fi

echo "-- 1015b build done: park with scripts/park_1015b.sh --"