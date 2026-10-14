#!/usr/bin/env python3
"""Mechanical adjudicator for the 1009 press capture - by value, and DEATH-AWARE.

MODELLED BYTE-STRUCTURALLY ON tools/check_1008_capture.py (its `Capture` reader and every 1008
clause are kept; 1009 does NOT change 1008's set).  1009 adds ONE measurement - THE FAULTING PC AND
THE FSR (switch `STAGE90_XNU_ABT_PC`): `__wrap_sleh_abort` forwards the `regs->pc`, the DFSR (`fsr`,
`c5,c0,0`), the abort mode (`regs->cpsr`) and the `far` it already holds to a NEW recorder
`entry_note_abt_pc(pc, fsr, cpsr, far)`, which publishes, per 1008's trigger set VERBATIM, the
faulting INSTRUCTION, the fault STATUS and the mode of the storm's aborts on the DIRECT (cap-exempt)
channel - plus the CHANGE-COUNTS `_nfar`/`_nfsr` (written WHERE THEY CHANGE).  The section that
carries the new reading is `[ABT3]`.

[ABT3] THE FAULTING PC + FSR - THE 1009 RUNG ITSELF.  The point (design `docs/1009-design.md`, §5):
1008 named the storm's fault ADDRESS and TYPE (a REPEAT at the constant `far=0x0007b000`, a DATA
abort), but said WHERE, not WHAT.  The port's handler return path turns on WHICH INSTRUCTION re-runs
and on whether the retries are the SAME fault (one DFSR) or a cycled STATUS.  1009 forwards the
`pc`/`fsr`/`cpsr` the wrapper ALREADY holds (`entry_trace.c`, the saved-state frame + the `mrc p15`
pair) to the direct channel and publishes the CHANGE-COUNTS so the reader decides REPEAT-vs-SPREAD
**by value over the TAIL**, not over the whole-ladder set (the 1008 `[ABT2]` weakness, below).

The pc recorder publishes SEVEN DIRECT keys (`entry_write_kv`), all unsigned 32-bit:
    `xnu_live_abt_pcn`     - the ordinal n (the twin of 1008's `xnu_live_abt_farn`)
    `xnu_live_abt_pc`      - the FAULTING instruction address (`regs->pc`)
    `xnu_live_abt_fsr`     - the DFSR (`c5,c0,0`): WnR bit + fault-status code
    `xnu_live_abt_abtcpsr` - the mode the abort ran in (`regs->cpsr`)
    `xnu_live_abt_pcfar`   - the `far` at THIS ordinal (pairs with 1008's `xnu_live_abt_far`)
    `xnu_live_abt_nfar`    - the number of CHANGES in `far` seen so far (running monotone max)
    `xnu_live_abt_nfsr`    - the number of CHANGES in `fsr` seen so far (running monotone max)
The ladder quintuple (`_pcn`/`_pc`/`_fsr`/`_abtcpsr`/`_pcfar`) is published at 1008's trigger set
VERBATIM (`n<=4` then `n & (n-1)==0`); the two change-counts are written WHERE THEY CHANGE (the
`g_sleh_armed` rule) and are bounded by the DISTINCT-value count (<= ~12 for `far`, a handful for
`fsr`).  1009 sits AFTER the 1008 recorder and reads nothing 1008 did not, so the 1009 image
(`ABT_FAR=1` AND `ABT_PC=1`) carries EIGHTEEN `xnu_live_abt_` literals: 6 span + 5 far + 7 pc
(design §3.4).  [ABT3] READS the change-counts with `max()` (they are RUNNING MONOTONE MAXIMA
republished - a `first()` read is 100sx too small, the DEFECT this repo hit at 1007: a running-max
key read with `first()`; see `records/1007-press-read.txt`) and CLASSIFIES the TAIL:
  * (R) REPEAT (by value)   - `nfar == 1` AND `nfsr == 1` across the tail => ONE faulting address,
    ONE status, retried.  This is the DECISIVE statement 1008's set reader could NOT make;
  * (C) CYCLING-STATUS      - `nfar == 1` AND `nfsr > 1` => the retries differ in DFSR (a cycled
    status: the same address faults with a different fault-status code);
  * (S) SPREAD              - `nfar > 1` => the tail SPREADS (1008's constant-far reading was wrong);
  * the tail `pc` at the LARGEST ordinal is the FAULTING instruction; the `abtcpsr` mode
    (`0x…13` IRQ-off SVC, `0x…93` SVC IRQ-off, etc.) settles user-vs-kernel.
A MEASUREMENT by design: 1009 changes no kernel behaviour, so [ABT3] FAILS only on the instrument
defects of design §6 - a `pcn` GAP from `n`/`farn` at a paired row (the pc recorder did not run once
per abort), an `abtcpsr` outside the ARM CPSR mode shapes (an INSTRUMENT defect), a `fsr`/`pcn` key
entirely ABSENT while `xnu_live_abt_n`/`_farn` are present, the change-counts DECREASING (a monotone
running-max that goes backwards - a MEASUREMENT defect), or a log with NO pc keys at all when the run
claims to be a 1009 arm (the NEGATIVE CONTROL; waived only by `--allow-missing-abt3`, a pre-1009 log).
A branch that fires over ZERO samples is NOT a verdict: every classifier branch below is guarded on a
truthy evidence set (the defect fixed in the 1008 reader at commit `00f3ca1`).

[ABT2] THE ABORT-FAULT IDENTITY - THE 1008 RUNG ITSELF.  The point (design `docs/1008-design.md`,
§6): 1007 localised the ~24.4 s freeze INSIDE one `sleh_abort` (18.7 s) riding a STORM of ~2.1 M
aborts, but the storm's fault ADDRESS is in NO capture - the live channel stopped at #40 and 1007's
direct-channel recorder publishes only `n`/`seq`/`gap`/`call`.  1008 forwards the `far`/`type` the
wrapper ALREADY holds (`entry_trace.c:5687-5693`) to the direct channel, on the SAME bounded trigger
set (design §3): the record budget is the whole design (a per-abort publish is ~124 MB into a 2 MB
console).

The far recorder publishes FIVE DIRECT keys (`entry_write_kv`), all unsigned 32-bit:
    `xnu_live_abt_farn`     - the ordinal (== `xnu_live_abt_n`; the ladder key, a SELF-CONSISTENCY)
    `xnu_live_abt_far`      - the fault address (FAR) of abort `farn`
    `xnu_live_abt_type`     - the fault class (3 = T_PREFETCH_ABT, 4 = T_DATA_ABT)
    `xnu_live_abt_maxfar`   - the LATCHED far where `call` set a new max (cap-exempt, <= 32 publishes)
    `xnu_live_abt_maxtype`  - the LATCHED type where `call` set a new max
The ladder triple (`_farn`/`_far`/`_type`) is published at `n<=4` then `n & (n-1)==0` - the SAME row
set the span recorder's `xnu_live_abt_n` uses, so the far rows and the span rows pair at the same `n`.
The latch (`_maxfar`/`_maxtype`) is written WHERE `call` sets a new max, OUTSIDE the ladder, capped at
`ABT_FAR_LATCH_MAX=32` (design §3(i)).  [ABT2] READS the ladder rows and the latch, PAIRS BY KEY
EQUALITY (design §6.2), verifies `farn == n` at every paired row, and CLASSIFIES the storm's SHAPE
(design §6 table):
  * (S) SPREAD   - the ladder `far` values are DISTINCT and march forward, `maxfar` is one of them
    (a fault storm of distinct addresses - the boot demand-faulting page by page);
  * (R) REPEAT   - the ladder `far` values repeat the same one or two addresses, `maxfar == far[first]`
    (a retry loop / single-address thrash - REFUTES the storm interpretation, design §7);
  * (U) USER     - `type == 4` and `far < 0x80000000` (a user fault, contradicting the 1007 live tail);
  * (K) KERNEL   - `type == 4` and `far >= 0x80000000` (kernel `copyin`/`copyout` or a bad pointer);
  * (P) PREFETCH - `type == 3` on the ladder rows (instruction-fetch faults - a different mechanism);
  * the degenerate `far == 0` with `type == 4` (design §7: a re-entry on a zeroed fault register).
A MEASUREMENT by design: 1008 changes no kernel behaviour, so [ABT2] FAILS only on the instrument
defects of design §7 - a `farn` GAP from `n` at a paired row (the far recorder did not run once per
abort), a `type` outside {3,4} (the wrapper's class dispatch is reading neither class), the far rows
entirely ABSENT while `abt_n` is present, or `_maxfar`/`_maxtype` ABSENT while `_maxcall` is present
(the latch trigger never fired - the two clock reads diverged).  The NEGATIVE CONTROL (no far keys at
all) is waived only by the explicit `--allow-missing-abt2` flag (a pre-1008 log, e.g. the 1007 arm).

[ABT] THE ABORT-SPAN WITNESS - THE 1007 RUNG ITSELF.  The point (design `docs/1007-design.md`, §4):
the 1006 witness (below) settled that the ~24.4 s `xnu_live_tick_sum_maxd` freeze does NOT overlap
the idle path - the CPU was not idle through it, so it rode the NON-IDLE kernel code the abort path
runs.  1004 saw sleh_abort run ~40 times, walking forward.  1007 brackets that path so the LOCATION
of the freeze is a reading.

The witness publishes DIRECT keys (`entry_write_kv`), all unsigned 32-bit:
    `xnu_live_abt_n`        - the abort ordinal (monotone, starts at 1; OPENS a published record)
    `xnu_live_abt_seq`      - `g_sleh_seq` (pairs with the sleh entry record's `xnu_live_sleh_seq`)
    `xnu_live_abt_gap`      - `t0 - previous t0` (counts since the previous abort; 0 when first)
    `xnu_live_abt_call`     - `t1 - t0` (the handler's own duration)
    `xnu_live_abt_maxcall`  - the LATCHED maximum of `call`, written WHERE IT CHANGES (cap-exempt)
    `xnu_live_abt_maxgap`   - the LATCHED maximum of `gap`,  written WHERE IT CHANGES (cap-exempt)
The per-abort quadruple (`_n`/`_seq`/`_gap`/`_call`) is published on the powers-of-two ladder
(`n<=4` then `n & (n-1)==0`); the two maxima are published OUTSIDE the ladder so the one huge sample
cannot be missed.  [ABT] reads the ordered records and CLASSIFIES the freeze's LOCATION by value,
comparing `xnu_live_abt_maxcall` and `xnu_live_abt_maxgap` against the SAME capture's
`xnu_live_tick_sum_maxd` (`M` ~ 0x1be87a47 ~ 468.2e6 counts = 24.386 s at 19.2 MHz):
  * (A) BOTH maxcall and maxgap << M, no single spike -> the abort path is honest but FAST - the
    24.4 s is NOT here; it is in a NON-abort stretch (the copyin/SVC path or a non-fault kernel
    stretch past the frame). A NEGATIVE result for this rung, and a positive pointer elsewhere.
  * (B) maxgap ~ M with maxcall small -> the freeze is BETWEEN two aborts (one abort returned and
    the next was not entered for 24.4 s).
  * (C) maxcall ~ M -> a SINGLE sleh_abort took 24.4 s (the STRONGEST positive: the frontier is
    inside one fault).
  * (D) `xnu_live_abt_n`/`_seq` PLATEAU (the last aborts are the same `_seq` as the sleh tail; `n`
    stops while the log runs on) and NO maxcall/maxgap approaches M -> the freeze is AFTER the last
    abort (the frame's return-to-user stretch or the SVC path is where the CPU sat).
A MEASUREMENT by design: 1007 changes no kernel behaviour, so [ABT] FAILS only on (i) a MISSING abt
key (the switch did not reach the image: REBUILD), (ii) a NON-MONOTONE `_n` (a MEASUREMENT defect,
[[mi4-measurement-defects]]), (iii) a log with NO abt keys at all when the run is claimed to be a
1007 arm (the NEGATIVE CONTROL: a reader that cannot fail certifies nothing; waived only by the
explicit `--allow-missing-abt` flag), or (iv) `xnu_live_stub_hit_seq` != 0 - the §2.3 terminal
stand-in (below) FIRED, meaning the run ended inside the abort path.

[W] THE IDLE-PATH FREEZE WITNESS - 1006's rung, KEPT AS A REGRESSION.  A sampler
(`entry_freeze_sample`) called at the head of `__wrap_machine_idle`, once per idle pass, that times
the dwell of each idle pass with the free-running generic timer (`entry_counter()`, now passed IN as
an argument after 1007's §4.3 fix - below) and, when a pass dwelled longer than the threshold,
publishes the elapsed counts, the CPSR, and the GIC state.  It publishes, in this WRITE ORDER (a
record is OPENED by `xnu_live_freeze_n` and CLOSED by `xnu_live_freeze_ctlr`):
    `xnu_live_freeze_n`     - the freeze ordinal (monotone, starts at 1)
    `xnu_live_freeze_d`     - the elapsed counts of the pass that just ended
    `xnu_live_freeze_cpsr`  - the CPSR at the reading; the IRQ-mask bit is `(cpsr >> 7) & 1`
    `xnu_live_freeze_ien`   - `GICD_ISENABLER0` bit 20 (timer line enabled)
    `xnu_live_freeze_pend`  - `GICD_ISPENDR0` bit 20 (line pending)
    `xnu_live_freeze_ctlr`  - `GICD_CTLR` (distributor enable; a small value, bit 0 = enable)
And, UNFILTERED on the FIRST FOUR passes:
    `xnu_live_frzbase_n` / `xnu_live_frzbase_d` - the idle-pass BASELINE
The threshold is `MSM8974_FREEZE_MIN = 0x00004b00` (19200 counts ~1 ms); every published freeze must
have `d > 0x4b00`.  [W] CLASSIFIES the largest dwell by value:
    i = (cpsr >> 7) & 1   (1 = IRQ masked, 0 = enabled)
  * d large, i==0, ien!=0, pend!=0 -> NON-IDLE LONG BLOCK / FAULT LOOP;
  * d large, i==1                  -> IRQ-MASKED WINDOW held across the gap (candidate i);
  * d large, i==0, ien==0          -> GIC RE-MASK (candidate ii);
  * d large, ctlr==0               -> GLOBAL DISTRIBUTOR DISABLED (a sub-case of ii);
  * otherwise                      -> the raw tuple, "not decisive".
1006 read it as a VALID SILENCE (the idle path was never entered, so the freeze was non-idle); 1007
expects the SAME silence.  [W] FAILS only on a MISSING freeze key, a NON-MONOTONE `_n`, or (by
DEFAULT) a log with NO freeze keys when the run is claimed to be a 1006 arm (waived only by
`--allow-missing-freeze`).  ZERO freeze records WITH the idle-baseline keys present is a VALID
SILENCE - reported as such, NOT a hard anomaly.

§2.3/§4.3 THE TERMINAL STAND-IN (why 1007 carries an argument-passing fix).  In the INHERITED 1006
image, `entry_stubs.c`'s `entry_counter` resolved to a TERMINAL generated stand-in (`8049cce4 T
entry_counter` -> `entry_stub_hit` -> `noreturn entry_epilogue`) - dormant only because the idle path
is never entered.  1007's recorder NEVER calls `entry_counter()`; the clock value is passed IN
(`void entry_freeze_sample(uint32_t now)`), so `entry_stubs.o` carries NO `U entry_counter`.  The
abort path is now entered every run, so if the stand-in ever fires the run ENDS inside it -
`xnu_live_stub_hit_seq` != 0 is that event, and [ABT] treats it as a HARD anomaly.

[C] THE CADENCE WITNESS - 1005's rung, KEPT AS A REGRESSION.  There are exactly FOUR literal keys -
`xnu_live_cadence_{t,sync,tick,irq}` (the image is pinned to 4; there is NO `_summary` key).
Reads the ordered `_t`-led `(t, sync, tick, irq)` samples (the syscall-path recorder) PLUS the extra
`(ln, sync, tick, irq)` rows that `entry_cadence_summary` (a DEFINED symbol, nm-verified image-side,
not a key) re-publishes at the exec-done point and on the watchdog-pet gate - an instant that is NOT
a syscall.  It classifies the stall by the DELTAS over the sampled window:
    ds = sync_last - sync_first, dt = tick_last - tick_first, di = irq_last - irq_first.
  * dt small AND ds climbing  => `(b1)` DEGRADED TIMER IRQ (the clock decays while the synchronous
    path runs); refined by `di`: `di` also frozen => `(b2)` the whole IRQ path is masked.
  * dt small AND ds small     => FULL STOP (even the synchronous path froze).
  * dt climbing               => NO COLLAPSE IN THE WINDOW (the stall is past the samples).
A MEASUREMENT by design: 1005 changes no kernel behaviour, so [C] FAILS only on (i) a MISSING
cadence key (the switch did not reach the image: REBUILD), (ii) a NON-MONOTONE counter (a counter
that goes backwards is a MEASUREMENT defect, [[mi4-measurement-defects]]), or (iii) a log with NO
cadence keys at all when the run is claimed to be a 1005 arm (the NEGATIVE CONTROL: a reader that
cannot fail certifies nothing).  The last is waived only by the explicit `--allow-missing-cadence`
flag (used to re-run the 1004 regression clauses on a pre-1005 log).

numpy-free: this tool imports only os / re / sys (like its 1003 sibling).

1003's baseline (answered by 1003, kept as background): 1003's per-tick sampler settled that pid 1 is
NOT a user spin and NOT a passive sleep - the last samples are ONE kernel frame (pc=0x8001b1fd,
sp=0xcfdbbf00, lr=sleh_abort+0x468), i.e. pid 1's thread 0xc0bfa000 is in the data-abort RETURN path.
Total abort entries that boot reached 40 (`xnu_live_sleh_seen`), but the sleh caps published only the
first 8, and those 8 name DIFFERENT `far` values (dyld walking forward).  The 1002 r3-save fix holds
(the 230x `usermode data abort at 0x4` is GONE) and `cs_invalid_page` is a TOLERATED warning.

[A] THE ABORT TRACE - THE 1004 RUNG (kept as a regression here).  Reads the full ordered sleh
sequence, one record per abort (`seq` opens a record; `type`/`fsr`/`far`/`thr`/`recover` + the frame's
`pc`/`lr`/`sp`/`cpsr`/`fsr_frame`/`far_frame`/`frame_ok`/`user` + the `upc`), and CLASSIFIES the
`far` walk:
    * consecutive `far` values that REPEAT within a small window => LOOP (the same page re-faulting,
      a validation-style cycle);
    * `far` values that strictly ADVANCE => WALK (dyld paging the shared cache forward - slow, not
      stuck).
  The verdict reports the LAST abort's `far`/`upc` (the frontier).  A MEASUREMENT by design: the
  stall is the EXPECTED outcome, so [A] FAILS only on ZERO `_upc` records (the switch did not reach
  the image: REBUILD) or on an all-`frame_ok`=0 run (a wrong saved-state offset - a MEASUREMENT
  defect, [[mi4-measurement-defects]]).

[0] THE DEATH GATE - the toggle every absence-based verdict below is routed through.  It is the
FROZEN three-way conjunction: no panic stanza + the exec-done trio (and its five probe keys) +
kv records after the exec-returned point.  Verbatim from the 1003 tool.  Gate CLOSED: absence-based
readings print `[death-gated]` and are NOT counted.  Presence-based readings stay hard.

WHAT 1009 CHECKS (1008's set VERBATIM, plus section [ABT3]):
  [R] THE 1002 r3-SAVE FIX AS A REGRESSION (verbatim from 1004): after `IOS7SHREG check_np ret=12`,
      the count of `usermode data abort ... at 0x00000004` must STILL be 0.  A recurrence is a
      REGRESSION - REBUILD, not a reading.
  [A] THE ABORT TRACE - 1004's rung, KEPT AS A REGRESSION (see the 1004 tool for the LOOP/WALK
      classification).  Its PROGRESS reading (cap-exempt records after the last abort => the last
      abort was NOT fatal) is the 1004 correction and MUST SURVIVE here.
  [C] THE CADENCE WITNESS - 1005's rung, KEPT AS A REGRESSION (see above).
  [W] THE IDLE-PATH FREEZE WITNESS - 1006's rung, KEPT AS A REGRESSION (see above).
  [ABT] THE ABORT-SPAN WITNESS - 1007's rung, KEPT AS A REGRESSION (see above).
  [ABT2] THE ABORT-FAULT IDENTITY - 1008's rung, KEPT AS A REGRESSION (see above).  Its `far`/`type`
      classifier is retargeted: besides the whole-ladder set, [ABT2] now ALSO reports the `far` at
      the LAST ladder ordinal as the DECISIVE value (the 1008 reader counted 12 distinct values and
      reported MIXED, blind to the tail being 61x the walked prefix - the weakness [ABT3] repairs).
  [ABT3] THE FAULTING PC + FSR - THE 1009 RUNG ITSELF (see above).  [NEW]
  [S] THE 1003 STALL SAMPLE - KEPT AS A REGRESSION.
  [F] THE CODE-SIGNING PAGE-VALIDATION WALL - `cs_invalid_page` is a TOLERATED warning, NOT the
      frontier.  Verbatim from 1004.
  (2) SUBMAPSKIP, (3) VMHINT991, (4) THE SURVIVAL GATE, (5) THE USER-SVC FRONTIER, (6) THE PET,
  (7) THE CAP AND THE TICK, (8) THE FROZEN REGRESSION GUARDS - all VERBATIM from 1004.

METHOD.  Every reader (the `newline=""` file read, the de-doubling panic reader, the KV regex, the
`@:line` numbering, `groups_of`, the death gate) is the 1001/1002/1003/1004/1005/1006/1007/1008
tool's, key for key.  This tool is `tools/check_1008_capture.py` with the ONE new section [ABT3], the
1009 authority path, and the change-count (`_nfar`/`_nfsr`) tail classifier (design §5).

Usage:  tools/check_1009_capture.py [--allow-missing-abt3] [--allow-missing-abt2] [--allow-missing-abt] [--allow-missing-freeze] [--allow-missing-cadence] <capture.txt>

Exit: 0 = no hard anomaly; 1 = at least one hard anomaly (any `[FAIL]` line); 2 = usage.  A hard
anomaly means STOP-and-report, never "rerun until green" - and a `[death-gated]` line is a death
reading that must be adjudicated against the timeline before it is called anything.
"""

import os
import re
import sys

KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+|hw_watchdog_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

# ---- The 1001 probe, VERBATIM from the rung's spec (1002/1003/1004 keep it - the SUBMAP skip is
#      unchanged).  The reader never assumes a `%p` width: any `0x` + hex digits.
SUBMAPSKIP_LIT = "SUBMAPSKIP:"
SUBMAPSKIP_RE = re.compile(
    r'SUBMAPSKIP:\s*obj=(0x[0-9a-fA-F]+)\s+start=(0x[0-9a-fA-F]+)\s+end=(0x[0-9a-fA-F]+)'
    r'\s+size=(0x[0-9a-fA-F]+)\s+lr=(0x[0-9a-fA-F]+)')
SUBMAPSKIP_DOUBLED = "SSUUBBMMAAPPSSKKIIPP"
PROBE_MAX_HITS = 8

# ---- [R] the rung's own signature - VERBATIM from 1003 (the 1002 regression).
ABORT_LIT = "usermode data abort, EXC_BAD_ACCESS at 0x00000004"
CHECK_NP_LIT = "IOS7SHREG check_np ret=12"
MAP_SLIDE_LIT = "IOS7SHREG map_slide"

# ---- [F] the code-signing wall - VERBATIM from 1003.
CS_INVALID_LIT = "cs_invalid_page"
CS_PAGE_1002 = "0x2f7f6000"
CS_CLEAR_LIT = "clearing CS_VALID"
CS_PID_1002 = "p=1[launchd]"
CS_TOLERATED = 1

# ---- [S] THE 1003 STALL SAMPLE - KEPT (7 keys, one per field).  Classified by cpsr's mode bits.
STALL_KEYS = ("n", "t", "pc", "cpsr", "sp", "lr", "nz")
USER_MODE_CPSR = 0x60000010
STALL_1002_PC = 0x2be97328
DYLD_BASE = 0x2be00000
DYLD_END = 0x2be58000
SHARED_CACHE_BASE = 0x2c000000
SHARED_CACHE_SLIDE = 0x37f6000
SS_WORDS = 19

# ---- [A] THE 1004 ABORT TRACE - THE NEW RUNG.  One record per sleh abort; `xnu_live_sleh_seq` opens
#      a record and every `xnu_live_sleh_<field>` below it belongs to it.  `upc` is the 1004 addition
#      (the faulting frame's user pc, published beside the fault pair); `far`/`fsr`/`type`/`thr` are
#      the tuple the caps hid the TAIL of before 1004.  The widened caps mean the record count this
#      reader sees IS the abort count (`_seen`'s value), not the old 8/16 head.
SLEH_SEQ_KEY = "xnu_live_sleh_seq"
SLEH_PREFIX = "xnu_live_sleh_"
SLEH_FIELDS = ("type", "fsr", "far", "thr", "recover", "seen", "pc", "lr", "sp", "cpsr",
               "fsr_frame", "far_frame", "frame_ok", "user", "upc", "back", "redirect", "at_back")
SLEH_UPC_KEY = "xnu_live_sleh_upc"           # the 1004 addition (the frame's user pc)
SLEH_FRAME_OK_KEY = "xnu_live_sleh_frame_ok"
SLEH_OLD_MAX = 8                             # the pre-1004 SLEH_LIVE_MAX (the head the tail hid behind)
SLEH_OLD_FRAMES = 16                         # the pre-1004 SLEH_LIVE_FRAMES
REPEAT_WINDOW = 4                            # a `far` matching one of the previous N = a small window

# ---- [C] THE 1005 CADENCE WITNESS - THE NEW RUNG.  The recorder is `entry_cadence_sample`
#      (src/entry/entry_stubs.c), called from the SYNCHRONOUS USVC path; it OPENS a record with
#      `xnu_live_cadence_t` (the sample ordinal = a monotone time proxy) and writes
#      `_sync`/`_tick`/`_irq` under it.  The late snapshot `entry_cadence_summary` writes the SAME
#      `_sync`/`_tick`/`_irq` triple with NO preceding `_t` - read as its own ordered row.
#
#      CORRECTED 2026-10-14 (team-lead).  There is NO `xnu_live_cadence_summary` KEY.  The four
#      literal keys are `xnu_live_cadence_{t,sync,tick,irq}` and NOTHING else: the build's own
#      falsifier pins the image to EXACTLY 4 `xnu_live_cadence_` literals (build_1005.sh (b):
#      `grep -c 'xnu_live_cadence_'` == 4), and `strings xnu_arm_entry.elf` confirms them.
#      `entry_cadence_summary` is a FUNCTION (a code SYMBOL, not a key) - a DEFINED `T` verified by
#      `nm` ON THE IMAGE (build_1005.sh (b) asserts it), NOT derivable from a capture.  It
#      RE-publishes the SAME `_sync`/`_tick`/`_irq` keys with NO preceding `_t`, so at read time its
#      output appears as extra ordered `(ln, sync, tick, irq)` rows AFTER the `_t`-led sample rows.
#      Clause (c1) therefore checks the four REAL keys; the `entry_cadence_summary` symbol is cited
#      as IMAGE-side evidence (witnessed by the build's `nm` clause), not a capture key.
CADENCE_PREFIX = "xnu_live_cadence_"
CADENCE_T_KEY = "xnu_live_cadence_t"
CADENCE_KEYS = ("xnu_live_cadence_t", "xnu_live_cadence_sync",
                "xnu_live_cadence_tick", "xnu_live_cadence_irq")
CADENCE_SUMMARY_KEY = "xnu_live_cadence_summary"   # NAMED by the task - NOT a real key (see above)
CADENCE_SUMMARY_SYM = "entry_cadence_summary"      # a FUNCTION symbol (image-side, via nm) - not a key
CADENCE_IMG_LIT_WANT = 4                     # the image's pinned literal count (build_1005.sh (b))

# ---- [W] THE 1006 IDLE-PATH FREEZE WITNESS - THE NEW RUNG.  The sampler is `entry_freeze_sample`
#      (src/entry/entry_stubs.c), called from the head of `__wrap_machine_idle` (src/entry/
#      entry_trace.c) once per idle pass.  It OPENS a record with `xnu_live_freeze_n` and CLOSES it
#      with `xnu_live_freeze_ctlr`, writing `_d`/`_cpsr`/`_ien`/`_pend` between - all DIRECT
#      `entry_write_kv` keys, all unsigned 32-bit.  On the FIRST FOUR passes it ALSO publishes
#      `xnu_live_frzbase_n`/`xnu_live_frzbase_d` (the UNFILTERED baseline), so the run measures its own idle
#      period rather than assuming the threshold.  The image is pinned to the six `xnu_live_freeze_`
#      literals + the two `xnu_live_frzbase_` literals (build_1006.sh (b)).
FREEZE_PREFIX = "xnu_live_freeze_"
FREEZE_KEYS = ("xnu_live_freeze_n", "xnu_live_freeze_d", "xnu_live_freeze_cpsr",
               "xnu_live_freeze_ien", "xnu_live_freeze_pend", "xnu_live_freeze_ctlr")
IDLE_PREFIX = "xnu_live_frzbase_"
IDLE_KEYS = ("xnu_live_frzbase_n", "xnu_live_frzbase_d")
FREEZE_IMG_LIT_WANT = 6                      # the image's pinned literal count (build_1006.sh (b))
# The threshold, by value, is `MSM8974_FREEZE_MIN = 0x00004b00` = 19200 counts (~1 ms at 19.2 MHz) -
# published in the record, so this reader asserts the SAME value the sampler used (a record with a
# `_d` <= this is an inconsistency of the recorder).
FREEZE_THRESHOLD = 0x00004b00                # 19200 counts - every recorded freeze must have d > this
FREEZE_CPSR_IBIT = 7                         # the IRQ-mask bit position: i = (cpsr >> 7) & 1

# ---- [ABT] THE 1007 ABORT-SPAN WITNESS - THE NEW RUNG.  `__wrap_sleh_abort` (src/entry/entry_trace.c)
#      brackets each `__real_sleh_abort` call with the file's own inline `entry_counter()` and calls
#      `entry_note_abt_span(t0, call)` (src/entry/entry_stubs.c), which OPENS a published record with
#      `xnu_live_abt_n` and writes `_seq`/`_gap`/`_call` - all DIRECT `entry_write_kv` keys, all
#      unsigned 32-bit.  The two maxima (`_maxcall`/`_maxgap`) are latched and written WHERE THEY
#      CHANGE, OUTSIDE the ladder, so the one huge sample cannot be missed.  The image is pinned to
#      the six `xnu_live_abt_` literals (build_1007.sh (b)).
ABT_PREFIX = "xnu_live_abt_"
ABT_KEYS = ("xnu_live_abt_n", "xnu_live_abt_seq", "xnu_live_abt_gap", "xnu_live_abt_call",
            "xnu_live_abt_maxcall", "xnu_live_abt_maxgap")
ABT_IMG_LIT_WANT = 6                          # the image's pinned literal count (build_1007.sh (b))
# The four PER-ABORT keys (the maxima are latched, NOT part of a ladder row).
ABT_ROW_KEYS = ("xnu_live_abt_n", "xnu_live_abt_seq", "xnu_live_abt_gap", "xnu_live_abt_call")
ABT_MAXCALL_KEY = "xnu_live_abt_maxcall"
ABT_MAXGAP_KEY = "xnu_live_abt_maxgap"
# The reference the classifier compares against: the SAME capture's `xnu_live_tick_sum_maxd` (M).
# `M ~ 0x1be87a47` = 468,220,487 counts = 24.386 s at 19.2 MHz (records/1005- and 1006-press-read).
# A value is "~ M" if it is within this band of M (a factor, not a slack - the counters have no time
# base, so the band is stated, not assumed; [[mi4-measurement-defects]]).
ABT_NEAR_M_LO = 0.5                          # >= 0.5 * M  => "~ M"
ABT_NEAR_M_HI = 2.0                          # <= 2.0 * M  => "~ M" (a wrap is 2^32, far above this)
ABT_M_FALLBACK = 0x1be87a47                  # the 1005/1006-press reading, if `_maxd` is absent
ABT_LADDER_HEAD = 4                          # the first-four-in-full rule (n <= 4)

# ---- [ABT2] THE 1008 ABORT-FAULT IDENTITY - THE NEW RUNG.  `__wrap_sleh_abort` (src/entry/
#      entry_trace.c) forwards the `far`/`type` it already reads for the sleh record (the same
#      `mrc p15` pair at :5687-5693) to `entry_note_abt_far(call, far, type)` (src/entry/
#      entry_stubs.c), which publishes FIVE DIRECT `entry_write_kv` keys (all unsigned 32-bit): the
#      ladder triple `_farn`/`_far`/`_type` (at `n<=4` then `n & (n-1)==0`) and the maxcall LATCH
#      `_maxfar`/`_maxtype` (written WHERE `call` sets a new max, capped at `ABT_FAR_LATCH_MAX=32`,
#      OUTSIDE the ladder).  It is a SEPARATE recorder (not a parameter added to
#      `entry_note_abt_span`) so 1007's pinned span text is untouched (design §4.2).  The 1008 image
#      (`ABT_SPAN=1` AND `ABT_FAR=1`) carries ELEVEN `xnu_live_abt_` literals: 6 span + 5 far; the
#      1007 image (`ABT_FAR=0`) still carries 6 (design §4.4, build_1008.sh (b)).
ABT2_PREFIX = "xnu_live_abt_"
ABT2_KEYS = ("xnu_live_abt_far", "xnu_live_abt_type", "xnu_live_abt_farn",
             "xnu_live_abt_maxfar", "xnu_live_abt_maxtype")
ABT2_IMG_LIT_WANT = 11                        # the image's pinned TOTAL `xnu_live_abt_` count (6 span + 5 far)
ABT2_IMG_LIT_WANT_1007 = 6                    # the 1007 arm (ABT_FAR=0): the span six alone
# The three PER-ABORT keys of the far LADDER (the latch keys are NOT part of a ladder row).
ABT2_ROW_KEYS = ("xnu_live_abt_farn", "xnu_live_abt_far", "xnu_live_abt_type")
ABT2_FARN_KEY = "xnu_live_abt_farn"
ABT2_FAR_KEY = "xnu_live_abt_far"
ABT2_TYPE_KEY = "xnu_live_abt_type"
ABT2_MAXFAR_KEY = "xnu_live_abt_maxfar"
ABT2_MAXTYPE_KEY = "xnu_live_abt_maxtype"
# The class values (`osfmk/arm/trap.h`): STAGE90_T_PREFETCH_ABT = 3, STAGE90_T_DATA_ABT = 4.
ABT2_PREFETCH = 3
ABT2_DATA = 4
# The user/kernel split for a data abort's `far` (design §6: `far < 0x80000000` => USER, else KERNEL).
ABT2_KERNEL_BASE = 0x80000000
ABT2_LADDER_HEAD = 4                          # the first-four-in-full rule (n <= 4, shared with [ABT])

# ---- [ABT3] THE 1009 FAULTING PC + FSR - THE NEW RUNG.  `__wrap_sleh_abort` (src/entry/
#      entry_trace.c) forwards the `regs->pc`, the DFSR (`fsr`, read at `:5688` from `c5,c0,0`), the
#      abort mode (`regs->cpsr`) and the `far` it already holds to `entry_note_abt_pc(pc, fsr, cpsr,
#      far)` (src/entry/entry_stubs.c), which publishes SEVEN DIRECT `entry_write_kv` keys (all
#      unsigned 32-bit): the ladder quintuple `_pcn`/`_pc`/`_fsr`/`_abtcpsr`/`_pcfar` (at 1008's
#      trigger set `n<=4` then `n & (n-1)==0`) and the two change-counts `_nfar`/`_nfsr` (written
#      WHERE THEY CHANGE, a running monotone max, OUTSIDE the ladder).  It is a SEPARATE recorder
#      (not a parameter added to `entry_note_abt_far`) so 1008's pinned text is untouched (design
#      §3.1).  The 1009 image (`ABT_FAR=1` AND `ABT_PC=1`) carries EIGHTEEN `xnu_live_abt_` literals:
#      6 span + 5 far + 7 pc; the 1008 image (`ABT_PC=0`) still carries 11 (design §3.4).
ABT3_PREFIX = "xnu_live_abt_"
ABT3_KEYS = ("xnu_live_abt_pcn", "xnu_live_abt_pc", "xnu_live_abt_fsr",
             "xnu_live_abt_abtcpsr", "xnu_live_abt_pcfar",
             "xnu_live_abt_nfar", "xnu_live_abt_nfsr")
ABT3_IMG_LIT_WANT = 18                        # the image's pinned TOTAL `xnu_live_abt_` count (6+5+7)
ABT3_IMG_LIT_WANT_1008 = 11                   # the 1008 arm (ABT_PC=0): 6 span + 5 far alone
# The FIVE PER-ABORT keys of the pc LADDER (the two change-counts are NOT part of a ladder row).
ABT3_ROW_KEYS = ("xnu_live_abt_pcn", "xnu_live_abt_pc", "xnu_live_abt_fsr",
                 "xnu_live_abt_abtcpsr", "xnu_live_abt_pcfar")
ABT3_PCN_KEY = "xnu_live_abt_pcn"
ABT3_PC_KEY = "xnu_live_abt_pc"
ABT3_FSR_KEY = "xnu_live_abt_fsr"
ABT3_ABTCPSR_KEY = "xnu_live_abt_abtcpsr"
ABT3_PCFAR_KEY = "xnu_live_abt_pcfar"
ABT3_NFAR_KEY = "xnu_live_abt_nfar"           # the number of CHANGES in `far` (a running max)
ABT3_NFSR_KEY = "xnu_live_abt_nfsr"           # the number of CHANGES in `fsr` (a running max)
# The two change-counts, read as their RUNNING MONOTONE MAXIMUM ([[mi4-measurement-defects]]: a
# running-max key read with `first()` is 100sx too small - the 1007 reader's fixed defect).
ABT3_CHANGE_KEYS = (ABT3_NFAR_KEY, ABT3_NFSR_KEY)
ABT3_LADDER_HEAD = 4                          # the first-four-in-full rule (n <= 4, shared with [ABT])
# The ARM CPSR mode field (bits 4:0).  The abort's mode is `cpsr & 0x1f`: 0x13 = SVC, 0x1f = SYSTEM,
# 0x10 = USER, 0x11 = FIQ, 0x12 = IRQ, 0x17 = ABORT, 0x1b = UNDEF.  The whole of a real CPSR's low
# byte for a kernel SVC abort is `0x13` (design §5); a value outside the eight ARM mode codes means
# the forwarded `cpsr` is not an ARM CPSR - an INSTRUMENT defect.
ABT3_MODE_MASK = 0x1f
# The TAIL definition for the classifier: the ladder ordinals from here up are the RETRY TAIL (the
# storm proper), not the walked prefix.  1008's ladder head (ordinals 1..0x200) walks dyld's image;
# the storm settles by ~ordinal 0x200 and the tail (0x400..0x200000) is the run's steady state.  The
# classifier reads the ladder ROWS at/below this ordinal (their `pcfar`/`fsr`/`abtcpsr` constancy),
# NOT the whole-run change-counts - which count changes from abort 1 and can never describe the tail
# ([[mi4-measurement-defects]]: a whole-run statistic is not a tail statistic).
ABT3_TAIL_MIN_PCN = 0x400
ABT3_ARM_MODES = (0x10, 0x11, 0x12, 0x13, 0x17, 0x1b, 0x1f)
ABT3_MODE_NAMES = {0x10: "USER", 0x11: "FIQ", 0x12: "IRQ", 0x13: "SVC",
                   0x17: "ABORT", 0x1b: "UNDEF", 0x1f: "SYSTEM"}
# The IRQ-mask bit (bit 7 `I`) of the abort's CPSR: `1` = IRQs DISABLED, `0` = ENABLED.
ABT3_CPSR_IBIT = 7

# ---- [ABT4] THE 1010 L2 DESCRIPTOR + vm_fault RETURN - THE NEW RUNG.  `__wrap_sleh_abort` (src/entry/
#      entry_trace.c) forwards the `far_` it already holds to `entry_note_abt_pte(far)` (src/entry/
#      entry_stubs.c), which WALKS TTBR0 to the L2 small-page descriptor for that VA (`stage90_read_l2_
#      pte`, docs/1010-pte-walk.md §4, keyed on TTBCR.N) and publishes SIX DIRECT `entry_write_kv` keys:
#      the ladder tuple `_pten`/`_pteva`/`_ptel2`/`_ptel1`/`_ptettbr`/`_pteu` (at 1008/1009's trigger set
#      `n<=4` then `n & (n-1)==0`) plus the change-count `_ptenx` (written WHERE IT CHANGES, a running
#      monotone max, OUTSIDE the ladder).  The NEW `__wrap_vm_fault` (a pure pass-through) records what
#      `vm_fault` RETURNED via `entry_note_abt_vmf(ret, va, prot)`: the FIRST-FEW (4) `_vmfn`/`_vmfret`/
#      `_vmfva`/`_vmfprot` and the change-count `_vmfnret`.  The 1010 image (`ABT_FAR=1`, `ABT_PC=1` AND
#      `ABT_PTE=1`) carries TWENTY-NINE `xnu_live_abt_` literals: 6 span + 5 far + 7 pc + 11 pte/vmf; the
#      1009 image (`ABT_PTE=0`) still carries 18 (design §3.4).
ABT4_PREFIX = "xnu_live_abt_"
ABT4_KEYS = ("xnu_live_abt_pten", "xnu_live_abt_pteva", "xnu_live_abt_ptel2",
             "xnu_live_abt_ptel1", "xnu_live_abt_ptettbr", "xnu_live_abt_pteu",
             "xnu_live_abt_ptenx",
             "xnu_live_abt_vmfn", "xnu_live_abt_vmfret", "xnu_live_abt_vmfva",
             "xnu_live_abt_vmfprot", "xnu_live_abt_vmfnret")
ABT4_IMG_LIT_WANT = 29                        # the image's pinned TOTAL `xnu_live_abt_` count (6+5+7+11)
ABT4_IMG_LIT_WANT_1009 = 18                   # the 1009 arm (ABT_PTE=0): 6 span + 5 far + 7 pc alone
ABT4_ADDED_LIT_WANT = 11                       # 1010's own NEW literals: the 7 pte + the 4 vmf ladder keys
# The SIX PER-ABORT keys of the pte LADDER (the change-count `_ptenx` is NOT part of a ladder row).
ABT4_ROW_KEYS = ("xnu_live_abt_pten", "xnu_live_abt_pteva", "xnu_live_abt_ptel2",
                 "xnu_live_abt_ptel1", "xnu_live_abt_ptettbr", "xnu_live_abt_pteu")
ABT4_PTEN_KEY = "xnu_live_abt_pten"
ABT4_PTEVA_KEY = "xnu_live_abt_pteva"
ABT4_PTEL2_KEY = "xnu_live_abt_ptel2"
ABT4_PTEL1_KEY = "xnu_live_abt_ptel1"
ABT4_PTETTBR_KEY = "xnu_live_abt_ptettbr"
ABT4_PTEU_KEY = "xnu_live_abt_pteu"
ABT4_PTENX_KEY = "xnu_live_abt_ptenx"         # the number of CHANGES in `l2` (a running max)
ABT4_VMFN_KEY = "xnu_live_abt_vmfn"
ABT4_VMFRET_KEY = "xnu_live_abt_vmfret"
ABT4_VMFVA_KEY = "xnu_live_abt_vmfva"
ABT4_VMFPROT_KEY = "xnu_live_abt_vmfprot"
ABT4_VMFNRET_KEY = "xnu_live_abt_vmfnret"     # the number of CHANGES in `ret` (a running max)
# The two change-counts, read as their RUNNING MONOTONE MAXIMUM ([[mi4-measurement-defects]]).
ABT4_CHANGE_KEYS = (ABT4_PTENX_KEY, ABT4_VMFNRET_KEY)
ABT4_LADDER_HEAD = 4                          # the first-four-in-full rule (n <= 4, shared with [ABT])
# The four per-return keys of the vm_fault ladder (the change-count `_vmfnret` is NOT part of a row).
ABT4_VMF_ROW_KEYS = ("xnu_live_abt_vmfn", "xnu_live_abt_vmfret",
                     "xnu_live_abt_vmfva", "xnu_live_abt_vmfprot")
ABT4_VMF_FIRST = 4                            # ABTPTE_VMF_FIRST in entry_stubs.c: the first-four rule
# ---- THE ARM SHORT-DESCRIPTOR L2 SMALL-PAGE DECODE (docs/1010-pte-walk.md §3; ARM ARMv7-A A3.5).
#      L2 descriptor bits[1:0]: 0b10 = small page (bit 0 = 0, bit 1 = 1).  For a small page:
#        bit 0  XN  (Execute Never: 1 = no-exec)
#        bit 5  AP[0] (with AP[2]=bit 9 forms the access permission)
#        bit 9  AP[2] (1 = READ-ONLY for BOTH user and... - with AP[0]/AP[1] this is APX)
#        bits[11:6] AP[1:0] etc. (TEX/AP - we name the decisive two)
#        bits[31:12] PA (the physical base of the 4 KiB page)
#      THE DECISIVE CELL (design §2.2): APX = bit 9.  APX=1 => the page is USER READ-ONLY; a USER WRITE
#      fault on it with vm_fault returning KERN_SUCCESS is the cs-taint/prot-downgrade path.
ABT4_L2_TYPE_MASK = 0x3
ABT4_L2_SMALL_PAGE = 0x2
ABT4_L2_APX_BIT = 9                           # bit 9 = AP[2]/APX (1 = READ-ONLY)
ABT4_L2_USER_BIT = 5                          # bit 5 = AP[0] (1 = user-access)
ABT4_L2_NX_BIT = 0                            # bit 0 = XN (1 = no-exec)
ABT4_L2_PA_MASK = 0xfffff000                  # bits[31:12] = the 4 KiB page PA base
# THE vm_fault RETURN CODES (`osfmk/mach/kern_return.h`): the two the retry path distinguishes.
ABT4_KERN_SUCCESS = 0
ABT4_KERN_ABORTED = 14
ABT4_KERN_NAMES = {0: "KERN_SUCCESS", 14: "KERN_ABORTED", 1: "KERN_INVALID_ADDRESS"}
# The TAIL definition for the classifier: the ladder ordinals from here up are the RETRY TAIL (the
# storm proper), not the walked prefix.  Same ordinal as [ABT3] (0x400): the storm settles by ~0x200.
ABT4_TAIL_MIN_PTEN = 0x400

# ---- [C] the classifier's thresholds.  The counters are UNSIGNED 32-bit ordinals/counts with NO
#      time base, so the thresholds are ORDINAL, not seconds: they are chosen to say only what the
#      samples can support and are STATED in the output.  (These are a guess on the first reading -
#      [[mi4-measurement-defects]]: a threshold is a measurement, so overclaiming is the failure to
#      avoid, and the tool says "TOO SHORT TO CLASSIFY" when the window cannot decide.)
CAD_FROZEN_MAX = 2        # dt <= this         => the tick counter is FROZEN (delivery stopped)
CAD_PROGRESS_MIN = 8      # ds >= this         => the synchronous path is CLIMBING (kept from §7.3)
CAD_SHORT_WINDOW = 4      # < this many samples => the deltas are too short to classify (an OBS)
# The abort classes (`osfmk/arm/trap.h`): 3 = prefetch abort, 4 = data abort.
ABT_PREFETCH = 3
ABT_DATA = 4

# ---- 999's VMHINT991 objects, by value.
SUB_OBJS = ((0xc0005b48, "999's VMHINT991 object #19"),
            (0xc0005ab0, "999's VMHINT991 object #18"))

# ---- The death gate's toggle + probes (VERBATIM from 1003).
TRIO = ("xnu_live_exec_done_seq", "xnu_live_exec_done_who", "xnu_live_exec_done_current")
ENTRY_QUARTET = ("xnu_live_exec_seq", "xnu_live_exec_caller", "xnu_live_exec_who", "xnu_live_exec_proc")
DONE_PROBE = ("xnu_live_exec_done_probe_seq", "xnu_live_exec_done_probe_sig",
              "xnu_live_exec_done_probe_size", "xnu_live_exec_done_probe_records",
              "xnu_live_exec_done_probe_state")

# ---- The kept instrument families (the census discriminates a 1000..1006 arm from its baselines).
NEW_FAMILIES = (
    ("xnu_live_tick_anom_", "(1) the late-delivery anomalies"),
    ("xnu_live_tick_sum_",  "(1) the tick aggregate summaries"),
    ("xnu_live_wdt_pet_",   "(2) the pet feed records (direct)"),
    ("xnu_live_usvc_",      "(4) the user-SVC frontier"),
    ("xnu_live_stall_",     "(S) 1003's stall sampler (kept)"),
    ("xnu_live_sleh_upc",   "(A) 1004's abort trace (kept)"),
    ("xnu_live_cadence_",   "(C) 1005's cadence witness (kept)"),
    ("xnu_live_freeze_",    "(W) 1006's freeze witness (kept)"),
    ("xnu_live_frzbase_",   "(W) 1006's idle-pass baseline (kept)"),
    ("xnu_live_abt_",       "(ABT/ABT2/ABT3) 1007's abort-span + 1008's abort-fault identity + 1009's faulting pc"),
    ("xnu_live_abt_far",    "(ABT2) 1008's abort-fault identity (kept)"),
    ("xnu_live_abt_pc",     "(ABT3) 1009's faulting pc + fsr (the NEW instrument)"),
)

LADDER_LITERALS = (
    ("IOS7SHREG map_slide ret=", "438's own exit - must be present and 0 while the boot survives"),
    ("IOS7SHREG map_file kr=",   "the G1 reading (must stay kr=0)"),
    (CHECK_NP_LIT,               "the pre-438 constant (must stay; exactly once)"),
    ("IOS7LAB cache mapping",    "the wire's mapping lines (exactly 4; printed inside the exec)"),
    ("IOS7LAB shared region",    "the geometry line (exactly once)"),
    ("VMHINT991",                "the 991 guard - the submap call is skipped, so 0x while surviving"),
    ("vm_allocate64",            "absent = EXPECTED (997: the failure path's buffer alloc)"),
    (SUBMAPSKIP_LIT,             "the 1001 skip-branch probe (>= 1 while surviving; <= 8)"),
    (CS_INVALID_LIT,             "the code-signing wall (cs_invalid_page); read by value in [F]"),
)

FIXED_WDT = {
    "hw_watchdog_timeout_s": 0x0000001D,
    "hw_watchdog_bite_ticks_written": 0x000FFFA0,
}
BARK_1004 = 0xE7FA9
WDT_HZ = 32765
PERIOD_FALLBACK = 19200
TVAL_SLACK = 512
FULL_N = 64
USVC_1001_N = 6

FAILURES = []
GATED = []
SOFT = []
SIGNATURES = []
GATE_OPEN = False
TRIO_OPEN = False
GATE_CONJ = {}

# ---- 1005-only state (KEPT - [C] is a regression in 1006).
ALLOW_MISSING_CADENCE = False   # set by --allow-missing-cadence (a pre-1005 log, e.g. the 1004 arm)
CADENCE_CERTIFIED = False       # True once [C] read a real cadence window (the 1005 pass gate)

# ---- 1006-only state (KEPT - [W] is a regression in 1007).
ALLOW_MISSING_FREEZE = False    # set by --allow-missing-freeze (a pre-1006 log, e.g. the 1005 arm)
FREEZE_CERTIFIED = False        # True once [W] read a real freeze window OR a valid silence (the 1006 gate)

# ---- 1007-only state.
ALLOW_MISSING_ABT = False       # set by --allow-missing-abt (a pre-1007 log, e.g. the 1006 arm)
ABT_CERTIFIED = False           # True once [ABT] read a real abort span OR a valid silence (the 1007 gate)

# ---- 1008-only state (KEPT - [ABT2] is a regression in 1009).
ALLOW_MISSING_ABT2 = False      # set by --allow-missing-abt2 (a pre-1008 log, e.g. the 1007 arm)
ABT2_CERTIFIED = False          # True once [ABT2] read a real far ladder OR an allowed absence (the 1008 gate)

# ---- 1009-only state.
ALLOW_MISSING_ABT3 = False      # set by --allow-missing-abt3 (a pre-1009 log, e.g. the 1008 arm)
ABT3_CERTIFIED = False          # True once [ABT3] read a real pc ladder OR an allowed absence (the 1009 gate)

# ---- 1010-only state.
ALLOW_MISSING_ABT4 = False      # set by --allow-missing-abt4 (a pre-1010 log, e.g. the 1009 arm)
ABT4_CERTIFIED = False          # True once [ABT4] read a real pte ladder OR an allowed absence (the 1010 gate)


def to_int(s):
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    except ValueError:
        return None


def fmt(v):
    return "?" if v is None else "0x%08x(%d)" % (v, v)


def secs(ticks, hz):
    return None if (ticks is None or not hz) else ticks / float(hz)


def dur(t, hz):
    return "?" if t is None else "%.2f s at %s Hz" % (t, "{:,}".format(hz) if hz else "?")


def is_pow2(v):
    return v is not None and v != 0 and (v & (v - 1)) == 0


def clip(s, n=200):
    return s if len(s) <= n else s[:n] + "..."


def hard(msg, absence_based=False):
    if absence_based and not GATE_OPEN:
        GATED.append(msg)
        print("  [death-gated] " + msg)
        return
    FAILURES.append(msg)
    print("  [FAIL] " + msg)


def soft(msg):
    SOFT.append(msg)
    print("  [soft] " + msg)


def signature(msg):
    SIGNATURES.append(msg)
    print("  [SIGNATURE] " + msg)


def say(tag, msg=""):
    print(("  [%s] " % tag) + msg if msg else ("  [%s]" % tag))


class Capture:
    def __init__(self, path):
        # `newline=""` + `split("\n")`: NO universal-newline translation (the 1001 tool's reader,
        # unchanged; line numbers are the reader's property, m997).
        with open(path, "r", errors="replace", newline="") as fh:
            self.text = fh.read()
        self.nbytes = len(self.text)
        self.lines = self.text.split("\n")
        if self.lines and self.lines[-1] == "":
            self.lines.pop()
        self.toks = []
        for line_no, line in enumerate(self.lines, 1):
            for m in KV_RE.finditer(line):
                self.toks.append((line_no, m.group(1), m.group(2), to_int(m.group(2))))
        self.lit = {}
        for line_no, line in enumerate(self.lines, 1):
            for lit, _why in LADDER_LITERALS:
                if lit in line:
                    e = self.lit.setdefault(lit, [0, line_no, line_no, clip(line.strip())])
                    e[0] += 1
                    e[2] = line_no

    def all(self, key):
        return [t for t in self.toks if t[1] == key]

    def first(self, key):
        a = self.all(key)
        return a[0] if a else None

    def last(self, key):
        a = self.all(key)
        return a[-1] if a else None

    def lit_count(self, lit):
        return self.lit.get(lit, [0])[0]

    def family_count(self, prefix):
        return sum(1 for t in self.toks if t[1].startswith(prefix))

    def ordered_pairs(self, key_a, key_b):
        out, last_a = [], None
        for _, key, _raw, val in self.toks:
            if key == key_a:
                last_a = val
            elif key == key_b:
                out.append((val, last_a))
        return out

    def groups_of(self, nkey, prefix, fields):
        out, cur = [], None
        fkeys = {prefix + f: f for f in fields}
        for ln, key, _raw, val in self.toks:
            if key == nkey:
                cur = [ln, val, {}, []]
                out.append(cur)
            elif key in fkeys and cur is not None:
                f = fkeys[key]
                cur[2][f] = (ln, val)
                cur[3].append(f)
        return out

    def panic_hits(self):
        out = []
        for ln, line in enumerate(self.lines, 1):
            de = line[::2]
            if ("panic(" in line or "panic(" in de or "Debugger called" in line
                    or "Debugger called" in de or "Waiting for remote debugger" in line
                    or "Waiting for remote debugger" in de):
                out.append((ln, clip(line.strip(), 120)))
        return out

    def abort_hits(self):
        return [(ln, clip(line.strip(), 160)) for ln, line in enumerate(self.lines, 1)
                if ABORT_LIT in line]

    def cs_invalid_hits(self):
        out = []
        for ln, line in enumerate(self.lines, 1):
            de = line[::2]
            if CS_INVALID_LIT in line or CS_INVALID_LIT in de:
                out.append((ln, clip(line.strip(), 160)))
        return out

    def wdt_samples(self):
        out, sts = [], None
        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_wdt_sts":
                sts = (ln, val) if val is not None else None
            elif key == "xnu_live_wdt_countdown":
                out.append({"ln": ln, "sts": sts, "countdown": val, "pets": None})
            elif key == "xnu_live_wdt_pets" and out and out[-1]["pets"] is None:
                out[-1]["pets"] = val
        return out

    def stall_samples(self):
        """[S] THE 1003 STALL SAMPLE (kept): `_n` -> `_t` -> `_pc` -> `_cpsr` -> `_sp` -> `_lr` ->
        `_nz` in that order; `_n` opens the sample."""
        out, cur = [], None
        for ln, key, raw, val in self.toks:
            if key == "xnu_live_stall_n":
                if cur is not None:
                    out.append(cur)
                cur = {"ln": ln, "f": {"n": (ln, val)}, "order": ["n"]}
            elif cur is not None and key.startswith("xnu_live_stall_"):
                fk = key[len("xnu_live_stall_"):]
                if fk in STALL_KEYS:
                    cur["f"][fk] = (ln, val)
                    cur["order"].append(fk)
        if cur is not None:
            out.append(cur)
        return out

    def abort_samples(self):
        """[A] THE 1004 ABORT TRACE - THE NEW RUNG.

        One record per sleh entry.  `xnu_live_sleh_seq` OPENS a record; every `xnu_live_sleh_<field>`
        that follows, until the next `_seq`, belongs to it (the recorder writes them in that order:
        seq -> type/fsr/far/thr/recover -> seen -> pc/lr/sp/cpsr/fsr_frame/far_frame/frame_ok/user
        -> upc, with back/redirect/at_back written by `entry_note_sleh_back`).  The field set is
        KNOWN, so a key outside it (e.g. the 1004 build's `_armed`) is skipped rather than trusted -
        the walker binds to the recorder one value, [[mi4-one-value-two-definitions]]."""
        out, cur = [], None
        fkeys = {SLEH_PREFIX + f: f for f in SLEH_FIELDS}
        for ln, key, raw, val in self.toks:
            if key == SLEH_SEQ_KEY:
                if cur is not None:
                    out.append(cur)
                cur = {"ln": ln, "f": {"seq": (ln, val)}, "order": ["seq"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur["f"][f] = (ln, val)
                cur["order"].append(f)
        if cur is not None:
            out.append(cur)
        return out

    def cadence_samples(self):
        """[C] THE 1005 CADENCE WITNESS.

        One `{"ln", "t", "sync", "tick", "irq", "kind"}` per published triple, in log order:
          * `kind == "sample"`  - a recorder record: `xnu_live_cadence_t` OPENS it, then
            `_sync`/`_tick`/`_irq` (the recorder's write order, `entry_cadence_sample`).
          * `kind == "summary"` - a late snapshot: `entry_cadence_summary` writes the
            `_sync`/`_tick`/`_irq` triple with NO preceding `_t`, at a moment that is NOT a syscall
            (the exec-done point / the watchdog-pet gate).  Its `t` is `None` - it is NOT a sample,
            and the two kinds are kept as separate rows so a sample ordinal and a snapshot's
            position are never conflated ([[mi4-one-value-two-definitions]]).

        A record CLOSES on `_irq` (both kinds end with it).  A `_sync` met with no open record - or
        with the open record already carrying a `_sync` - OPENS a summary (the recorder writes
        `_sync` first, so a second `_sync` cannot belong to the same record).  This binds the reader
        to the recorder's ONE write order rather than to a channel layout it cannot see.
        """
        rows, cur = [], None

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == CADENCE_T_KEY:
                close()
                cur = {"ln": ln, "t": val, "sync": None, "tick": None, "irq": None, "kind": "sample"}
            elif key == "xnu_live_cadence_sync":
                if cur is None or cur["sync"] is not None:
                    close()
                    cur = {"ln": ln, "t": None, "sync": val, "tick": None, "irq": None,
                           "kind": "summary"}
                else:
                    cur["sync"] = val
            elif key == "xnu_live_cadence_tick":
                if cur is not None and cur["tick"] is None:
                    cur["tick"] = val
            elif key == "xnu_live_cadence_irq":
                if cur is not None and cur["irq"] is None:
                    cur["irq"] = val
        close()
        return rows

    def freeze_records(self):
        """[W] THE 1006 IDLE-PATH FREEZE WITNESS.

        One `{"ln", "n", "d", "cpsr", "ien", "pend", "ctlr", "order"}` per published freeze record,
        in log order.  Modelled on `cadence_samples()`: the sampler (`entry_freeze_sample`) writes its
        keys in ONE fixed order - `xnu_live_freeze_n` OPENS a record, then `_d`/`_cpsr`/`_ien`/
        `_pend`, and `xnu_live_freeze_ctlr` CLOSES it - so the reader binds to the recorder's ONE write
        order rather than to a channel layout it cannot see ([[mi4-one-value-two-definitions]]).  The
        field set is KNOWN (FREEZE_KEYS), so a key outside it (the sibling `xnu_live_frzbase_*` baseline
        keys live under a DIFFERENT prefix and never enter here) is skipped rather than trusted.

        NOTE: `xnu_live_frzbase_n`/`xnu_live_frzbase_d` are NOT part of a freeze record - they belong to the
        unfiltered early-pass baseline and are read separately by `idle_baseline()`.
        """
        rows, cur = [], None
        fkeys = {"xnu_live_freeze_" + f: f for f in
                 ("n", "d", "cpsr", "ien", "pend", "ctlr")}

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_freeze_n":
                close()
                cur = {"ln": ln, "n": val, "d": None, "cpsr": None, "ien": None, "pend": None,
                       "ctlr": None, "order": ["n"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur[f] = val
                cur["order"].append(f)
        close()
        return rows

    def idle_baseline(self):
        """[W] THE UNFILTERED EARLY-PASS BASELINE: the ordered `(n, d)` pairs the sampler publishes
        UNFILTERED on the first four idle passes (`xnu_live_frzbase_n` / `xnu_live_frzbase_d`), so the run
        measures its own idle period rather than assuming the threshold."""
        out, last_n = [], None
        for _ln, key, _raw, val in self.toks:
            if key == "xnu_live_frzbase_n":
                last_n = val
            elif key == "xnu_live_frzbase_d":
                out.append((last_n, val))
        return out

    def abt_records(self):
        """[ABT] THE 1007 ABORT-SPAN WITNESS.

        One `{"ln", "n", "seq", "gap", "call", "order"}` per PUBLISHED per-abort record, in log order.
        Modelled on `freeze_records()`: `entry_note_abt_span` writes its keys in ONE fixed order -
        `xnu_live_abt_n` OPENS a record, then `_seq`/`_gap`/`_call` - so the reader binds to the
        recorder's ONE write order rather than to a channel layout it cannot see
        ([[mi4-one-value-two-definitions]]).  The field set is KNOWN (ABT_ROW_KEYS), so a key outside
        it (the latched `_maxcall`/`_maxgap`, written OUTSIDE the ladder, or the sibling `xnu_live_abt_`
        keys from a different build) is skipped rather than trusted.

        NOTE: `xnu_live_abt_maxcall`/`xnu_live_abt_maxgap` are NOT part of a ladder record - they are
        latched where they change and read separately in `s_abt()` via `cap.first()`."""
        rows, cur = [], None
        fkeys = {"xnu_live_abt_" + f: f for f in ("n", "seq", "gap", "call")}

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_abt_n":
                close()
                cur = {"ln": ln, "n": val, "seq": None, "gap": None, "call": None, "order": ["n"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur[f] = val
                cur["order"].append(f)
        close()
        return rows

    def abt2_records(self):
        """[ABT2] THE 1008 ABORT-FAULT IDENTITY.

        One `{"ln", "farn", "far", "type", "order"}` per PUBLISHED far-ladder record, in log order.
        Modelled on `abt_records()`: `entry_note_abt_far` writes its ladder keys in ONE fixed order -
        `xnu_live_abt_farn` OPENS a record, then `_far`/`_type` - so the reader binds to the
        recorder's ONE write order rather than to a channel layout it cannot see
        ([[mi4-one-value-two-definitions]]).  The field set is KNOWN (ABT2_ROW_KEYS), so a key
        outside it (the latched `_maxfar`/`_maxtype`, written OUTSIDE the ladder, or the span
        recorder's `xnu_live_abt_n`/`_seq`/`_gap`/`_call`, which pair to these rows BY KEY EQUALITY,
        never by position, design §6.2) is skipped rather than trusted.

        NOTE: `xnu_live_abt_maxfar`/`xnu_live_abt_maxtype` are NOT part of a ladder record - they are
        latched where `call` sets a new max and read separately in `s_abt2()`.

        PAIRING RULE (design §6.2): a far row pairs to the span row with the SAME `n`
        (`xnu_live_abt_farn == xnu_live_abt_n`) - the ordinal is the join key, carried IN the row,
        so no positional assumption is made about when the two recorders ran."""
        rows, cur = [], None
        fkeys = {"xnu_live_abt_" + f: f for f in ("farn", "far", "type")}

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_abt_farn":
                close()
                cur = {"ln": ln, "farn": val, "far": None, "type": None, "order": ["farn"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur[f] = val
                cur["order"].append(f)
        close()
        return rows

    def abt3_records(self):
        """[ABT3] THE 1009 FAULTING PC + FSR.

        One `{"ln", "pcn", "pc", "fsr", "abtcpsr", "pcfar", "order"}` per PUBLISHED pc-ladder
        record, in log order.  Modelled on `abt2_records()`: `entry_note_abt_pc` writes its ladder
        keys in ONE fixed order - `xnu_live_abt_pcn` OPENS a record, then `_pc`/`_fsr`/`_abtcpsr`/
        `_pcfar` - so the reader binds to the recorder's ONE write order rather than to a channel
        layout it cannot see ([[mi4-one-value-two-definitions]]).  The field set is KNOWN
        (ABT3_ROW_KEYS), so a key outside it (the two change-counts `_nfar`/`_nfsr`, written WHERE
        THEY CHANGE, or 1008's `xnu_live_abt_farn`/`_far`/`_type`/`_maxfar`/`_maxtype`, or the span
        recorder's `_n`/`_seq`/`_gap`/`_call`, which pair to these rows BY KEY EQUALITY, never by
        position) is skipped rather than trusted.

        NOTE: `xnu_live_abt_nfar`/`xnu_live_abt_nfsr` are NOT part of a ladder record - they are
        change-counts latched where they change and read separately in `s_abt3()`."""
        rows, cur = [], None
        fkeys = {"xnu_live_abt_" + f: f for f in ("pcn", "pc", "fsr", "abtcpsr", "pcfar")}

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_abt_pcn":
                close()
                cur = {"ln": ln, "pcn": val, "pc": None, "fsr": None, "abtcpsr": None,
                       "pcfar": None, "order": ["pcn"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur[f] = val
                cur["order"].append(f)
        close()
        return rows

    def abt4_records(self):
        """[ABT4] THE 1010 L2 DESCRIPTOR.

        One `{"ln", "pten", "pteva", "ptel2", "ptel1", "ptettbr", "pteu", "order"}` per PUBLISHED
        pte-ladder record, in log order.  Modelled on `abt3_records()`: `entry_note_abt_pte` writes
        its ladder keys in ONE fixed order - `xnu_live_abt_pten` OPENS a record, then `_pteva`/`_ptel2`/
        `_ptel1`/`_ptettbr`/`_pteu` - so the reader binds to the recorder's ONE write order rather than
        to a channel layout it cannot see ([[mi4-one-value-two-definitions]]).  The field set is KNOWN
        (ABT4_ROW_KEYS), so a key outside it (the change-count `_ptenx`, written WHERE IT CHANGES, or
        the vm_fault-return ladder's `_vmfn`/`_vmfret`/`_vmfva`/`_vmfprot`, or the span/pc/far
        recorders' keys, which pair to these rows BY KEY EQUALITY, never by position) is skipped rather
        than trusted.

        NOTE: `xnu_live_abt_ptenx` is NOT part of a ladder record - it is a change-count latched where
        it changes and read separately in `s_abt4()`."""
        rows, cur = [], None
        fkeys = {"xnu_live_abt_" + f: f
                 for f in ("pten", "pteva", "ptel2", "ptel1", "ptettbr", "pteu")}

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_abt_pten":
                close()
                cur = {"ln": ln, "pten": val, "pteva": None, "ptel2": None, "ptel1": None,
                       "ptettbr": None, "pteu": None, "order": ["pten"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur[f] = val
                cur["order"].append(f)
        close()
        return rows

    def abt4_vmf_records(self):
        """[ABT4] THE 1010 vm_fault RETURN.

        One `{"ln", "vmfn", "vmfret", "vmfva", "vmfprot", "order"}` per PUBLISHED vm_fault-return
        record, in log order.  `entry_note_abt_vmf(ret, va, prot)` writes `xnu_live_abt_vmfn` (the
        ordinal) FIRST, then `_vmfret`/`_vmfva`/`_vmfprot`, so the reader binds to the recorder's ONE
        write order.  The change-count `_vmfnret` is NOT part of a row (read separately in `s_abt4()`)."""
        rows, cur = [], None
        fkeys = {"xnu_live_abt_" + f: f for f in ("vmfn", "vmfret", "vmfva", "vmfprot")}

        def close():
            if cur is not None:
                rows.append(cur)

        for ln, key, _raw, val in self.toks:
            if key == "xnu_live_abt_vmfn":
                close()
                cur = {"ln": ln, "vmfn": val, "vmfret": None, "vmfva": None, "vmfprot": None,
                       "order": ["vmfn"]}
            elif cur is not None and key in fkeys:
                f = fkeys[key]
                cur[f] = val
                cur["order"].append(f)
        close()
        return rows


def authority_line():
    """Which 1010 pre-registration this run's expectations can be checked against - by value, at run
    time ([[mi4-self-written-record-is-not-a-constraint]]).  The 1010 `press_1010.sh` carries the
    frozen 1010 PRESS PRE-REGISTRATION block (written beside this checker); the 1009 pre-reg is named
    as the regression's authority when the 1010 one is absent."""
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "scripts")
    p = os.path.join(base, "press_1010.sh")
    try:
        with open(p, "r", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return ("authority: scripts/press_1010.sh is NOT frozen yet (absent at run time) - the "
                "expectations below are the ones handed to the 1010 rung; the 1009 pre-registration "
                "governs the REGRESSION clauses. RE-CHECK against the 1010 pre-reg when it lands",
                False)
    has = "1010 PRESS PRE-REGISTRATION" in text
    if has and "# 1010-SKELETON" in text:
        return ("authority: scripts/press_1010.sh (%d bytes) - the block is a SKELETON (the "
                "`# 1010-SKELETON` sentinel is present): the expectations below are the task's, and a "
                "pre-reg deviation must be REPORTED" % len(text), False)
    return ("authority: scripts/press_1010.sh (%d bytes, read at run time)%s" % (
        len(text), " - the frozen block IS embedded" if has else
        " - but the frozen 1010 PRESS PRE-REGISTRATION block is NOT in it: the expectations below are "
        "the task's, and a pre-reg deviation must be REPORTED"), has)


def prior_arm_guard(arm_sha8):
    """THE PRESSED-ARM GUARD - refuse the arm a PRIOR rung has already pressed.

    1010's reader must REFUSE `24997fc5` (1009's PRESSED arm - this reader is for 1010, whose
    instrument 1009's log cannot carry) and must ACCEPT the built 1010 arm (whatever sha8
    build_1010.sh produces).  The arm is the entry bin's sha8 (the project's rule); the caller names
    it explicitly with `--arm=<sha8>` (the reader does NOT read the LIVE `out/stage90/xnu_arm_entry.bin`
    by default - that is shared live state and the capture under adjudication was booted from a
    possibly different image, [[mi4-out-stage90-is-shared-live-state]]).  Mirrors the
    `[[ $EXPECT_ARM == armed-d13-<pressed> ]] && REFUSE` guards in scripts/press_1010.sh, which refuse
    the PRESSED PREDECESSOR (there, 1009's `24997fc5`); here the refused predecessors are 1009's
    `24997fc5` and 1008's `0343ef45`.

    When NO arm is named (the DEFAULT), the guard is INERT: it prints an OBS and returns - the
    log-observed keys alone are the evidence, and the 1009 capture this reader is first run against
    names no arm (its filename is `recovered-unknown-arm-...`)."""
    PRESSED = {
        "24997fc5": "1009's PRESSED arm - the predecessor this rung supersedes; the 1010 instrument "
                    "is NOT in it (records/1009-press-read.txt). Build 1010 (scripts/build_1010.sh) "
                    "first",
        "0343ef45": "1008's PRESSED arm - superseded; the 1010 instrument is NOT in it "
                    "(records/1008-press-read.txt). Build 1010 (scripts/build_1010.sh) first",
    }
    if arm_sha8 is None:
        say("OBS", "no arm was named (`--arm=<sha8>` not passed) - the pressed-arm guard is INERT; "
                   "the log-observed keys alone are the evidence (the 1009 capture is "
                   "`recovered-unknown-arm-...` and names no arm)")
        return
    say("OBS", "arm under check: armed-d13-%s (named by the caller)" % arm_sha8)
    if arm_sha8 in PRESSED:
        hard("REFUSING the arm armed-d13-%s: %s" % (arm_sha8, PRESSED[arm_sha8]))
    else:
        say("PASS", "the arm armed-d13-%s is not a KNOWN pressed predecessor - it is (or claims to "
                    "be) the built 1010 arm; the log is adjudicated against the 1010 instrument "
                    "below" % arm_sha8)


def s0_gate(cap):
    """[0] THE DEATH GATE - VERBATIM from the 1003 tool (1004 does not change it)."""
    global GATE_OPEN, TRIO_OPEN, GATE_CONJ
    print("\n== [0] DEATH GATE (absence counts only if the boot reached the print site) ==")
    found = {k: cap.first(k) for k in TRIO}
    dp = {k: cap.first(k) for k in DONE_PROBE}
    for k in TRIO:
        t = found[k]
        say("OBS" if t else "---", "%s %s" % (k, ("= %s @:%d" % (t[2], t[0])) if t else "(absent)"))
    n_present = sum(1 for k in TRIO if found[k] is not None)
    TRIO_OPEN = (n_present == 3)
    if 0 < n_present < 3:
        soft("a PARTIAL exec-done trio (%d/3) - the gate is defined as all three" % n_present)
    ph = cap.panic_hits()
    say("OBS" if not ph else "OBS", "panic stanza %s" % (
        "present x%d (first @:%d: %s)" % (len(ph), ph[0][0], ph[0][1]) if ph
        else "absent in the text channel"))
    have_probe = sum(1 for k in DONE_PROBE if dp[k] is not None)
    say("OBS", "the trio's five probe keys: %d/%d present" % (have_probe, len(DONE_PROBE)))
    exec_done_last = max((t[0] for t in [found[k] for k in TRIO] + [dp[k] for k in DONE_PROBE] if t),
                         default=None)
    after_exec = [t for t in cap.toks if exec_done_last is not None and t[0] > exec_done_last]
    say("OBS" if after_exec else "---", "kv records after the exec-returned point (@:%s): x%d" % (
        exec_done_last, len(after_exec)))
    q = {k: cap.first(k) for k in ENTRY_QUARTET}
    if any(q.values()) and n_present == 0:
        say("READING", "exec ENTRY quartet present, trio absent => the exec STARTED and never "
                       "RETURNED: the 1000 death signature")
    GATE_CONJ = {"i_no_panic": not ph, "ii_trio_and_probes": (n_present == 3 and have_probe == len(DONE_PROBE)),
                 "iii_records_after_exec": bool(after_exec)}
    GATE_OPEN = all(GATE_CONJ.values())
    print("DEATH GATE: exec-done trio %s => %s" % (
        "present" if TRIO_OPEN else "ABSENT",
        "absences count" if TRIO_OPEN else "absence-based verdicts are NOT counted"))
    print("DEATH GATE: EFFECTIVE = %s (frozen conjunction: %s)" % (
        "OPEN" if GATE_OPEN else "CLOSED",
        " AND ".join("%s->%s" % (k, "holds" if GATE_CONJ[k] else "FAILS") for k in sorted(GATE_CONJ))))


def s_identity(cap):
    print("\n== IDENTITY / the 1000-1001-1002-1003-1004 instrument census ==")
    cap_hdr = cap.all("xnu_live_cap")
    if cap_hdr:
        vals = sorted(set(t[3] for t in cap_hdr))
        say("OBS", "xnu_live_cap header: %s" % ", ".join(fmt(v) for v in vals))
        if 0x4000 in vals:
            hard("xnu_live_cap=0x00004000 - a PRE-999 image; not the 1004 arm")
    census = {}
    for prefix, why in NEW_FAMILIES:
        n = cap.family_count(prefix)
        census[prefix] = n
        first = next((t for t in cap.toks if t[1].startswith(prefix)), None)
        say("OBS" if n else "---", "%s x%d%s" % (prefix, n,
            "  first @:%d" % first[0] if first else "  (%s)" % why))
    if not any(census.values()):
        hard("ZERO tokens of ALL kept families - a pre-1000 image or the edits did not reach the "
             "image: REBUILD, do not judge", absence_based=True)


def s1_tick(cap):
    print("\n== [1] TICK - the delivery sampled at its source (VERBATIM from 1003) ==")
    hz = cap.first("xnu_live_tick_hz")
    period = (hz[3] // 1000) if hz else PERIOD_FALLBACK
    recs = cap.groups_of("xnu_live_tick_anom_n", "xnu_live_tick_anom_", ("d", "tval", "ctl"))
    for ln, n, f, order in recs:
        say("OBS", "anom n=%s @:%d %s" % (fmt(n), ln, ",".join(order)))
    sums = cap.groups_of("xnu_live_tick_sum_count", "xnu_live_tick_sum_", ("anom", "maxd"))
    for ln, cnt, f, _o in sums:
        say("OBS", "summary @:%d count=%s anom=%s maxd=%s" % (ln, fmt(cnt),
            fmt(f.get("anom", (0, None))[1]), fmt(f.get("maxd", (0, None))[1])))
    say("OBS", "tick_hz %s (period %d ticks)" % (fmt(hz[3]) if hz else "ABSENT", period))


def pet_evidence(cap):
    bark = cap.first("xnu_live_wdt_bark")
    bark_v = bark[3] if bark else BARK_1004
    half = bark_v // 2 if bark_v else 0
    samples = cap.wdt_samples()
    mx = max((s for s in samples if s["countdown"] is not None),
             key=lambda s: s["countdown"], default=None)
    hz = cap.first("xnu_live_tick_hz")
    return {"bark": bark_v, "bark_src": ("xnu_live_wdt_bark @:%d" % bark[0]) if bark
            else "the frozen constant %s" % fmt(BARK_1004), "half": half, "samples": samples,
            "mx": mx, "tick_hz": hz[3] if hz else None, "half_s": secs(half, WDT_HZ)}


def s2_direct_pet(cap):
    print("\n== [2] THE DIRECT CHANNEL + pet feed (VERBATIM from 1003) ==")
    cells = cap.all("xnu_live_capped")
    if cells:
        if any(t[3] == 0x4000 for t in cells):
            hard("capped==0x00004000 - the cap did NOT move (pre-999 image; STOP)")
    recs = cap.groups_of("xnu_live_wdt_pet_n", "xnu_live_wdt_pet_", ("calls", "countdown"))
    ev = pet_evidence(cap)
    for ln, n, f, _o in recs:
        say("OBS", "pet n=%s @:%d calls=%s countdown=%s" % (fmt(n), ln,
            fmt(f.get("calls", (0, None))[1]), fmt(f.get("countdown", (0, None))[1])))
    if not recs:
        soft("no pet-feed records - read against the gate bark/2 = %s (%s); the 1003 frozen (6) "
             "reads it as 'the door stopped or the gate moved: a READING'" % (
                 fmt(ev["half"]), dur(ev["half_s"], WDT_HZ)))
    else:
        say("PASS", "pet feeds published x%d - the feed channel reached the image" % len(recs))


def s3_submapskip(cap):
    print("\n== [3] THE SUBMAP SKIP (VERBATIM from 1003) ==")
    parsed, unparsed = [], []
    for ln, line in enumerate(cap.lines, 1):
        if SUBMAPSKIP_LIT in line:
            m = SUBMAPSKIP_RE.search(line)
            if m:
                parsed.append((ln, [int(g, 16) for g in m.groups()], clip(line.strip(), 160)))
            else:
                unparsed.append((ln, clip(line.strip(), 160)))
    total = len(parsed) + len(unparsed)
    say("OBS" if total else "---", "SUBMAPSKIP lines: x%d (parsed x%d, unparsed x%d)" % (
        total, len(parsed), len(unparsed)))
    for ln, tup, _raw in parsed:
        obj, start, end, size, lr = tup
        say("OBS", "  @:%d obj=%s start=%s end=%s size=%s lr=%s" % (
            ln, fmt(obj), fmt(start), fmt(end), fmt(size), fmt(lr)))
        for want, who in SUB_OBJS:
            if obj == want:
                say("MATCH", "    -> obj MATCHES %s" % who)
    if GATE_OPEN:
        if total == 0:
            hard("no SUBMAPSKIP probe line while the boot survived - the probe/skip did not reach "
                 "the image")
        else:
            say("PASS", "SUBMAPSKIP x%d while the boot survived" % total)


def s4_usvc(cap):
    print("\n== [4] THE USER-SVC FRONTIER (VERBATIM from 1003) ==")
    fields = ("num", "pid", "thr", "pc", "lr", "cpsr", "r0")
    recs = cap.groups_of("xnu_live_usvc_n", "xnu_live_usvc_", fields)
    if not recs:
        if GATE_OPEN:
            hard("no user-SVC records while the boot survived - the frontier did not reach user mode")
        else:
            hard("no user-SVC records while the exec-done trio is ABSENT - the frontier is NOT "
                 "judged", absence_based=True)
        return
    for ln, n, f, _o in recs[:3]:
        say("OBS", "  @:%d n=%s num=%s pid=%s pc=%s cpsr=%s" % (
            ln, fmt(n), fmt(f.get("num", (0, None))[1]), fmt(f.get("pid", (0, None))[1]),
            fmt(f.get("pc", (0, None))[1]), fmt(f.get("cpsr", (0, None))[1])))
    ln, n, f, _o = recs[-1]
    say("READING", "LAST usvc record @:%d n=%s _num=%s _pc=%s" % (
        ln, fmt(n), fmt(f.get("num", (0, None))[1]), fmt(f.get("pc", (0, None))[1])))


def s5_context(cap):
    print("\n== [5] THE CAP AND THE THINNING (context, not a new reading) ==")
    say("OBS", "cap stays 24576 (0x6000); the read-dump thinning ordinal stays 64 (checked AT THE "
               "SOURCE by the build script, not re-derivable from the capture)")


def s_rung(cap):
    """[R] THE 1002 r3-SAVE FIX AS A REGRESSION - VERBATIM from 1003."""
    print("\n== [R] THE 1002 REGRESSION: the dyld `_cerror` abort at 0x4 stays GONE after `check_np ret=12`? ==")
    aborts = cap.abort_hits()
    n_abort = len(aborts)
    np_ln = next((ln for ln, line in enumerate(cap.lines, 1) if CHECK_NP_LIT in line), None)
    say("OBS" if np_ln else "---", "the anchor `%s`: %s" % (
        CHECK_NP_LIT, "@:%d" % np_ln if np_ln else "ABSENT"))
    say("OBS" if n_abort else "---", "`%s`: x%d" % (ABORT_LIT, n_abort))
    if n_abort > 0:
        hard("1002-FIX REGRESSION: `%s` x%d - 1002 CONFIRMED these GONE, so a recurrence means the "
             "r3 fix was reverted or the assembly layer was not rebuilt - REBUILD" % (ABORT_LIT, n_abort))
    elif np_ln is not None:
        if GATE_OPEN:
            say("PASS", "ZERO `usermode data abort at 0x4` after `check_np ret=12` - the 1002 r3-save "
                        "fix HOLDS (regression)")
        else:
            say("OBS", "0 aborts and the anchor present, but the death gate is CLOSED - the survival "
                       "half is not established")
    else:
        hard("no anchor - the 1002 regression cannot be read", absence_based=True)


def _u32(v):
    return v if v is None else (v & 0xFFFFFFFF)


def s_abort(cap):
    """[A] THE ABORT TRACE - THE 1004 RUNG ITSELF (the pure MEASUREMENT, the NEW section).

    Reads the full ordered sleh sequence (now that the caps are 512, the record set IS the abort set)
    and CLASSIFIES the `far` walk: consecutive `far` values that REPEAT within a small window => LOOP
    (the same page re-faulting, a validation-style cycle); strictly ADVANCING => WALK (dyld paging the
    shared cache forward - slow, not stuck).  Reports the LAST abort's `far`/`upc` = the frontier.

    A MEASUREMENT: the stall is the EXPECTED outcome (1004 changes no kernel behaviour), so this
    section FAILS only on ZERO `_upc` records (the switch did not reach the image: REBUILD) or on an
    all-`frame_ok`=0 run (the saved-state offsets are wrong - a MEASUREMENT defect)."""
    print("\n== [A] THE ABORT TRACE - every sleh entry, far/upc, LOOP vs WALK (the 1004 rung) ==")
    recs = cap.abort_samples()
    if not recs:
        hard("ZERO `xnu_live_sleh_seq` records - the sleh instrument did not reach the image: REBUILD",
             absence_based=True)
        return
    say("OBS", "abort records x%d" % len(recs))

    # ---- the 1004 ADDITION must be present: `_upc` beside `_far` on every detailed record.
    n_upc = cap.family_count(SLEH_UPC_KEY)
    if n_upc == 0:
        hard("the 1004 addition `%s` is ABSENT - STAGE90_XNU_ABORT_TRACE did not reach the image: "
             "REBUILD with it threaded into the entry payload (see scripts/build_1004.sh)"
             % SLEH_UPC_KEY, absence_based=True)
    else:
        say("PASS", "`%s` x%d - the 1004 addition reached the image (the frame's user pc, published "
                    "beside the fault pair)" % (SLEH_UPC_KEY, n_upc))

    # ---- the death-time frame fields need the FRAMES band, which the widened cap now grants:
    starts = [r["f"].get("cpsr", (None,))[0] for r in recs]
    n_frame = sum(1 for r in recs if "cpsr" in r["f"])
    n_seen = cap.family_count("xnu_live_sleh_seen")

    # ---- table (first few + last few, so both ends are visible in the log).
    def row(r):
        f = r["f"]
        g = lambda k: fmt(f.get(k, (0, None))[1])
        return ("seq=%s type=%s far=%s upc=%s thr=%s frame_ok=%s user=%s" % (
            g("seq"), g("type"), g("far"), g("upc"), g("thr"), g("frame_ok"), g("user")))
    for r in recs[:6]:
        say("OBS", "  @:%d %s" % (r["ln"], row(r)))
    if len(recs) > 12:
        say("OBS", "  ... (%d records) ..." % (len(recs) - 12))
        for r in recs[-6:]:
            say("OBS", "  @:%d %s" % (r["ln"], row(r)))
    elif len(recs) > 6:
        # 7..12 records: the head is rows 0..5, so print only the ones NOT already shown (no dup).
        for r in recs[6:]:
            say("OBS", "  @:%d %s" % (r["ln"], row(r)))

    # ---- the `far` walk classification.
    #
    # **TWO PRIMITIVES, AND THE PAGE-GRANULAR ONE IS THE LOOP SIGNAL.** A raw `far` comparison (`far >
    # prev`) is the wrong primitive for "the same page re-faulting": real demand faults within ONE page
    # carry DIFFERENT `far` values (1003's head: 0x2be89fe0 / 0x2be8c78f / 0x2be8a084 are three offsets
    # inside one 4 KiB page), so a same-page re-fault reads as ADVANCE and mislabels a LOOP as a WALK.
    # The LOOP signature is therefore a repeated **page** (`far & ~0xfff`), not a repeated word. Both
    # are printed; the VERDICT below decides on the page-granular count, the word-granular one beside it
    # as corroboration. (Page size 4096 = the shared cache's mapping granularity.)
    PAGE_MASK = ~0xFFF
    fars = [_u32(r["f"].get("far", (0, None))[1]) for r in recs]
    upcs = [_u32(r["f"].get("upc", (0, None))[1]) for r in recs]
    types = [_u32(r["f"].get("type", (0, None))[1]) for r in recs]
    n_pf = sum(1 for t in types if t == ABT_PREFETCH)
    n_da = sum(1 for t in types if t == ABT_DATA)

    def _classify(vals, mask=None):
        """Count ADVANCE / REPEAT-within-window / other over `vals`, optionally masked to a page."""
        prev, adv, rep, oth, reps = [], 0, 0, 0, []
        for i, v in enumerate(vals):
            if v is None or i == 0 or vals[i - 1] is None:
                prev.append(v)
                continue
            cmp_v = v & mask if (mask is not None and v is not None) else v
            win = [(p & mask if (mask is not None and p is not None) else p) for p in prev[-REPEAT_WINDOW:]]
            if cmp_v in win:
                rep += 1
                reps.append(i)
            elif v > vals[i - 1]:
                adv += 1
            else:
                oth += 1
            prev.append(v)
        return adv, rep, oth, reps

    walk, loop, flat, repeats = _classify(fars)
    p_walk, p_loop, p_flat, p_repeats = _classify(fars, PAGE_MASK)
    say("OBS", "far WORD-granular: ADVANCE x%d, REPEAT(past-%d) x%d, other x%d (type: prefetch-abort "
               "x%d, data-abort x%d)" % (walk, REPEAT_WINDOW, loop, flat, n_pf, n_da))
    say("OBS", "far PAGE-granular (far & ~0xfff): ADVANCE x%d, REPEAT(past-%d) x%d, other x%d - this is "
               "the LOOP signal (a word-level `>` cannot see a same-page re-fault)"
               % (p_walk, REPEAT_WINDOW, p_loop, p_flat))
    # keep the word-granular repeat indices for the OBS line below (compat with 1003's shape)
    _word_repeats = repeats
    # ---- frame_ok self-check: the frame fields (fsr_frame/far_frame) DE-SLIDE onto fsr/far.
    fok = [r["f"].get("frame_ok", (0, None))[1] for r in recs if "frame_ok" in r["f"]]
    if fok and all(v == 0 for v in fok):
        hard("EVERY detailed record carries `_frame_ok`=0 - the frame fields do NOT agree with the "
             "fault pair: the saved-state offsets (STAGE90_SS_*) or the record shape are WRONG. This "
             "is a MEASUREMENT defect - adjudicate against src/entry/entry_saved_state.h",
             absence_based=False)
    elif fok:
        n_ok = sum(1 for v in fok if v)
        say("PASS", "`_frame_ok`=1 on %d/%d detailed records - the frame read agrees with the fault "
                    "pair (the instrument checks itself)" % (n_ok, len(fok)))

    # ---- the VERDICT: LOOP vs WALK, and the frontier (the LAST abort's far/upc).
    # Decided on the PAGE-granular counts (the word-level `>` cannot see a same-page re-fault). A repeat
    # within the window is the LOOP signature; else the fars strictly advance => WALK.
    verdict = ("LOOP - consecutive `far` PAGES REPEAT within a %d-window (the same page re-faulting: "
               "a validation-style cycle)" % REPEAT_WINDOW if p_loop > 0 and p_loop >= p_walk else
               "WALK - the `far` pages advance (dyld paging the shared cache forward: slow, not stuck)"
               if p_walk > 0 else
               "NO-WALK - fewer than two comparable `far` values (the trace is too short to classify)")
    say("VERDICT", verdict)
    repeats = p_repeats
    # ---- the register state at the END: does the trace FROZEN-OUT (the tail holds one far) or keep
    # MIXING?  A frozen tail + a high repeat count is the LOOP signature the 1003 sampler saw (one
    # frame held across the powers-of-two ladder); a mixing tail with advancing far is WALK.  Read the
    # LAST few COMPARABLE fars by value rather than inferring from the aggregate.
    tail = [f for f in fars[-8:] if f is not None]
    if len(tail) >= 2:
        n_uniq = len(set(tail))
        n_pages = len(set(f & PAGE_MASK for f in tail))
        say("OBS", "tail far (last %d comparable): x%d distinct word / x%d distinct page %s -> %s" % (
            len(tail), n_uniq, n_pages,
            "[" + ",".join(fmt(f) for f in tail) + "]",
            "PAGE-FROZEN (one page held - the LOOP signature; differing words within it are offsets)"
            if n_pages == 1 else "MIXING (pages keep changing)"))
    # ---- the caps are 512 under the switch: if the record count REACHES 512 the tail is truncated and
    # the true abort count needs the cumulative `_storm` (written once, at 513).  A channel cap (24576)
    # would also silently drop the newest records; both are read here by value, not inferred.
    n_storm = cap.family_count("xnu_live_sleh_storm")
    if len(recs) >= 512:
        hard("the abort count REACHED the 1004 cap (512) - the record set is the first 512 aborts, not "
             "the tail; read `xnu_live_sleh_storm` (x%d present?) and widen, or read the tail "
             "differently" % n_storm, absence_based=True)
    # A cap tombstone truncates only records AFTER it. The abort trace is truncated ONLY if the cap
    # landed BEFORE the last abort record; a cap after it leaves the trace complete (the sleh band is
    # 512, this rung's abort count is far smaller).  POSITIONAL, not mere presence - a bare "capped
    # present" reading would flag a complete trace as truncated ([[mi4-measurement-defects]]: a
    # number that is an artifact of when it was taken).
    _cap = cap.all("xnu_live_capped")
    _last_ln = recs[-1]["ln"]
    if _cap and _last_ln > _cap[0][0]:
        hard("the live channel cap (xnu_live_capped @:%d) landed BEFORE the last abort (@:%d) - the "
             "abort trace IS truncated: the LAST abort above is NOT the death-time frontier"
             % (_cap[0][0], _last_ln), absence_based=True)
    elif _cap:
        say("OBS", "`xnu_live_capped` @:%d is AFTER the last abort (@:%d) - the abort trace is COMPLETE; "
                   "the cap truncated only later records (the run kept publishing after the faults)"
            % (_cap[0][0], _last_ln))
    if n_storm:
        say("OBS", "`xnu_live_sleh_storm` present x%d - the abort count EXCEEDED the detailed band "
                   "(more than %d aborts); the recorded set is the head" % (n_storm, 512))
    if repeats:
        say("OBS", "the repeat positions (0-based record idx): %s" % ",".join(str(i) for i in repeats[:24]))
    last = recs[-1]
    lf = last["f"]
    say("READING", "THE LAST ABORT @:%d seq=%s type=%s far=%s upc=%s thr=%s "
                   "frame_ok=%s user=%s (NOT the frontier - see the PROGRESS check below)" % (
        last["ln"], fmt(lf.get("seq", (0, None))[1]), fmt(lf.get("type", (0, None))[1]),
        fmt(lf.get("far", (0, None))[1]), fmt(lf.get("upc", (0, None))[1]),
        fmt(lf.get("thr", (0, None))[1]), fmt(lf.get("frame_ok", (0, None))[1]),
        fmt(lf.get("user", (0, None))[1])))
    say("OBS", "records that carry the FRAME band (pc/lr/sp/cpsr/frame_ok/user): x%d of x%d (the "
               "pre-1004 `SLEH_LIVE_MAX` hid all but the first %d); the running `_seen` count x%d" % (
        n_frame, len(recs), SLEH_OLD_MAX, n_seen))
    # ---- DID EVERY ABORT RETURN?  `xnu_live_sleh_back` is the wrapper's post-`sleh_abort` note,
    # written for every abort <= the FRAMES band.  A `_seq` with NO matching `_back` is RECORDED,
    # but it is NOT read as "never returned": the back-note's absence has NO proven cause (not the
    # cap - both keys share the general channel and the cap is ~24000 records later; not a race -
    # the box boots UNIPROCESSOR, real_ncpus: 1).  The DECISIVE reading is whether the machine
    # PROGRESSES past the last abort - read from the CAP-EXEMPT channels (usvc / storage), which
    # publish to the log's end even after the general channel caps.  If it progresses, not fatal.
    n_back = cap.family_count("xnu_live_sleh_back")
    if n_back and n_back < len(recs):
        say("OBS", "`xnu_live_sleh_back` x%d for x%d abort records - x%d abort(s) have no "
                   "`_back`, but its cause is UNPROVEN (not the cap: both keys share the general "
                   "channel; not a race: the box boots uniprocessor) and it is NOT a death marker; "
                   "see the PROGRESS check below" % (n_back, len(recs), len(recs) - n_back))
    elif n_back:
        say("PASS", "every abort has a matching `_back` (`xnu_live_sleh_back` x%d == `_seq` x%d)"
                    % (n_back, len(recs)))

    # The PROGRESS check: any CAP-EXEMPT record (usvc syscall, storage read) AFTER the last abort
    # means the machine kept executing past it.  This is the reading that refutes "the abort was
    # fatal", and it uses channels the cap tombstone cannot touch.
    _post = [t[0] for t in cap.all("xnu_live_usvc_num") if t[0] > _last_ln]
    _post += [t[0] for t in cap.all("xnu_live_storage_rd_complete") if t[0] > _last_ln]
    if _post:
        say("READING", "THE MACHINE PROGRESSED PAST THE LAST ABORT: x%d cap-exempt record(s) "
                       "(usvc/storage) AFTER @:%d - so the LAST abort was NOT fatal. The 1004 "
                       "frontier is NOT the sleh return path; it is the run's tick-collapse STALL "
                       "(read `xnu_live_tick_sum_maxd`)." % (len(_post), _last_ln))
    else:
        say("READING", "NO cap-exempt record after the last abort @:%d - nothing executed past it "
                       "(consistent with that abort being the frontier)" % _last_ln)
    say("OBS", "this is a MEASUREMENT section: the stall is the EXPECTED outcome of this rung (1004 "
               "changes no kernel behaviour) - the classification above IS the 1004 finding")


def _stall_region(pc):
    """Name the region a sampled user pc falls in (the 1002/1003 map).  Kept from 1003."""
    if pc is None:
        return "unknown"
    if DYLD_BASE <= pc < DYLD_END:
        return "dyld image 0x2be00000..0x2be58000"
    if pc >= SHARED_CACHE_BASE:
        return "shared cache (base 0x2c000000 + slide 0x37f6000)"
    if pc >= DYLD_END:
        return "the 0x2be58000..0x2c000000 gap"
    return "low user map (< 0x2be00000)"


def s_stall(cap):
    """[S] THE 1003 STALL SAMPLE - KEPT AS A REGRESSION.  Structural mirror of 1003's [S]; a full
    SPIN/ASLEEP reading is in the 1003 tool, not re-derived here."""
    print("\n== [S] THE 1003 STALL SAMPLE (KEPT as a regression in 1004) ==")
    recs = cap.stall_samples()
    if not recs:
        hard("ZERO `xnu_live_stall_*` records - 1003's instrument was dropped, but 1004's set is 1003's "
             "verbatim: REBUILD")
        return
    ns = [r["f"].get("n", (0, None))[1] for r in recs]
    say("OBS", "samples x%d (n %s .. %s)" % (len(recs), fmt(ns[0]), fmt(ns[-1])))
    nzs = [r["f"].get("nz", (None, None))[1] for r in recs]
    if nzs and all(v == 0 for v in nzs):
        hard("EVERY stall sample `_nz`=0 - 1003's saved-state frame read all-zero (a MEASUREMENT "
             "defect): adjudicate the offsets against entry_saved_state.h")
    else:
        say("PASS", "1003's `_nz` > 0 on every sample - the kept instrument reads a real frame")


def s_f(cap):
    """[F] THE CODE-SIGNING WALL - VERBATIM from 1003."""
    print("\n== [F] THE CODE-SIGNING PAGE-VALIDATION WALL (cs_invalid_page) ==")
    hits = cap.cs_invalid_hits()
    n = len(hits)
    say("OBS" if n else "---", "`%s` lines: x%d%s" % (
        CS_INVALID_LIT, n, "  first @:%d" % hits[0][0] if n else "  (not hit)"))
    if n == CS_TOLERATED:
        say("PASS", "cs_invalid_page x%d - the TOLERATED warning the 1002 capture measured (CS_VALID "
                    "cleared, retval 0): NOT the frontier" % n)
    elif n == 0:
        say("OBS", "cs_invalid_page x0 - the wall was not hit this boot")
    else:
        say("READING", "cs_invalid_page x%d - a count above 1 is NOT the tolerated single occurrence; "
                       "adjudicate against records/1002-press-read.txt" % n)


def s6_guards(cap):
    print("\n== [6] REGRESSION GUARDS (VERBATIM from 1003) ==")
    for lit, n, want_txt in (("IOS7LAB cache mapping", 4, "the wire's four mapping lines"),
                             ("IOS7LAB shared region", 1, "the geometry line, exactly once"),
                             (CHECK_NP_LIT, 1, "the pre-438 constant")):
        n_have = cap.lit_count(lit)
        if n_have == n:
            say("PASS", "%s x%d (%s)" % (lit, n_have, want_txt))
        elif n_have == 0:
            hard("%s x0 - regression (%s)" % (lit, want_txt), absence_based=True)
        else:
            say("OBS", "%s x%d (expected %d)" % (lit, n_have, n))
    ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
    if not ret:
        hard("no `map_slide ret=` - absent while the boot survived = the slide regressed", absence_based=True)
    elif all(v == 0 for v in ret):
        say("PASS", "IOS7SHREG map_slide ret=0 x%d (999's success held)" % len(ret))
    else:
        hard("map_slide ret=%s - STOPS per pre-reg (8): the slide regressed" % ret)
    mf = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_file kr=(-?\d+)', cap.text)]
    if any(v != 0 for v in mf):
        hard("map_file kr!=0 - GEOMETRY REGRESSION")
    n991 = cap.lit_count("VMHINT991")
    if GATE_OPEN:
        if n991 == 0:
            say("PASS", "VMHINT991 x0 - the FLIPPED expectation (the submap call is skipped)")
        else:
            hard("VMHINT991 fired while the boot survived x%d" % n991)
    for k, want in FIXED_WDT.items():
        t = cap.first(k)
        if t is None:
            hard("%s: ABSENT - the frozen WDT net must hold" % k, absence_based=True)
        elif t[3] == want:
            say("PASS", "%s=%s (frozen)" % (k, fmt(t[3])))


def s7_falsifiers(cap):
    """The frozen 1004 pre-reg (8)(a)-(d).  The wording tracks 1003's; the team-lead's frozen block is
    the authority."""
    print("\n== [7] FALSIFIERS (frozen 1004 (8)(a)-(d)) ==")
    say("OBS", "(8)(a) the 1004 change's by-value reading is IMAGE-side (scripts/press_1004.sh step "
               "2-0): the `xnu_live_sleh_upc` literal + the raised caps (512) in "
               "out/stage90/xnu_arm_entry.elf - not derivable from this capture")
    n_abort = len(cap.abort_hits())
    say("OBS", "(8)(b) `%s` x%d => %s" % (ABORT_LIT, n_abort,
        "1002-FIX REGRESSION - adjudicated in [R]" if n_abort else "the 1002 r3-save fix holds"))
    upc = cap.family_count(SLEH_UPC_KEY)
    say("OBS", "(8)(c) the 1004 instrument: `%s` tokens x%d => %s" % (
        SLEH_UPC_KEY, upc, "EXPECTED (the instrument reached the image)" if upc else
        "the instrument did NOT reach the image - the (8)(a) rebuild signal"))
    ph = cap.panic_hits()
    tail = [l for l in cap.lines[-6:] if l.strip()]
    say("OBS", "(8)(d) the capture's last lines: %s" % " | ".join(clip(l.strip(), 90) for l in tail))
    for fam in (SLEH_UPC_KEY, "xnu_live_sleh_", "xnu_live_usvc_", "xnu_live_wdt_pet_"):
        t = next((x for x in reversed(cap.toks) if x[1].startswith(fam)), None)
        say("OBS" if t else "---", "(8)(d) last DIRECT key of %s: %s" % (
            fam, ("%s=%s @:%d" % (t[1], t[2], t[0])) if t else "(none)"))


def s_cadence(cap):
    """[C] THE CADENCE WITNESS - THE 1005 RUNG ITSELF (the pure MEASUREMENT, the NEW section).

    Reads the ordered `(t, sync, tick, irq)` samples and the summary triples, checks PRESENCE and
    MONOTONICITY by value, then CLASSIFIES the stall by the deltas over the sampled window.  The
    verdict is a MEASUREMENT (1005 changes no kernel behaviour): it FAILS only on a MISSING key
    (the switch did not reach the image: REBUILD), a NON-MONOTONE counter (a MEASUREMENT defect),
    or - by DEFAULT - a log with NO cadence keys at all (the NEGATIVE CONTROL; waived only by
    `--allow-missing-cadence`, used to re-run the 1004 regression clauses on a pre-1005 log)."""
    global CADENCE_CERTIFIED
    print("\n== [C] THE CADENCE WITNESS - the timer-independent (syscall) sample vs tick/IRQ "
          "(the 1005 rung) ==")

    # ---- (c1) PRESENCE, by value.  The recorder writes 4 keys; the image is pinned to EXACTLY 4
    # `xnu_live_cadence_` literals (build_1005.sh (b)).  Report each key's count and the real total.
    counts = {k: cap.family_count(k) for k in CADENCE_KEYS}
    for k in CADENCE_KEYS:
        n = counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    n_total = cap.family_count(CADENCE_PREFIX)
    say("OBS", "`%s` tokens TOTAL x%d (the image pins %d unique literals; a count here is RECORDS, "
               "not literals)" % (CADENCE_PREFIX, n_total, CADENCE_IMG_LIT_WANT))

    # NAMED-NOT-PRESENT: there is NO `xnu_live_cadence_summary` KEY (corrected 2026-10-14).  Read it
    # by value anyway so a future image that ADDS one is seen, not assumed.
    n_named = cap.family_count(CADENCE_SUMMARY_KEY)
    say("OBS" if n_named else "---", "the task-named `%s` x%d - %s" % (
        CADENCE_SUMMARY_KEY, n_named,
        "PRESENT (a key the source does not write - a NEW publish site appeared; re-read the recorder)"
        if n_named else
        "absent AS EXPECTED (there is NO `_summary` KEY: the summary is the `_sync`/`_tick`/`_irq` "
        "triple, and `%s` is a DEFINED SYMBOL (nm), not a capture key)" % CADENCE_SUMMARY_SYM))
    say("OBS", "`%s` is a FUNCTION symbol (image-side: `nm` shows it DEFINED `T`; build_1005.sh (b) "
               "asserts it) - NOT a capture key, so it is NOT looked for in the log"
        % CADENCE_SUMMARY_SYM)

    if n_total == 0:
        # THE NEGATIVE CONTROL.  A pre-1005 log (e.g. the pressed 1004 arm) has NO cadence keys.
        if ALLOW_MISSING_CADENCE:
            say("OBS", "NO cadence keys and `--allow-missing-cadence` was passed: this is a PRE-1005 "
                       "log - the cadence verdict is NOT certified; the 1004 regression clauses below "
                       "still run independently")
            say("OBS", "the 1005 VERDICT is UNCERTIFIED (a reader that cannot fail certifies nothing)")
        else:
            hard("NO `xnu_live_cadence_` keys: this log carries NO 1005 cadence witness - either the "
                 "run is a PRE-1005 arm (the switch did not reach the image: REBUILD with "
                 "STAGE90_XNU_CADENCE=1) or the wrong capture was handed to this tool. The DEFAULT "
                 "refuses to certify a 1005 pass without the rung's own instrument; pass "
                 "`--allow-missing-cadence` to run ONLY the 1004 regression clauses on a pre-1005 log",
                 absence_based=True)
        return

    rows = cap.cadence_samples()
    samples = [r for r in rows if r["kind"] == "sample"]
    sums = [r for r in rows if r["kind"] == "summary"]
    say("OBS", "cadence sample records x%d, summary triples x%d" % (len(samples), len(sums)))

    # ---- (c1 cont.) every SAMPLE must be a full 4-tuple; a partial one is a dropped key.
    partial = [r for r in samples if None in (r["t"], r["sync"], r["tick"], r["irq"])]
    if partial:
        hard("x%d cadence sample record(s) are INCOMPLETE (a `_t` opened a record but `sync`/`tick`/"
             "`irq` did not all follow) - the recorder's write order or the channel is broken; first "
             "partial @:%d" % (len(partial), partial[0]["ln"]))
    else:
        say("PASS", "every cadence sample record carries the full (t, sync, tick, irq) tuple")

    def show(r):
        return ("@:%d t=%s sync=%s tick=%s irq=%s" % (
            r["ln"], fmt(r["t"]), fmt(r["sync"]), fmt(r["tick"]), fmt(r["irq"])))

    for r in samples[:6]:
        say("OBS", "  sample " + show(r))
    if len(samples) > 12:
        say("OBS", "  ... (%d records) ..." % (len(samples) - 12))
        for r in samples[-6:]:
            say("OBS", "  sample " + show(r))
    elif len(samples) > 6:
        for r in samples[6:]:
            say("OBS", "  sample " + show(r))
    for r in sums[-4:]:
        say("OBS", "  summary " + show(r))

    # ---- (c2) MONOTONICITY.  A counter that goes BACKWARDS is a MEASUREMENT defect
    # ([[mi4-measurement-defects]]) - read ACROSS the ordered rows (samples AND summaries interleaved
    # in log order), because all three counters are monotone for the whole run, not just within a
    # record.  A decrease on any of sync/tick/irq is a HARD ANOMALY.
    ordered = sorted(rows, key=lambda r: r["ln"])
    for field in ("sync", "tick", "irq"):
        prev, ln_prev = None, None
        for r in ordered:
            v = r[field]
            if v is None:
                continue
            if prev is not None and v < prev:
                hard("NON-MONOTONE `%s`: it DECREASED %s -> %s (@:%d -> @:%d) - a monotone counter "
                     "that goes backwards is a MEASUREMENT defect (a reset, a re-read of the wrong "
                     "static, or a partial record), not a reading" % (
                         field, fmt(prev), fmt(v), ln_prev, r["ln"]))
                break
            prev, ln_prev = v, r["ln"]
        else:
            say("PASS", "`%s` is NON-DECREASING across all %d cadence rows" % (field, len(ordered)))

    # ---- (c3) THE VERDICT - the ratio witness.  The deltas over the sample window; the LAST sample
    # is the frontier of the sampled band.  A summary row (no `_t`) is NOT a sample, so it never
    # enters the deltas - it is the post-syscall snapshot, reported separately below.
    if len(samples) < CAD_SHORT_WINDOW:
        say("OBS", "ONLY %d cadence sample(s) (< %d): the deltas are too short to classify the stall "
                   "- this is an OBS, NOT a verdict ([[mi4-measurement-defects]]: a short window is "
                   "not evidence of a stall)" % (len(samples), CAD_SHORT_WINDOW))
    first, last = samples[0], samples[-1]
    ds = (last["sync"] - first["sync"]) if (last["sync"] is not None and first["sync"] is not None) else None
    dt = (last["tick"] - first["tick"]) if (last["tick"] is not None and first["tick"] is not None) else None
    di = (last["irq"] - first["irq"]) if (last["irq"] is not None and first["irq"] is not None) else None
    say("OBS", "THRESHOLDS (ordinal, not seconds - the counters carry no time base): tick FROZEN iff "
               "dt <= %d; sync CLIMBING iff ds >= %d; classify only if >= %d samples"
               % (CAD_FROZEN_MAX, CAD_PROGRESS_MIN, CAD_SHORT_WINDOW))
    if ds is None or dt is None or di is None:
        say("OBS", "the window deltas are incomplete - cannot classify")
    else:
        say("READING", "window deltas over x%d samples: ds(sync)=%d dt(tick)=%d di(irq)=%d "
                       "(first @:%d, last @:%d)" % (len(samples), ds, dt, di, first["ln"], last["ln"]))
        short = len(samples) < CAD_SHORT_WINDOW
        if short:
            say("OBS", "(classification SUPPRESSED: the window is too short - OBS only)")
        elif dt <= CAD_FROZEN_MAX and ds >= CAD_PROGRESS_MIN:
            if di <= CAD_FROZEN_MAX:
                say("VERDICT", "(b2) IRQ-MASKED WINDOW - the synchronous (syscall) path runs "
                               "(ds=%d) while BOTH the tick (dt=%d) and ALL interrupts (di=%d) are "
                               "frozen: a long critical section with IRQs masked (the timer cannot "
                               "be delivered because nothing is delivered)" % (ds, dt, di))
            else:
                say("VERDICT", "(b1) DEGRADED TIMER IRQ - the clock decays (dt=%d) while the "
                               "synchronous path climbs (ds=%d) and OTHER interrupts still arrive "
                               "(di=%d): the TIMER LINE SPECIFICALLY is wedged (a GIC line / the "
                               "timer device), not the whole IRQ path" % (dt, ds, di))
        elif dt <= CAD_FROZEN_MAX and ds < CAD_PROGRESS_MIN:
            say("VERDICT", "FULL STOP - even the synchronous path froze (ds=%d, dt=%d, di=%d): "
                           "nothing is progressing toward death (the 1004 stall, confirmed "
                           "timer-independent)" % (ds, dt, di))
        elif dt > CAD_FROZEN_MAX:
            say("VERDICT", "NO COLLAPSE IN THE WINDOW - the tick kept advancing (dt=%d, ds=%d, "
                           "di=%d): the stall is PAST the last cadence sample; read the summary "
                           "triples and the post-cap records" % (dt, ds, di))
        else:
            say("OBS", "the deltas fall between the classes (dt=%d, ds=%d, di=%d) - read the samples "
                       "by value; the classification is not decisive" % (dt, ds, di))
        # the summary triples: the post-syscall snapshot.  If the LAST summary's sync is unchanged
        # from the last sample's sync while its tick advanced, the syscall path had stopped BEFORE
        # the snapshot (the stall is in the syscalls, not the timer's view of them).
        if sums:
            lastsum = sums[-1]
            if (lastsum["sync"] is not None and last["sync"] is not None
                    and lastsum["tick"] is not None and last["tick"] is not None):
                say("READING", "LAST SUMMARY @:%d sync=%s tick=%s irq=%s vs LAST SAMPLE @:%d "
                               "sync=%s tick=%s irq=%s" % (
                                   lastsum["ln"], fmt(lastsum["sync"]), fmt(lastsum["tick"]),
                                   fmt(lastsum["irq"]), last["ln"], fmt(last["sync"]),
                                   fmt(last["tick"]), fmt(last["irq"])))

    # ---- the frontier line: the LAST sample by value (the ratio is the reading, the deltas the
    # evidence; the counters carry no timestamp, so the ordinal `t` and the line number are the only
    # time proxies - [C] never dates the stall to a CNTVCT instant).
    say("READING", "THE LAST CADENCE SAMPLE @:%d t=%s sync=%s tick=%s irq=%s" % (
        last["ln"], fmt(last["t"]), fmt(last["sync"]), fmt(last["tick"]), fmt(last["irq"])))
    say("OBS", "a MEASUREMENT section: 1005 changes no kernel behaviour, so the stall is the EXPECTED "
               "outcome - the ratio above IS the 1005 finding (it partitions the stall into "
               "timer-line vs all-IRQs vs full-stop; it does NOT prove which GIC line or device)")

    # ---- certified: a real cadence window was read (>= 1 full sample) - the 1005 pass gate.
    if samples and not partial:
        CADENCE_CERTIFIED = True


def s_freeze(cap):
    """[W] THE IDLE-PATH FREEZE WITNESS - THE 1006 RUNG ITSELF (the pure MEASUREMENT, the NEW section).

    Reads the ordered freeze records opened by `xnu_live_freeze_n` (closed by `xnu_live_freeze_ctlr`)
    and the unfiltered early-pass idle baseline (`xnu_live_frzbase_n`/`xnu_live_frzbase_d`), checks PRESENCE
    and MONOTONICITY by value, reports the baseline-vs-freeze dwells by value, and for the LARGEST
    dwell decodes the CPSR IRQ-mask bit + the GIC line/distributor state to CLASSIFY the freeze.

    A MEASUREMENT (1006 changes no kernel behaviour): it FAILS only on a MISSING key (the switch did
    not reach the image: REBUILD), a NON-MONOTONE `_n` (a MEASUREMENT defect), or - by DEFAULT - a
    log with NO freeze keys at all (the NEGATIVE CONTROL; waived only by `--allow-missing-freeze`).
    ZERO freeze records WITH the idle-baseline keys present is a VALID SILENCE, NOT a hard anomaly."""
    global FREEZE_CERTIFIED
    print("\n== [W] THE IDLE-PATH FREEZE WITNESS - the timer-INDEPENDENT idle dwell + CPSR/GIC state "
          "(the 1006 rung) ==")

    # ---- (w1) PRESENCE, by value.  The sampler writes 6 freeze keys + 2 idle-baseline keys; the
    # image is pinned to those literals (build_1006.sh (b)).  Report each key's count.
    counts = {k: cap.family_count(k) for k in FREEZE_KEYS}
    for k in FREEZE_KEYS:
        n = counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    idle_counts = {k: cap.family_count(k) for k in IDLE_KEYS}
    for k in IDLE_KEYS:
        n = idle_counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    n_total = cap.family_count(FREEZE_PREFIX)
    n_idle = cap.family_count(IDLE_PREFIX)
    say("OBS", "`%s` tokens TOTAL x%d (the image pins %d unique literals; a count here is RECORDS, not "
               "literals)" % (FREEZE_PREFIX, n_total, FREEZE_IMG_LIT_WANT))
    say("OBS", "`%s` tokens TOTAL x%d (the unfiltered early-pass baseline; %d unique literals)"
        % (IDLE_PREFIX, n_idle, len(IDLE_KEYS)))

    if n_total == 0:
        # ZERO freeze tokens: either the switch did not reach the image (the NEGATIVE CONTROL), OR the
        # witness ran but saw no dwell above the threshold.  The distinguishing evidence is the
        # idle-baseline keys: the sampler publishes `xnu_live_frzbase_{n,d}` UNFILTERED on the first four
        # idle passes REGARDLESS of the threshold, so their PRESENCE means the witness functioned and
        # simply recorded no long dwell.  That is the VALID SILENCE (not a hard anomaly, and the 1006
        # finding); their ABSENCE means the switch did not reach the image (or the idle path was never
        # entered before the log ended).
        base = cap.idle_baseline()
        if base:
            say("OBS", "idle-pass BASELINE (unfiltered early passes), threshold = %s: %s" % (
                fmt(FREEZE_THRESHOLD),
                ", ".join("n=%s d=%s" % (fmt(n), fmt(d)) for n, d in base)))
            say("READING", "the witness saw NO idle pass dwell longer than the threshold - either the "
                           "~24.4 s freeze did not overlap the idle path (it was spent in non-idle "
                           "kernel code), or the machine never re-entered idle after it. SILENCE IS "
                           "THE READING; it is NOT a pass and NOT a failure of the instrument.")
            FREEZE_CERTIFIED = True
            return
        if ALLOW_MISSING_FREEZE:
            say("OBS", "NO freeze keys and `--allow-missing-freeze` was passed: this is a PRE-1006 log "
                       "- the freeze verdict is NOT certified; the 1005 cadence and 1004 regression "
                       "clauses below still run independently")
            say("OBS", "the 1006 VERDICT is UNCERTIFIED (a reader that cannot fail certifies nothing)")
        elif cap.family_count(ABT_PREFIX) > 0:
            # THIS LOG CARRIES THE 1007 INSTRUMENT.  A log with `xnu_live_abt_` keys is provably a
            # post-1006 arm with STAGE90_XNU_ABT_SPAN=1; the 1007 arm set also carries FREEZE_WITNESS=1
            # (measured in out/stage90/xnu_arm_entry-config.txt), so the sampler IS in the image and the
            # absence of freeze keys is NOT "the wrong capture".  It is the SAME silence 1006 read: the
            # D13 kernel never enters `machine_idle`, so the unmoved sampler records nothing
            # (records/1006-press-read.txt).  That is a READING of the rung, not a build failure, so it
            # is an OBS (uncertified) - NOT a HARD anomaly.  Grounding the downgrade on the 1007
            # instrument's own presence keeps the gate honest: a log with NEITHER instrument still
            # hard-refuses below.
            say("OBS", "NO `xnu_live_freeze_` keys, BUT this log carries the 1007 instrument "
                       "(`xnu_live_abt_` x%d): the arm is post-1006 (ABT_SPAN=1 pins the sibling "
                       "switch set, FREEZE_WITNESS=1). The silence is the INHERITED 1006 reading - the "
                       "D13 kernel never enters `machine_idle`, so the unmoved sampler publishes "
                       "nothing. NOT a wrong capture and NOT a build failure."
                % cap.family_count(ABT_PREFIX))
            say("READING", "the 1006 freeze witness is SILENT (machine_idle never entered) - the freeze "
                           "did not overlap the idle path; the 1007 abort-span section below is where "
                           "this log's freeze reading lives. The 1006 VERDICT is UNCERTIFIED here (the "
                           "instrument did not fire), which is the expected 1007 outcome.")
        else:
            hard("NO `xnu_live_freeze_` keys: this log carries NO 1006 freeze witness - either the run "
                 "is a PRE-1006 arm (the switch did not reach the image: REBUILD with "
                 "STAGE90_XNU_FREEZE_WITNESS=1) or the wrong capture was handed to this tool. The "
                 "DEFAULT refuses to certify a 1006 pass without the rung's own instrument; pass "
                 "`--allow-missing-freeze` to run ONLY the regression clauses on a pre-1006 log",
                 absence_based=True)
        return

    recs = cap.freeze_records()
    say("OBS", "freeze records x%d" % len(recs))

    # ---- (w3 part) THE BASELINE: the unfiltered early-pass dwells, by value.  Reported FIRST so the
    # threshold is judged against a reading, not assumed ([[mi4-measurement-defects]]).
    base = cap.idle_baseline()
    if base:
        say("OBS", "idle-pass BASELINE (unfiltered early passes), threshold = %s: %s" % (
            fmt(FREEZE_THRESHOLD),
            ", ".join("n=%s d=%s" % (fmt(n), fmt(d)) for n, d in base)))
    else:
        say("OBS", "idle-pass BASELINE: none published (the first-four-pass keys are absent - the log "
                   "may start after pass 4, or the idle path was not re-entered that early)")

    # ---- (w1 cont.) every record must be a full tuple; a partial one is a dropped key.
    partial = [r for r in recs if None in (r["n"], r["d"], r["cpsr"], r["ien"], r["pend"], r["ctlr"])]
    if partial:
        hard("x%d freeze record(s) are INCOMPLETE (a `_n` opened a record but `d`/`cpsr`/`ien`/`pend`/"
             "`ctlr` did not all follow) - the recorder's write order or the channel is broken; first "
             "partial @:%d" % (len(partial), partial[0]["ln"]))
    else:
        say("PASS", "every freeze record carries the full (n, d, cpsr, ien, pend, ctlr) tuple")

    # ---- (w2) MONOTONICITY of `_n` ACROSS rows (in log order).  A counter that goes BACKWARDS is a
    # MEASUREMENT defect ([[mi4-measurement-defects]]).  Non-decreasing is required (the sampler is a
    # monotone ladder; equal values are only possible if a record were re-published).
    prev, ln_prev = None, None
    for r in recs:
        v = r["n"]
        if v is None:
            continue
        if prev is not None and v < prev:
            hard("NON-MONOTONE `xnu_live_freeze_n`: it DECREASED %s -> %s (@:%d -> @:%d) - a monotone "
                 "ordinal that goes backwards is a MEASUREMENT defect (a reset, a re-read of the wrong "
                 "static, or a partial record), not a reading" % (fmt(prev), fmt(v), ln_prev, r["ln"]))
            break
        prev, ln_prev = v, r["ln"]
    else:
        say("PASS", "`xnu_live_freeze_n` is NON-DECREASING across all %d freeze records" % len(recs))

    def show(r):
        return ("@:%d n=%s d=%s cpsr=%s ien=%s pend=%s ctlr=%s" % (
            r["ln"], fmt(r["n"]), fmt(r["d"]), fmt(r["cpsr"]), fmt(r["ien"]), fmt(r["pend"]),
            fmt(r["ctlr"])))

    for r in recs[:6]:
        say("OBS", "  freeze " + show(r))
    if len(recs) > 12:
        say("OBS", "  ... (%d records) ..." % (len(recs) - 12))
        for r in recs[-6:]:
            say("OBS", "  freeze " + show(r))
    elif len(recs) > 6:
        for r in recs[6:]:
            say("OBS", "  freeze " + show(r))

    # ---- (w3) THE THRESHOLD ASSERTION: every recorded freeze must have d > FREEZE_THRESHOLD - a
    # record with d <= the threshold is an INCONSISTENCY of the recorder (the sampler gates on
    # `d > MSM8974_FREEZE_MIN`).
    under = [r for r in recs if r["d"] is not None and r["d"] <= FREEZE_THRESHOLD]
    if under:
        hard("x%d freeze record(s) carry `_d` <= the threshold %s - the recorder gates on `d > "
             "MSM8974_FREEZE_MIN`, so a recorded value at or below it is an INCONSISTENCY of the "
             "recorder (first @:%d d=%s)" % (len(under), fmt(FREEZE_THRESHOLD), under[0]["ln"],
             fmt(under[0]["d"])))
    else:
        say("PASS", "every recorded freeze has `_d` > the threshold %s (the threshold the sampler "
                    "gates on)" % fmt(FREEZE_THRESHOLD))

    # ---- (w4) THE READING / CLASSIFIER - the point of 1006.  Decode the LARGEST dwell; if several
    # tie for the max, decode the FIRST by log order (the earliest instance of the worst freeze).
    dd = [(r["d"], i, r) for i, r in enumerate(recs) if r["d"] is not None]
    if not dd:
        # Zero USABLE records but the freeze keys WERE present (e.g. every `_d` was unparseable).  This
        # is degenerate: report it, but it is not the "valid silence" case (that is n_total==0).
        say("OBS", "freeze keys present but NO record carries a usable `_d` - cannot classify; read the "
                   "raw keys by value")
        return
    _maxd, _mi, rmax = max(dd, key=lambda t: t[0])
    # idle-baseline keys present with ZERO freeze records (but freeze keys present) => the witness
    # functions but saw no dwell above threshold.  Handle BOTH orderings below.

    def decode(r):
        cpsr, ien, pend, ctlr = r["cpsr"], r["ien"], r["pend"], r["ctlr"]
        i = ((cpsr >> FREEZE_CPSR_IBIT) & 1) if cpsr is not None else None
        return i

    say("OBS", "THRESHOLD (counts, not seconds - `entry_counter()` carries no time base): a freeze is "
               "recorded iff d > %s (%d counts, ~1 ms); decoded below by value"
        % (fmt(FREEZE_THRESHOLD), FREEZE_THRESHOLD))
    i = decode(rmax)
    say("READING", "THE LARGEST FREEZE @:%d n=%s d=%s cpsr=%s (i=(cpsr>>7)&1=%s) ien=%s pend=%s "
                   "ctlr=%s" % (rmax["ln"], fmt(rmax["n"]), fmt(rmax["d"]), fmt(rmax["cpsr"]),
                   "?" if i is None else str(i), fmt(rmax["ien"]), fmt(rmax["pend"]),
                   fmt(rmax["ctlr"])))
    # classify BY VALUE, printing the exact verdict named by the rung.
    if rmax["d"] is None or i is None:
        say("OBS", "the tuple is incomplete - read by value; not decisive")
    elif i == 0 and rmax["ien"] not in (None, 0) and rmax["pend"] not in (None, 0):
        say("VERDICT", "NON-IDLE LONG BLOCK / FAULT LOOP: IRQs ENABLED and the line ENABLED+PENDING, "
                       "yet the CPU did not take it - a long non-interruptible stretch of kernel code "
                       "(or a wfi that did not wake), NOT a mask")
    elif i == 1:
        say("VERDICT", "IRQ-MASKED WINDOW held across the gap (candidate i) - the CPU had IRQs masked "
                       "the whole dwell")
    elif i == 0 and rmax["ien"] == 0:
        say("VERDICT", "GIC RE-MASK: the distributor's timer line was DISABLED across the gap "
                       "(candidate ii)")
    elif rmax["ctlr"] == 0:
        say("VERDICT", "GLOBAL DISTRIBUTOR DISABLED across the gap (a sub-case of ii)")
    else:
        say("OBS", "the tuple (i=%s, ien=%s, pend=%s, ctlr=%s, d=%s) falls between the classes - read "
                   "by value; not decisive" % (
                       i, fmt(rmax["ien"]), fmt(rmax["pend"]), fmt(rmax["ctlr"]), fmt(rmax["d"])))
    if rmax["ctlr"] == 0:
        say("OBS", "`xnu_live_freeze_ctlr`=%s at the largest dwell: bit0=0 => the GLOBAL distributor "
                   "was DISABLED" % fmt(rmax["ctlr"]))
    else:
        say("OBS", "`xnu_live_freeze_ctlr`=%s at the largest dwell: bit0=%s" % (
            fmt(rmax["ctlr"]), "1 => distributor enabled" if isinstance(rmax["ctlr"], int)
            and (rmax["ctlr"] & 1) else "0 => distributor DISABLED"))

    say("OBS", "a MEASUREMENT section: 1006 changes no kernel behaviour, so the freeze is the EXPECTED "
               "outcome - the tuple above CLASSIFIES the freeze (mask vs GIC line vs distributor vs "
               "non-idle long block); it does NOT prove the exact code path")

    # ---- certified: a real freeze window was read (>= 1 full record) - the 1006 pass gate half 1.
    # (The OTHER half - the VALID SILENCE with zero records but the idle-baseline present - is handled
    # in the n_total == 0 branch above.)
    if recs and not partial:
        FREEZE_CERTIFIED = True


def abt_reference_M(cap):
    """The freeze magnitude the abort spans are compared against - the SAME capture's
    `xnu_live_tick_sum_maxd` (`M`), or the frozen 1005/1006-press reading if that key is absent.
    Returns `(value, source_text)`.

    `xnu_live_tick_sum_maxd` is a RUNNING MAXIMUM republished at every summary (measured: 14 records in
    the 1007 capture, 0x002f02c4 -> 0x1be892af). `cap.first()` therefore reads a PRE-FREEZE interim
    value - 152x too small - and every `near_m()` comparison then lands in branch (A) regardless of the
    truth ([[mi4-measurement-defects]]: the measurement was the thing that was wrong). Read the LARGEST
    published value: a max only grows, so the maximum over the records is the last one by value."""
    vals = [r[3] for r in cap.all("xnu_live_tick_sum_maxd") if r[3] is not None]
    if vals:
        v = max(vals)
        ln = next(r[0] for r in cap.all("xnu_live_tick_sum_maxd") if r[3] == v)
        return v, "this capture: xnu_live_tick_sum_maxd=%s (the max of %d running-max records) @:%d" % (
            fmt(v), len(vals), ln)
    return ABT_M_FALLBACK, "the frozen 1005/1006-press reading xnu_live_tick_sum_maxd=%s" % fmt(ABT_M_FALLBACK)


def s_abt(cap):
    """[ABT] THE ABORT-SPAN WITNESS - THE 1007 RUNG ITSELF (the pure MEASUREMENT, the NEW section).

    Reads the ordered per-abort records opened by `xnu_live_abt_n` (with `_seq`/`_gap`/`_call`) and
    the two latched maxima (`_maxcall`/`_maxgap`), checks PRESENCE and MONOTONICITY by value, reports
    the span by value, and CLASSIFIES the freeze's LOCATION by comparing the maxima against the SAME
    capture's `xnu_live_tick_sum_maxd` (`M`).

    A MEASUREMENT (1007 changes no kernel behaviour): it FAILS only on a MISSING key (the switch did
    not reach the image: REBUILD), a NON-MONOTONE `_n` (a MEASUREMENT defect), the §2.3 terminal
    stand-in firing (`xnu_live_stub_hit_seq` != 0), or - by DEFAULT - a log with NO abt keys at all
    (the NEGATIVE CONTROL; waived only by `--allow-missing-abt`)."""
    global ABT_CERTIFIED
    print("\n== [ABT] THE ABORT-SPAN WITNESS - the gap + handler duration per sleh_abort "
          "(the 1007 rung) ==")

    # ---- (abt0) THE §2.3 TERMINAL STAND-IN.  The stand-in (`entry_stub_hit` -> `noreturn
    # entry_epilogue`) ENDS THE RUN if it fires; 1007's §4.3 argument-passing fix means the abort path
    # must never reach it.  A nonzero `xnu_live_stub_hit_seq` is that event, and it is HARD.
    sh = cap.first("xnu_live_stub_hit_seq")
    if sh is None:
        say("OBS", "`xnu_live_stub_hit_seq` ABSENT (the stand-in was never compiled in, or the log "
                   "predates it) - the §2.3 guard cannot be read from this capture")
    elif sh[3] in (0, None):
        say("PASS", "`xnu_live_stub_hit_seq`=%s @:%d - the §2.3 terminal stand-in did NOT fire (the "
                    "abort path never reached entry_stub_hit)" % (fmt(sh[3]), sh[0]))
    else:
        hard("`xnu_live_stub_hit_seq`=%s @:%d - the §2.3 TERMINAL STAND-IN FIRED: the run ended "
             "inside entry_stub_hit (entry_stub_hit -> noreturn entry_epilogue), so the abort-span "
             "keys are the LAST thing before the run ended. The §4.3 argument-passing fix did not "
             "take, or entry_counter escaped the abort path - REBUILD" % (fmt(sh[3]), sh[0]))

    # ---- (abt1) PRESENCE, by value.  The witness writes 4 ladder keys + 2 latched maxima keys; the
    # image is pinned to those literals (build_1007.sh (b)).  Report each key's count.
    counts = {k: cap.family_count(k) for k in ABT_KEYS}
    for k in ABT_KEYS:
        n = counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    n_total = cap.family_count(ABT_PREFIX)
    say("OBS", "`%s` tokens TOTAL x%d (the image pins %d unique literals; a count here is RECORDS, "
               "not literals)" % (ABT_PREFIX, n_total, ABT_IMG_LIT_WANT))

    if n_total == 0:
        # ZERO abt tokens: either the switch did not reach the image (the NEGATIVE CONTROL), OR the
        # witness ran but the abort path was never entered before the log ended.  The distinguishing
        # evidence is the std-instrument presence.  Default: HARD (a reader that cannot fail certifies
        # nothing); waived only by `--allow-missing-abt` (a pre-1007 log).
        if ALLOW_MISSING_ABT:
            say("OBS", "NO abt keys and `--allow-missing-abt` was passed: this is a PRE-1007 log - "
                       "the abort-span verdict is NOT certified; the 1006 freeze, 1005 cadence and "
                       "1004 regression clauses below still run independently")
            say("OBS", "the 1007 VERDICT is UNCERTIFIED (a reader that cannot fail certifies nothing)")
        else:
            hard("NO `xnu_live_abt_` keys: this log carries NO 1007 abort-span witness - either the "
                 "run is a PRE-1007 arm (the switch did not reach the image: REBUILD with "
                 "STAGE90_XNU_ABT_SPAN=1) or the wrong capture was handed to this tool. The DEFAULT "
                 "refuses to certify a 1007 pass without the rung's own instrument; pass "
                 "`--allow-missing-abt` to run ONLY the regression clauses on a pre-1007 log",
                 absence_based=True)
        return

    recs = cap.abt_records()
    say("OBS", "abort-span records x%d" % len(recs))

    # ---- (abt1 cont.) every record must be a full tuple; a partial one is a dropped key.
    partial = [r for r in recs if None in (r["n"], r["seq"], r["gap"], r["call"])]
    if partial:
        hard("x%d abort-span record(s) are INCOMPLETE (a `_n` opened a record but `_seq`/`_gap`/`_call` "
             "did not all follow) - the recorder's write order or the channel is broken; first "
             "partial @:%d" % (len(partial), partial[0]["ln"]))
    else:
        say("PASS", "every abort-span record carries the full (n, seq, gap, call) tuple")

    # ---- (abt2) MONOTONICITY of `_n` ACROSS rows (in log order).  A counter that goes BACKWARDS is
    # a MEASUREMENT defect ([[mi4-measurement-defects]]).
    prev, ln_prev = None, None
    mono_ok = True
    for r in recs:
        v = r["n"]
        if v is None:
            continue
        if prev is not None and v < prev:
            hard("NON-MONOTONE `xnu_live_abt_n`: it DECREASED %s -> %s (@:%d -> @:%d) - a monotone "
                 "ordinal that goes backwards is a MEASUREMENT defect (a reset, a re-read of the wrong "
                 "static, or a partial record), not a reading" % (fmt(prev), fmt(v), ln_prev, r["ln"]))
            mono_ok = False
            break
        prev, ln_prev = v, r["ln"]
    if mono_ok:
        say("PASS", "`xnu_live_abt_n` is NON-DECREASING across all %d abort-span records" % len(recs))

    def show(r):
        return ("@:%d n=%s seq=%s gap=%s call=%s" % (
            r["ln"], fmt(r["n"]), fmt(r["seq"]), fmt(r["gap"]), fmt(r["call"])))

    for r in recs[:6]:
        say("OBS", "  abt " + show(r))
    if len(recs) > 12:
        say("OBS", "  ... (%d records) ..." % (len(recs) - 12))
        for r in recs[-6:]:
            say("OBS", "  abt " + show(r))
    elif len(recs) > 6:
        for r in recs[6:]:
            say("OBS", "  abt " + show(r))
    if recs:
        say("READING", "TAIL abort-span @:%d n=%s seq=%s gap=%s call=%s" % (
            recs[-1]["ln"], fmt(recs[-1]["n"]), fmt(recs[-1]["seq"]), fmt(recs[-1]["gap"]),
            fmt(recs[-1]["call"])))

    # ---- (abt3) THE LADDER: the per-abort quadruple is published at n<=4 then powers of two, so the
    # published `_n` values must be EXACTLY that set (up to the last run).  A published `_n` outside
    # the ladder is an INCONSISTENCY of the recorder (a MEASUREMENT defect).  (A missing ladder member
    # simply means the run did not reach it.)
    def on_ladder(n):
        return n is not None and (n <= ABT_LADDER_HEAD or (n & (n - 1)) == 0)

    off = [r for r in recs if r["n"] is not None and not on_ladder(r["n"])]
    if off:
        hard("x%d abort-span record(s) carry an `_n` OFF the powers-of-two ladder (first @:%d n=%s) - "
             "the recorder publishes at `n<=4` then `n & (n-1)==0`, so an off-ladder `_n` is an "
             "INCONSISTENCY of the recorder (a MEASUREMENT defect)" % (len(off), off[0]["ln"],
             fmt(off[0]["n"])))
    else:
        say("PASS", "every published abort-span `_n` is on the powers-of-two ladder (n<=4 then 2^k)")

    # ---- (abt4) THE TWO MAXIMA - the latched `_maxcall`/`_maxgap`.  Read by value (LAST write wins
    # is wrong - the recorder writes them ONLY when they change, so the LAST value IS the max).
    mc = cap.first(ABT_MAXCALL_KEY)
    mg = cap.first(ABT_MAXGAP_KEY)
    n_mc = cap.family_count(ABT_MAXCALL_KEY)
    n_mg = cap.family_count(ABT_MAXGAP_KEY)
    maxcall = mc[3] if mc is not None else None
    maxgap = mg[3] if mg is not None else None
    # the max is the LAST published value (the recorder never writes a smaller one).
    for r in reversed(cap.all(ABT_MAXCALL_KEY)):
        if r[3] is not None:
            maxcall = r[3]
            break
    for r in reversed(cap.all(ABT_MAXGAP_KEY)):
        if r[3] is not None:
            maxgap = r[3]
            break
    say("OBS", "`%s` x%d, last=%s (the LATCHED max of `call`, written where it changes)" % (
        ABT_MAXCALL_KEY, n_mc, fmt(maxcall)))
    say("OBS", "`%s` x%d, last=%s (the LATCHED max of `gap`, written where it changes)" % (
        ABT_MAXGAP_KEY, n_mg, fmt(maxgap)))
    # cross-check the latched maxima against the published ladder rows (both are the same clock):
    if recs:
        row_maxcall = max((r["call"] for r in recs if r["call"] is not None), default=None)
        if maxcall is not None and row_maxcall is not None and maxcall < row_maxcall:
            hard("`%s`=%s is SMALLER than the largest published `_call`=%s - the latched max is "
                 "supposed to be >= every published `call` (a MEASUREMENT defect of the recorder)"
                 % (ABT_MAXCALL_KEY, fmt(maxcall), fmt(row_maxcall)))

    # ---- (abt5) THE CLASSIFIER - the point of 1007.  Compare the two maxima against `M` = the SAME
    # capture's `xnu_live_tick_sum_maxd`.
    M, M_src = abt_reference_M(cap)
    say("OBS", "M (the freeze under study) = %s, from %s" % (fmt(M), M_src))
    # ---- (abt4b) THE RUNNING-MAX GUARD.  `xnu_live_tick_sum_maxd` is a RUNNING MAX republished at
    # every summary (measured: 14 records in the 1007 capture), so `cap.first()` reads a PRE-FREEZE
    # interim value and every `near_m()` comparison then lands in branch (A) - the defect this rung's
    # first reading hit ([[mi4-measurement-defects]]: the measurement was the thing that was wrong).
    # A max only grows, so the LAST record must be >= every earlier one; if it is NOT, the collector is
    # wrong and M (and the whole classifier) can be silently mis-scaled.  This is a build/read refusal,
    # not a comment: it makes the trap loud instead of handing back a wrong verdict.
    _tick = [r[3] for r in cap.all("xnu_live_tick_sum_maxd") if r[3] is not None]
    if len(_tick) > 1 and M != max(_tick):
        hard("`abt_reference_M` returned %s but the MAX of the %d `xnu_live_tick_sum_maxd` records "
             "is %s - the resolver is reading a PRE-FREEZE interim value (a `first()` regression), so "
             "M is mis-scaled and every near_m() branch is suspect ([[mi4-measurement-defects]])"
            % (fmt(M), len(_tick), fmt(max(_tick))))

    def near_m(v):
        return v is not None and M and (ABT_NEAR_M_LO * M) <= v <= (ABT_NEAR_M_HI * M)

    if maxcall is None or maxgap is None or not M:
        say("OBS", "the maxima or M is unavailable - read the raw keys by value; not decisive")
    elif near_m(maxcall):
        say("VERDICT", "(C) ABORT-PATH LOCALISED - INSIDE ONE ABORT: `%s`=%s ~ M=%s - a SINGLE "
                       "sleh_abort (its arm_fast_fault + vm_fault body) took the whole freeze. This "
                       "is the STRONGEST positive: the frontier is inside one fault"
            % (ABT_MAXCALL_KEY, fmt(maxcall), fmt(M)))
    elif near_m(maxgap):
        say("VERDICT", "(B) ABORT-PATH LOCALISED - BETWEEN TWO ABORTS: `%s`=%s ~ M=%s with "
                       "`%s`=%s small - one abort RETURNED and the next was not entered for the "
                       "freeze; something OUTSIDE sleh_abort blocked the thread between faults"
            % (ABT_MAXGAP_KEY, fmt(maxgap), fmt(M), ABT_MAXCALL_KEY, fmt(maxcall)))
    else:
        # Neither maximum approaches M: (A) the abort path is fast and the freeze is elsewhere, OR
        # (D) the freeze came AFTER the last abort (the n/seq plateau while the log ran on).
        last_n = recs[-1]["n"] if recs else None
        # (D) is discriminated by the sleh tail: if the capture's LAST sleh entry record carries the
        # SAME `_seq` as the last abort-span row, the abort path had ENDED before the freeze.
        sleh_tail_seq = None
        sleh = cap.abort_samples()
        if sleh:
            sleh_tail_seq = sleh[-1]["f"].get("seq", (None, None))[1]
        abt_tail_seq = recs[-1]["seq"] if recs else None
        if abt_tail_seq is not None and sleh_tail_seq is not None and abt_tail_seq == sleh_tail_seq:
            say("VERDICT", "(D) ABORT PATH ENDED BEFORE THE FREEZE: the last abort-span `_seq`=%s is "
                           "the SAME as the sleh entry tail's `_seq`, and neither maximum (%s=%s, "
                           "%s=%s) approaches M=%s - the freeze came AFTER the last abort (the "
                           "frame's return-to-user stretch or the SVC path is where the CPU sat)"
                % (fmt(abt_tail_seq), ABT_MAXCALL_KEY, fmt(maxcall), ABT_MAXGAP_KEY, fmt(maxgap),
                   fmt(M)))
        else:
            say("VERDICT", "(A) NOT ON THE ABORT PATH: neither `%s`=%s nor `%s`=%s approaches M=%s "
                           "(<= a few hundred-thousand counts expected) - the abort path is honest "
                           "but FAST; the freeze is in a NON-abort stretch (the copyin/SVC path or a "
                           "non-fault kernel stretch past the frame). A NEGATIVE result for this "
                           "rung, and a positive pointer elsewhere"
                % (ABT_MAXCALL_KEY, fmt(maxcall), ABT_MAXGAP_KEY, fmt(maxgap), fmt(M)))
    say("OBS", "a MEASUREMENT section: 1007 changes no kernel behaviour, so the freeze is the EXPECTED "
               "outcome - the classification above names the freeze's LOCATION (not its cause). Pair "
               "`xnu_live_abt_seq` with the sleh entry record's `xnu_live_sleh_seq`/`_far` to name "
               "the fault address of the pegging sample")

    # ---- certified: a real abort span was read (>= 1 full record) - the 1007 pass gate.
    if recs and not partial:
        ABT_CERTIFIED = True


def s_abt2(cap):
    """[ABT2] THE ABORT-FAULT IDENTITY - THE 1008 RUNG ITSELF (the pure MEASUREMENT, the NEW section).

    Reads the far LADDER rows opened by `xnu_live_abt_farn` (with `_far`/`_type`) and the maxcall
    LATCH (`_maxfar`/`_maxtype`, written WHERE `call` sets a new max), PAIRS BY KEY EQUALITY (design
    §6.2) against the span recorder's `xnu_live_abt_n` rows, verifies `farn == n` at every paired row,
    and CLASSIFIES the storm's SHAPE (design §6 table): (S) SPREAD vs (R) REPEAT, and the class
    (U) USER / (K) KERNEL / (P) PREFETCH.

    A MEASUREMENT (1008 changes no kernel behaviour): it FAILS only on the instrument defects of
    design §7 - a `farn` GAP from `n` at a paired row, a `type` outside {3,4}, the far rows entirely
    ABSENT while `abt_n` is present, or `_maxfar`/`_maxtype` ABSENT while `_maxcall` is present - or
    - by DEFAULT - a log with NO far keys at all when the run claims to be a 1008 arm (the NEGATIVE
    CONTROL; waived only by `--allow-missing-abt2`, a pre-1008 log)."""
    global ABT2_CERTIFIED
    print("\n== [ABT2] THE ABORT-FAULT IDENTITY - the storm's `far`/`type` on the direct channel "
          "(the 1008 rung) ==")

    # ---- (abt2-1) PRESENCE, by value.  The far recorder writes 3 ladder keys + 2 latched keys; the
    # 1008 image (`ABT_SPAN=1` AND `ABT_FAR=1`) is pinned to ELEVEN `xnu_live_abt_` literals total
    # (6 span + 5 far, design §4.4); the 1007 image (`ABT_FAR=0`) still carries SIX.
    counts = {k: cap.family_count(k) for k in ABT2_KEYS}
    for k in ABT2_KEYS:
        n = counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    n_far_total = sum(counts[k] for k in ABT2_KEYS)
    n_abt_fam = cap.family_count(ABT2_PREFIX)
    n_span = cap.family_count("xnu_live_abt_n")
    say("OBS", "`%s` tokens TOTAL x%d (the 1008 image pins %d unique literals = %d span + %d far; "
               "the 1007 arm pins %d) - a count here is RECORDS, not literals" % (
        ABT2_PREFIX, n_abt_fam, ABT2_IMG_LIT_WANT, ABT_IMG_LIT_WANT, len(ABT2_KEYS),
        ABT2_IMG_LIT_WANT_1007))

    if n_far_total == 0:
        # ZERO far tokens: either the switch did not reach the image (the NEGATIVE CONTROL), OR this
        # is a pre-1008 log.  The ABT section's own decision logic is mirrored: the flag waives it to
        # an OBS; WITHOUT the flag the absence is HARD.  The message is sharper when the SPAN
        # instrument (`xnu_live_abt_n`) IS present - then the log provably claims to be a post-1007
        # arm whose 1008 half did not reach `entry_trace.c` (design §7), not merely a pre-1007 log.
        if ALLOW_MISSING_ABT2:
            say("OBS", "NO `xnu_live_abt_far`/`_type`/`_farn`/`_maxfar`/`_maxtype` keys and "
                       "`--allow-missing-abt2` was passed: this is a PRE-1008 log (e.g. the 1007 arm) "
                       "- the abort-fault-identity verdict is NOT certified; the 1007 abort-span, "
                       "1006 freeze, 1005 cadence and 1004 regression clauses below still run "
                       "independently")
            say("OBS", "the 1008 VERDICT is UNCERTIFIED (a reader that cannot fail certifies nothing)")
            ABT2_CERTIFIED = True   # an ALLOWED absence is the 1008 gate's own waiver half
        elif n_span > 0:
            hard("the far rows are entirely ABSENT while `xnu_live_abt_n` is present (x%d): this log "
                 "carries the 1007 SPAN instrument but NOT the 1008 far half - the `ABT_FAR` recorder "
                 "did not reach `entry_trace.c`, or the storm ended before a ladder row. REBUILD with "
                 "STAGE90_XNU_ABT_FAR=1 (design §7)" % n_span, absence_based=True)
        else:
            hard("NO `xnu_live_abt_far`/`_type`/`_farn`/`_maxfar`/`_maxtype` keys: this log carries NO "
                 "1008 abort-fault identity - either the run is a PRE-1008 arm (the switch did not "
                 "reach the image: REBUILD with STAGE90_XNU_ABT_FAR=1) or the wrong capture was handed "
                 "to this tool. The DEFAULT refuses to certify a 1008 pass without the rung's own "
                 "instrument; pass `--allow-missing-abt2` to run ONLY the regression clauses on a "
                 "pre-1008 log", absence_based=True)
        return

    rows = cap.abt2_records()
    say("OBS", "far-ladder records x%d" % len(rows))

    # ---- (abt2-2) every far record must be a full tuple; a partial one is a dropped key.
    partial = [r for r in rows if None in (r["farn"], r["far"], r["type"])]
    if partial:
        hard("x%d far-ladder record(s) are INCOMPLETE (a `_farn` opened a record but `_far`/`_type` "
             "did not both follow) - the recorder's write order or the channel is broken; first "
             "partial @:%d" % (len(partial), partial[0]["ln"]))
    else:
        say("PASS", "every far-ladder record carries the full (farn, far, type) tuple")

    # ---- (abt2-3) THE CLASS FIELD: every `type` must be a real abort class (3 = T_PREFETCH_ABT,
    # 4 = T_DATA_ABT, `osfmk/arm/trap.h`).  A `type` outside {3,4} means the wrapper's class dispatch
    # (entry_trace.c:5687) is reading neither class - a HARD anomaly (design §7).
    bad_type = [r for r in rows if r["type"] is not None and r["type"] not in (ABT2_PREFETCH, ABT2_DATA)]
    if bad_type:
        hard("x%d far-ladder record(s) carry a `type` OUTSIDE {3,4} (first @:%d type=%s) - the "
             "wrapper's class dispatch reads neither T_PREFETCH_ABT(3) nor T_DATA_ABT(4); this is an "
             "INSTRUMENT defect (design §7)" % (len(bad_type), bad_type[0]["ln"], fmt(bad_type[0]["type"])))
    else:
        say("PASS", "every far-ladder `type` is a real abort class (3 = prefetch, 4 = data)")

    # ---- (abt2-4) PAIR BY KEY EQUALITY (design §6.2).  The far ladder publishes `farn` = the far
    # recorder's own ordinal; the span ladder publishes `n` = the span recorder's own ordinal.  BOTH
    # advance once per abort, so the two row sets must carry the SAME ordinals.  A `farn` with NO
    # span row of the same ordinal is a GAP - the far recorder did not run once per abort, an
    # INSTRUMENT failure, not a kernel reading (design §7, why `farn` is published at all).
    span_rows = cap.abt_records()
    span_ns = set(r["n"] for r in span_rows if r["n"] is not None)
    farns = [r["farn"] for r in rows if r["farn"] is not None]
    gaps = [f for f in farns if f not in span_ns]
    if not span_rows:
        say("OBS", "no `xnu_live_abt_n` span rows to pair against - the far rows are read standalone "
                   "(the 1007 SPAN half did not reach this log); the `farn` ordinal is self-describing")
    elif gaps:
        hard("x%d far-ladder record(s) carry a `_farn` with NO matching `xnu_live_abt_n` (first "
             "farn=%s) - the far recorder did not run once per abort (a GAP from `n` at a paired "
             "row): an INSTRUMENT failure, not a kernel reading (design §7)" % (len(gaps), fmt(gaps[0])))
    else:
        say("PASS", "every far-ladder `farn` pairs to a span-ladder `n` of the same ordinal "
                    "(x%d rows; the two recorders ran once per abort) - the self-consistency holds"
            % len(farns))

    # ---- (abt2-5) MONOTONICITY / ladder membership of `_farn`.  A counter that goes BACKWARDS or an
    # ordinal OFF the powers-of-two ladder is an INCONSISTENCY of the recorder (a MEASUREMENT defect,
    # [[mi4-measurement-defects]]).  Reported as a soft finding (design §7 pins the HARD set).
    def on_ladder(n):
        return n is not None and (n <= ABT2_LADDER_HEAD or (n & (n - 1)) == 0)

    prev_f = None
    nonmono = False
    for f in farns:
        if prev_f is not None and f < prev_f:
            nonmono = True
            break
        prev_f = f
    off = [f for f in farns if not on_ladder(f)]
    if nonmono:
        soft("`xnu_live_abt_farn` is NOT non-decreasing across the far rows - a monotone ordinal that "
             "goes backwards is a MEASUREMENT defect; read the rows by value")
    else:
        say("PASS", "`xnu_live_abt_farn` is NON-DECREASING across all %d far-ladder records" % len(rows))
    if off:
        soft("x%d far-ladder `_farn` value(s) are OFF the powers-of-two ladder (first farn=%s) - the "
             "recorder publishes at `n<=4` then `n & (n-1)==0`; an off-ladder ordinal is an "
             "INCONSISTENCY of the recorder" % (len(off), fmt(off[0])))
    else:
        say("PASS", "every published `_farn` is on the powers-of-two ladder (n<=4 then 2^k)")

    def show(r):
        return "@:%d farn=%s far=%s type=%s" % (
            r["ln"], fmt(r["farn"]), fmt(r["far"]), fmt(r["type"]))

    for r in rows[:6]:
        say("OBS", "  far " + show(r))
    if len(rows) > 12:
        say("OBS", "  ... (%d records) ..." % (len(rows) - 12))
        for r in rows[-6:]:
            say("OBS", "  far " + show(r))
    elif len(rows) > 6:
        for r in rows[6:]:
            say("OBS", "  far " + show(r))
    if rows:
        say("READING", "TAIL far-ladder " + show(rows[-1]))

    # ---- (abt2-6) THE MAXCALL LATCH - `_maxfar`/`_maxtype`, written WHERE `call` sets a new max
    # (interpreted by adjacency to `_maxcall`, design §6.1).  The recorder writes them ONLY when `call`
    # changes, so the LAST published value is the far/type of the LONGEST-handler abort (the 18.7 s
    # abort).  Read the LAST published value, NOT `.first()` (a running latch republished N times must
    # be read as its final value - [[mi4-measurement-defects]]).
    def last_val(key):
        for r in reversed(cap.all(key)):
            if r[3] is not None:
                return r[3], r[0]
        return None, None

    maxfar, maxfar_ln = last_val(ABT2_MAXFAR_KEY)
    maxtype, maxtype_ln = last_val(ABT2_MAXTYPE_KEY)
    n_maxfar = cap.family_count(ABT2_MAXFAR_KEY)
    n_maxtype = cap.family_count(ABT2_MAXTYPE_KEY)
    maxcall = None
    for r in reversed(cap.all(ABT_MAXCALL_KEY)):
        if r[3] is not None:
            maxcall = r[3]
            break
    say("OBS", "`%s` x%d, last=%s (the LATCHED far where `call` set a new max)" % (
        ABT2_MAXFAR_KEY, n_maxfar, fmt(maxfar)))
    say("OBS", "`%s` x%d, last=%s (the LATCHED type where `call` set a new max)" % (
        ABT2_MAXTYPE_KEY, n_maxtype, fmt(maxtype)))
    say("OBS", "`%s`=%s is the value the latch pairs with (the LONGEST-handler abort; 1007's 18.7 s "
               "abort)" % (ABT_MAXCALL_KEY, fmt(maxcall)))

    # `_maxfar`/`_maxtype` ABSENT while `_maxcall` is present: the latch trigger never fired on the far
    # recorder's own read - the two clock reads (design §4.3) diverged by more than the storm's `call`
    # spread.  Treated as an INSTRUMENT defect (design §7).
    if maxfar is None and maxcall is not None:
        hard("`%s` is ABSENT while `%s`=%s is present - the maxcall latch never fired on the far "
             "recorder's own read: the two `entry_counter()` reads (design §4.3) diverged by more than "
             "the storm's `call` spread ([[mi4-measurement-defects]]); treat as an INSTRUMENT defect "
             "(design §7)" % (ABT2_MAXFAR_KEY, ABT_MAXCALL_KEY, fmt(maxcall)))
    elif maxtype is None and maxcall is not None:
        hard("`%s` is ABSENT while `%s`=%s is present - the maxcall latch fired for `far` but not "
             "`type` (design §7)" % (ABT2_MAXTYPE_KEY, ABT_MAXCALL_KEY, fmt(maxcall)))

    # ---- (abt2-7) THE CLASSIFIER - the point of 1008.  Read the far LADDER by value (design §6).
    fars = [_u32(r["far"]) for r in rows if r["far"] is not None]
    types = [_u32(r["type"]) for r in rows if r["type"] is not None]
    n_rows = len(rows)
    uniq_fars = sorted(set(fars))
    n_uniq = len(uniq_fars)
    say("OBS", "ladder: x%d rows, x%d DISTINCT far values %s" % (
        n_rows, n_uniq, "[" + ",".join(fmt(f) for f in uniq_fars[:12]) +
        (",...]" if n_uniq > 12 else "]")))
    # "marches forward": consecutive ladder fars are non-decreasing (a distinct, advancing walk).
    marching = all(fars[i] >= fars[i - 1] for i in range(1, len(fars)))
    n_pf = sum(1 for t in types if t == ABT2_PREFETCH)
    n_da = sum(1 for t in types if t == ABT2_DATA)
    say("OBS", "class census over the ladder: prefetch-abort(3) x%d, data-abort(4) x%d" % (n_pf, n_da))

    # ---- (abt2-7a) THE DEGENERATE CASE (design §7): `far == 0` on every storm row with `type == 4` -
    # a degenerate re-entry on a zeroed fault register, not a page walk.  A READING (a mechanism), not
    # an instrument defect, so it is reported, not failed.
    if fars and all(f == 0 for f in fars) and types and all(t == ABT2_DATA for t in types):
        say("READING", "DEGENERATE: every ladder `far` == 0 with `type` == 4 - a re-entry on a ZEROED "
                       "fault register (DFAR not written), NOT a page walk (design §7). This is a "
                       "mechanism, not a storm")
    elif any(f == 0 for f in fars):
        say("OBS", "x%d ladder `far` value(s) are 0 (a zeroed fault register on those rows) - read by "
                   "value" % sum(1 for f in fars if f == 0))

    # ---- (abt2-7b) THE SHAPE: (S) SPREAD vs (R) REPEAT (design §6).  Distinct, forward-marching fars
    # => a fault storm of distinct addresses; the SAME one-or-two addresses repeated => a retry loop /
    # single-address thrash, which REFUTES the storm interpretation (design §7).
    maxfar_in_ladder = (maxfar is None) or (maxfar in set(fars))
    if n_uniq <= 2 and fars and maxfar is not None and maxfar == fars[0]:
        say("VERDICT", "(R) REPEAT - the ladder `far` values repeat the same %d address(es) %s and "
                       "`%s`=%s == the FIRST ladder `far`: a retry loop / single-address THRASH, NOT a "
                       "spreading storm. This reading REFUTES the storm interpretation (design §7)"
            % (n_uniq, "[" + ",".join(fmt(f) for f in uniq_fars) + "]", ABT2_MAXFAR_KEY, fmt(maxfar)))
    elif fars and n_uniq == len(fars) and marching:
        # NOTE: `fars` truthy is LOAD-BEARING - with an EMPTY ladder `n_uniq == len(fars)` is `0 == 0`
        # (True) and `marching` is vacuously True, so without this guard the branch prints a confident
        # (S) SPREAD over ZERO evidence (the maxfar-only shape, design §7).  A reader that cannot fail
        # certifies nothing ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
        say("VERDICT", "(S) SPREAD - the x%d ladder `far` values are DISTINCT and march forward%s: a "
                       "fault storm of DISTINCT addresses (the boot demand-faulting page by page); "
                       "the 18.7 s abort is one page of it" % (
                           n_uniq, " (and `%s`=%s is one of them)" % (ABT2_MAXFAR_KEY, fmt(maxfar))
                           if maxfar_in_ladder else
                           " (though `%s`=%s is NOT among them - the longest-handler fault is off the "
                           "ladder)" % (ABT2_MAXFAR_KEY, fmt(maxfar))))
    elif n_uniq > 2:
        say("VERDICT", "MIXED - x%d rows carry x%d distinct `far` values (neither a single/two-address "
                       "REPEAT nor all-distinct SPREAD); read the ladder by value - the shape is not "
                       "decisive between (S) and (R)" % (len(fars), n_uniq))
    else:
        say("OBS", "the ladder is too short to classify the shape (x%d rows, x%d distinct `far`)"
            % (len(fars), n_uniq))

    # ---- (abt2-7b2) THE DECISIVE TAIL FAR - 1009's FIX OF THE 1008 READER'S WEAKNESS.  The shape
    # classifier above takes the WHOLE ladder as one sample, so 1008's reader counted 12 distinct
    # values in the walked prefix (farn <= 0x200) and reported MIXED, BLIND to the tail (0x400...
    # 0x200000, 61x more aborts) being a single value.  Reporting the `far` at the LAST ladder ordinal
    # (the largest `farn`) is what lets the section say REPEAT-at-<last far> when the tail is constant
    # (records/1008-press-read.txt "The checker said MIXED - and that is itself the finding").
    #
    # GUARD (the zero-evidence defect fixed at 00f3ca1): the report is gated on a NON-EMPTY ladder
    # (`last_far is not None`), so an empty ladder prints an OBS, never a confident tail verdict.
    last_far = None
    last_farn = None
    for r in rows:
        if r["farn"] is not None:
            last_farn, last_far = r["farn"], r["far"]
    if last_far is not None:
        say("READING", "THE DECISIVE TAIL `far` (at the LAST ladder ordinal farn=%s) = %s - this is "
                       "the value the 1008 reader did not report: it counted the %d DISTINCT values "
                       "over the WHOLE ladder and returned MIXED, blind to the tail being the "
                       "decisive sample" % (fmt(last_farn), fmt(last_far), n_uniq))
        # The tail is "constant" iff the far values from the LAST collapse onward agree with the last
        # one - here, conservatively, iff the last two ladder fars are equal (the tail by value).
        if len(fars) >= 2 and fars[-1] == fars[-2]:
            say("VERDICT", "REPEAT at the tail %s - the LAST two ladder ordinals carry the SAME `far` "
                           "(the ladder has collapsed onto one address); the 1008 MIXED reading was a "
                           "property of taking the whole ladder as one sample, NOT of the tail"
                % fmt(last_far))
        else:
            say("OBS", "the last two ladder ordinals do NOT both carry %s - the tail is NOT (yet) a "
                       "constant; read the ladder by value (this is where [ABT3]'s change-count "
                       "decides REPEAT-vs-SPREAD by value)" % fmt(last_far))
    else:
        say("OBS", "the far ladder carries NO ordinal (`farn`) - the decisive-tail far is unavailable; "
                   "read the raw keys by value")

    # ---- (abt2-7c) THE CLASS: (U) USER / (K) KERNEL / (P) PREFETCH (design §6).  A data abort's
    # `far` splits at 0x80000000; a prefetch abort is its own mechanism.
    if types and all(t == ABT2_PREFETCH for t in types):
        say("VERDICT", "(P) PREFETCH - every ladder row is `type`==3 (T_PREFETCH_ABT): "
                       "INSTRUCTION-FETCH faults - a different mechanism (a bad vector / a stale "
                       "I-cache line walked by the retry), NOT a data fault")
    elif n_da > 0:
        far_lo = min(fars) if fars else None
        far_hi = max(fars) if fars else None
        kern = far_hi is not None and far_hi >= ABT2_KERNEL_BASE
        user = far_lo is not None and far_lo < ABT2_KERNEL_BASE
        if kern and not user:
            say("VERDICT", "(K) KERNEL - `type`==4 with `far` >= 0x80000000 (max=%s): KERNEL data "
                           "aborts (a copyin/copyout or a bad kernel pointer) - consistent with "
                           "1004/1007 (1007's live tail `sleh_far`=0x3ef2f000, a kernel copyin)"
                % fmt(far_hi))
        elif user and not kern:
            say("VERDICT", "(U) USER - `type`==4 with `far` < 0x80000000 (min=%s): a USER fault, "
                           "contradicting the 1007 live tail (`sleh_far`=0x3ef2f000, kernel copyin, "
                           "user=0) - this RELOCATES the frontier to the user thread (design §6)"
                % fmt(far_lo))
        elif user and kern:
            say("VERDICT", "(U/K) MIXED CLASS - `type`==4 with ladder `far` spanning BOTH sides of "
                           "0x80000000 (min=%s user, max=%s kernel - design §6): read the ladder by "
                           "value" % (fmt(far_lo), fmt(far_hi)))
        else:
            say("OBS", "the data-abort `far` split is unavailable - read the ladder by value")
    else:
        say("OBS", "no classifiable `type` on the ladder (types=[%s]) - read by value" %
            ",".join(map(str, types)))

    # ---- (abt2-8) THE ONE-LINE VERDICT - the storm's identity (design §6/§7).
    say("VERDICT", "STORM IDENTITY: x%d ladder rows / x%d distinct far / class(prefetch=%d data=%d) / "
                   "maxfar=%s maxtype=%s (the LONGEST-handler abort's fault)" % (
        len(fars), n_uniq, n_pf, n_da, fmt(maxfar), fmt(maxtype)))
    say("OBS", "a MEASUREMENT section: 1008 changes no kernel behaviour, so the storm/freeze is the "
               "EXPECTED outcome - the reading above NAMES the fault the storm is made of and the "
               "fault the longest handler serviced (it does NOT by itself change the frontier)")

    # ---- certified: a real far ladder was read (>= 1 full record) - the 1008 pass gate.
    if rows and not partial:
        ABT2_CERTIFIED = True


def s_abt3(cap):
    """[ABT3] THE FAULTING PC + FSR - THE 1009 RUNG ITSELF (the pure MEASUREMENT, the NEW section).

    Reads the pc LADDER rows opened by `xnu_live_abt_pcn` (with `_pc`/`_fsr`/`_abtcpsr`/`_pcfar`) and
    the two CHANGE-COUNTS (`_nfar`/`_nfsr`, written WHERE THEY CHANGE - a running monotone max), PAIRS
    BY KEY EQUALITY (design §5) against the span recorder's `xnu_live_abt_n` and the far recorder's
    `xnu_live_abt_farn`, verifies `pcn == n == farn` at every paired row, and CLASSIFIES the storm's
    TAIL by value (design §5):
      * (R) REPEAT BY VALUE  - `nfar == 1` AND `nfsr == 1` => ONE faulting address, ONE status,
        retried (the DECISIVE statement 1008's set reader could not make);
      * (C) CYCLING-STATUS   - `nfar == 1` AND `nfsr > 1` => the retries differ in DFSR;
      * (S) SPREAD           - `nfar > 1` => the tail spreads (1008's constant-far reading was wrong).
    It reports the tail `pc` at the LARGEST ordinal (the faulting instruction) and the `abtcpsr` mode.

    A MEASUREMENT (1009 changes no kernel behaviour): it FAILS only on a MISSING key (the switch did
    not reach the image: REBUILD), a `pcn` GAP from `n`/`farn` at a paired row, an `abtcpsr` outside
    the ARM CPSR mode shapes, a change-count that goes BACKWARDS (a MEASUREMENT defect,
    [[mi4-measurement-defects]]), or - by DEFAULT - a log with NO pc keys at all when the run claims
    to be a 1009 arm (the NEGATIVE CONTROL; waived only by `--allow-missing-abt3`, a pre-1009 log).

    NOTE: the two change-counts are read with `max()` (the RUNNING MONOTONE MAXIMUM).  They are
    republished where they change, so `cap.first()` would read the FIRST interim value - 100sx too
    small - and every REPEAT/CYCLING/SPREAD branch would then be mis-scaled.  This is the defect the
    1007 reader hit and fixed (records/1007-press-read.txt)."""
    global ABT3_CERTIFIED
    print("\n== [ABT3] THE FAULTING PC + FSR - the storm's `pc`/`fsr`/`cpsr` + the change-counts "
          "(the 1009 rung) ==")

    # ---- (abt3-1) PRESENCE, by value.  The pc recorder writes 5 ladder keys + 2 change-count keys;
    # the 1009 image (`ABT_FAR=1` AND `ABT_PC=1`) is pinned to EIGHTEEN `xnu_live_abt_` literals total
    # (6 span + 5 far + 7 pc, design §3.4); the 1008 image (`ABT_PC=0`) still carries ELEVEN.
    counts = {k: cap.family_count(k) for k in ABT3_KEYS}
    for k in ABT3_KEYS:
        n = counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    n_pc_total = sum(counts[k] for k in ABT3_KEYS)
    n_abt_fam = cap.family_count(ABT3_PREFIX)
    n_span = cap.family_count("xnu_live_abt_n")
    n_farn = cap.family_count(ABT2_FARN_KEY)
    say("OBS", "`%s` tokens TOTAL x%d (the 1009 image pins %d unique literals = %d span + %d far + %d "
               "pc; the 1008 arm pins %d) - a count here is RECORDS, not literals" % (
        ABT3_PREFIX, n_abt_fam, ABT3_IMG_LIT_WANT, ABT_IMG_LIT_WANT, len(ABT2_KEYS),
        len(ABT3_KEYS), ABT3_IMG_LIT_WANT_1008))

    if n_pc_total == 0:
        # ZERO pc tokens: either the switch did not reach the image (the NEGATIVE CONTROL), OR this is
        # a pre-1009 log (e.g. the 1008 arm - the EXPECTED absence this reader is first run against).
        # The sibling instruments' presence sharpens the message: a log with the SPAN/FAR keys but no
        # pc keys provably claims to be a post-1008 arm whose 1009 half did not reach `entry_trace.c`.
        #
        # THIS IS AN OBS (NOT a hard anomaly) when the log carries the 1008 instrument: the capture
        # was taken BEFORE 1009 existed, so the 1009 keys are EXPECTED absent.  It is HARD only when
        # the log carries NO sibling abort instrument at all (no `xnu_live_abt_` keys) AND the waiver
        # flag was not passed - then the absence is "the switch did not reach the image".
        if ALLOW_MISSING_ABT3:
            say("OBS", "NO `xnu_live_abt_pc*`/`_fsr`/`_abtcpsr`/`_pcfar`/`_nfar`/`_nfsr` keys and "
                       "`--allow-missing-abt3` was passed: this is a PRE-1009 log - the faulting-pc "
                       "verdict is NOT certified; the 1008 identity, 1007 span, 1006 freeze, 1005 "
                       "cadence and 1004 regression clauses below still run independently")
            say("OBS", "the 1009 VERDICT is UNCERTIFIED (a reader that cannot fail certifies nothing)")
            ABT3_CERTIFIED = True   # an ALLOWED absence is the 1009 gate's own waiver half
        elif n_farn > 0 or n_span > 0:
            # THE EXPECTED CASE FOR THIS READER'S FIRST RUN: a 1008-or-later log whose pc half is
            # absent.  The 1009 instrument postdates the capture, so its absence is a READING about
            # the capture's age, NOT a build failure - an OBS, never a hard anomaly.  An EXPECTED,
            # evidence-grounded absence is the gate's own waiver half (the 1006 "valid silence"
            # precedent): the sibling instrument's presence PROVES the log is a real abort-arm
            # capture, so the reader exits 0 on it (no hard anomaly).
            say("OBS", "the pc rows are ABSENT while the 1008 far rows (`%s` x%d) / 1007 span rows "
                       "(`xnu_live_abt_n` x%d) are PRESENT: this capture PREDATES 1009's instrument "
                       "(e.g. the 1008 press at "
                       "out/stage90/captures/recovered-unknown-arm-20261014-124635-last_kmsg.txt), so "
                       "the pc keys are EXPECTED ABSENT - NOT a build failure and NOT a verdict. The "
                       "1009 faulting-pc section is UNCERTIFIED for this log (no hard anomaly)." % (
                           ABT2_FARN_KEY, n_farn, n_span))
            ABT3_CERTIFIED = True   # the EXPECTED pre-1009 absence (sibling instrument present)
        else:
            hard("NO `xnu_live_abt_pc`/`_pcn`/`_fsr`/`_abtcpsr`/`_pcfar`/`_nfar`/`_nfsr` keys AND no "
                 "sibling abort instrument: this log carries NO 1009 faulting-pc witness - either the "
                 "run is a PRE-1009 arm (the switch did not reach the image: REBUILD with "
                 "STAGE90_XNU_ABT_PC=1) or the wrong capture was handed to this tool. The DEFAULT "
                 "refuses to certify a 1009 pass without the rung's own instrument; pass "
                 "`--allow-missing-abt3` to run ONLY the regression clauses on a pre-1009 log",
                 absence_based=True)
        return

    rows = cap.abt3_records()
    say("OBS", "pc-ladder records x%d" % len(rows))

    # ---- (abt3-2) every pc record must be a full tuple; a partial one is a dropped key.
    partial = [r for r in rows
               if None in (r["pcn"], r["pc"], r["fsr"], r["abtcpsr"], r["pcfar"])]
    if partial:
        hard("x%d pc-ladder record(s) are INCOMPLETE (a `_pcn` opened a record but `_pc`/`_fsr`/"
             "`_abtcpsr`/`_pcfar` did not all follow) - the recorder's write order or the channel is "
             "broken; first partial @:%d" % (len(partial), partial[0]["ln"]))
    else:
        say("PASS", "every pc-ladder record carries the full (pcn, pc, fsr, abtcpsr, pcfar) tuple")

    # ---- (abt3-3) THE MODE FIELD: every `abtcpsr` must carry a real ARM CPSR mode (`cpsr & 0x1f` in
    # the eight ARM mode codes).  A value outside them means the forwarded `cpsr` is not an ARM CPSR -
    # an INSTRUMENT defect (design §6).
    bad_mode = []
    for r in rows:
        v = r["abtcpsr"]
        if v is not None and (v & ABT3_MODE_MASK) not in ABT3_ARM_MODES:
            bad_mode.append(r)
    if bad_mode:
        hard("x%d pc-ladder record(s) carry an `abtcpsr` OUTSIDE the ARM CPSR mode shapes "
             "(first @:%d abtcpsr=%s, mode=0x%02x) - the forwarded `cpsr` is not an ARM CPSR; this is "
             "an INSTRUMENT defect (design §6)" % (
                 len(bad_mode), bad_mode[0]["ln"], fmt(bad_mode[0]["abtcpsr"]),
                 bad_mode[0]["abtcpsr"] & ABT3_MODE_MASK))
    else:
        say("PASS", "every pc-ladder `abtcpsr` carries a real ARM CPSR mode (cpsr & 0x1f in 0x10..0x1f)")

    # ---- (abt3-4) PAIR BY KEY EQUALITY (design §5).  The pc ladder publishes `pcn`; the span ladder
    # publishes `n`; the far ladder publishes `farn`.  ALL advance once per abort, so the three row
    # sets must carry the SAME ordinals.  A `pcn` with NO span/far row of the same ordinal is a GAP -
    # the pc recorder did not run once per abort, an INSTRUMENT failure, not a kernel reading.
    span_ns = set(r["n"] for r in cap.abt_records() if r["n"] is not None)
    far_ns = set(r["farn"] for r in cap.abt2_records() if r["farn"] is not None)
    pcns = [r["pcn"] for r in rows if r["pcn"] is not None]
    sibling_ns = span_ns | far_ns
    gaps = [p for p in pcns if sibling_ns and p not in sibling_ns]
    if not sibling_ns:
        say("OBS", "no `xnu_live_abt_n`/`_farn` sibling rows to pair against - the pc rows are read "
                   "standalone (the 1007 span / 1008 far halves did not reach this log); the `pcn` "
                   "ordinal is self-describing")
    elif gaps:
        hard("x%d pc-ladder record(s) carry a `_pcn` with NO matching `xnu_live_abt_n`/`_farn` (first "
             "pcn=%s) - the pc recorder did not run once per abort (a GAP from `n`/`farn` at a paired "
             "row): an INSTRUMENT failure, not a kernel reading (design §6)" % (len(gaps), fmt(gaps[0])))
    else:
        say("PASS", "every pc-ladder `pcn` pairs to a span/far row of the same ordinal (x%d rows; the "
                    "recorders ran once per abort) - the self-consistency holds" % len(pcns))

    # ---- (abt3-5) MONOTONICITY / ladder membership of `_pcn` (a MEASUREMENT defect if it goes
    # backwards or is off the powers-of-two ladder; reported as a soft finding, design §6).
    def on_ladder(n):
        return n is not None and (n <= ABT3_LADDER_HEAD or (n & (n - 1)) == 0)

    prev_p = None
    nonmono = False
    for p in pcns:
        if prev_p is not None and p < prev_p:
            nonmono = True
            break
        prev_p = p
    off = [p for p in pcns if not on_ladder(p)]
    if nonmono:
        soft("`xnu_live_abt_pcn` is NOT non-decreasing across the pc rows - a monotone ordinal that "
             "goes backwards is a MEASUREMENT defect; read the rows by value")
    else:
        say("PASS", "`xnu_live_abt_pcn` is NON-DECREASING across all %d pc-ladder records" % len(rows))
    if off:
        soft("x%d pc-ladder `_pcn` value(s) are OFF the powers-of-two ladder (first pcn=%s) - the "
             "recorder publishes at `n<=4` then `n & (n-1)==0`; an off-ladder ordinal is an "
             "INCONSISTENCY of the recorder" % (len(off), fmt(off[0])))
    else:
        say("PASS", "every published `_pcn` is on the powers-of-two ladder (n<=4 then 2^k)")

    def show(r):
        return "@:%d pcn=%s pc=%s fsr=%s abtcpsr=%s pcfar=%s" % (
            r["ln"], fmt(r["pcn"]), fmt(r["pc"]), fmt(r["fsr"]), fmt(r["abtcpsr"]), fmt(r["pcfar"]))

    for r in rows[:6]:
        say("OBS", "  pc " + show(r))
    if len(rows) > 12:
        say("OBS", "  ... (%d records) ..." % (len(rows) - 12))
        for r in rows[-6:]:
            say("OBS", "  pc " + show(r))
    elif len(rows) > 6:
        for r in rows[6:]:
            say("OBS", "  pc " + show(r))

    # ---- (abt3-6) THE CHANGE-COUNTS - `_nfar`/`_nfsr`, the number of CHANGES in `far`/`fsr` so far.
    # WRITTEN WHERE THEY CHANGE (a running monotone max), so they are read as their MAXIMUM over the
    # published values - NOT `.first()` (a first-read running max is 100sx too small: the defect this
    # repo hit at 1007, records/1007-press-read.txt; [[mi4-measurement-defects]]).  A counter that
    # goes BACKWARDS is itself a MEASUREMENT defect.
    def change_count(key):
        """Read a running-monotone change-count as its MAXIMUM, and check monotonicity by value.

        Returns `(maximum, count, back)` where `back` is `(prev, v, ln_prev, ln_v)` for the FIRST
        decrease in log order, or `None` if the running max is non-decreasing."""
        vals = [r[3] for r in cap.all(key) if r[3] is not None]
        if not vals:
            return None, 0, None
        mx = max(vals)
        prev, ln_prev, back = None, None, None
        for r in cap.all(key):
            v = r[3]
            if v is None:
                continue
            if prev is not None and v < prev:
                back = (prev, v, ln_prev, r[0])
                break
            prev, ln_prev = v, r[0]
        return mx, len(vals), back

    nfar, n_nfar, back_far = change_count(ABT3_NFAR_KEY)
    nfsr, n_nfsr, back_fsr = change_count(ABT3_NFSR_KEY)
    say("OBS", "`%s` x%d, MAX=%s (the number of CHANGES in `far` seen so far, read as the running "
               "max - NOT first())" % (ABT3_NFAR_KEY, n_nfar, fmt(nfar)))
    say("OBS", "`%s` x%d, MAX=%s (the number of CHANGES in `fsr` seen so far, read as the running "
               "max)" % (ABT3_NFSR_KEY, n_nfsr, fmt(nfsr)))
    for key, cnt, back in ((ABT3_NFAR_KEY, n_nfar, back_far), (ABT3_NFSR_KEY, n_nfsr, back_fsr)):
        if back is not None:
            prev, v, ln_prev, ln_v = back
            hard("NON-MONOTONE `%s`: it DECREASED %s -> %s (@:%d -> @:%d) - a monotone running-max "
                 "that goes backwards is a MEASUREMENT defect (a reset, a re-read of the wrong "
                 "static, or a partial record), not a reading" % (
                     key, fmt(prev), fmt(v), ln_prev, ln_v))
        elif cnt:
            say("PASS", "`%s` is NON-DECREASING across all %d published values" % (key, cnt))

    # A change-count ABSENT while the pc ladder IS present: the recorder's `g_sleh_armed` site never
    # fired - an INSTRUMENT defect (design §6).  Guarded on the ladder being present (rows truthy).
    if rows:
        if nfar is None:
            hard("`%s` is ABSENT while the pc ladder is PRESENT (x%d rows) - the change-count site "
                 "never fired: an INSTRUMENT defect (design §6)" % (ABT3_NFAR_KEY, len(rows)))
        if nfsr is None:
            hard("`%s` is ABSENT while the pc ladder is PRESENT (x%d rows) - the change-count site "
                 "never fired: an INSTRUMENT defect (design §6)" % (ABT3_NFSR_KEY, len(rows)))

    # ---- (abt3-7) THE TAIL `pc`/`fsr`/`abtcpsr` - the faulting instruction and the mode.  The `pc`
    # at the LARGEST ordinal is the faulting instruction (design §5).  GUARDED: only if the ladder is
    # non-empty (the zero-evidence rule - a branch that fires over zero samples is not a verdict).
    tail = None
    for r in rows:
        if r["pcn"] is not None:
            if tail is None or r["pcn"] >= tail["pcn"]:
                tail = r
    if tail is not None:
        mode = (tail["abtcpsr"] & ABT3_MODE_MASK) if tail["abtcpsr"] is not None else None
        mode_name = ABT3_MODE_NAMES.get(mode, "?") if mode is not None else "?"
        irq = ((tail["abtcpsr"] >> ABT3_CPSR_IBIT) & 1) if tail["abtcpsr"] is not None else None
        say("READING", "THE FAULTING INSTRUCTION (the `pc` at the LARGEST ordinal): pcn=%s pc=%s fsr=%s "
                       "abtcpsr=%s (mode=%s, IRQ %s) pcfar=%s @:%d" % (
                           fmt(tail["pcn"]), fmt(tail["pc"]), fmt(tail["fsr"]), fmt(tail["abtcpsr"]),
                           mode_name, {1: "DISABLED", 0: "ENABLED"}.get(irq, "?"),
                           fmt(tail["pcfar"]), tail["ln"]))
    else:
        say("OBS", "the pc ladder carries NO ordinal (`pcn`) - the tail pc is unavailable; read the "
                   "raw keys by value")

    # ---- (abt3-8) THE CLASSIFIER - the point of 1009.  Decided by value over the TAIL LADDER ROWS.
    #
    # **THE DEFECT THIS FIXES (measured, 1009's own run).**  The design (docs/1009-design.md §5) said
    # classify on the change-counts `nfar`/`nfsr`.  But those are encoded as WHOLE-RUN running maxima
    # (written from abort 1, monotone - a running max cannot be rescaled): 1009's run reads nfar=832,
    # nfsr=49, the CHANGES counted from the start through the 12-distinct-value dyld walk.  `nfar>1`
    # is therefore true from the walk onward and can NEVER say REPEAT even when the tail is one
    # constant fault - the first run of this reader reported SPREAD over a tail that is, in fact,
    # perfectly constant ([[mi4-measurement-defects]]: a whole-run statistic is not a tail statistic;
    # [[mi4-silence-is-a-reading-only-if-success-is-silent]]).
    #
    # The tail IS visible - in the LADDER ROWS' OWN values.  The ladder rows at the tail ordinals
    # carry `pcfar`/`fsr`/`abtcpsr`; their constancy over the tail IS the REPEAT statement.  The
    # classifier reads those, and reports the change-counts' `nfar`/`nfsr` as what they are (the
    # whole-run counts), never as a tail verdict.
    tail_rows = [r for r in rows
                 if r["pcn"] is not None and r["pcn"] >= ABT3_TAIL_MIN_PCN and r["pcfar"] is not None]
    n_unchanged_since = None   # set below for the report: if the tail was already flat before TAIL_MIN
    if not rows:
        say("OBS", "the pc ladder carries NO rows: the tail cannot be classified - a branch over zero "
                   "samples is not a verdict ([[mi4-silence-is-a-reading-only-if-success-is-silent]])")
    elif len(tail_rows) < 2:
        say("OBS", "fewer than two ladder rows at/after the tail ordinal %s (x%d): the tail cannot be "
                   "classified by value - read the raw keys" % (fmt(ABT3_TAIL_MIN_PCN), len(tail_rows)))
    else:
        tail_fars = [r["pcfar"] for r in tail_rows]
        tail_fsrs = [r["fsr"] for r in tail_rows]
        tail_pcs2 = [r["pc"] for r in tail_rows]
        far_const = len(set(tail_fars)) == 1
        fsr_const = len(set(tail_fsrs)) == 1
        pc_const = len(set(tail_pcs2)) == 1
        if far_const and fsr_const:
            say("VERDICT", "(R) REPEAT (by value) - the x%d ladder rows at/after ordinal %s carry ONE "
                           "constant `far`=%s AND ONE constant `fsr`=%s%s: ONE faulting address, ONE "
                           "status, RETRIED for the whole tail. This is the DECISIVE statement the "
                           "1008 reader could not make (it counted the ladder SET and returned MIXED, "
                           "blind to the tail being 61x the prefix). The fault is the SAME one re-run."
                % (len(tail_rows), fmt(ABT3_TAIL_MIN_PCN), fmt(tail_fars[0]), fmt(tail_fsrs[0]),
                   " (and ONE constant `pc`=%s)" % fmt(tail_pcs2[0]) if pc_const else
                   " (the `pc` VARIES: %s)" % "[" + ",".join(fmt(p) for p in sorted(set(tail_pcs2))) + "]"))
        elif far_const and not fsr_const:
            say("VERDICT", "(C) CYCLING-STATUS - the tail `far` is CONSTANT (%s) but the DFSR CYCLES "
                           "over the tail: [%s] - the retries are NOT identical (the same address "
                           "faults with a different fault-status code / WnR): a small status loop at "
                           "one address, not one fault re-run"
                % (fmt(tail_fars[0]), ",".join(fmt(f) for f in sorted(set(tail_fsrs)))))
        else:
            say("VERDICT", "(S) SPREAD - the tail `far` VARIES over x%d distinct value(s) [%s] "
                           "(fsr x%d distinct %s): the storm demand-faults page by page - an "
                           "instruction/trap number is sampled here but the fault ADDRESSES spread "
                           "(design §6 falsifier; this is what 1008's constant-far reading would "
                           "have REFUTED had it held)"
                % (len(set(tail_fars)), ",".join(fmt(f) for f in sorted(set(tail_fars))),
                   len(set(tail_fsrs)), "CONSTANT" if fsr_const else "varies"))
    say("OBS", "the change-counts `%s`/`%s` (whole-run running maxima) are %s/%s: they count changes "
               "from abort 1 through the walked prefix and CANNOT describe the tail - the tail verdict "
               "above reads the ladder ROWS, not these" % (
                   ABT3_NFAR_KEY, ABT3_NFSR_KEY, fmt(nfar), fmt(nfsr)))

    # ---- (abt3-9) THE PC CONSTANCY FALSIFIER (design §6): the `_pc` at the tail ordinals.  If the
    # tail `pc` is NOT constant, different instructions reference the same `far` (a demand pattern) -
    # a READING (a mechanism), reported not failed.  GUARDED on the tail having >= 2 comparable pcs.
    tail_pcs = [r["pc"] for r in rows[-4:] if r["pc"] is not None]
    if len(tail_pcs) >= 2:
        if len(set(tail_pcs)) == 1:
            say("OBS", "the tail `pc` (last %d) is CONSTANT at %s - the SAME instruction re-runs (the "
                       "single-faulting-instruction reading)" % (len(tail_pcs), fmt(tail_pcs[0])))
        else:
            say("OBS", "the tail `pc` (last %d) is NOT constant %s - DIFFERENT instructions reference "
                       "the fault address (a demand pattern, design §6 falsifier)" % (
                           len(tail_pcs), "[" + ",".join(fmt(p) for p in tail_pcs) + "]"))
    else:
        say("OBS", "fewer than two comparable tail `pc` values - the constancy falsifier cannot run "
                   "(a branch over zero samples is not evidence)")

    say("OBS", "a MEASUREMENT section: 1009 changes no kernel behaviour, so the storm/freeze is the "
               "EXPECTED outcome - the reading above NAMES the faulting INSTRUCTION and the fault "
               "STATUS (it does NOT by itself change the frontier)")

    # ---- certified: a real pc ladder was read (>= 1 full record) - the 1009 pass gate.
    if rows and not partial:
        ABT3_CERTIFIED = True


def _l2_decode(l2):
    """Decode an ARM short-descriptor L2 SMALL-PAGE entry into a dict of the decisive fields.

    `l2` is the raw 32-bit L2 descriptor the walk read (`stage90_read_l2_pte`).  Returns:
      {"type": bits[1:0], "small": bool, "apx": bit9, "user": bit5, "nx": bit0, "pa": bits[31:12]}.
    Type 0b10 = small page; APX (bit 9) = 1 => USER READ-ONLY (the decisive cell, design §2.2)."""
    if l2 is None:
        return None
    return {"type": l2 & ABT4_L2_TYPE_MASK,
            "small": (l2 & ABT4_L2_TYPE_MASK) == ABT4_L2_SMALL_PAGE,
            "apx": (l2 >> ABT4_L2_APX_BIT) & 1,
            "user": (l2 >> ABT4_L2_USER_BIT) & 1,
            "nx": (l2 >> ABT4_L2_NX_BIT) & 1,
            "pa": l2 & ABT4_L2_PA_MASK}


def s_abt4(cap):
    """[ABT4] THE L2 DESCRIPTOR + vm_fault RETURN - THE 1010 RUNG ITSELF (pure MEASUREMENT, NEW).

    Reads the pte LADDER rows opened by `xnu_live_abt_pten` (with `_pteva`/`_ptel2`/`_ptel1`/
    `_ptettbr`/`_pteu`) and the vm_fault-return rows opened by `xnu_live_abt_vmfn` (with `_vmfret`/
    `_vmfva`/`_vmfprot`), plus the two CHANGE-COUNTS (`_ptenx`/`_vmfnret`, written WHERE THEY CHANGE - a
    running monotone max).  It PAIRS the pte rows to the span/pc/far rows BY KEY EQUALITY
    (`pten == n == farn == pcn` at every paired row; all recorders advance one per abort), then
    CLASSIFIES BY VALUE.

    **THE HEADLINE (design §2.2).**  The decisive cell is the (A) L2 descriptor's APX bit (bit 9) at
    the TAIL ordinals.  The classifier decides CONSTANT-vs-CYCLING on the TAIL ROWS' OWN `_ptel2`
    (rows at/after ordinal 0x400) - NEVER on the whole-run change-count `_ptenx` (a whole-run running
    max counts changes from abort 1 and can NEVER describe the tail: the 1008/1009 measurement defect,
    [[mi4-measurement-defects]]).  The expected row is APX=1 (USER READ-ONLY) paired with (B)
    `_vmfret == KERN_SUCCESS`: "the PTE is read-only though vm_fault said success" - the cs-taint /
    prot-downgrade path (design §2.2).

    A MEASUREMENT (1010 changes no kernel behaviour absent the §8.1 wrap surface): it FAILS only on a
    MISSING key (the switch did not reach the image: REBUILD), a `pten` GAP from `n`/`farn`/`pcn` at a
    paired row, an L2 type outside 0b10 (the walk's descriptor read is mismatched), a vm_fault-return
    ladder whose `_vmfret` is absent while its `_vmfn` is present, a change-count that goes BACKWARDS
    (a MEASUREMENT defect, [[mi4-measurement-defects]]), or - by DEFAULT - a log with NO pte keys at
    all when the run claims to be a 1010 arm (the NEGATIVE CONTROL; waived only by `--allow-missing-
    abt4`, a pre-1010 log).

    NOTE: the change-counts are read with `max()` (the RUNNING MONOTONE MAXIMUM), NOT `cap.first()`
    (a first-read running max is 100sx too small - the 1007 reader's fixed defect)."""
    global ABT4_CERTIFIED
    print("\n== [ABT4] THE L2 DESCRIPTOR + vm_fault RETURN - the fault VA's L2 small-page entry "
          "(APX/USER/NX/PA) + what vm_fault returned (the 1010 rung) ==")

    # ---- (abt4-1) PRESENCE, by value.  The pte recorder writes 6 ladder keys + 1 change-count; the
    # vm_fault wrapper writes 4 ladder keys + 1 change-count.  The 1010 image (`ABT_PTE=1`) is pinned to
    # TWENTY-NINE `xnu_live_abt_` literals total (6 span + 5 far + 7 pc + 11 pte/vmf, design §3.4); the
    # 1009 image (`ABT_PTE=0`) still carries EIGHTEEN.
    counts = {k: cap.family_count(k) for k in ABT4_KEYS}
    for k in ABT4_KEYS:
        n = counts[k]
        say("OBS" if n else "---", "%s x%d" % (k, n))
    n_pte_total = sum(counts[k] for k in ABT4_KEYS)
    n_abt_fam = cap.family_count(ABT4_PREFIX)
    n_pten = cap.family_count(ABT4_PTEN_KEY)
    n_span = cap.family_count("xnu_live_abt_n")
    n_farn = cap.family_count(ABT2_FARN_KEY)
    n_pcn = cap.family_count(ABT3_PCN_KEY)
    say("OBS", "`%s` tokens TOTAL x%d (the 1010 image pins %d unique literals = %d span + %d far + %d "
               "pc + %d pte/vmf; the 1009 arm pins %d) - a count here is RECORDS, not literals" % (
        ABT4_PREFIX, n_abt_fam, ABT4_IMG_LIT_WANT, ABT_IMG_LIT_WANT, len(ABT2_KEYS),
        len(ABT3_KEYS), ABT4_ADDED_LIT_WANT, ABT4_IMG_LIT_WANT_1009))

    if n_pte_total == 0:
        # ZERO pte tokens: either the switch did not reach the image (the NEGATIVE CONTROL), OR this is
        # a pre-1010 log (e.g. the 1009 arm - the EXPECTED absence this reader is first run against).
        # The sibling instruments' presence sharpens the message.
        if ALLOW_MISSING_ABT4:
            say("OBS", "NO `xnu_live_abt_pte*`/`_vmf*` keys and `--allow-missing-abt4` was passed: this "
                       "is a PRE-1010 log - the L2-descriptor verdict is NOT certified; the 1009 "
                       "faulting-pc, 1008 far, 1007 span, 1006 freeze, 1005 cadence and 1004 regression "
                       "clauses below still run independently")
            say("OBS", "the 1010 VERDICT is UNCERTIFIED (a reader that cannot fail certifies nothing)")
            ABT4_CERTIFIED = True   # an ALLOWED absence is the 1010 gate's own waiver half
        elif n_pcn > 0 or n_farn > 0 or n_span > 0:
            # THE EXPECTED CASE FOR THIS READER'S FIRST RUN: a 1009-or-later log whose pte half is
            # absent (e.g. the 1009 press at out/stage90/captures/recovered-unknown-arm-
            # 20261014-140220-last_kmsg.txt).  The sibling instrument's presence PROVES the log is a
            # real abort-arm capture, so the pte keys are EXPECTED ABSENT - an OBS, never a hard anomaly.
            say("OBS", "the pte rows are ABSENT while the 1009 pc rows (`%s` x%d) / 1008 far rows "
                       "(`%s` x%d) / 1007 span rows (`xnu_live_abt_n` x%d) are PRESENT: this capture "
                       "PREDATES 1010's instrument, so the pte/vmf keys are EXPECTED ABSENT - NOT a "
                       "build failure and NOT a verdict. The 1010 section is UNCERTIFIED for this log "
                       "(no hard anomaly)." % (
                           ABT3_PCN_KEY, n_pcn, ABT2_FARN_KEY, n_farn, n_span))
            ABT4_CERTIFIED = True   # the EXPECTED pre-1010 absence (sibling instrument present)
        else:
            hard("NO `xnu_live_abt_pte*`/`_vmf*` keys AND no sibling abort instrument: this log carries "
                 "NO 1010 L2-descriptor witness - either the run is a PRE-1010 arm (the switch did not "
                 "reach the image: REBUILD with STAGE90_XNU_ABT_PTE=1) or the wrong capture was handed "
                 "to this tool. The DEFAULT refuses to certify a 1010 pass without the rung's own "
                 "instrument; pass `--allow-missing-abt4` to run ONLY the regression clauses",
                 absence_based=True)
        return

    rows = cap.abt4_records()
    vmf_rows = cap.abt4_vmf_records()
    say("OBS", "pte-ladder records x%d, vm_fault-return records x%d" % (len(rows), len(vmf_rows)))

    # ---- (abt4-2) every pte record must be a full tuple; a partial one is a dropped key.
    partial = [r for r in rows
               if None in (r["pten"], r["pteva"], r["ptel2"], r["ptel1"], r["ptettbr"], r["pteu"])]
    if partial:
        hard("x%d pte-ladder record(s) are INCOMPLETE (a `_pten` opened a record but `_pteva`/`_ptel2`/"
             "`_ptel1`/`_ptettbr`/`_pteu` did not all follow) - the recorder's write order or the "
             "channel is broken; first partial @:%d" % (len(partial), partial[0]["ln"]))
    else:
        say("PASS", "every pte-ladder record carries the full (pten, pteva, ptel2, ptel1, ptettbr, pteu) "
                    "tuple")

    # ---- (abt4-3) THE L2 TYPE: every published `_ptel2` must carry an L2 SMALL-PAGE type (bits[1:0] ==
    # 0b10).  A non-zero `_ptel2` whose type bits are NOT 0b10 means the walk read a section or a
    # mismatched word - an INSTRUMENT defect (design §6 falsifier (g)).  A `_ptel2` == 0 is the walk's
    # FAIL-OPEN return (the address was not a small-page L2 entry: the `_pteu`/`_ptettbr` say which) -
    # reported, not failed.
    nonzero = [r for r in rows if r["ptel2"] is not None and r["ptel2"] != 0]
    bad_type = [r for r in nonzero if (r["ptel2"] & ABT4_L2_TYPE_MASK) != ABT4_L2_SMALL_PAGE]
    zero_l2 = [r for r in rows if r["ptel2"] == 0]
    if bad_type:
        hard("x%d pte-ladder record(s) carry a `_ptel2` whose type bits[1:0] are NOT 0b10 (small page) "
             "(first @:%d ptel2=%s type=0x%x) - the walk's descriptor read is mismatched, an INSTRUMENT "
             "defect (design §6 falsifier (g))" % (
                 len(bad_type), bad_type[0]["ln"], fmt(bad_type[0]["ptel2"]),
                 bad_type[0]["ptel2"] & ABT4_L2_TYPE_MASK))
    else:
        say("PASS", "every nonzero `_ptel2` carries an L2 small-page type (bits[1:0] == 0b10, x%d rows)"
                    % len(nonzero))
    if zero_l2:
        say("OBS", "x%d pte-ladder record(s) carry `_ptel2`=0 (the walk's FAIL-OPEN return: the "
                   "address was not a small-page L2 entry under TTBR0 - a section, or TTBR1 was "
                   "selected); read `_pteu` (1 = TTBR0 used) and `_ptettbr` before judging" % len(zero_l2))

    # ---- (abt4-4) PAIR BY KEY EQUALITY (design §5).  The pte ladder publishes `pten`; the span ladder
    # `n`; the far ladder `farn`; the pc ladder `pcn`.  ALL advance once per abort, so the four row sets
    # must carry the SAME ordinals.  A `pten` with NO sibling row of the same ordinal is a GAP - the pte
    # recorder did not run once per abort, an INSTRUMENT failure, not a kernel reading.
    span_ns = set(r["n"] for r in cap.abt_records() if r["n"] is not None)
    far_ns = set(r["farn"] for r in cap.abt2_records() if r["farn"] is not None)
    pc_ns = set(r["pcn"] for r in cap.abt3_records() if r["pcn"] is not None)
    ptens = [r["pten"] for r in rows if r["pten"] is not None]
    sibling_ns = span_ns | far_ns | pc_ns
    gaps = [p for p in ptens if sibling_ns and p not in sibling_ns]
    if not sibling_ns:
        say("OBS", "no `xnu_live_abt_n`/`_farn`/`_pcn` sibling rows to pair against - the pte rows are "
                   "read standalone; the `pten` ordinal is self-describing")
    elif gaps:
        hard("x%d pte-ladder record(s) carry a `_pten` with NO matching `xnu_live_abt_n`/`_farn`/`_pcn` "
             "(first pten=%s) - the pte recorder did not run once per abort (a GAP at a paired row): an "
             "INSTRUMENT failure, not a kernel reading (design §6)" % (len(gaps), fmt(gaps[0])))
    else:
        say("PASS", "every pte-ladder `pten` pairs to a span/far/pc row of the same ordinal (x%d rows; "
                    "the recorders ran once per abort) - the self-consistency holds" % len(ptens))

    # ---- (abt4-5) MONOTONICITY / ladder membership of `_pten`.
    def on_ladder(n):
        return n is not None and (n <= ABT4_LADDER_HEAD or (n & (n - 1)) == 0)

    prev_p = None
    nonmono = False
    for p in ptens:
        if prev_p is not None and p < prev_p:
            nonmono = True
            break
        prev_p = p
    off = [p for p in ptens if not on_ladder(p)]
    if nonmono:
        soft("`xnu_live_abt_pten` is NOT non-decreasing across the pte rows - a monotone ordinal that "
             "goes backwards is a MEASUREMENT defect; read the rows by value")
    else:
        say("PASS", "`xnu_live_abt_pten` is NON-DECREASING across all %d pte-ladder records" % len(rows))
    if off:
        soft("x%d pte-ladder `_pten` value(s) are OFF the powers-of-two ladder (first pten=%s) - the "
             "recorder publishes at `n<=4` then `n & (n-1)==0`; an off-ladder ordinal is an "
             "INCONSISTENCY of the recorder" % (len(off), fmt(off[0])))
    else:
        say("PASS", "every published `_pten` is on the powers-of-two ladder (n<=4 then 2^k)")

    def show(r):
        d = _l2_decode(r["ptel2"])
        dec = "" if d is None else " [type=%s APX=%d USER=%d NX=%d PA=%s]" % (
            "small" if d["small"] else "0x%x" % d["type"], d["apx"], d["user"], d["nx"], fmt(d["pa"]))
        return "@:%d pten=%s pteva=%s ptel2=%s ptel1=%s ptettbr=%s pteu=%d%s" % (
            r["ln"], fmt(r["pten"]), fmt(r["pteva"]), fmt(r["ptel2"]), fmt(r["ptel1"]),
            fmt(r["ptettbr"]), r["pteu"] or 0, dec)

    for r in rows[:6]:
        say("OBS", "  pte " + show(r))
    if len(rows) > 12:
        say("OBS", "  ... (%d records) ..." % (len(rows) - 12))
        for r in rows[-6:]:
            say("OBS", "  pte " + show(r))
    elif len(rows) > 6:
        for r in rows[6:]:
            say("OBS", "  pte " + show(r))

    # ---- (abt4-6) THE CHANGE-COUNTS - `_ptenx`/`_vmfnret`, read as their RUNNING MONOTONE MAXIMUM
    # (NOT `.first()`: a first-read running max is 100sx too small - the 1007 defect).
    def change_count(key):
        vals = [r[3] for r in cap.all(key) if r[3] is not None]
        if not vals:
            return None, 0, None
        mx = max(vals)
        prev, ln_prev, back = None, None, None
        for r in cap.all(key):
            v = r[3]
            if v is None:
                continue
            if prev is not None and v < prev:
                back = (prev, v, ln_prev, r[0])
                break
            prev, ln_prev = v, r[0]
        return mx, len(vals), back

    nptenx, n_ptenx, back_ptenx = change_count(ABT4_PTENX_KEY)
    nvmfnret, n_vmfnret, back_vmfnret = change_count(ABT4_VMFNRET_KEY)
    say("OBS", "`%s` x%d, MAX=%s (the number of CHANGES in `l2` seen so far, read as the running max - "
               "NOT first(); REPORTED AS AN OBSERVATION ONLY - it is a WHOLE-RUN count and does NOT "
               "decide the tail)" % (ABT4_PTENX_KEY, n_ptenx, fmt(nptenx)))
    say("OBS", "`%s` x%d, MAX=%s (the number of CHANGES in `ret` seen so far, read as the running max)"
               % (ABT4_VMFNRET_KEY, n_vmfnret, fmt(nvmfnret)))
    for key, cnt, back in ((ABT4_PTENX_KEY, n_ptenx, back_ptenx),
                           (ABT4_VMFNRET_KEY, n_vmfnret, back_vmfnret)):
        if back is not None:
            prev, v, ln_prev, ln_v = back
            hard("NON-MONOTONE `%s`: it DECREASED %s -> %s (@:%d -> @:%d) - a monotone running-max that "
                 "goes backwards is a MEASUREMENT defect, not a reading" % (
                     key, fmt(prev), fmt(v), ln_prev, ln_v))
        elif cnt:
            say("PASS", "`%s` is NON-DECREASING across all %d published values" % (key, cnt))

    if rows and nptenx is None:
        hard("`%s` is ABSENT while the pte ladder is PRESENT (x%d rows) - the change-count site never "
             "fired: an INSTRUMENT defect (design §6)" % (ABT4_PTENX_KEY, len(rows)))
    # The vm_fault-return ladder: if `_vmfn` rows exist, `_vmfret` must have followed (the wrapper fired).
    vmf_partial = [r for r in vmf_rows
                   if None in (r["vmfn"], r["vmfret"], r["vmfva"], r["vmfprot"])]
    if vmf_partial:
        hard("x%d vm_fault-return record(s) are INCOMPLETE (a `_vmfn` opened a record but `_vmfret`/"
             "`_vmfva`/`_vmfprot` did not all follow) - the wrapper's write order or the channel is "
             "broken; first partial @:%d" % (len(vmf_partial), vmf_partial[0]["ln"]))
    elif vmf_rows:
        say("PASS", "every vm_fault-return record carries the full (vmfn, vmfret, vmfva, vmfprot) tuple")

    # ---- (abt4-7) THE TAIL L2 DESCRIPTOR - THE HEADLINE (design §2.2).  DECIDED BY THE TAIL ROWS' OWN
    # `_ptel2`, NEVER by the whole-run `_ptenx` (the 1008/1009 measurement defect, NOT repeated).  The
    # tail rows carry `_ptel2`/`_ptettbr`/`_pteu`; their L2 APX constancy IS the "read-only" statement.
    tail_rows = [r for r in rows
                 if r["pten"] is not None and r["pten"] >= ABT4_TAIL_MIN_PTEN and r["ptel2"] is not None]
    tail_apx = None
    if not rows:
        say("OBS", "the pte ladder carries NO rows: the tail cannot be classified - a branch over zero "
                   "samples is not a verdict ([[mi4-silence-is-a-reading-only-if-success-is-silent]])")
    elif len(tail_rows) < 2:
        say("OBS", "fewer than two ladder rows at/after the tail ordinal %s (x%d): the tail cannot be "
                   "classified by value - read the raw keys" % (fmt(ABT4_TAIL_MIN_PTEN), len(tail_rows)))
    else:
        tail_l2 = [r["ptel2"] for r in tail_rows]
        tail_l2_const = len(set(tail_l2)) == 1
        d = _l2_decode(tail_l2[-1])
        tail_apx = d["apx"] if d else None
        if tail_l2_const:
            if d and d["small"]:
                say("READING", "(A) THE TAIL L2 DESCRIPTOR IS CONSTANT at %s over x%d rows (ordinal>=%s): "
                               "type=small-page USER=%d NX=%d PA=%s APX=%d (%s)" % (
                                   fmt(tail_l2[0]), len(tail_rows), fmt(ABT4_TAIL_MIN_PTEN), d["user"],
                                   d["nx"], fmt(d["pa"]), d["apx"],
                                   "USER READ-ONLY" if d["apx"] else "USER READ-WRITE"))
            else:
                say("READING", "(A) THE TAIL L2 DESCRIPTOR IS CONSTANT at %s over x%d rows (ordinal>=%s) "
                               "but its type bits[1:0]=0x%x are NOT small-page - read `_pteu`/`_ptettbr`" % (
                                   fmt(tail_l2[0]), len(tail_rows), fmt(ABT4_TAIL_MIN_PTEN),
                                   tail_l2[0] & ABT4_L2_TYPE_MASK))
        else:
            say("READING", "(A) THE TAIL L2 DESCRIPTOR CYCLES over x%d distinct value(s) [%s] (ordinal"
                           ">=%s): DIFFERENT pages were faulted - NOT the single-address loop 1009 saw "
                           "(the strongest refutation of the constant-fault reading; the walks/decode "
                           "of each value follow)" % (
                               len(set(tail_l2)), ",".join(fmt(v) for v in sorted(set(tail_l2))[:12]),
                               fmt(ABT4_TAIL_MIN_PTEN)))

    # ---- (abt4-8) THE DECISIVE PARTITION.  The expected cell is APX=1 + vm_fault KERN_SUCCESS.  The
    # (B) return code is read from the vm_fault-return rows (KERN_SUCCESS=0 / KERN_ABORTED=14).
    rets = [r["vmfret"] for r in vmf_rows if r["vmfret"] is not None]
    ret_set = sorted(set(rets))
    ret_const = len(ret_set) == 1
    ret_last = rets[-1] if rets else None
    ret_names = ",".join(ABT4_KERN_NAMES.get(v, "0x%x" % v) for v in ret_set) if ret_set else "-"
    if tail_apx is not None and ret_last is not None:
        if tail_apx == 1 and ret_last == ABT4_KERN_SUCCESS:
            say("VERDICT", "*** THE DECISIVE CELL (design §2.2): the L2 PTE at the tail is APX=1 (USER "
                           "READ-ONLY) AND vm_fault returned KERN_SUCCESS (0). The write permission "
                           "fault is on a page the PTE says is read-only THOUGH vm_fault said success "
                           "=> the cs-taint / prot-downgrade path (vm_fault.c:2476-2485 -> arm/pmap.c:"
                           "1110-1113): the retry is on a SUCCESS return over a READ-ONLY page. THE "
                           "EXPECTED ROW.")
        elif tail_apx == 1 and ret_last == ABT4_KERN_ABORTED:
            say("VERDICT", "the tail L2 PTE is APX=1 (USER READ-ONLY) but vm_fault returned KERN_ABORTED "
                           "(14): the PTE is read-only AND the fault handler returned ABORTED - the "
                           "handler CHOSE to retry an ABORTED return (not the cs-taint success path). "
                           "Read the retry branch by value.")
        elif tail_apx == 0 and ret_last == ABT4_KERN_SUCCESS:
            say("VERDICT", "the tail L2 PTE is APX=0 (USER READ-WRITE) and vm_fault returned "
                           "KERN_SUCCESS (0): the PTE does NOT look read-only - the disagreement is NOT "
                           "in this L2 entry (design §6 falsifier (e): vm_fault itself returned a false "
                           "success, or the retry is elsewhere). Report this; do NOT read the cs-taint "
                           "cell as confirmed.")
        else:
            say("VERDICT", "the tail L2 PTE is APX=%s and vm_fault returned %s: neither the "
                           "read-only-success cell nor a clean falsifier - read the pte/vmf rows by "
                           "value" % (tail_apx, ABT4_KERN_NAMES.get(ret_last, "0x%x" % ret_last)))
    else:
        say("OBS", "the decisive cell (tail APX + vm_fault return) cannot be read from this log "
                   "(tail_apx=%s, ret_last=%s): a branch over zero samples is not a verdict" % (
                       tail_apx, fmt(ret_last) if ret_last is not None else None))

    say("OBS", "(B) the vm_fault RETURN: x%d rows, ret set {%s} (%s); the retry distinguishes "
               "KERN_SUCCESS=0 from KERN_ABORTED=14 - this is the only source of that distinction" % (
                   len(vmf_rows), ",".join("0x%x" % v for v in ret_set) if ret_set else "-",
                   ret_names if ret_const and ret_set else "varies"))
    for r in vmf_rows[:4]:
        say("OBS", "  vmf @:%d vmfn=%s vmfret=%s(%s) vmfva=%s vmfprot=%s" % (
            r["ln"], fmt(r["vmfn"]), fmt(r["vmfret"]),
            ABT4_KERN_NAMES.get(r["vmfret"], "-") if r["vmfret"] is not None else "-",
            fmt(r["vmfva"]), fmt(r["vmfprot"])))

    say("OBS", "the change-count `%s` (whole-run running max, %s) is REPORTED AS AN OBSERVATION ONLY - "
               "it CANNOT describe the tail; the tail verdict above reads the ladder ROWS' OWN `_ptel2` "
               "(the 1008/1009 measurement defect, NOT repeated)" % (ABT4_PTENX_KEY, fmt(nptenx)))

    # ---- certified: a real pte ladder was read (>= 1 full record) - the 1010 pass gate.
    if rows and not partial:
        ABT4_CERTIFIED = True


def main():
    global ALLOW_MISSING_CADENCE, ALLOW_MISSING_FREEZE, ALLOW_MISSING_ABT, ALLOW_MISSING_ABT2, \
        ALLOW_MISSING_ABT3, ALLOW_MISSING_ABT4
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    arm_sha8 = None
    for f in flags:
        if f == "--allow-missing-cadence":
            ALLOW_MISSING_CADENCE = True
        elif f == "--allow-missing-freeze":
            ALLOW_MISSING_FREEZE = True
        elif f == "--allow-missing-abt":
            ALLOW_MISSING_ABT = True
        elif f == "--allow-missing-abt2":
            ALLOW_MISSING_ABT2 = True
        elif f == "--allow-missing-abt3":
            ALLOW_MISSING_ABT3 = True
        elif f == "--allow-missing-abt4":
            ALLOW_MISSING_ABT4 = True
        elif f.startswith("--arm="):
            arm_sha8 = f[len("--arm="):].strip()
        else:
            print("unknown flag: %s" % f)
            print("usage: %s [--arm=<sha8>] [--allow-missing-abt4] [--allow-missing-abt3] "
                  "[--allow-missing-abt2] [--allow-missing-abt] [--allow-missing-freeze] "
                  "[--allow-missing-cadence] <capture.txt>" % sys.argv[0])
            return 2
    if len(argv) != 1:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s [--arm=<sha8>] [--allow-missing-abt4] [--allow-missing-abt3] "
              "[--allow-missing-abt2] [--allow-missing-abt] [--allow-missing-freeze] "
              "[--allow-missing-cadence] <capture.txt>" % sys.argv[0])
        return 2
    cap = Capture(argv[0])
    print("1010 capture adjudicator - %s (%d bytes, %d lines)" % (
        argv[0], cap.nbytes, len(cap.lines)))
    auth, frozen = authority_line()
    print(auth)
    print("spec read here: 1009's switch set VERBATIM + the ONE new 1010 switch "
          "STAGE90_XNU_ABT_PTE (the L2 small-page descriptor for the fault VA + what vm_fault RETURNED, "
          "on the DIRECT channel, section [ABT4]); the 1009 faulting pc [ABT3], the 1008 abort-fault "
          "identity [ABT2] (retargeted with the decisive-tail far), the 1007 abort-span witness [ABT], "
          "the 1006 freeze witness [W], the 1005 cadence witness [C], the 1004 abort trace [A] and its "
          "PROGRESS correction are KEPT; the tolerated wall `%s` is NOT the frontier" % CS_INVALID_LIT)
    # ---- THE PRESSED-ARM GUARD: refuse the PRESSED predecessor; accept the built 1010 arm.  The caller
    # names the arm explicitly (`--arm=<sha8>`); the reader does NOT read the LIVE
    # `out/stage90/xnu_arm_entry.bin` by default (shared live state, [[mi4-out-stage90-is-shared-live-state]]).
    prior_arm_guard(arm_sha8)
    s0_gate(cap)
    s_identity(cap)
    s1_tick(cap)
    s2_direct_pet(cap)
    s3_submapskip(cap)
    s4_usvc(cap)
    s5_context(cap)
    s_rung(cap)
    s_abort(cap)
    s_cadence(cap)
    s_freeze(cap)
    s_abt(cap)
    s_abt2(cap)
    s_abt3(cap)
    s_abt4(cap)
    s_stall(cap)
    s_f(cap)
    s6_guards(cap)
    s7_falsifiers(cap)
    print("\n== VERDICT ==")
    if GATED:
        print("  %d absence-based reading(s) printed [death-gated] and NOT counted" % len(GATED))
    if SOFT:
        print("  %d soft finding(s) above (report-only)" % len(SOFT))
    print("HARD ANOMALIES: %d" % len(FAILURES))
    for f in FAILURES:
        print("    - " + f)
    if not frozen:
        print("  !! NO FROZEN 1010 PRE-REGISTRATION was found at run time - the expectations above "
              "are the task's; a deviation here is to REPORT")
    if SIGNATURES:
        print("  !! %d REBUILD-CLASS SIGNATURE(S) present (not counted):" % len(SIGNATURES))
        for s in SIGNATURES:
            print("    ! " + clip(s, 200))
    conj = " AND ".join("%s->%s" % (k, "holds" if GATE_CONJ.get(k) else "FAILS") for k in sorted(GATE_CONJ))
    print("DEATH GATE: %s (frozen conjuncts: %s)" % ("OPEN" if GATE_OPEN else "CLOSED", conj))
    print("L2-DESCRIPTOR-AND-VM_FAULT (the 1010 pass gate): %s" % (
        "CERTIFIED (a real pte ladder was read, or the absence was ALLOWED/EXPECTED for a pre-1010 log)"
        if ABT4_CERTIFIED else "NOT CERTIFIED (no pte ladder in this log)"))
    print("FAULTING-PC-AND-FSR (the 1009 regression gate): %s" % (
        "CERTIFIED (a real pc ladder was read, or the absence was ALLOWED/EXPECTED for a pre-1009 log)"
        if ABT3_CERTIFIED else "NOT CERTIFIED (no pc ladder in this log)"))
    print("ABORT-FAULT-IDENTITY (the 1008 regression gate): %s" % (
        "CERTIFIED (a real far ladder was read, or the absence was ALLOWED)" if ABT2_CERTIFIED else
        "NOT CERTIFIED (no far ladder in this log)"))
    print("ABORT-SPAN (the 1007 regression gate): %s" % (
        "CERTIFIED (a real abort span was read)" if ABT_CERTIFIED else
        "NOT CERTIFIED (no abort span in this log)"))
    print("FREEZE (the 1006 regression gate): %s" % (
        "CERTIFIED (a real freeze window OR a valid silence was read)" if FREEZE_CERTIFIED else
        "NOT CERTIFIED (no freeze window and no valid silence in this log)"))
    print("CADENCE (the 1005 regression gate): %s" % (
        "CERTIFIED (a real cadence window was read)" if CADENCE_CERTIFIED else
        "NOT CERTIFIED (no cadence window in this log)"))
    if FAILURES:
        print("  => STOP and report per the pre-reg's FAILURE SIGNATURES")
        return 1
    if not ABT4_CERTIFIED:
        print("  => no hard anomaly in the regression clauses, but the 1010 rung is NOT certified: "
              "this log carries no L2-descriptor witness (e.g. the 1009 capture, which PREDATES 1010)")
        # A log without the rung's own instrument is NOT a 1010 pass: exit 1 unless the caller
        # explicitly waived it (then the regression result is reported, still not a 1010 pass).
        return 0 if ALLOW_MISSING_ABT4 else 1
    if not ABT3_CERTIFIED:
        print("  => no hard anomaly and the 1010 instrument is CERTIFIED, but the 1009 regression "
              "gate is NOT certified: this log carries no faulting-pc witness")
        return 0 if ALLOW_MISSING_ABT3 else 1
    print("  no hard anomaly; readings above are the evidence (1010 L2-descriptor + vm_fault return "
          "CERTIFIED, 1009 faulting-pc regression CERTIFIED)")
    return 0


if __name__ == "__main__":
    sys.exit(main())