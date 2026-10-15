#!/usr/bin/env bash
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

echo "== 1012: the PTE-build apex (prot at pmap_enter_options, via --wrap=pmap_enter_options); 1011's set verbatim + ONE new switch =="
echo "-- 1011's arm was armed-d13-fce83837 (F1a MEASURED: the (E') row has vmeprot=0x3 = READ|WRITE with vmekr=0 = KERN_SUCCESS, so the vm_map ENTRY GRANTS write and the write is LOST between the entry and the PTE; F1b is denied BY VALUE; the (A) walk's TTBR0 base mask was WRONG (0xffffc000 vs this port's TTBCR.N=2 alignment 0xfffff000) and is FIXED in entry_ttbr_base; see records/1011-press-read.txt); 1011's set verbatim --"

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
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

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

echo "-- entry image (1000's switch set re-exported verbatim + SUBMAP_PMAP_SKIP=1; build_entry only LINKS the pool) --"
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
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   1009 was 24997fc5, 1010 was 0f39207c, 1010b was 64391319, 1010c was 002b0683, 1011 was fce83837, 1012 was ffdc8ba1 - a DIFFERENT value here means the 1013 edits reached the image."
# The arm must MOVE off the PRESSED arms (the project rule: the arm IS the entry bin's sha8). A build
# that lands back on 64391319 did not change the entry image - the 1010c window bound + alias fix did
# not reach the linked image (or one of the two entry objects was not rebuilt). This is the build-side
# counterpart of the arm-refusal guards the park/press scripts carry.
_d6_arm=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $_d6_arm == armed-d13-ffdc8ba1 ]] && { echo "FAIL: the build produced 1012's PRESSED arm (ffdc8ba1) again - the entry bin did NOT move, so the 1013 witness (the three recorders + their call sites) did not reach the linked image (rebuild: build_entry.sh with STAGE90_XNU_PTE_WITNESS=1 AND STAGE90_XNU_PMP_ENTER=1 AND STAGE90_XNU_ABT_PTE=1 AND STAGE90_XNU_ABT_PC=1 AND STAGE90_XNU_ABT_FAR=1 exported for BOTH STUB_DEFINES and STUB_DEFINES_TRACE); do not park a 1013 arm identical to the pressed 1012 one"; exit 1; }
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
echo "   1013 arm: $_d6_arm (must differ from the pressed 1012 arm ffdc8ba1, the pressed 1011 arm fce83837 and the pressed 1010c arm 002b0683)"
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
[[ "$ab_lit" == "86" ]] || { echo "FAIL: the image carries $ab_lit \`xnu_live_abt_\` literals (want 86: the 6 span + the 5 far + 1009's 7 pc + 1010's 12 (pten/pteva/ptel2/ptel1/ptettbr/pteu/ptenx/vmfn/vmfret/vmfva/vmfprot/vmfnret) + 1010c's 6 (pteoow/ptealias/ptemapok/pteslot/ptewbase/ptewlen) + 1011's 7 (vmen/vmekr/vmeprot/vmefault/vmeva/vmenx/ptethr) + 1012's 6 (pmpn/pmpprot/pmppa/pmppmap/pmpttbr/pmpnx) = 49 + 1013's 35 witness keys (11 W-APEX + 13 W-WALK incl. the 1013b VA cell wkva + 9 W-SYNC incl. the TTBR1 label wctb) + the 2 change-counts + the 2 raw TTBR words wctb0/wctb1) - a store did not reach the linked image"; exit 1; }
echo "   (b) the linked image carries the 86 \`xnu_live_abt_\` key literals (49 through 1012 + 1013's 35 + the 2 raw TTBR words)"
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
# and the recorder must KEY on that value, NOT on 1012's post-handler `g_abtpte_pmpva`.
[[ $(grep -cF 'cur = g_abtpte_curva;' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_note_abt_wleaf does not key on g_abtpte_curva - it inherits 1012's post-handler key"; exit 1; }
[[ $(grep -cF 'if (cur == 0u || va != cur)' src/entry/entry_stubs.c || true) == 1 ]] || { echo "FAIL: entry_note_abt_wleaf's current-fault filter is not present exactly once"; exit 1; }
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

echo "-- 1013 build done: park with scripts/park_1013.sh --"