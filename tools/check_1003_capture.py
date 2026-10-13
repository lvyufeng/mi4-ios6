#!/usr/bin/env python3
"""Mechanical adjudicator for the 1003 press capture - by value, and DEATH-AWARE.

1003 IS THE FIX RUNG for the frontier the 1002 press read names.  1002's r3-save fix TOOK BY VALUE
(the USVC hook's save set gained r3 - `{r0,r1,r8,lr}` -> `{r0-r3,r8,lr}` - so `fleh_swi`'s `swi_trap`
dispatch of the `0x80000000` traps kept its live r3) and was SELF-TESTING: the 230x byte-identical

    launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004 ... (Translation fault on page)

on dyld's `_cerror`/TPIDRURO=0 path (right after `IOS7SHREG check_np ret=12`) is GONE.  The frontier
then MOVED to a NEW/OLD wall - the CODE-SIGNING page-validation path, the same wall 999 stopped on:

    CODE SIGNING: cs_invalid_page(0x2f7f6000): p=1[launchd] clearing CS_VALID

1003's ONE change is the STALL INSTRUMENT, a pure MEASUREMENT (switch `STAGE90_XNU_STALL`): a per-tick
sample of the interrupted pid-1 frame, recorded by `entry_note_stall_sample` (src/entry/entry_stubs.c)
at ONE gated call site in `msm8974_handle_interrupt`'s powers-of-two ladder (src/platform/darwin13/
pe_msm8974.c), read in section [S].  It answers the one question the 1002 read left open - whether pid 1
is SPINNING in user space or ASLEEP - and changes no kernel behaviour, so the stall is the EXPECTED
outcome.  1003's switch set is 1002's VERBATIM (`USVC=1` and
`SUBMAP_PMAP_SKIP=1` stay on as regressions - the frontier must not re-litigate the 1001/1002 deaths)
plus the one new switch.

[0] THE DEATH GATE - the toggle every absence-based verdict below is routed through.  It is the
FROZEN three-way conjunction from the 1003 pre-registration (4)-(8): all three of
    (i)   the text channel carries NO panic stanza;
    (ii)  the exec-done trio xnu_live_exec_done_seq / _who / _current AND its five
          xnu_live_exec_done_probe_* keys are present;
    (iii) kv records from AFTER the exec-returned point exist.
The trio is the visually prominent half (and the toggle the task named): with all three of it the tool
prints `DEATH GATE: exec-done trio present => absences count`; otherwise `DEATH GATE: exec-done trio
ABSENT => ...`.  The EFFECTIVE gate is the three-way conjunction, printed with each conjunct's state -
where the two disagree the frozen text wins and the disagreement is REPORTED (a pre-reg deviation),
never papered over.  Gate CLOSED: absence-based readings print `[death-gated]` and are NOT counted
(death readings, not regressions); exit stays 0 on their account.  Presence-based readings (a wrong
value in a record that IS there, a malformed record) keep reporting by value and stay hard - a value
that arrived is evidence whatever the run did next.  The frozen REBUILD signatures are the exception:
failure signature (c) (the log ends with a capped key and no direct key after it) prints under
`[SIGNATURE]` and never counts.

WHAT 1003 CHECKS (the frozen block in `scripts/press_1003.sh` is the authority; `authority_line()`
reads THAT file):
  [R] THE 1002 r3-SAVE FIX AS A REGRESSION.  After `IOS7SHREG check_np ret=12`, the count of
      `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the WHOLE capture must STILL
      be 0 (1002 confirmed it).  A recurrence here is a REGRESSION of the 1002 fix (the USVC hook was
      reverted or the assembly layer not rebuilt) - REBUILD, not a 1003 reading.
  [S] THE STALL SAMPLE - THE 1003 RUNG ITSELF (the reason this rung exists).  The per-tick sample of
      the interrupted pid-1 frame, published as `xnu_live_stall_n` / _t / _pc / _cpsr / _sp / _lr /
      _nz on the tick's powers-of-two ladder.  Classifies each sample's pc/cpsr as USER vs KERNEL, and
      reports SPINNING (user pc advancing / looping) vs ASLEEP (all-kernel) - a MEASUREMENT by design:
      the stall is the EXPECTED outcome, so [S] FAILS only on ZERO samples (the instrument did not
      reach the image) or on all-`_nz`=0 (a wrong saved-state offset - a MEASUREMENT defect).
  [F] THE 1003 RUNG'S FRONTIER - THE CODE-SIGNING PAGE-VALIDATION WALL, AND WHY IT IS *NOT* THE STALL.
      `cs_invalid_page(0x2f7f6000): p=1[launchd] clearing CS_VALID` is a TOLERATED warning (CS_VALID
      cleared, retval 0 - the fault PROCEEDS), recorded ONCE in the 1002 capture and NOT the frontier
      (the frontier is the pid-1 user-space STALL, measured in [S]).

  [S] AND [F] TOGETHER are the 1003 reading: the boot is EXPECTED to run into pid-1 user mode, print
      `map_slide`, run its SVCs, and stall with the kernel alive ~23 min; the ONE tolerated
      `cs_invalid_page` sits mid-run and is NOT a failure.
  (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr>
      size=<ptr> lr=<ptr>`, BOUNDED (max 8).  EXPECTED >= 1; most likely EXACTLY 2, obj = 999's two
      VMHINT991 objects 0xc0005b48 / 0xc0005ab0.  ABSENCE while the gate holds = the skip did not
      reach the image (rebuild).
  (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = the skip did
      not reach the image, OR another caller path still arrives at the lookup with a non-object
      (adjudicate by lr/obj - a READING).
  (4) THE SURVIVAL GATE - the 1000 adjudicator's lesson, carried verbatim from the 1001/1002 tool:
      absence counts ONLY under the gate.
  (5) THE USER-SVC FRONTIER continues.  KEYS xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr
      / _r0 (first 64 in FULL - USVC_FULL_N 64u - then a powers-of-two ladder).  _pid = 1; _pc a user
      VA (never 0x8000xxxx); _cpsr a USER-mode CPSR.  Read the count by value against 1002's.
  (6) THE PET.  The feed gate is `count >= bark/2` = 14.5 s.  If 1003 lives past ~14.5 s with the read
      door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` (the FIRST feed) MUST
      appear.  ZERO feeds past 14.5 s with the door demonstrably used = the door stopped or the gate
      moved: a READING (SOFT, exactly as the 1001/1002 tool reads it).
  (7) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
      thinning ordinal stays 64.  A `xnu_live_capped=0x6000` tombstone is NOT a regression (the direct
      channel carries the reading past it).  The tick summaries continue.
  (8) THE FROZEN REGRESSION GUARDS - re-verified ONLY under the gate: the geometry line exactly once,
      `IOS7LAB cache mapping` x4, the shared region x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`,
      `IOS7SHREG map_slide ret=0`.

METHOD.  Every reader (the `newline=""` file read, the de-doubling panic reader, the KV regex, the
`@:line` numbering) is the 1001/1002 tool's, key for key - none of it is re-derived.  This tool is
`tools/check_1002_capture.py` with the ONE new clause [F] (the code-signing frontier) and the 1003
authority path; the clauses 1002 got right are mirrored verbatim.

Usage:  tools/check_1003_capture.py <capture.txt>

By value, on the project's own captures:
  A. the 1002 capture (the frontier this rung answers - the code-signing wall):
     out/stage90/captures/recovered-armed-d13-b04976fe-*-last_kmsg.txt
     The 1002 r3-save fix held (0x0 aborts); the frontier is `cs_invalid_page(0x2f7f6000) p=1[launchd]`.
     [F] must name it by value; EXPECTED EXIT 0 unless a later hard anomaly is present.
  B. the 1001 capture (the PRE-1002 image - the [R] FALSIFIER's control):
     records/1001-press-read.txt's capture.  It carries the 230x abort and the pre-1002 image cannot
     have the 1003 change: [R] and [F] must DISCRIMINATE it and NOT mistake this pre-1002 image for a
     1003 pass.  EXPECTED EXIT 1.

Exit: 0 = no hard anomaly; 1 = at least one hard anomaly (any `[FAIL]` line); 2 = usage.  A hard
anomaly means STOP-and-report, never "rerun until green" - and a `[death-gated]` line is a death
reading that must be adjudicated against the timeline before it is called anything.
"""

import os
import re
import sys

KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+|hw_watchdog_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

# The 1001 probe, VERBATIM from the rung's spec (1002 keeps it - the SUBMAP skip is unchanged). The
# reader never assumes a `%p` width: any `0x` + hex digits, because the string is the contract and the
# width is the console's.
SUBMAPSKIP_LIT = "SUBMAPSKIP:"
SUBMAPSKIP_RE = re.compile(
    r'SUBMAPSKIP:\s*obj=(0x[0-9a-fA-F]+)\s+start=(0x[0-9a-fA-F]+)\s+end=(0x[0-9a-fA-F]+)'
    r'\s+size=(0x[0-9a-fA-F]+)\s+lr=(0x[0-9a-fA-F]+)')
# The panic path's tank flush writes each character TWICE (`ppaanniicc(...)`, 1000 record, reader
# note). A SUBMAPSKIP that arrived through THAT path would be invisible to the literal reader - so the
# doubled form is counted too and reported, rather than read as "the probe never printed".
SUBMAPSKIP_DOUBLED = "SSUUBBMMAAPPSSKKIIPP"
PROBE_MAX_HITS = 8           # the probe's own bound ("max 8 hits, then silent")

# [R] THE RUNG'S OWN SIGNATURE - the 1001 frontier, VERBATIM from the frozen pre-reg (1) and 1001's
# capture @:4104.  The ANSI colour codes the console wraps around it do not break the literal, so a
# plain substring count is exact (measured: 230 on the 1001 capture, 0 on the 999 capture).
ABORT_LIT = "usermode data abort, EXC_BAD_ACCESS at 0x00000004"
CHECK_NP_LIT = "IOS7SHREG check_np ret=12"     # the anchor [R] hangs the first-line reading on
MAP_SLIDE_LIT = "IOS7SHREG map_slide"          # the line 999 prints NEXT (1002's expected reading)

# [F] THE 1003 RUNG'S FRONTIER - the CODE-SIGNING page-validation wall.  The 1002 press read names it
# by value: `CODE SIGNING: cs_invalid_page(0x2f7f6000): p=1[launchd] clearing CS_VALID` - the SAME
# wall 999 stopped on.  BUT 1003's reading (records/1002-press-read.txt, section [6], and scripts/
# press_1003.sh (F)) is that this wall is TOLERATED (CS_VALID cleared, retval 0 - the fault PROCEEDS)
# and is NOT the frontier: the frontier is the pid-1 user-space STALL.  It appears EXACTLY ONCE in the
# 1002 capture (measured: 1, @:4114), mid-run, and the boot SURVIVES past it.  [F] reads it by value
# and, coherent with (F), treats the ONE tolerated occurrence as the expected reading - never a
# failure.  A recurrence of MANY (the wall ceasing to tolerate) would be the real F (a reading).
CS_INVALID_LIT = "cs_invalid_page"
CS_PAGE_1002 = "0x2f7f6000"                # the page the 1002 frontier named (tolerated, once)
CS_CLEAR_LIT = "clearing CS_VALID"
CS_PID_1002 = "p=1[launchd]"               # the pid tag (launchd) - a single tolerated occurrence
CS_TOLERATED = 1                           # the 1002 capture measured cs_invalid_page EXACTLY ONCE

# [S] THE STALL SAMPLE - the 1003 rung itself.  The per-tick sample of the interrupted pid-1 frame,
# published on the tick's powers-of-two ladder (the SAME cadence as `xnu_live_tick_irq`) by
# `entry_note_stall_sample`.  Seven keys, one per field.  The classification is by cpsr's mode bits
# (0x10 = USR, else kernel), cross-checked against the pc's half.  USER_MODE_CPSR is the USR-mode/T
# value the 1002 SVC tail carried (0x60000010); a kernel sample carries SVC mode (>= 0x80000000).
STALL_KEYS = ("n", "t", "pc", "cpsr", "sp", "lr", "nz")
USER_MODE_CPSR = 0x60000010                 # pid 1's user cpsr (the 1002 SVC tail's value)
STALL_1002_PC = 0x2be97328                  # the pc the three 1002 mach_absolute_time SVCs came from
DYLD_BASE = 0x2be00000                      # dyld's own image (below the shared cache)
DYLD_END = 0x2be58000
SHARED_CACHE_BASE = 0x2c000000              # the dyld shared-cache window base
SHARED_CACHE_SLIDE = 0x37f6000              # ... + its slide (the 1002/1003 mapping)
SS_WORDS = 19                               # STAGE90_SS_SIZE/4 on D13 (the frame the sampler walks)

# 999's VMHINT991 objects, by value (999 capture @:4096-97 of the 1001 record; 1000 record, "The death,
# by value"): two FREED vm_map-zone elements (w04=0xbaddecaf = ZALLOC's ZP_NOT_POISONED) that the
# unguarded call was handed as vm_objects.  A 1002 SUBMAPSKIP whose `obj` is one of these NAMES a
# caller by measurement.
SUB_OBJS = ((0xc0005b48, "999's VMHINT991 object #19"),
            (0xc0005ab0, "999's VMHINT991 object #18"))

# The death gate's toggle (all three; the trio is direct/cap-exempt, so it survives saturation).
TRIO = ("xnu_live_exec_done_seq", "xnu_live_exec_done_who", "xnu_live_exec_done_current")
# The exec ENTRY quartet: written where the exec STARTS (present even in the 1000 death capture) -
# the presence-based half of the exec reading, reported by value and never used as a survival toggle.
ENTRY_QUARTET = ("xnu_live_exec_seq", "xnu_live_exec_caller", "xnu_live_exec_who", "xnu_live_exec_proc")
# The trio's own probe keys (999 @:28718-22): present iff the exec-returned path ran to its end.
DONE_PROBE = ("xnu_live_exec_done_probe_seq", "xnu_live_exec_done_probe_sig",
              "xnu_live_exec_done_probe_size", "xnu_live_exec_done_probe_records",
              "xnu_live_exec_done_probe_state")

# The four instrument families 1000 introduced and 1001/1002 KEEP (the census discriminates a 1000/1001/
# 1002 arm from its baselines). SUBMAPSKIP is counted separately in [3]: it is the 1001 rung, kept on.
NEW_FAMILIES = (
    ("xnu_live_tick_anom_", "(1) the late-delivery anomalies"),
    ("xnu_live_tick_sum_",  "(1) the tick aggregate summaries"),
    ("xnu_live_wdt_pet_",   "(2) the pet feed records (direct)"),
    ("xnu_live_usvc_",      "(4) the user-SVC frontier"),
)

LADDER_LITERALS = (
    ("IOS7SHREG map_slide ret=", "438's own exit - must be present and 0 while the boot survives (999's success)"),
    ("IOS7SHREG map_file kr=",   "the G1 reading (must stay kr=0)"),
    (CHECK_NP_LIT,               "the pre-438 constant (must stay; exactly once)"),
    ("IOS7LAB cache mapping",    "the wire's mapping lines (exactly 4; printed inside the exec)"),
    ("IOS7LAB shared region",    "the geometry line (exactly once; 0 = the define did not reach)"),
    ("VMHINT991",                "the 991 guard - 1001/1002 FLIP it: the submap call is skipped, so 0x while the boot survives"),
    ("vm_allocate64",            "absent = EXPECTED (997: the failure path's buffer alloc)"),
    (SUBMAPSKIP_LIT,             "the 1001 skip-branch probe, kept in 1002 (>= 1 while the boot survives; <= 8 by construction)"),
    (CS_INVALID_LIT,             "the 1003 frontier's wall (cs_invalid_page); read by value in [F] - 1003's fix aims here"),
)

# The watchdog net is 999/1000's, unchanged by 1001 or 1002 (the fixes touch vm_map.c and traps_lo.s).
FIXED_WDT = {
    "hw_watchdog_timeout_s": 0x0000001D,          # 29 s
    "hw_watchdog_bite_ticks_written": 0x000FFFA0, # bark 0xE7FA9 + the same extra 0x17FF7
}
BARK_1003 = 0xE7FA9          # xnu_live_wdt_bark, frozen (25 s was 0xC7FB5)
WDT_HZ = 32765               # the record's own constant: 950,185 ticks = 29.0 s (1000 record S[2])
PERIOD_FALLBACK = 19200      # tick_hz 19,200,000 / MSM8974_TICK_HZ_DIV 1000
TVAL_SLACK = 512             # ticks the entry's two reads may sit apart
FULL_N = 64                  # USVC_FULL_N - the full-field window
USVC_1001_N = 6              # 1001's six SVCs - the comparison, not a required count (pre-reg (5))

FAILURES = []   # hard anomalies that COUNT (value-based, or absence-based with the gate OPEN)
GATED = []      # absence-based readings the closed gate suppressed (printed, never counted)
SOFT = []       # report-only findings (never change the exit code)
SIGNATURES = []  # frozen REBUILD signatures: absences that ARE the death signature (printed, not counted)
GATE_OPEN = False      # the EFFECTIVE gate (the frozen three-way conjunction, unless no frozen text)
TRIO_OPEN = False      # the task's named toggle: all three of the exec-done trio present
GATE_CONJ = {}         # (i) no panic, (ii) trio + probes, (iii) records after the exec-returned point


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
    """A hard anomaly. `absence_based=True` routes it through the death gate: with the gate CLOSED
    the reading is printed as a death reading and NEVER sets exit 1."""
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
    """A frozen REBUILD signature (pre-reg (8)(c) / failure signature (c)): an absence that IS the
    death signature. Printed prominently and re-stated in the verdict; never counted, because no
    capture-side marker separates a 1003 image from a pre-1003 baseline."""
    SIGNATURES.append(msg)
    print("  [SIGNATURE] " + msg)


def say(tag, msg=""):
    print(("  [%s] " % tag) + msg if msg else ("  [%s]" % tag))


class Capture:
    def __init__(self, path):
        # `newline=""` + `split("\n")`: NO universal-newline translation. The captures carry lone CR
        # bytes; a text-mode read folds each into a line break and the `@:line` numbers come out off
        # the project's grep/wc numbering after the first one. Line numbers are the reader's property
        # (m997); this reader is the 1001 tool's, unchanged.
        with open(path, "r", errors="replace", newline="") as fh:
            self.text = fh.read()
        self.nbytes = len(self.text)
        self.lines = self.text.split("\n")
        if self.lines and self.lines[-1] == "":
            self.lines.pop()  # a trailing newline is not a line
        self.toks = []  # (line_no, key, raw, value_int|None) in stream order
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
        """Values of (a, b) walked in stream order - for 'b at the last preceding a' predicates."""
        out, last_a = [], None
        for _, key, _raw, val in self.toks:
            if key == key_a:
                last_a = val
            elif key == key_b:
                out.append((val, last_a))
        return out

    def groups_of(self, nkey, prefix, fields):
        """One record = the nkey line + the field lines that follow it, in stream order."""
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
        """(line, raw) of the panic stanza's own lines, READ BY VALUE, de-doubling first: the panic
        path's tank flush writes every character twice (`ppaanniicc(...)`, 1000 record reader note),
        so the doubled form must be searched too or `panic(` is invisible in exactly the capture that
        has one. The keyed forms are matched too so a one-character-per-column flush is not missed."""
        out = []
        for ln, line in enumerate(self.lines, 1):
            de = line[::2]                      # the character-doubled form, de-doubled
            if ("panic(" in line or "panic(" in de or "Debugger called" in line
                    or "Debugger called" in de or "Waiting for remote debugger" in line
                    or "Waiting for remote debugger" in de):
                out.append((ln, clip(line.strip(), 120)))
        return out

    def abort_hits(self):
        """The rung's own signature lines, by value: `usermode data abort ... at 0x00000004`. The
        console wraps the line in ANSI colour codes but the literal is contiguous, so a substring
        count is exact (measured: 230 on the 1001 capture, 0 on 999/1000)."""
        return [(ln, clip(line.strip(), 160)) for ln, line in enumerate(self.lines, 1)
                if ABORT_LIT in line]

    def cs_invalid_hits(self):
        """[F] THE 1003 FRONTIER, by value: the `cs_invalid_page` lines (the code-signing page-
        validation wall).  De-doubled too, since the panic path's tank flush writes every character
        twice - though cs_invalid_page is normally printed through the ordinary console.  Returns
        (line, raw) pairs in stream order."""
        out = []
        for ln, line in enumerate(self.lines, 1):
            de = line[::2]
            if CS_INVALID_LIT in line or CS_INVALID_LIT in de:
                out.append((ln, clip(line.strip(), 160)))
        return out

    def wdt_samples(self):
        """The watchdog standings, one per published sample: `entry_wdt_pet` writes
        `_sts` -> `_countdown` -> `_pets` in that order (entry_trace.c:2238-40) on powers of two of
        the pass count (:2237), so a sample is that triple. `count` is the caller's `wdt_count()` -
        the rising count the feed gate tests - and `_pets` is the count BEFORE this call's feed."""
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
        """[S] THE STALL SAMPLE, one per published tick.  `entry_note_stall_sample` writes the seven
        keys in one block, `_n` -> `_t` -> `_pc` -> `_cpsr` -> `_sp` -> `_lr` -> `_nz`, in that order
        (entry_stubs.c:2692-98), so a sample is that run of keys.  `_n` opens it (the sample counter);
        the DICT is filled by the following field keys, the first `_n` after a completed sample opens
        the next.  A `_n` never reads a NAME (the reader keeps raw text), so this is a straight walk."""
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


def authority_line():
    """Which 1003 pre-registration this run's expectations can be checked against - by value, at run
    time. The tool must not pretend a frozen text exists (mi4-self-written-record-is-not-a-constraint),
    and it must say which one it found: the frozen block's (4) gate is the THREE-way conjunction this
    tool evaluates."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "scripts", "press_1003.sh")
    try:
        with open(p, "r", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return ("authority: scripts/press_1003.sh is NOT frozen yet (absent at run time) - the "
                "expectations below are the ones handed to the 1003 rung (records/1002-press-read.txt, "
                "section [6]); RE-CHECK them against the pre-registration when it lands", False)
    has = "1003 PRESS PRE-REGISTRATION" in text
    return ("authority: scripts/press_1003.sh (%d bytes, read at run time)%s" % (
        len(text), " - the frozen block IS embedded (its (4) gate is the three-way conjunction this "
        "tool evaluates; its (1) self-test, its (6) pet reading and its (8)(c) signature are followed "
        "verbatim)" if has else " - but the frozen 1003 PRESS PRE-REGISTRATION block is NOT in it: the "
        "expectations below are the task's, and a pre-reg deviation must be REPORTED"), has)


def s0_gate(cap):
    """[0] THE DEATH GATE - the toggle every absence-based verdict below is routed through.

    The task named the trio as the survival toggle; the frozen pre-reg (4) states a three-way
    conjunction (no panic + trio&probes + records after the exec-returned point). Both are evaluated
    and printed; the EFFECTIVE gate is the frozen conjunction, and a disagreement is REPORTED."""
    global GATE_OPEN, TRIO_OPEN, GATE_CONJ
    print("\n== [0] DEATH GATE (the 1000 record's S[5] owed lesson: absence counts only if the boot "
          "reached the print site) ==")
    found = {k: cap.first(k) for k in TRIO}
    dp = {k: cap.first(k) for k in DONE_PROBE}
    for k in TRIO:
        t = found[k]
        say("OBS" if t else "---", "%s %s" % (k, ("= %s @:%d" % (t[2], t[0])) if t else "(absent)"))
    n_present = sum(1 for k in TRIO if found[k] is not None)
    TRIO_OPEN = (n_present == 3)
    if 0 < n_present < 3:
        soft("a PARTIAL exec-done trio (%d/3) - the gate is defined as all three; read the values "
             "above before treating this as a clean death signature" % n_present)
    # ---- (i) the text channel carries NO panic stanza (the doubled form included).
    ph = cap.panic_hits()
    if ph:
        say("OBS", "panic stanza present x%d (first @:%d: %s)" % (len(ph), ph[0][0], ph[0][1]))
    else:
        say("OBS", "no panic stanza in the text channel")
    # ---- (ii) the trio AND its five probe keys.
    have_probe = sum(1 for k in DONE_PROBE if dp[k] is not None)
    if any(dp.values()):
        say("OBS", "the trio's five probe keys: %d/%d present%s" % (
            have_probe, len(DONE_PROBE), "" if have_probe == len(DONE_PROBE) else
            " (missing: %s)" % ",".join(k.replace("xnu_live_exec_done_probe_", "") for k in DONE_PROBE
                                        if dp[k] is None)))
    else:
        say("OBS", "the trio's five probe keys: 0/%d present" % len(DONE_PROBE))
    # ---- (iii) kv records from AFTER the exec-returned point (the direct channel's own next records).
    exec_done_last = max((t[0] for t in [found[k] for k in TRIO] + [dp[k] for k in DONE_PROBE] if t),
                         default=None)
    after_exec = [t for t in cap.toks if exec_done_last is not None and t[0] > exec_done_last]
    if exec_done_last is not None:
        say("OBS" if after_exec else "---", "kv records after the exec-returned point (@:%d): x%d%s" % (
            exec_done_last, len(after_exec),
            "  first: %s @:%d" % (after_exec[0][1], after_exec[0][0]) if after_exec else " (none)"))
    else:
        say("---", "kv records after the exec-returned point: cannot be read (no trio/probe key at all)")
    # The exec ENTRY quartet: presence-based, reported by value. Its presence with the trio absent IS
    # the death signature the 1001 rung removed.
    q = {k: cap.first(k) for k in ENTRY_QUARTET}
    if any(q.values()):
        say("OBS", "exec ENTRY quartet: %s" % "  ".join(
            "%s=%s @:%s" % (k.replace("xnu_live_exec_", ""), t[2], t[0]) if t else "%s=(absent)" %
            k.replace("xnu_live_exec_", "") for k, t in q.items()))
        if n_present == 0:
            say("READING", "quartet present, trio absent => the exec STARTED and never RETURNED: this "
                           "is the 1000 death signature, and 1001's fix is aimed exactly here")
    # ---- THE EFFECTIVE GATE: the frozen three-way conjunction.
    GATE_CONJ = {"i_no_panic": not ph, "ii_trio_and_probes": (n_present == 3 and have_probe == len(DONE_PROBE)),
                 "iii_records_after_exec": bool(after_exec)}
    GATE_OPEN = all(GATE_CONJ.values())
    say("OBS", "gate conjuncts (frozen pre-reg (4)): (i) no panic = %s; (ii) trio + 5 probes = %s; "
               "(iii) records after the exec-returned point = %s" % (
                   GATE_CONJ["i_no_panic"], GATE_CONJ["ii_trio_and_probes"],
                   GATE_CONJ["iii_records_after_exec"]))
    print("  " + "-" * 96)
    # The CLAUSE the task named is printed verbatim either way (its wording is the task's).
    if TRIO_OPEN:
        print("DEATH GATE: exec-done trio present => absences count")
    else:
        print("DEATH GATE: exec-done trio ABSENT => the run died before the exec-returned point; "
              "absence-based verdicts are NOT counted (death readings, not regressions)")
    if GATE_OPEN != TRIO_OPEN:
        soft("GATE DISAGREEMENT: the trio toggle reads %s and the frozen three-way gate reads %s. The "
             "EFFECTIVE gate is the frozen conjunction; the tool's default where the difference is the "
             "trio-only case is the task's. Adjudicate both - an instrument that did not reach the "
             "image looks exactly like a death" % ("OPEN" if TRIO_OPEN else "CLOSED",
                                                    "OPEN" if GATE_OPEN else "CLOSED"))
    print("DEATH GATE: EFFECTIVE = %s (frozen conjunction: %s)" % (
        "OPEN" if GATE_OPEN else "CLOSED",
        " AND ".join("%s->%s" % (k, "holds" if GATE_CONJ[k] else "FAILS") for k in sorted(GATE_CONJ))))
    print("  " + "-" * 96)
    # If post-exec evidence exists while the exec trio is missing, say so LOUDLY (the gate is not
    # flipped - the lead adjudicates).
    if not TRIO_OPEN:
        hints = []
        cs = [ln for ln, line in enumerate(cap.lines, 1) if "cs_invalid_page" in line]
        if cs:
            hints.append("cs_invalid_page x%d (first @:%d)" % (len(cs), cs[0]))
        for prefix, what in (("xnu_live_usvc_", "usvc records"), (SUBMAPSKIP_LIT, "SUBMAPSKIP lines"),
                             ("IOS7LAB cache mapping", "the wire's mapping lines"), ("VMHINT991", "the 991 guard")):
            n = cap.lit_count(prefix) if prefix in cap.lit else cap.family_count(prefix)
            if n:
                hints.append("%s x%d" % (what, n))
        if hints:
            soft("GATE CONFLICT: the trio is absent but post-exec evidence IS present (%s) - the "
                 "toggle still needs adjudication: an instrument that did not reach the image looks "
                 "exactly like a death" % "; ".join(hints))


def s_identity(cap):
    print("\n== IDENTITY / the 1000-1001-1002-1003 instrument census ==")
    cap_hdr = cap.all("xnu_live_cap")
    if cap_hdr:
        vals = sorted(set(t[3] for t in cap_hdr))
        say("OBS", "xnu_live_cap header: %s%s" % (", ".join(fmt(v) for v in vals),
            "  (the 999/1000/1001/1002 cap; 0x4000 = a pre-999 image)" if vals == [0x6000] else ""))
        if 0x4000 in vals:
            hard("xnu_live_cap=0x00004000 - a PRE-999 image; not the 1003 arm")
    else:
        say("OBS", "no xnu_live_cap header token (older image or the header itself was lost)")
    census = {}
    for prefix, why in NEW_FAMILIES:
        n = cap.family_count(prefix)
        census[prefix] = n
        first = next((t for t in cap.toks if t[1].startswith(prefix)), None)
        say("OBS" if n else "---", "%s x%d%s" % (prefix, n,
            "  first @:%d" % first[0] if first else "  (%s)" % why))
    if not any(census.values()):
        hard("ZERO tokens of ALL FOUR kept families - the capture carries none of 1000's instruments "
             "- either a pre-1000 image or the edits did not reach the image: REBUILD, do not judge",
             absence_based=True)
    rd = cap.all("xnu_live_storage_rd_ordinal")
    if rd:
        say("OBS", "xnu_live_storage_rd_ordinal: %d records, first %s @:%d, last %s @:%d" % (
            len(rd), fmt(rd[0][3]), rd[0][0], fmt(rd[-1][3]), rd[-1][0]))
    else:
        say("OBS", "xnu_live_storage_rd_ordinal: none (the read door never ran?)")
    hz = cap.first("xnu_live_tick_hz")
    say("OBS", "xnu_live_tick_hz: %s%s" % (fmt(hz[3]) if hz else "ABSENT",
        "  (period recorded = tick_hz/%du = %d ticks)" % (1000, (hz[3] // 1000) if hz else PERIOD_FALLBACK)
        if hz else "  (fallback period %d)" % PERIOD_FALLBACK))


def s1_tick(cap):
    print("\n== [1] TICK - the delivery sampled at its source (frozen A/B: masked vs 2nd writer) ==")
    hz = cap.first("xnu_live_tick_hz")
    period = (hz[3] // 1000) if hz else PERIOD_FALLBACK
    recs = cap.groups_of("xnu_live_tick_anom_n", "xnu_live_tick_anom_", ("d", "tval", "ctl"))
    if not recs:
        say("OBS", "no xnu_live_tick_anom_* records")
    for ln, n, f, order in recs:
        d = f.get("d", (0, None))[1]
        tval = f.get("tval", (0, None))[1]
        ctl = f.get("ctl", (0, None))[1]
        if d is None or tval is None or ctl is None:
            hard("anom n=%s @:%d is missing fields (%s) - the record's shape broke" % (
                fmt(n), ln, ",".join(order)), absence_based=True)
            continue
        resid = min(d % period, period - (d % period)) if period else 0
        pred = (period - d) & 0xFFFFFFFF
        delta = min((tval - pred) & 0xFFFFFFFF, (pred - tval) & 0xFFFFFFFF)
        ctl_sig = bool(ctl & 0x2) or not (ctl & 0x1)
        say("OBS", "anom n=%s @:%d d=%s (mod period %d: residue %d) tval=%s pred=%s delta=%d ctl=%s%s" % (
            fmt(n), ln, fmt(d), period, resid, fmt(tval), fmt(pred), delta, fmt(ctl),
            "  CTL SIGNATURE (IMASK set / ENABLE clear)" if ctl_sig else ""))
        if delta <= TVAL_SLACK:
            say("READING", "  -> _tval consistent with _d: MASKED DELIVERY (the countdown ran "
                           "through the whole gap)")
        else:
            say("READING", "  -> _tval a value the gap could not have produced: a SECOND WRITER "
                           "armed the deadline")
        if ctl_sig:
            say("READING", "  -> _ctl carries the second writer's signature (bit1 IMASK / bit0 clear)")
    if recs:
        ns = [r[1] for r in recs]
        if ns != list(range(1, len(ns) + 1)):
            hard("anom _n not 1..N ascending (got %s) - the ordinal-before-write rule broke" %
                 [fmt(v) for v in ns], absence_based=True)
        if len(recs) > 4:
            hard("anom records %d > 4 (the first-four publish bound violated)" % len(recs))
    sums = cap.groups_of("xnu_live_tick_sum_count", "xnu_live_tick_sum_", ("anom", "maxd"))
    if not sums:
        say("OBS", "no tick summaries (xnu_live_tick_sum_*) at all")
    for ln, cnt, f, _o in sums:
        say("OBS", "summary @:%d count=%s anom=%s maxd=%s" % (ln, fmt(cnt),
            fmt(f.get("anom", (0, None))[1]), fmt(f.get("maxd", (0, None))[1])))
    tq = [t[3] for t in cap.all("xnu_live_tick_irq")]
    if tq:
        say("OBS", "tick_irq ladder: %s" % " ".join(fmt(v) for v in tq))
    if sums:
        last_cnt = sums[-1][1]
        last_anom = sums[-1][2].get("anom", (0, None))[1]
        cnts = [g[1] for g in sums if g[1] is not None]
        climbing = len(cnts) >= 2 and cnts[-1] > cnts[0]
        stopped = (last_cnt == 122 and (last_anom or 0) == 0 and
                   not any(v is not None and v > 0x40 for v in tq))
        if last_cnt is not None and last_cnt > 122:
            say("READING", "sum_count=%d > 122 - the tick RESUMED after 999's collapse" % last_cnt)
        elif stopped:
            say("READING", "sum_count still 122 with 0 anomaly events and no later ladder value - "
                           "deliveries stopped entirely (a different finding than the collapse)")
        elif climbing and (last_anom or 0) == 0:
            say("READING", "absence of anomaly records WITH sum_count climbing - the healthy tick "
                           "(the negative cell this instrument is built to make readable)")
        else:
            say("READING", "sum_count %s with anom %s - neither the resumed nor the still-122 shape; "
                           "report the pair verbatim" % (fmt(last_cnt), fmt(last_anom)))


def pet_evidence(cap):
    """The feed gate's evidence, BY VALUE, with the reading it is and is not."""
    bark = cap.first("xnu_live_wdt_bark")
    bark_v = bark[3] if bark else BARK_1003
    half = bark_v // 2 if bark_v else 0
    samples = cap.wdt_samples()
    mx = max((s for s in samples if s["countdown"] is not None),
             key=lambda s: s["countdown"], default=None)
    hz = cap.first("xnu_live_tick_hz")
    tick_hz = hz[3] if hz else None
    sums = cap.groups_of("xnu_live_tick_sum_count", "xnu_live_tick_sum_", ("anom", "maxd"))
    maxds = [g[2]["maxd"][1] for g in sums if "maxd" in g[2] and g[2]["maxd"][1] is not None]
    maxd = max(maxds) if maxds else None
    return {"bark": bark_v, "bark_src": ("xnu_live_wdt_bark @:%d" % bark[0]) if bark else
            "the frozen constant %s (the key is ABSENT)" % fmt(BARK_1003),
            "half": half, "samples": samples, "mx": mx, "tick_hz": tick_hz, "maxd": maxd,
            "maxd_s": secs(maxd, tick_hz), "half_s": secs(half, WDT_HZ)}


def print_pet_evidence(ev, cap):
    say("OBS", "pet feed evidence (the reading this section's verdict rests on):")
    say("OBS", "  bark = %s from %s => bark/2 = %s (%s)" % (
        fmt(ev["bark"]), ev["bark_src"], fmt(ev["half"]), dur(ev["half_s"], WDT_HZ)))
    if ev["samples"]:
        for s in ev["samples"]:
            past = (s["countdown"] is not None and ev["half"] and s["countdown"] >= ev["half"])
            say("OBS", "  sample @:%d sts=%s countdown=%s (%s) pets=%s%s" % (
                s["ln"], fmt(s["sts"][1] if s["sts"] else None), fmt(s["countdown"]),
                dur(secs(s["countdown"], WDT_HZ), WDT_HZ), fmt(s["pets"]),
                "   <== AT/PAST THE FEED GATE" if past else ""))
    else:
        say("OBS", "  no xnu_live_wdt_countdown samples published at all")
    if ev["mx"] is not None and ev["half"]:
        say("OBS", "  largest sample %s vs gate %s => %s" % (
            fmt(ev["mx"]["countdown"]), fmt(ev["half"]),
            "PAST the gate (the feed branch was reachable)" if ev["mx"]["countdown"] >= ev["half"]
            else "below the gate"))
    if ev["maxd"] is not None:
        say("OBS", "  largest tick_sum_maxd = %s (a masked window; %s) - a LOWER BOUND on running "
                   "time, %s the %s gate" % (fmt(ev["maxd"]), dur(ev["maxd_s"], ev["tick_hz"]),
                   "past" if (ev["maxd_s"] or 0) > (ev["half_s"] or 1 << 30) else "below",
                   dur(ev["half_s"], WDT_HZ)))
    else:
        say("OBS", "  no tick_sum_maxd (no summaries) - no second evidence of running time")


def s2_direct_pet(cap):
    print("\n== [2] THE DIRECT CHANNEL + pet feed (zero feeds is read against the GATE, bark/2) ==")
    cells = cap.all("xnu_live_capped")
    if not cells:
        say("PASS", "no xnu_live_capped key at all - the whole boot fit in the counted channel")
    else:
        for ln, _k, _r, v in cells:
            say("OBS", "xnu_live_capped=%s @:%d" % (fmt(v), ln))
        if any(t[3] == 0x4000 for t in cells):
            hard("capped==0x00004000 - the cap did NOT move (pre-999 image; STOP)")
        last_cap_ln = cells[-1][0]
        after = [t for t in cap.toks if t[0] > last_cap_ln]
        if after:
            say("READING", "capped tombstone @:%d and %d kv record(s) AFTER it (first: %s @:%d) - "
                           "those are direct by construction: the rung works as designed" % (
                               last_cap_ln, len(after), after[0][1], after[0][0]))
        else:
            hard("capped tombstone @:%d followed by NOTHING (no direct key of any kind) - the channel "
                 "edits did not reach the image: REBUILD" % last_cap_ln, absence_based=True)
        kv_last = max(t[0] for t in cap.toks) if cap.toks else None
        if kv_last is not None and last_cap_ln == kv_last:
            signature("failure signature (c): the LOG ENDS with a capped key (xnu_live_capped=%s @:%d) "
                      "and no kv record after it - the direct channel edit did not reach the image: "
                      "REBUILD" % (fmt(cells[-1][3]), last_cap_ln))
    recs = cap.groups_of("xnu_live_wdt_pet_n", "xnu_live_wdt_pet_", ("calls", "countdown"))
    ev = pet_evidence(cap)
    print_pet_evidence(ev, cap)
    if not recs:
        say("OBS", "no xnu_live_wdt_pet_* records - no pet fired")
    ns = [r[1] for r in recs]
    for ln, n, f, _o in recs:
        say("OBS", "pet n=%s @:%d calls=%s countdown=%s" % (fmt(n), ln,
            fmt(f.get("calls", (0, None))[1]), fmt(f.get("countdown", (0, None))[1])))
    for n in ns:
        if n is not None and n > 4 and not is_pow2(n):
            hard("pet _n=%s is neither <= 4 nor a power of two - the publish rule broke" % fmt(n))
    if ns and all(v is not None for v in ns) and ns != sorted(ns):
        hard("pet _n not ascending: %s" % [fmt(v) for v in ns])
    if recs and recs[0][2].get("countdown"):
        say("OBS", "first feed's countdown %s vs bark/2 %s - the threshold-gate reading (>= is the "
                   "gate; the frozen pair is one value, two readers)" % (
                       fmt(recs[0][2]["countdown"][1]), fmt(ev["half"])))
    pairs = cap.ordered_pairs("xnu_live_storage_rd_ordinal", "xnu_live_wdt_pet_calls")
    if not pairs:
        say("OBS", "xnu_live_wdt_pet_calls: none")
    else:
        say("OBS", "pet _calls vs the FROZEN sentence (power-of-two OR <= 4, AND <= last preceding "
                   "xnu_live_storage_rd_ordinal):")
        ok = 0
        for v, last_rd in pairs:
            p = is_pow2(v) or (v is not None and v <= 4)
            l = v is not None and last_rd is not None and v <= last_rd
            ok += (p and l)
            say("OBS", "  calls=%s  (pow2-or-<=4)=%s  <=last-rd(%s)=%s" % (
                fmt(v), "yes" if p else "no", fmt(last_rd), "yes" if l else "no"))
        say("OBS", "tally: both %d/%d - report any miss as a pre-reg deviation at adjudication" % (
            ok, len(pairs)))
    # ---- THE VERDICT: zero feeds read against the feed gate, not against the door.
    rd = cap.all("xnu_live_storage_rd_ordinal")
    climbing = bool(rd) and (rd[-1][3] or 0) > (rd[0][3] or 0)
    if not recs:
        door = "the storage door was demonstrably used (rd_ordinal %s -> %s)" % (
            fmt(rd[0][3]), fmt(rd[-1][3])) if climbing else "the storage door's ordinals did not climb"
        if ev["mx"] is not None and ev["half"] and ev["mx"]["countdown"] >= ev["half"]:
            hard("a published WDT sample shows the count PAST the feed gate (countdown %s >= bark/2 %s "
                 "@:%d) yet ZERO xnu_live_wdt_pet_* records exist - the same call's feed branch had to "
                 "fire: read the feed mechanism, do not reinterpret (%s)" % (
                     fmt(ev["mx"]["countdown"]), fmt(ev["half"]), ev["mx"]["ln"], door),
                 absence_based=True)
        elif (ev["maxd_s"] or 0) > (ev["half_s"] or 1 << 30):
            soft("the boot demonstrably ran past the feed moment (largest masked window %s > bark/2 "
                 "%s) with ZERO xnu_live_wdt_pet_* records (%s). The frozen pre-reg (6) calls this "
                 "'the door stopped or the gate moved: a READING, not an assumed defect', so it is "
                 "reported SOFT with the numbers. To make it a clean death-vs-door reading, look for a "
                 "PUBLISHED xnu_live_wdt_countdown sample at/past bark/2 (none above); the sample "
                 "publish gate is pow2(calls), so a sample below the threshold cannot prove the gate "
                 "was never reachable." % (
                     dur(ev["maxd_s"], ev["tick_hz"]), dur(ev["half_s"], WDT_HZ), door))
        else:
            soft("zero pet-feed records - SOFT, by the evidence printed above: no published sample and "
                 "no masked window ever showed the count past bark/2 (%s). This is the 1000 record "
                 "S[2] reading ('the first feedable moment is 4.2 s after the death'), NOT the "
                 "pre-reg's (6) 'the door stopped or the gate moved'. It stays soft because the sample "
                 "publish gate is pow2(calls): a sample below the threshold cannot prove the gate was "
                 "never reachable, only that no sample saw it. The door: %s" % (
                     fmt(ev["half"]), door))
    else:
        say("PASS", "pet feeds published x%d - the feed channel reached the image" % len(recs))
    # the summaries must appear at BOTH claimed moments
    trio = [cap.first(k) for k in TRIO]
    sums = cap.groups_of("xnu_live_tick_sum_count", "xnu_live_tick_sum_", ("anom", "maxd"))
    sum_lines = [g[0] for g in sums]
    if all(t is None for t in trio):
        say("---", "exec-done trio ABSENT - the exec-moment summary is judged by the DEATH GATE, not "
                   "here (see [0]); with the gate closed a missing summary is a death reading")
    else:
        dpl = [cap.first(k) for k in DONE_PROBE]
        exec_ln = max(t[0] for t in trio + [d for d in dpl if d])
        at_exec = [l for l in sum_lines if l > exec_ln]
        say("OBS" if at_exec else "---", "summaries after the exec-returned point (trio + probe keys, "
            "last @:%d): %s" % (exec_ln, at_exec if at_exec else "NONE"))
        if not at_exec and sums:
            hard("tick summaries never appear after the exec-done trio - the exec-moment call did "
                 "not reach the image: REBUILD", absence_based=True)
    if recs:
        pet_ln = recs[0][0]
        at_pet = [l for l in sum_lines if l >= pet_ln]
        say("OBS" if at_pet else "---", "summaries at/after the first pet publish (@:%d): %s" % (
            pet_ln, at_pet if at_pet else "NONE"))


def s3_submapskip(cap):
    """[3] THE 1001 RUNG'S OWN PROBE - kept on in 1002 - the skip branch's bounded kprintf, by value."""
    print("\n== [3] THE SUBMAP SKIP (the 1001 rung, kept on: the probe that names the callers by "
          "measurement) ==")
    parsed, unparsed, doubled = [], [], 0
    for ln, line in enumerate(cap.lines, 1):
        if SUBMAPSKIP_DOUBLED in line:
            doubled += 1
        if SUBMAPSKIP_LIT in line:
            m = SUBMAPSKIP_RE.search(line)
            if m:
                parsed.append((ln, [int(g, 16) for g in m.groups()], clip(line.strip(), 160)))
            else:
                unparsed.append((ln, clip(line.strip(), 160)))
    total = len(parsed) + len(unparsed)
    if doubled:
        soft("the SUBMAPSKIP text appears CHARACTER-DOUBLED x%d (the panic path's tank flush, 1000 "
             "record reader note) - the literal reader cannot see the probe there; de-double by value "
             "before concluding anything about the skip" % doubled)
    say("OBS" if total else "---", "SUBMAPSKIP lines: x%d (parsed x%d, unparsed x%d)%s" % (
        total, len(parsed), len(unparsed),
        "  first @:%d" % (parsed or unparsed)[0][0] if total else "  (the probe never printed)"))
    for ln, tup, raw in parsed:
        obj, start, end, size, lr = tup
        consistent = "yes" if size == (end - start) else "NO (end-start = %s)" % fmt(end - start)
        say("OBS", "  @:%d obj=%s start=%s end=%s size=%s lr=%s  (size == end-start: %s)" % (
            ln, fmt(obj), fmt(start), fmt(end), fmt(size), fmt(lr), consistent))
        for want, who in SUB_OBJS:
            if obj == want:
                say("MATCH", "    -> obj MATCHES %s (%s): the probe reached the skip branch with the "
                             "SAME freed vm_map-zone element the 991 guard named, at lr=%s" % (
                                 who, fmt(want), fmt(lr)))
        if size != (end - start):
            soft("SUBMAPSKIP @:%d prints size %s but end-start = %s - read the producer before "
                 "interpreting the range" % (ln, fmt(size), fmt(end - start)))
    for ln, raw in unparsed:
        soft("SUBMAPSKIP @:%d does not match the frozen format `SUBMAPSKIP: obj=%%p start=%%p end=%%p "
             "size=%%p lr=%%p`: %s" % (ln, raw))
    if total > PROBE_MAX_HITS:
        soft("SUBMAPSKIP x%d > the probe's bound of %d hits - the bound broke (the producer is "
             "bounded by construction)" % (total, PROBE_MAX_HITS))
    if GATE_OPEN:
        if total == 0:
            hard("no SUBMAPSKIP probe line while the boot survived - the probe/skip did not reach the "
                 "image")
        else:
            say("PASS", "SUBMAPSKIP x%d while the boot survived - the skip branch ran (%s)" % (
                total, "the probe/skip reached the image" if parsed else
                "but no line matched the frozen format - see the [soft] lines above"))
        if parsed and len(parsed) != 2:
            soft("SUBMAPSKIP parsed x%d (the rung's expectation is 'most likely exactly 2', one per "
                 "submap enter) - report the count by value, not against 2" % len(parsed))
    else:
        hard("SUBMAPSKIP x%d while the exec-done trio is ABSENT - the probe/skip's reach is NOT judged "
             "(death reading: the skip branch is entered during the exec, before the trio's print "
             "site)" % total, absence_based=True)
        # (8)(c): a death AT THE SAME CALL. The NAME is the discriminator, and the panic stanza shows
        # it in THIS capture - but the tool cannot know whether a 1002 image produced it, so this is
        # printed as a signature and left to the lead.
        same_call = [(ln, line.strip()) for ln, line in enumerate(cap.lines, 1)
                     if re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line)
                     or re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line[::2])]
        if total == 0 and same_call:
            signature("(8)(c)/failure signature (a): a panic naming the SAME call (hw_lock_lock / "
                      "vm_map_pmap_enter / vm_map_enter) with SUBMAPSKIP ABSENT - if this capture is a "
                      "1003 image the 1001 skip did not reach it: REBUILD, do not re-adjudicate. "
                      "@:%d: %s" % (same_call[0][0], same_call[0][1]))
        elif total == 0:
            signature("SUBMAPSKIP x0 with the exec-done trio absent - the frozen (8)(c) shape is 'a "
                      "death at the same call with SUBMAPSKIP absent'. Whether THIS capture is a 1003 "
                      "image cannot be read from the log, so the reach verdict is the lead's (no "
                      "capture-side marker separates a 1003 image from a pre-1003 baseline)")


def s4_usvc(cap):
    print("\n== [4] THE USER-SVC FRONTIER (last _n = the last syscall the user side managed) ==")
    fields = ("num", "pid", "thr", "pc", "lr", "cpsr", "r0")
    recs = cap.groups_of("xnu_live_usvc_n", "xnu_live_usvc_", fields)
    n_tok = cap.family_count("xnu_live_usvc_")
    if not recs:
        if n_tok:
            soft("xnu_live_usvc_ tokens x%d but no `_n` record - the record shape broke; the reader "
                 "cannot walk the frontier" % n_tok)
        if GATE_OPEN:
            hard("no user-SVC records while the boot survived the exec-return - the frontier did not "
                 "reach user mode (check cs_invalid_page ordering before concluding)")
        else:
            hard("no user-SVC records while the exec-done trio is ABSENT - the frontier is NOT judged "
                 "(death reading: 1000 died in kernel mode, before any user SVC existed)", absence_based=True)
        cs = [ln for ln, line in enumerate(cap.lines, 1) if "cs_invalid_page" in line]
        say("OBS" if cs else "---", "cs_invalid_page: %s" % (
            "x%d, first @:%d  %s" % (len(cs), cs[0], clip(cap.lines[cs[0] - 1].strip(), 110)) if cs
            else "absent"))
        say("OBS", "kept-instrument census (an image built before STAGE90_XNU_USVC=1 carries none of "
                   "these, whatever the frontier did): %s" % ", ".join(
                       "%s x%d" % (p, cap.family_count(p)) for p, _ in NEW_FAMILIES))
        if cs:
            say("CTX", "cs_invalid_page IS present => user mode WAS reached in this boot; if the image "
                       "predates STAGE90_XNU_USVC=1 that alone explains 0 records - the finding is "
                       "then INSTRUMENT-side, not frontier-side.")
        else:
            say("CTX", "no cs_invalid_page either => the capture carries no independent user-mode "
                       "evidence; the reading is 'user mode not reached', and the point the boot "
                       "stopped at is [0]'s business")
        return
    ns = [r[1] for r in recs]
    if ns != sorted(ns) or len(set(ns)) != len(ns):
        hard("usvc _n not strictly ascending: %s" % [fmt(v) for v in ns], absence_based=True)
    full = [r for r in recs if r[1] is not None and r[1] <= FULL_N]
    lad = [r for r in recs if r[1] is not None and r[1] > FULL_N]
    for i, r in enumerate(full):
        if r[1] != i + 1:
            hard("full-window usvc _n skips: expected %d got %s @:%d" % (i + 1, fmt(r[1]), r[0]),
                 absence_based=True)
            break
    for r in lad:
        if not is_pow2(r[1]):
            hard("ladder usvc _n=%s not a power of two @:%d" % (fmt(r[1]), r[0]))
    nums = []
    for ln, n, f, order in recs:
        num = f.get("num", (0, None))[1]
        nums.append(num)
        if n is None:
            hard("usvc record @:%d has an unparsable _n - the reader cannot walk the frontier" % ln,
                 absence_based=True)
            continue
        if n <= FULL_N:
            missing = [k for k in fields if k not in f]
            if missing:
                hard("usvc n=%s @:%d missing full-window fields: %s (the 64-window shape broke)" % (
                    fmt(n), ln, ",".join(missing)), absence_based=True)
                continue
            pid = f["pid"][1]
            cpsr = f["cpsr"][1]
            pc = f["pc"][1]
            if pid != 1:
                hard("usvc n=%s _pid=%s (frozen expectation `_pid` = 1 - launchd) - pre-reg "
                     "deviation: report, do not reinterpret" % (fmt(n), fmt(pid)))
            if cpsr is None or (cpsr & 0x1F) != 0x10:
                hard("usvc n=%s _cpsr=%s is not a USER-mode CPSR (mode bits 0x10; frozen) - a "
                     "kernel CPSR means the offset or the pointer is wrong: REBUILD" % (
                         fmt(n), fmt(cpsr)))
            if pc is None or pc == 0 or (pc >> 16) == 0x8000:
                hard("usvc n=%s _pc=%s - not a user VA (never 0x8000xxxx / 0; frozen) - REBUILD" % (
                    fmt(n), fmt(pc)))
        else:
            if num == 0:
                hard("ladder usvc _n=%s carries _num=0 (frozen: a deviation - SS_R12 points at "
                     "the wrong frame word; re-run the checker, then rebuild)" % fmt(n))
    if nums and all(v == 0 for v in nums):
        hard("(6)(c): ALL usvc _num = 0 - SS_R12 points at the wrong frame word")
    say("PASS", "user-SVC records x%d (n from %s to %s) while the boot survived - the frontier "
                "reached user mode" % (len(recs), fmt(ns[0]), fmt(ns[-1])))
    # (5): the fix should let MORE user SVCs through than 1001's six - READ BY VALUE, not required.
    if len(recs) > USVC_1001_N:
        say("READING", "user-SVC records x%d > 1001's %d - the fix let MORE user SVCs through (dyld no "
                       "longer dies at _cerror): pre-reg (5)'s expectation, met" % (
                           len(recs), USVC_1001_N))
    elif len(recs) == USVC_1001_N:
        soft("user-SVC records x%d == 1001's %d - the fix did NOT widen the frontier; read the last "
             "_n and [R] together before calling the frontier unchanged" % (len(recs), USVC_1001_N))
    else:
        soft("user-SVC records x%d < 1001's %d - FEWER than the baseline; report by value with [R]" % (
            len(recs), USVC_1001_N))
    say("OBS", "the first records, by value (n num pid thr pc lr cpsr r0):")
    for ln, n, f, _o in recs[:3]:
        say("OBS", "  @:%d n=%s num=%s pid=%s thr=%s pc=%s lr=%s cpsr=%s r0=%s" % (
            ln, fmt(n), fmt(f.get("num", (0, None))[1]), fmt(f.get("pid", (0, None))[1]),
            fmt(f.get("thr", (0, None))[1]), fmt(f.get("pc", (0, None))[1]),
            fmt(f.get("lr", (0, None))[1]), fmt(f.get("cpsr", (0, None))[1]),
            fmt(f.get("r0", (0, None))[1])))
    pcs = [r[2]["pc"][1] for r in full if "pc" in r[2]]
    if pcs:
        in_win = sum(1 for v in pcs if v is not None and 0x2C000000 <= v < 0x40000000)
        say("OBS", "full-window _pc range [%s .. %s]; %d/%d inside the 0x2c000000..0x40000000 "
                   "window" % (fmt(min(pcs)), fmt(max(pcs)), in_win, len(pcs)))
    cpsrs = sorted(set(r[2]["cpsr"][1] for r in full if "cpsr" in r[2]))
    say("OBS", "full-window _cpsr values: %s" % (" ".join(fmt(v) for v in cpsrs[:8]) +
        (" ..." if len(cpsrs) > 8 else "")))
    ln, n, f, order = recs[-1]
    frontier = "n=%s _num=%s" % (fmt(n), fmt(f.get("num", (0, None))[1]))
    if n is not None and n <= FULL_N:
        frontier += " pid=%s pc=%s lr=%s cpsr=%s r0=%s" % (
            fmt(f.get("pid", (0, None))[1]), fmt(f.get("pc", (0, None))[1]),
            fmt(f.get("lr", (0, None))[1]), fmt(f.get("cpsr", (0, None))[1]),
            fmt(f.get("r0", (0, None))[1]))
    say("READING", "LAST usvc record @:%d %s - THE FRONTIER: the last syscall the user side "
                   "managed, by construction whether or not the counted channel survives it" % (
                       ln, frontier))
    say("OBS", "full-window records: %d; ladder records: %d" % (len(full), len(lad)))


def s5_context(cap):
    print("\n== [5] THE CAP AND THE THINNING (context, not a new reading) ==")
    say("OBS", "cap stays 24576 (0x6000); the read-dump thinning ordinal moved 256 -> 64 at "
               "committed 0504be5 and is checked AT THE SOURCE by the build script "
               "(`rd_full_ = (st_read_count < 64u)`), not re-derivable from the capture")
    cells = cap.all("xnu_live_capped")
    if cells:
        say("OBS", "capped tombstone present (%s) - expected: the counted channel saturates and "
                   "the direct channel carries the trio, the pet feed, the anomalies, the "
                   "summaries and the SVC frontier past it" % fmt(cells[-1][3]))
    else:
        say("OBS", "no capped tombstone - the whole boot fit the counted channel")


def s_rung(cap):
    """[R] THE 1002 r3-SAVE FIX AS A REGRESSION (frozen pre-reg (1)).

    After `IOS7SHREG check_np ret=12`, does dyld PROCEED with ZERO `usermode data abort ... at
    0x00000004`?  The 999 EXPECTED reading: the NEXT line is `IOS7SHREG map_slide ...` and the abort
    count is 0.  Count > 0 while the boot reached check_np => the FALSIFIER (pre-reg (8)(b)): the r3
    clobber was NOT the cause, the structural TPIDRURO item moves to the FRONT (1003).  This is
    PRESENCE-based - the aborts, if there, are evidence whatever the run did next - so it is counted
    (hard) with no death-gate suppression.  A capture that never reached check_np gets no verdict: its
    0 aborts prove nothing, and are printed [death-gated]."""
    print("\n== [R] THE 1002 REGRESSION: the dyld `_cerror` abort at 0x4 stays GONE after `check_np ret=12`? ==")
    aborts = cap.abort_hits()
    n_abort = len(aborts)
    # The anchor: the line the pre-reg hangs the reading on. Its ABSENCE is what gates the verdict.
    np_ln = None
    for ln, line in enumerate(cap.lines, 1):
        if CHECK_NP_LIT in line:
            np_ln = ln
            break
    say("OBS" if np_ln else "---", "the anchor `%s`: %s" % (
        CHECK_NP_LIT, "@:%d" % np_ln if np_ln else "ABSENT (the run never reached it)"))
    say("OBS" if n_abort else "---", "`%s`: x%d%s" % (
        ABORT_LIT, n_abort, "  first @:%d  last @:%d" % (aborts[0][0], aborts[-1][0]) if aborts else ""))
    # The PRIMARY reading: the next line after check_np - map_slide (999 / expected) vs abort (1001).
    if np_ln is not None:
        nxt = [ln for ln in range(np_ln + 1, min(np_ln + 4, len(cap.lines) + 1))
               if cap.lines[ln - 1].strip()]
        if nxt:
            first_ln = nxt[0]
            first_txt = clip(cap.lines[first_ln - 1].strip(), 120)
            is_slide = MAP_SLIDE_LIT in cap.lines[first_ln - 1]
            say("READING", "the NEXT non-empty line after `check_np ret=12` (@:%d): %s %s" % (
                first_ln, first_txt,
                "<== IOS7SHREG map_slide (999's reading - the fix worked)"
                if is_slide else "<== NOT map_slide: dyld did NOT proceed on the expected line"))
    else:
        say("---", "the NEXT-line reading cannot be taken (no anchor)")
    # THE VERDICT.  Presence-based: a wrong presence (aborts) is evidence whatever the run did next.
    if n_abort > 0:
        if np_ln is not None:
            hard("1002-FIX REGRESSION: `%s` x%d right around `check_np ret=12` - 1002 CONFIRMED these "
                 "GONE (the USVC hook's r3 save set closed the dyld `_cerror`/TPIDRURO fault), so a "
                 "recurrence here means the 1002 fix was reverted or the assembly layer was not "
                 "rebuilt - REBUILD, this is not a 1003 reading. (The 1003 fix rides on this baseline; "
                 "a reverted r3 save would move the frontier back to dyld.) READ THIS LOUDLY." % (
                     ABORT_LIT, n_abort))
        else:
            hard("`%s` x%d WITHOUT the `check_np ret=12` anchor - the aborts are present but the print "
                 "site they hang on is absent; report both by value" % (ABORT_LIT, n_abort))
    elif np_ln is not None:
        if GATE_OPEN:
            say("PASS", "ZERO `usermode data abort at 0x4` after `check_np ret=12` while the boot "
                        "survived - the 1002 r3-save fix HOLDS (regression): the dyld `_cerror` "
                        "frontier did not reopen")
        else:
            say("OBS", "0 aborts and the anchor present, but the death gate is CLOSED - the survival "
                       "half of the self-test is not established; read [0] before calling it a pass")
    else:
        hard("no `%s` and no anchor - the 1002 regression cannot be read: the run never reached the "
             "dyld checkpoint, so 0 aborts prove nothing (death reading, not a pass)" % ABORT_LIT,
             absence_based=True)


def s_stall(cap):
    """[S] THE STALL SAMPLE - THE 1003 RUNG ITSELF (the pure MEASUREMENT).

    The per-tick sample of the INTERRUPTED pid-1 frame, published by `entry_note_stall_sample` on the
    tick's powers-of-two ladder (the SAME cadence as `xnu_live_tick_irq`) as seven keys:
    `xnu_live_stall_n` (counter) / `_t` (the tick number) / `_pc` / `_cpsr` / `_sp` / `_lr` / `_nz`
    (count of non-zero words in the saved-state frame - the self-check).

    THE CADENCE IS SPARSE BY DESIGN (the tick is 1 kHz; the ladder is 1,2,4,...,2^k, so consecutive
    samples sit ~2^k ms apart and get sparser): few samples, or a large `_t` gap between them, is NOT
    a fault - over a ~23 min alive window there are ~20 samples.  The LAST few bracket the frontier.

    THE CLASSIFICATION (by cpsr's mode bits FIRST, the pc as corroboration): a USER sample has cpsr
    mode bits 0x10 (USR/T, e.g. 0x60000010) and a user pc (< 0x80000000); a KERNEL sample has cpsr
    >= 0x80000000 (SVC mode) and a kernel pc (>= 0x80000000).  Disjoint readings:
      * user pcs that ADVANCE across samples, or REPEAT on one small loop => pid 1 is SPINNING in user
        space, and the pc says WHERE (dyld's own image 0x2be00000..0x2be58000, or above it the
        shared-cache window 0x2c000000 + slide 0x37f6000; the 1002 stall pc was 0x2be97328);
      * every sample a kernel pc => pid 1 is ASLEEP/blocked, and the kernel pc says IN WAIT.

    THIS IS A MEASUREMENT: the stall is the EXPECTED outcome (1003 changes no kernel behaviour), so a
    still-stalled run is NOT a failure.  FAIL ONLY ON: (a) ZERO samples => the instrument did not
    reach the image (rebuild); (b) EVERY sample `_nz` = 0 => the saved-state frame was read all-zero -
    the frame pointer/offset is wrong (a MEASUREMENT defect - adjudicate against entry_saved_state.h).
    """
    print("\n== [S] THE STALL SAMPLE - the per-tick pid-1 frame (the 1003 MEASUREMENT rung) ==")
    recs = cap.stall_samples()
    n_tok = cap.family_count("xnu_live_stall_")
    if not recs:
        # (a) THE FALSIFIER: the instrument did not reach the image.
        hard("ZERO `xnu_live_stall_*` records - the STALL instrument did not reach the image: this is "
             "the 1003 rung's own FALSIFIER (pre-reg (0)/(F)); REBUILD with STAGE90_XNU_STALL=1 "
             "exported before build_xnu_arm_kernel.sh, do not judge the frontier from this capture")
        if n_tok:
            soft("xnu_live_stall_* tokens x%d but no `_n` record - the record shape broke; the reader "
                 "cannot walk the samples" % n_tok)
        return
    ns = [r["f"].get("n", (0, None))[1] for r in recs]
    say("OBS", "samples x%d (n %s .. %s; the ladder is SPARSE by design - consecutive samples sit "
               "~2^k ms apart and get sparser)" % (
                   len(recs), fmt(ns[0]), fmt(ns[-1])))
    # The per-sample table, by value - the pc/cpsr/nz triple is the reading.
    user_n = kern_n = 0
    pcs, cpsrs, nzs = [], [], []
    for r in recs:
        ln = r["ln"]
        f = r["f"]
        n = f.get("n", (None, None))[1]
        t = f.get("t", (None, None))[1]
        pc = f.get("pc", (None, None))[1]
        cpsr = f.get("cpsr", (None, None))[1]
        sp = f.get("sp", (None, None))[1]
        lr = f.get("lr", (None, None))[1]
        nz = f.get("nz", (None, None))[1]
        missing = [k for k in STALL_KEYS if k not in f]
        if missing:
            soft("stall sample n=%s @:%d is missing fields (%s) - the 7-key block shape broke" % (
                fmt(n), ln, ",".join(missing)))
        # CLASSIFY: cpsr mode bits first, pc corroborates.
        mode = (cpsr & 0x1F) if cpsr is not None else None
        is_user = (mode == 0x10) or (pc is not None and pc < 0x80000000)
        if nz is not None:
            nzs.append(nz)
        if pc is not None:
            pcs.append(pc)
        if cpsr is not None:
            cpsrs.append(cpsr)
        if is_user:
            user_n += 1
            where = _stall_region(pc)
        else:
            kern_n += 1
            where = "kernel (%s)" % ("SVC mode" if (cpsr is not None and cpsr >= 0x80000000)
                                     else "cpsr mode 0x%x" % (mode if mode is not None else 0))
        say("OBS", "  @:%d n=%s t=%s pc=%s cpsr=%s sp=%s lr=%s nz=%s  -> %s  [%s]" % (
            ln, fmt(n), fmt(t), fmt(pc), fmt(cpsr), fmt(sp), fmt(lr), fmt(nz),
            "USER" if is_user else "KERNEL", where))
    # ---- THE SELF-CHECK: `_nz` = the count of non-zero words in the frame (STAGE90_SS_SIZE/4 = 19).
    if nzs and all(v == 0 for v in nzs):
        # (b) the ONE MEASUREMENT defect: an all-zero frame cannot be a real interrupted frame.
        hard("EVERY stall sample carries `_nz`=0 - the saved-state frame read ALL ZERO: an interrupted "
             "frame is never empty (it holds pc/cpsr/sp/lr at least), so the frame pointer or the "
             "STAGE90_SS_* offsets are WRONG. This is a MEASUREMENT defect, not a frontier reading: "
             "adjudicate the offsets against src/entry/entry_saved_state.h before any [S] conclusion")
    else:
        say("PASS", "`_nz` > 0 on every sample (values %s; the frame carries %d words) - the "
                    "interrupted-frame reader read a REAL frame" % (
                        " ".join(fmt(v) for v in nzs[:8]) + (" ..." if len(nzs) > 8 else ""), SS_WORDS))
    # ---- THE CLASSIFICATION VERDICT (a reading, never a failure).
    distinct = sorted(set(pcs))
    say("OBS", "classification: USER pc x%d, KERNEL pc x%d (cpsr mode bits are the primary classifier, "
               "pc corroborates); distinct pcs x%d" % (user_n, kern_n, len(distinct)))
    if user_n and user_n >= kern_n:
        # SPINNING: user pcs that advance or repeat on a small loop.
        if len(distinct) <= 1:
            say("READING", "SPINNING (wedged): every sample is the SAME user pc %s - pid 1 is stuck at "
                           "one user instruction, in %s" % (
                               fmt(distinct[0]), _stall_region(distinct[0])))
        else:
            lo, hi = min(distinct), max(distinct)
            loop = " (a SMALL loop: span 0x%x)" % (hi - lo) if (hi - lo) <= 0x4000 else \
                   " (the pcs ADVANCE: span 0x%x)" % (hi - lo)
            say("READING", "SPINNING: the interrupted frame is USER mode with %d distinct pc(s) %s..%s%s "
                           "- pid 1 is spinning in user space, NOT asleep" % (
                               len(distinct), fmt(lo), fmt(hi), loop))
        say("READING", "  the last user pc %s is in %s; the 1002 stall pc was %s" % (
            fmt(distinct[-1]), _stall_region(distinct[-1]), fmt(STALL_1002_PC)))
    elif user_n == 0:
        say("READING", "ASLEEP/blocked: EVERY sample is a KERNEL pc - pid 1 is not running in user "
                       "space (it is asleep in the kernel, or the timer fires only while a kernel "
                       "thread runs); the kernel pcs %s say IN WAIT" % (
                           " ".join(fmt(v) for v in distinct[:8]) + (" ..." if len(distinct) > 8 else "")))
    else:
        soft("MIXED: USER pc x%d, KERNEL pc x%d - neither the all-user SPINNING nor the all-kernel "
             "ASLEEP shape; report the samples above verbatim (a user pc interleaved with kernel pcs "
             "is a migration, or a ladder sampled across both)" % (user_n, kern_n))
    say("OBS", "cpsr values: %s" % (
        " ".join(fmt(v) for v in sorted(set(cpsrs))[:8]) + (" ..." if len(set(cpsrs)) > 8 else "")))
    # This section is a MEASUREMENT: it NEVER hard-FAILs merely because the boot still stalls.
    say("OBS", "this is a MEASUREMENT section: the stall is the EXPECTED outcome of this rung (1003 "
               "changes no kernel behaviour) - the classification above IS the 1003 finding")


def _stall_region(pc):
    """Name the region a sampled user pc falls in (the 1002/1003 map): dyld's own image
    (0x2be00000..0x2be58000), else the dyld shared-cache window (base 0x2c000000 + slide 0x37f6000),
    else the pc's own half.  The 1002 stall pc (0x2be97328) is ABOVE dyld's image end - the
    shared-cache/libSystem region."""
    if pc is None:
        return "unknown"
    if DYLD_BASE <= pc < DYLD_END:
        return "dyld image 0x2be00000..0x2be58000"
    if pc >= SHARED_CACHE_BASE:
        return "shared cache (base 0x2c000000 + slide 0x37f6000)"
    if pc >= DYLD_END:
        return "the 0x2be58000..0x2c000000 gap (above dyld, below the shared cache - where the 1002 " \
               "stall pc 0x2be97328 sat)"
    return "low user map (< 0x2be00000)"


def s_f(cap):
    """[F] THE 1003 RUNG'S OWN FRONTIER - the CODE-SIGNING page-validation wall.

    The 1002 press read names it by value: `CODE SIGNING: cs_invalid_page(0x2f7f6000): p=1[launchd]
    clearing CS_VALID` - the SAME wall 999 stopped on.  But 1003's reading (records/1002-press-read.txt
    section [6]; scripts/press_1003.sh (F)) is that this line is a TOLERATED warning (CS_VALID cleared,
    retval 0 - the fault PROCEEDS) and NOT the frontier: it appears EXACTLY ONCE (CS_TOLERATED = 1) and
    the boot gets PAST it.  The frontier is the pid-1 user-space STALL, which [S] measures.  [F] reads
    it BY VALUE - the count, the exact page `0x2f7f6000`, the `p=1[launchd]` tag, and whether the boot
    got PAST it (a record/line after the last cs_invalid_page) - and treats the SINGLE tolerated
    occurrence as the expected reading, never a failure.  It is PRESENCE-based, so a reading is reported
    by value, but the verdict is death-gated (the wall prints during the exec, before the trio)."""
    print("\n== [F] THE 1003 FRONTIER: the code-signing page-validation wall (cs_invalid_page) ==")
    hits = cap.cs_invalid_hits()
    n = len(hits)
    say("OBS" if n else "---", "`%s` lines: x%d%s" % (
        CS_INVALID_LIT, n, "  first @:%d" % hits[0][0] if n else "  (the wall was not hit)"))
    for ln, raw in hits[:8]:
        say("OBS", "  @:%d %s" % (ln, raw))
    if n > 8:
        say("OBS", "  ... (%d more)" % (n - 8))
    # The exact page the 1002 frontier named, and the pid tag.  Counted by SUBSTRING over the raw
    # text (NOT cap.lit_count - that dict only tracks LADDER_LITERALS, and these are frontier-specific).
    page_n = cap.text.count(CS_PAGE_1002)
    say("OBS" if page_n else "---", "the 1002 page `%s`: x%d%s" % (
        CS_PAGE_1002, page_n, "  (as named in the 1002 press read)" if page_n else "  (absent)"))
    pid_n = cap.text.count(CS_PID_1002)
    say("OBS" if pid_n else "---", "the 1002 pid tag `%s`: x%d" % (CS_PID_1002, pid_n))
    clear_n = cap.text.count(CS_CLEAR_LIT)
    say("OBS" if clear_n else "---", "`%s`: x%d" % (CS_CLEAR_LIT, clear_n))
    # Did the boot get PAST the wall?  A kv record or a text line after the LAST cs_invalid_page.
    if n:
        last_ln = hits[-1][0]
        after_kv = [t for t in cap.toks if t[0] > last_ln]
        after_txt = [ln for ln in range(last_ln + 1, len(cap.lines) + 1)
                     if cap.lines[ln - 1].strip()]
        say("READING", "after the LAST `%s` (@:%d): %d kv record(s)%s, %d non-empty text line(s) - "
                       "the boot %s" % (
                           CS_INVALID_LIT, last_ln, len(after_kv),
                           " (first: %s @:%d)" % (after_kv[0][1], after_kv[0][0]) if after_kv else "",
                           len(after_txt),
                           "GOT PAST the wall" if (after_kv or after_txt) else "STOPPED at the wall"))
    else:
        say("---", "no `%s` => the wall was not reached (or the fix removed it): read [R] and [0] to "
                   "place this run" % CS_INVALID_LIT)
    # THE VERDICT.  cs_invalid_page is a TOLERATED warning, and the 1002 capture measured it EXACTLY
    # ONCE - so the count is reported by value and the expected reading is x1, never a failure.  It is
    # NOT the frontier (the frontier is the pid-1 stall, [S]): a single tolerated occurrence is the
    # EXPECTED reading of this rung, not a defect.
    if n == 0:
        say("OBS", "cs_invalid_page x0 - the wall was not hit (or the code path did not run this boot); "
                   "1003's expected reading is the SINGLE TOLERATED occurrence measured in the 1002 "
                   "capture - read [S] for the frontier that IS the reading")
    elif n == CS_TOLERATED:
        say("PASS", "cs_invalid_page x%d - the TOLERATED warning the 1002 capture measured (CS_VALID "
                    "cleared, retval 0, the fault PROCEEDS): the EXPECTED reading, NOT the frontier "
                    "(the frontier is the pid-1 user-space stall, measured in [S])" % n)
    else:
        say("READING", "cs_invalid_page x%d (the 1002 capture measured the tolerated warning x%d) - a "
                       "count above 1 is NOT the tolerated single occurrence; adjudicate against "
                       "records/1002-press-read.txt and the frozen pre-reg (F) before reading it as "
                       "anything: a wall that stopped tolerating is a frontier finding, not a regression "
                       "of 1003" % (n, CS_TOLERATED))
    if n and not GATE_OPEN:
        print("  [death-gated] cs_invalid_page x%d with the exec-done trio ABSENT - the wall's reach "
              "is a DEATH READING (the wall prints during the exec, before the trio's print site)" % n)


def s6_guards(cap):
    print("\n== [6] REGRESSION GUARDS (absence counts only while the death gate is OPEN) ==")
    for lit, n, want_txt in (("IOS7LAB cache mapping", 4, "the wire's four mapping lines"),
                             ("IOS7LAB shared region", 1, "the geometry line, exactly once"),
                             (CHECK_NP_LIT, 1, "the pre-438 constant")):
        n_have = cap.lit_count(lit)
        if n_have == n:
            say("PASS", "%s x%d (%s)" % (lit, n_have, want_txt))
        elif n_have == 0:
            hard("%s x0 - regression (%s)" % (lit, want_txt), absence_based=True)
        else:
            say("OBS", "%s x%d (expected %d: %s)" % (lit, n_have, n, want_txt))
    ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
    if not ret:
        hard("no `map_slide ret=` - 999 measured ret=0 (the 438 success); absent while the boot "
             "survived = the slide regressed, STOP", absence_based=True)
    elif all(v == 0 for v in ret):
        say("PASS", "IOS7SHREG map_slide ret=0 x%d (999's success held)" % len(ret))
    else:
        hard("map_slide ret=%s - STOPS per pre-reg (8): the slide regressed, STOP" % ret)
    mf = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_file kr=(-?\d+)', cap.text)]
    if any(v != 0 for v in mf):
        hard("map_file kr!=0 - GEOMETRY REGRESSION (G1 held kr=0); stop and re-model")
    if not mf:
        hard("no `map_file kr=` - the G1 reading is absent while the boot survived (the call is on "
             "the same exec path as the skip): report, do not model", absence_based=True)
    say("OBS", "map_file kr= values: %s" % (mf if mf else "none"))
    # ---- THE FLIP (unchanged from 1001): VMHINT991 x0 while the boot survives.
    n991 = cap.lit_count("VMHINT991")
    objs = re.findall(r'VMHINT991: obj=(0x[0-9a-fA-F]+)', cap.text)
    if GATE_OPEN:
        if n991 == 0:
            say("PASS", "VMHINT991 x0 - the FLIPPED 1001/1002 expectation: the submap call is skipped, "
                        "so the guard at the far end of it never runs")
        else:
            hard("VMHINT991 fired while the boot survived - the skip did not reach the image, or "
                 "another caller path still reaches vm_pmap_enter with a non-object (x%d; objs %s)" % (
                     n991, ", ".join(objs[:8])))
    else:
        say("OBS", "VMHINT991 x%d (objs %s) - DEATH READING, no verdict: the guard sits DOWNSTREAM of "
                   "the lock inside the same call, and the trio is absent" % (
                       n991, ", ".join(objs[:8]) if objs else "none"))
    for lit, want in (("vm_allocate64", 0),):
        n = cap.lit_count(lit)
        say("PASS" if n == want else "OBS", "%s x%d (expected %d)" % (lit, n, want))
    for k, want in FIXED_WDT.items():
        t = cap.first(k)
        if t is None:
            hard("%s: ABSENT - the frozen WDT net must hold (999/1000 measured %s)" % (k, fmt(want)),
                 absence_based=True)
        elif t[3] == want:
            say("PASS", "%s=%s (frozen value, 999's net unchanged)" % (k, fmt(t[3])))
        else:
            say("OBS", "%s=%s (frozen value is %s - report verbatim)" % (k, fmt(t[3]), fmt(want)))
    bark = cap.first("xnu_live_wdt_bark")
    if bark is None:
        hard("xnu_live_wdt_bark: ABSENT - the watchdog install reading must hold while the boot "
             "survives (its print site is the first pet call, before exec)", absence_based=True)
    else:
        say("PASS" if bark[3] == BARK_1003 else "OBS", "xnu_live_wdt_bark=%s (frozen %s)" % (
            fmt(bark[3]), fmt(BARK_1003)))
    slept = sum(1 for line in cap.lines if "mini4: the CPU slept" in line)
    usb = cap.family_count("xnu_live_usb_")
    say("OBS" if (slept or usb) else "PASS", "USB/pcx keys: `xnu_live_usb_*` x%d, "
        "`mini4: the CPU slept` lines x%d (expected 0/0 - the kernel never idles on this rung)" % (
            usb, slept))


def s7_falsifiers(cap):
    """The frozen 1003 pre-reg (8)(a)-(d).

    (8)(a) is the image-side reading of the 1003 change (the press's own pre-flight, read off the elf -
    not a capture reading); (8)(b) is [R]'s regression predicate (the 1002 abort recurrence), reported
    here by value, never re-fired; (8)(c) is the stall's own reading ([S]): the boot still stalling at
    the same last usvc pc 0x2be97328 is the EXPECTED outcome, and a boot that instead ADVANCES (prints
    more, reaches a new wall) is the frozen falsifier; (8)(d) is the frontier-advanced reading, whose
    evidence is the last direct keys and the text channel's last stanza.  The tolerated
    `cs_invalid_page(0x2f7f6000)` p=1[launchd] (ONCE) is reported by value for [F] but is NOT a
    falsifier - it is a warning the fault proceeds past."""
    print("\n== [7] FALSIFIERS (frozen 1003 (8)(a)-(d)) ==")
    # ---- (8)(a): the 1003 change in the image. Read off the elf by the press's pre-flight, NOT this
    # capture - state so, by value.
    say("OBS", "(8)(a) the 1003 change's by-value reading is IMAGE-side "
               "(scripts/press_1003.sh step 2-0): the recorder symbol `entry_note_stall_sample` (T) and "
               "the 7 `xnu_live_stall_*` literals in out/stage90/xnu_arm_entry.elf - not derivable from "
               "this capture. If the instrument is NOT in the image, the layer was not rebuilt and "
               "nothing below is a test of the 1003 fix (see [S])")
    # ---- (8)(b): the signed check_np-adjacent 1002 abort - a REGRESSION predicate now, adjudicated
    # in [R], restated here. 1002 CONFIRMED these GONE, so a recurrence is a rebuild signal.
    n_abort = len(cap.abort_hits())
    say("OBS", "(8)(b) `%s` x%d => %s" % (
        ABORT_LIT, n_abort,
        "1002-FIX REGRESSION - adjudicated in [R] (one finding, one hard anomaly): the r3 save was "
        "reverted / the assembly layer was not rebuilt" if n_abort else
        "the 1002 r3-save fix holds (no regression)"))
    # ---- (8)(c): the STALL's own reading ([S]).  The EXPECTED outcome is the boot STILL stalling
    # (same last usvc pc 0x2be97328); the falsifier is a boot that ADVANCES instead.  Adjudicated in
    # [S]; restated here by value, never re-fired.
    stall = cap.stall_samples()
    n_stall = len(stall)
    last_usvc = cap.last("xnu_live_usvc_pc")
    say("OBS", "(8)(c) the stall instrument: `xnu_live_stall_*` samples x%d (adjudicated in [S]); the "
               "last `xnu_live_usvc_pc` %s => %s" % (
                   n_stall, fmt(last_usvc[3]) if last_usvc else "(absent)",
                   "EXPECTED (the boot still stalls; a stall instrument that reached the image is the "
                   "measurement this rung was built for)" if n_stall else
                   "the stall instrument did NOT reach the image - the (8)(a) rebuild signal ([S])"))
    # ---- the tolerated code-signing wall, by value (its count is coherent: CS_TOLERATED = 1).
    n_cs = cap.cs_invalid_hits()
    page_n = cap.text.count(CS_PAGE_1002)
    say("OBS", "(8)(c) the TOLERATED code-signing wall: `%s` x%d, page `%s` x%d (the 1002 capture "
               "measured the tolerated warning EXACTLY ONCE; adjudicated in [F], NOT a falsifier)" % (
                   CS_INVALID_LIT, len(n_cs), CS_PAGE_1002, page_n))
    # ---- the PRESERVED 1001 pre-reg (8)(c): the death AT THE SAME submap call. Its signature is
    # printed in [3]; here the predicate, reported by value (never re-fired).
    sc = [(ln, line.strip()) for ln, line in enumerate(cap.lines, 1)
          if re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line)
          or re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line[::2])]
    n_sub = cap.lit_count(SUBMAPSKIP_LIT)
    say("OBS", "(1001)(8)(c) death at the same call: panic naming hw_lock_lock / vm_map_pmap_enter / "
        "vm_map_enter = %s; SUBMAPSKIP x%d => %s" % (
            ("YES @:%d" % sc[0][0]) if sc else "no", n_sub,
            "SIGNATURE (see [3]): if this capture is a 1003 image the 1001 skip did not reach it - "
            "REBUILD" if (sc and n_sub == 0) else
            "the signature does not fire" if n_sub else "no panic naming the call in this capture"))
    # ---- (8)(d): death ELSEWHERE, later than the 1001/1002 point => the frontier ADVANCED.
    ph = cap.panic_hits()
    if ph:
        say("OBS", "(8)(d) the text channel's last panic stanza @:%d: %s" % (ph[0][0], ph[0][1]))
    tail = [l for l in cap.lines[-6:] if l.strip()]
    say("OBS", "(8)(d) the capture's last lines (the death stanza, by value): %s" % (
        " | ".join(clip(l.strip(), 90) for l in tail)))
    for fam, why in (("xnu_live_usvc_", "the user-SVC frontier"), ("xnu_live_wdt_pet_", "the last feed"),
                     ("xnu_live_tick_anom_", "the last anomaly"), ("xnu_live_exec_done_", "the exec-done point")):
        t = next((x for x in reversed(cap.toks) if x[1].startswith(fam)), None)
        say("OBS" if t else "---", "(8)(d) last DIRECT key of %s (%s): %s" % (
            fam, why, ("%s=%s @:%d" % (t[1], t[2], t[0])) if t else "(none)"))
    # ---- the 1001 pre-reg's (6)(a)-(c), re-mapped, reported by value (never re-fired).
    trio_any = any(cap.first(k) for k in TRIO)
    usvc = cap.family_count("xnu_live_usvc_")
    if usvc == 0:
        ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
        premise = trio_any and bool(ret) and all(v == 0 for v in ret)
        say("OBS", "(1001)(6)(a) `xnu_live_usvc_n` ABSENT: premise (exec trio AND `map_slide ret=0` "
                   "present) = %s%s" % ("TRUE" if premise else "FALSE",
            " - adjudicated in [4] (the gate-open USVC expectation), NOT re-fired here" if premise else
            " - the premise is false, so it does not fire"))
    else:
        say("OBS", "(1001)(6)(a) does not apply: usvc records x%d exist" % usvc)
    pets = cap.family_count("xnu_live_wdt_pet_")
    say("OBS", "(1001)(6)(b) zero pet-feed records: %s" % (
        "adjudicated in [2], not re-fired here (the frozen (6) reads it as 'the door stopped or the "
        "gate moved: a READING')" if pets == 0 else "does not apply: pet records x%d exist" % pets))
    n_usvc = len(cap.all("xnu_live_usvc_n"))
    usvc_num0 = [t for t in cap.all("xnu_live_usvc_num") if t[3] == 0]
    if n_usvc and len(usvc_num0) == n_usvc:
        say("OBS", "(1001)(6)(c) ALL usvc _num = 0 - adjudicated in [4] (one finding, one hard anomaly)")
    else:
        say("OBS", "(1001)(6)(c) moot: usvc `_num` records x%d, of them 0-valued x%d" % (
            len(cap.all("xnu_live_usvc_num")), len(usvc_num0)))


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    cap = Capture(sys.argv[1])
    print("1003 capture adjudicator - %s (%d bytes, %d lines)" % (
        sys.argv[1], cap.nbytes, len(cap.lines)))
    auth, frozen = authority_line()
    print(auth)
    print("spec read here: 1002's switch set VERBATIM (USVC=1, SUBMAP_PMAP_SKIP=1) + the ONE new 1003 "
          "switch STAGE90_XNU_STALL (the per-tick pid-1 stall sampler, section [S]); the tolerated "
          "code-signing wall `%s` (`%s`) is NOT the frontier; the 1002 regression self-test is ZERO "
          "`%s` after `%s`" % (CS_INVALID_LIT, CS_PAGE_1002, ABORT_LIT, CHECK_NP_LIT))
    s0_gate(cap)
    s_identity(cap)
    s1_tick(cap)
    s2_direct_pet(cap)
    s3_submapskip(cap)
    s4_usvc(cap)
    s5_context(cap)
    s_rung(cap)
    s_stall(cap)
    s_f(cap)
    s6_guards(cap)
    s7_falsifiers(cap)
    print("\n== VERDICT ==")
    if GATED:
        print("  %d absence-based reading(s) printed [death-gated] above and NOT counted (the run "
              "died before the gate opened)" % len(GATED))
    if SOFT:
        print("  %d soft finding(s) above (report-only, never counted)" % len(SOFT))
    print("HARD ANOMALIES: %d" % len(FAILURES))
    for f in FAILURES:
        print("    - " + f)
    if not frozen:
        print("  !! NO FROZEN 1003 PRE-REGISTRATION was found at run time - the expectations above "
              "are the task's (see [0]/[R]/[7] for what that changes); a deviation here is to REPORT")
    if SIGNATURES:
        print("  !! %d REBUILD-CLASS SIGNATURE(S) present (not counted, and not a pass either):" %
              len(SIGNATURES))
        for s in SIGNATURES:
            print("    ! " + clip(s, 200))
        print("     An exit 0 here means 'the gate did not count it', NOT 'the rung worked': on the "
              "1003 arm these are the pre-reg's own REBUILD signatures - confirm the arm identity "
              "(the runner log / the entry-config switch) before acting")
    conj = " AND ".join("%s->%s" % (k, "holds" if GATE_CONJ.get(k) else "FAILS")
                       for k in sorted(GATE_CONJ))
    if GATE_OPEN:
        print("DEATH GATE: OPEN (trio %s; frozen conjuncts: %s) - absence-based verdicts count" % (
            "present" if TRIO_OPEN else "present-by-conjunction-only", conj))
    else:
        print("DEATH GATE: CLOSED (trio %s; frozen conjuncts: %s) - absence-based verdicts NOT counted" % (
            "present" if TRIO_OPEN else "absent", conj))
    if FAILURES:
        print("  => STOP and report per the pre-reg's FAILURE SIGNATURES: read every [FAIL] by value "
              "against the arm's record before acting on it")
        return 1
    print("  no hard anomaly; readings above are the evidence (a [death-gated] line is a death "
          "reading to adjudicate against the timeline, not a pass)")
    return 0


if __name__ == "__main__":
    sys.exit(main())