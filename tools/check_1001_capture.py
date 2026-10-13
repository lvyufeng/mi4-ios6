#!/usr/bin/env python3
"""Mechanical adjudicator for the 1001 press capture - by value, and DEATH-AWARE.

1001 IS THE FIX RUNG for the death the 1000 record names.  `vm_map_enter`'s pmap-enter tail calls
`vm_map_pmap_enter` WITHOUT an `is_submap` guard (records/1000-press-read.txt, "Root cause"); on a
submap enter the `object` parameter is the submap itself, so the call locks and probes a `vm_map` as
if it were a `vm_object`.  1000 died there (`hw_lock_lock(): PANIC: Lock 0xc0005b50 = 0xffb5be0b`,
XNU time 10.31 s) between the exec ENTRY quartet and the exec DONE trio.  The rung's switch
`STAGE90_XNU_SUBMAP_PMAP_SKIP=1` skips the call when `is_submap` and prints a bounded probe from the
skip branch, VERBATIM:

    SUBMAPSKIP: obj=%p start=%p end=%p size=%p lr=%p

(max 8 hits, then silent; `%p` renders `0x` + 8 lowercase hex digits in this kernel - VMHINT991's
own lines read `obj=0xc0005b48`).  Most likely exactly 2 hits, with `obj` the two FREED vm_map-zone
elements 999's VMHINT991 named: 0xc0005b48 (#19) and 0xc0005ab0 (#18).  An obj match is reported as
a line (soft evidence), never as a hard verdict: naming the callers is the rung's finding, not its
pass/fail.

THE DEATH GATE (the headline change; owed to 1001 by records/1000-press-read.txt S[5] and "Pre-
registration deviations" 4).  The 1000 tool read guard ABSENCE as regression - and its exit=1 on the
1000 capture was wrong BY CONTEXT, not by parsing: the run died at 10.31 s, before the exec returned,
so `IOS7LAB cache mapping`, `check_np ret=12`, `map_slide`, `VMHINT991` and the tick summaries were
never printed because their print sites were never REACHED.  Here, every check whose evidence is an
ABSENCE counts as a hard anomaly ONLY under the gate - and the gate is the FROZEN one from
`scripts/press_1001.sh` (3), all three conjuncts:
    (i)   the text channel carries NO panic stanza;
    (ii)  the exec-done trio xnu_live_exec_done_seq / _who / _current AND its five
          xnu_live_exec_done_probe_* keys are present (999 had them at @:28715-22; capped-exempt -
          on the 1000 press the trio never printed because the exec never returned);
    (iii) kv records from AFTER the exec-returned point exist (the direct channel's own next records).
The trio is the visually prominent half (and the toggle the 1001 task named first): with all three of
it the tool prints `DEATH GATE: exec-done trio present => absences count`; with any of it missing,
`DEATH GATE: exec-done trio ABSENT => ...`.  The EFFECTIVE gate is the three-way conjunction above,
printed with each conjunct's state - where the two disagree, the frozen text wins and the disagreement
is REPORTED (a pre-reg deviation), never papered over.  Gate CLOSED: absence-based readings print
`[death-gated]` and are NOT counted (death readings, not regressions); exit stays 0 on their account.
Presence-based readings (a wrong value in a record that IS there, a malformed record, the entry quartet
when present) keep reporting by value and stay hard - a value that arrived is evidence whatever the run
did next.  Two frozen REBUILD signatures are the exception that proves the rule, because there the
ABSENCE *is* the death signature: (7)(a) (a panic naming hw_lock_lock / vm_map_pmap_enter /
vm_map_enter with SUBMAPSKIP absent) and failure signature (c) (the log ends with a capped key and no
direct key after it).  Neither is counted either - the reader cannot tell a 1001 image from a pre-1001
baseline by any capture-side marker - but both are printed under `[SIGNATURE]` and re-stated in the
verdict block, so an exit 0 can never be read as "the rung worked" when one of them is present.

WHAT FLIPS FROM THE 1000 TOOL (everything else is the 1000 tool's, key for key):
  (2) VMHINT991 x0 was 1000's death signature; for 1001 it is the EXPECTATION.  The call is skipped
      for submaps, so the guard at the far end of that same call should never print while the boot
      survives.  Gate OPEN + count 0 = PASS; gate OPEN + count > 0 = HARD (the skip did not reach the
      image, or another caller path still reaches vm_pmap_enter with a non-object); gate CLOSED = the
      count is a death reading, no verdict.
  (3) SUBMAPSKIP (new): gate OPEN + 0 lines = HARD (the probe/skip did not reach the image); >= 1 =
      PASS, every parsed 5-tuple printed by value; gate CLOSED = death-gated report only.
  (4) USVC (new expectation): the 1000 rung's user-SVC instrument had NO reading because 1000 died in
      kernel mode.  Gate OPEN + 0 `xnu_live_usvc_` records = HARD (the frontier did not reach user
      mode - with `cs_invalid_page`'s own presence/order printed as context, because an image built
      before STAGE90_XNU_USVC=1 explains 0 records without the frontier having failed).
  (5) PET SEMANTICS: zero feed records are read against the feed moment, never against the door.  The
      gate is inside `entry_wdt_pet` (`count >= g_wdt_bark_ticks / 2u`, entry_trace.c:2250; bark =
      950,185 WDT ticks at 32,765 Hz = 29.0 s, so the first feedable moment is 14.5 s).  Two cells,
      and they are NOT the same reading:
        * a PUBLISHED `xnu_live_wdt_countdown` sample AT or past bark/2 with zero feed records - the
          sample and the feed branch are read from the same `count` in the same call (entry_trace.c:
          2234-2251), so the branch had to fire: HARD while the gate is open (death-gated when not).
        * the boot merely PROVABLY LIVED past 14.5 s (largest `xnu_live_tick_sum_maxd` > bark/2) with
          zero feeds - the frozen pre-reg (5) calls that "the door stopped or the gate moved: a
          READING, not an assumed defect", so it is reported SOFT with the numbers, not counted.
      The distinction is stated in the output both times, because the sample publish gate is
      `pow2(calls)` (entry_trace.c:2237): a small largest sample proves nothing about the gate's
      reachability - 999 reached user mode and still published nothing past 2.10 s.

Usage:  tools/check_1001_capture.py <capture.txt>

SELF-TESTS (both run 2026-10-13, `python3 -I`; the two existing baselines bracket the tool's job):
  A. the 1000 capture (the death, pre-1001 image):
     out/stage90/captures/recovered-unknown-arm-20261013-072537-last_kmsg.txt
     The trio is absent (and a panic stanza is present) => gate CLOSED.  Every absence the 1000 tool
     hard-failed on (exec-done trio, `IOS7LAB cache mapping` x0, `IOS7LAB shared region` x0,
     `check_np ret=12` x0) must now print `[death-gated]`; VMHINT991 x0 and SUBMAPSKIP x0 are death
     readings with no verdict; the tick census (anom x16 / sum x12) and rd_ordinal x158 still print by
     value; the `[SIGNATURE]` lines MUST report (7)(a) (SUBMAPSKIP absent, the 1000 panic) even though
     they do not count.  EXPECTED EXIT 0 - this is the "an absence is not a regression when the run
     died before the print site" test, and the reason 1000's own tool exit 1 was wrong-by-context.
  B. the 999 capture (the surviving baseline, pre-1000 image):
     out/stage90/captures/recovered-unknown-arm-20261013-030640-last_kmsg.txt
     Trio AND probe keys present @:28715-22, no panic, later records => gate OPEN (both the frozen
     three-way gate and the trio toggle agree), and the DISCRIMINATING FAILS fire: VMHINT991 x2
     (flipped: >0 while the boot survived), SUBMAPSKIP x0, `xnu_live_usvc_` x0 - plus the 1000-tool
     census hard (a pre-1000 image carries none of the four families), which is a correct REBUILD
     reading here.  This is the test that proves the tool CAN fail.  EXPECTED EXIT 1.

Exit: 0 = no hard anomaly; 1 = at least one hard anomaly (any `[FAIL]` line); 2 = usage.  A hard
anomaly means STOP-and-report, never "rerun until green" - and a `[death-gated]` line is a death
reading that must be adjudicated against the timeline before it is called anything.
"""

import os
import re
import sys

KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+|hw_watchdog_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

# The 1001 probe, VERBATIM from the rung's spec. The reader never assumes a `%p` width: any `0x` +
# hex digits, because the string is the contract and the width is the console's.
SUBMAPSKIP_LIT = "SUBMAPSKIP:"
SUBMAPSKIP_RE = re.compile(
    r'SUBMAPSKIP:\s*obj=(0x[0-9a-fA-F]+)\s+start=(0x[0-9a-fA-F]+)\s+end=(0x[0-9a-fA-F]+)'
    r'\s+size=(0x[0-9a-fA-F]+)\s+lr=(0x[0-9a-fA-F]+)')
# The panic path's tank flush writes each character TWICE (`ppaanniicc(...)`, 1000 record, reader
# note). A SUBMAPSKIP that arrived through THAT path would be invisible to the literal reader - so the
# doubled form is counted too and reported, rather than read as "the probe never printed".
SUBMAPSKIP_DOUBLED = "SSUUBBMMAAPPSSKKIIPP"
PROBE_MAX_HITS = 8           # the probe's own bound ("max 8 hits, then silent")

# 999's VMHINT991 objects, by value (999 capture @:4096-97; 1000 record, "The death, by value"): two
# FREED vm_map-zone elements (w04=0xbaddecaf = ZALLOC's ZP_NOT_POISONED) that the unguarded call was
# handed as vm_objects. A 1001 SUBMAPSKIP whose `obj` is one of these NAMES a caller by measurement.
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

# The four instrument families 1000 introduced and 1001 KEEPS (the census discriminates a 1000/1001
# arm from its baselines). SUBMAPSKIP is counted separately in [3]: it is the rung, not an instrument.
NEW_FAMILIES = (
    ("xnu_live_tick_anom_", "(1) the late-delivery anomalies"),
    ("xnu_live_tick_sum_",  "(1) the tick aggregate summaries"),
    ("xnu_live_wdt_pet_",   "(2) the pet feed records (direct)"),
    ("xnu_live_usvc_",      "(4) the user-SVC frontier"),
)

LADDER_LITERALS = (
    ("IOS7SHREG map_slide ret=", "438's own exit - must be present and 0 while the boot survives (999's success)"),
    ("IOS7SHREG map_file kr=",   "the G1 reading (must stay kr=0)"),
    ("check_np ret=12",          "the pre-438 constant (must stay; exactly once)"),
    ("IOS7LAB cache mapping",    "the wire's mapping lines (exactly 4; printed inside the exec)"),
    ("IOS7LAB shared region",    "the geometry line (exactly once; 0 = the define did not reach)"),
    ("VMHINT991",                "the 991 guard - 1001 FLIPS it: the submap call is skipped, so 0x while the boot survives"),
    ("vm_allocate64",            "absent = EXPECTED (997: the failure path's buffer alloc)"),
    (SUBMAPSKIP_LIT,             "the 1001 skip-branch probe (>= 1 while the boot survives; <= 8 by construction)"),
)

# The watchdog net is 999/1000's, unchanged by 1001 (the fix touches osfmk/vm/vm_map.c only).
FIXED_WDT = {
    "hw_watchdog_timeout_s": 0x0000001D,          # 29 s
    "hw_watchdog_bite_ticks_written": 0x000FFFA0, # bark 0xE7FA9 + the same extra 0x17FF7
}
BARK_1001 = 0xE7FA9          # xnu_live_wdt_bark, frozen (25 s was 0xC7FB5)
WDT_HZ = 32765               # the record's own constant: 950,185 ticks = 29.0 s (1000 record S[2])
PERIOD_FALLBACK = 19200      # tick_hz 19,200,000 / MSM8974_TICK_HZ_DIV 1000
TVAL_SLACK = 512             # ticks the entry's two reads may sit apart
FULL_N = 64                  # USVC_FULL_N - the full-field window

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
    the reading is printed as a death reading and NEVER sets exit 1 (1000's five exit-1 items were
    all of this kind)."""
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
    """A frozen REBUILD signature (pre-reg (7)(a) / failure signature (c)): an absence that IS the
    death signature. Printed prominently and re-stated in the verdict; never counted, because no
    capture-side marker separates a 1001 image from a pre-1001 baseline."""
    SIGNATURES.append(msg)
    print("  [SIGNATURE] " + msg)


def say(tag, msg=""):
    print(("  [%s] " % tag) + msg if msg else ("  [%s]" % tag))


class Capture:
    def __init__(self, path):
        # `newline=""` + `split("\n")`: NO universal-newline translation. The captures carry lone CR
        # bytes; a text-mode read folds each into a line break and the `@:line` numbers come out off
        # the project's grep/wc numbering after the first one (measured on the 999 capture: the cap
        # tombstone reads @:28781 translated vs @:28714 in records/999-press-read.txt). Line numbers
        # are the reader's property (m997); this reader is the 1000 tool's, unchanged.
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


def authority_line():
    """Which 1001 pre-registration this run's expectations can be checked against - by value, at run
    time. The tool must not pretend a frozen text exists (mi4-self-written-record-is-not-a-constraint),
    and it must say which one it found: the frozen block's (3) gate is a THREE-way conjunction while
    the 1001 tool's task named the trio alone, so where they differ the output must show both."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "scripts", "press_1001.sh")
    try:
        with open(p, "r", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return ("authority: scripts/press_1001.sh is NOT frozen yet (absent at run time) - the "
                "expectations below are the ones handed to the 1001 rung (records/1000-press-read.txt, "
                "S Givens (a)-(f)); RE-CHECK them against the pre-registration when it lands", False)
    has = "1001 PRESS PRE-REGISTRATION" in text
    return ("authority: scripts/press_1001.sh (%d bytes, read at run time)%s" % (
        len(text), " - the frozen block IS embedded (its (3) gate is the three-way conjunction this "
        "tool evaluates; its (5) pet reading and its (7)(a)/(c) signatures are followed verbatim)"
        if has else " - but the frozen 1001 PRESS PRE-REGISTRATION block is NOT in it: the "
        "expectations below are the task's, and a pre-reg deviation must be REPORTED"), has)


def s0_gate(cap):
    """[0] THE DEATH GATE - the toggle every absence-based verdict below is routed through.

    The task named the trio as the survival toggle; `scripts/press_1001.sh` (3) FREEZES a three-way
    conjunction (no panic + trio&probes + records after the exec-returned point). Both are evaluated
    and printed; the EFFECTIVE gate is the frozen conjunction, and a disagreement is REPORTED (from
    where the tool knows the difference, the trio-only case, the tool's default is the task's)."""
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
    # The exec ENTRY quartet: presence-based, reported by value (1000 record S[5] verified it against
    # 999's @19406-09). Its presence with the trio absent IS the death signature this rung removes.
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
    say("OBS", "gate conjuncts (frozen pre-reg (3)): (i) no panic = %s; (ii) trio + 5 probes = %s; "
               "(iii) records after the exec-returned point = %s" % (
                   GATE_CONJ["i_no_panic"], GATE_CONJ["ii_trio_and_probes"],
                   GATE_CONJ["iii_records_after_exec"]))
    print("  " + "-" * 96)
    # The CLAUSE the 1001 task named is printed verbatim either way (its wording is the task's).
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
    # flipped - the lead adjudicates): "the capture is a death" and "the trio did not reach the image"
    # are different findings with the same shape.
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
    print("\n== IDENTITY / the 1000-1001 instrument census ==")
    cap_hdr = cap.all("xnu_live_cap")
    if cap_hdr:
        vals = sorted(set(t[3] for t in cap_hdr))
        say("OBS", "xnu_live_cap header: %s%s" % (", ".join(fmt(v) for v in vals),
            "  (the 999/1000/1001 cap; 0x4000 = a pre-999 image)" if vals == [0x6000] else ""))
        if 0x4000 in vals:
            hard("xnu_live_cap=0x00004000 - a PRE-999 image; not the 1001 arm")
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
    """The feed gate's evidence, BY VALUE, with the reading it is and is not.

    The gate is `count >= g_wdt_bark_ticks / 2u` inside the SAME call that publishes the sample
    (entry_trace.c:2234-2251), so:
      * a published sample AT or past the gate PROVES the boot reached the feedable moment, and (in
        the same call, unless the death landed between the two writes) that the branch fired;
      * a small largest sample proves NOTHING - the publish gate is `pow2(calls)` (:2237), which the
        999 capture shows stopping early (4 samples, largest 2.10 s, on a boot that reached user mode).
    """
    bark = cap.first("xnu_live_wdt_bark")
    bark_v = bark[3] if bark else BARK_1001
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
            "the frozen constant %s (the key is ABSENT)" % fmt(BARK_1001),
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
    print("\n== [2] THE DIRECT CHANNEL + pet feed (1001: zero feeds is read against the GATE, bark/2) ==")
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
    # ---- THE VERDICT THIS RUNG FLIPPED: zero feeds read against the feed gate, not against the door.
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
                 "%s) with ZERO xnu_live_wdt_pet_* records (%s). The frozen pre-reg (5) calls this "
                 "'the door stopped or the gate moved: a READING, not an assumed defect', so it is "
                 "reported SOFT with the numbers. To make it a clean death-vs-door reading, look for a "
                 "PUBLISHED xnu_live_wdt_countdown sample at/past bark/2 (none above); the sample "
                 "publish gate is pow2(calls), so a sample below the threshold cannot prove the gate "
                 "was never reachable. (1000's (6)(b) 'zero feed with the door used => rebuild' was "
                 "REFUTED by the timeline, not satisfied.)" % (
                     dur(ev["maxd_s"], ev["tick_hz"]), dur(ev["half_s"], WDT_HZ), door))
        else:
            soft("zero pet-feed records - SOFT, by the evidence printed above: no published sample and "
                 "no masked window ever showed the count past bark/2 (%s). This is the 1000 record "
                 "S[2] reading ('the first feedable moment is 4.2 s after the death'), NOT the "
                 "pre-reg's (5) 'the door stopped or the gate moved'. It stays soft because the sample "
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
        # The exec-returned POINT is the last of the trio AND its five probe keys (the probe keys are
        # written with the trio - pre-reg (3) names them together).
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
    """[3] THE 1001 RUNG'S OWN PROBE - the skip branch's bounded kprintf, read by value."""
    print("\n== [3] THE SUBMAP SKIP (the rung itself: the probe that names the callers by measurement) ==")
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
        # (7)(a): a death AT THE SAME CALL. The NAME is the discriminator, and the panic stanza shows
        # it in THIS capture - but the tool cannot know whether a 1001 image produced it, so this is
        # printed as a signature and left to the lead (an exit 0 that a signature can accompany
        # is why the tool re-states it in the verdict).
        same_call = [(ln, line.strip()) for ln, line in enumerate(cap.lines, 1)
                     if re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line)
                     or re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line[::2])]
        if total == 0 and same_call:
            signature("(7)(a)/failure signature (a): a panic naming the SAME call (hw_lock_lock / "
                      "vm_map_pmap_enter / vm_map_enter) with SUBMAPSKIP ABSENT - if this capture is a "
                      "1001 image the skip did not reach it: REBUILD, do not re-adjudicate. @:%d: %s" % (
                          same_call[0][0], same_call[0][1]))
        elif total == 0:
            signature("SUBMAPSKIP x0 with the exec-done trio absent - the frozen (7)(a) shape is 'a "
                      "death at the same call with SUBMAPSKIP absent'. The 1000 capture's panic stanza "
                      "names hw_lock_lock; whether THIS capture is a 1001 image cannot be read from the "
                      "log, so the reach verdict is the lead's (no capture-side marker separates a "
                      "1001 image from a pre-1001 baseline)")


def s4_usvc(cap):
    print("\n== [4] THE USER-SVC FRONTIER (last _n = the last syscall the user side managed) ==")
    fields = ("num", "pid", "thr", "pc", "lr", "cpsr", "r0")
    recs = cap.groups_of("xnu_live_usvc_n", "xnu_live_usvc_", fields)
    n_tok = cap.family_count("xnu_live_usvc_")
    if not recs:
        if n_tok:
            soft("xnu_live_usvc_ tokens x%d but no `_n` record - the record shape broke; the reader "
                 "cannot walk the frontier" % n_tok)
        # ---- 1001'S NEW EXPECTATION: the boot survives the exec-return, so user mode is expected.
        if GATE_OPEN:
            hard("no user-SVC records while the boot survived the exec-return - the frontier did not "
                 "reach user mode (check cs_invalid_page ordering before concluding)")
        else:
            hard("no user-SVC records while the exec-done trio is ABSENT - the frontier is NOT judged "
                 "(death reading: 1000 died in kernel mode, before any user SVC existed)", absence_based=True)
        # The context the message asks for, BY VALUE: what else in THIS capture says user mode happened.
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
                       "then INSTRUMENT-side, not frontier-side. Read the usvc switches off the arm's "
                       "record before calling the frontier failed.")
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


def s6_guards(cap):
    print("\n== [6] REGRESSION GUARDS (absence counts only while the death gate is OPEN) ==")
    for lit, n, want_txt in (("IOS7LAB cache mapping", 4, "the wire's four mapping lines"),
                             ("IOS7LAB shared region", 1, "the geometry line, exactly once"),
                             ("check_np ret=12", 1, "the pre-438 constant")):
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
        hard("map_slide ret=%s - STOPS per pre-reg (5): the slide regressed, STOP" % ret)
    mf = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_file kr=(-?\d+)', cap.text)]
    if any(v != 0 for v in mf):
        hard("map_file kr!=0 - GEOMETRY REGRESSION (G1 held kr=0); stop and re-model")
    if not mf:
        hard("no `map_file kr=` - the G1 reading is absent while the boot survived (the call is on "
             "the same exec path as the skip): report, do not model", absence_based=True)
    say("OBS", "map_file kr= values: %s" % (mf if mf else "none"))
    # ---- THE FLIP: 1000 expected VMHINT991 x2; 1001 expects 0 while the boot survives.
    n991 = cap.lit_count("VMHINT991")
    objs = re.findall(r'VMHINT991: obj=(0x[0-9a-fA-F]+)', cap.text)
    if GATE_OPEN:
        if n991 == 0:
            say("PASS", "VMHINT991 x0 - the FLIPPED 1001 expectation: the submap call is skipped, so "
                        "the guard at the far end of it never runs")
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
        say("PASS" if bark[3] == BARK_1001 else "OBS", "xnu_live_wdt_bark=%s (frozen %s)" % (
            fmt(bark[3]), fmt(BARK_1001)))
    slept = sum(1 for line in cap.lines if "mini4: the CPU slept" in line)
    usb = cap.family_count("xnu_live_usb_")
    say("OBS" if (slept or usb) else "PASS", "USB/pcx keys: `xnu_live_usb_*` x%d, "
        "`mini4: the CPU slept` lines x%d (expected 0/0 - the kernel never idles on this rung)" % (
            usb, slept))


def s7_falsifiers(cap):
    """The frozen 1001 pre-reg (7)(a)-(c), and the 1000 pre-reg's (6)(a)-(c) as 1001 reads them.

    (6)(a)/(6)(b) have become the GATE-OPEN branches of [4] and [2]: one finding, one hard anomaly -
    reported here by value, never re-fired. (7)(a) is the death-at-the-same-call signature (printed in
    [3]); (7)(c) is the frontier-advanced reading, whose evidence is the last direct keys and the text
    channel's last stanza - both printed here."""
    print("\n== [7] FALSIFIERS (frozen 1001 (7)(a)-(c); the 1000 (6)(a)-(c) re-mapped) ==")
    ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
    # ---- (7)(a): the death AT THE SAME CALL. The signature is printed in [3] (the absence and the
    # panic name belong together); here it is restated as the falsifier's own predicate.
    sc = [(ln, line.strip()) for ln, line in enumerate(cap.lines, 1)
          if re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line)
          or re.search(r'hw_lock|vm_map_pmap_enter|vm_map_enter', line[::2])]
    n_sub = cap.lit_count(SUBMAPSKIP_LIT)
    say("OBS", "(7)(a) death at the same call: panic naming hw_lock_lock / vm_map_pmap_enter / "
        "vm_map_enter = %s; SUBMAPSKIP x%d => %s" % (
            ("YES @:%d" % sc[0][0]) if sc else "no", n_sub,
            "SIGNATURE (see [3]): if this capture is a 1001 image the skip did not reach it - "
            "REBUILD" if (sc and n_sub == 0) else
            "the signature does not fire" if n_sub else "no panic naming the call in this capture"))
    # ---- (7)(b): VMHINT991 > 0 while the gate holds - the predicate lives in [6].
    n991 = cap.lit_count("VMHINT991")
    say("OBS", "(7)(b) VMHINT991 x%d while the gate is %s => %s" % (
        n991, "OPEN" if GATE_OPEN else "CLOSED",
        "adjudicated in [6] (one finding, one hard anomaly)" if (n991 and GATE_OPEN) else
        "does not fire"))
    # ---- (7)(c): death ELSEWHERE, later than the 1000 point => the frontier ADVANCED.
    ph = cap.panic_hits()
    if ph:
        say("OBS", "(7)(c) the text channel's last panic stanza @:%d: %s" % (ph[0][0], ph[0][1]))
    tail = [l for l in cap.lines[-6:] if l.strip()]
    say("OBS", "(7)(c) the capture's last lines (the death stanza, by value): %s" % (
        " | ".join(clip(l.strip(), 90) for l in tail)))
    for fam, why in (("xnu_live_usvc_", "the user-SVC frontier"), ("xnu_live_wdt_pet_", "the last feed"),
                     ("xnu_live_tick_anom_", "the last anomaly"), ("xnu_live_exec_done_", "the exec-done point")):
        t = next((x for x in reversed(cap.toks) if x[1].startswith(fam)), None)
        say("OBS" if t else "---", "(7)(c) last DIRECT key of %s (%s): %s" % (
            fam, why, ("%s=%s @:%d" % (t[1], t[2], t[0])) if t else "(none)"))
    # ---- the 1000 pre-reg's (6)(a)-(c), re-mapped.
    trio_any = any(cap.first(k) for k in TRIO)
    usvc = cap.family_count("xnu_live_usvc_")
    if usvc == 0:
        premise = trio_any and bool(ret) and all(v == 0 for v in ret)
        say("OBS", "(6)(a) `xnu_live_usvc_n` ABSENT: premise (exec trio AND `map_slide ret=0` present) "
                   "= %s%s" % ("TRUE" if premise else "FALSE",
            " - adjudicated in [4] (the 1001 gate-open USVC expectation), NOT re-fired here: one "
            "finding, one hard anomaly" if premise else
            " - the premise is false, so (6)(a) does not fire (1000 record S[3]: the same reading)"))
    else:
        say("OBS", "(6)(a) does not apply: usvc records x%d exist" % usvc)
    pets = cap.family_count("xnu_live_wdt_pet_")
    if pets == 0:
        say("OBS", "(6)(b) zero pet-feed records: 1000's record S[2] refuted the frozen sentence BY "
                   "THE TIMELINE (the feed gate is bark/2 and the boot died before it); the frozen 1001 "
                   "(5) reads it as 'the door stopped or the gate moved: a READING, not an assumed "
                   "defect' - adjudicated in [2], not re-fired here")
    else:
        say("OBS", "(6)(b) does not apply: pet records x%d exist" % pets)
    usvc_num0 = [t for t in cap.all("xnu_live_usvc_num") if t[3] == 0]
    n_usvc = len(cap.all("xnu_live_usvc_n"))
    if n_usvc and len(usvc_num0) == n_usvc:
        say("OBS", "(6)(c) ALL usvc _num = 0 - adjudicated in [4] (one finding, one hard anomaly)")
    else:
        say("OBS", "(6)(c) moot: usvc `_num` records x%d, of them 0-valued x%d" % (
            len(cap.all("xnu_live_usvc_num")), len(usvc_num0)))


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    cap = Capture(sys.argv[1])
    print("1001 capture adjudicator - %s (%d bytes, %d lines)" % (
        sys.argv[1], cap.nbytes, len(cap.lines)))
    auth, frozen = authority_line()
    print(auth)
    print("spec read here: STAGE90_XNU_SUBMAP_PMAP_SKIP=1, probe `%s obj=%%p start=%%p end=%%p "
          "size=%%p lr=%%p` (max %d hits)" % (SUBMAPSKIP_LIT, PROBE_MAX_HITS))
    s0_gate(cap)
    s_identity(cap)
    s1_tick(cap)
    s2_direct_pet(cap)
    s3_submapskip(cap)
    s4_usvc(cap)
    s5_context(cap)
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
        print("  !! NO FROZEN 1001 PRE-REGISTRATION was found at run time - the expectations above "
              "are the task's (see [0]/[7] for what that changes); a deviation here is to REPORT")
    if SIGNATURES:
        print("  !! %d REBUILD-CLASS SIGNATURE(S) present (not counted, and not a pass either):" %
              len(SIGNATURES))
        for s in SIGNATURES:
            print("    ! " + clip(s, 200))
        print("     An exit 0 here means 'the gate did not count it', NOT 'the rung worked': on the "
              "1001 arm these are the pre-reg's own REBUILD signatures - confirm the arm identity "
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