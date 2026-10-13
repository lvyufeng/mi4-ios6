#!/usr/bin/env python3
"""Mechanical adjudicator for the 1004 press capture - by value, and DEATH-AWARE.

MODELLED BYTE-STRUCTURALLY ON tools/check_1003_capture.py.  1004 adds ONE measurement - the ABORT
TRACE (switch `STAGE90_XNU_ABORT_TRACE`): the sleh (data/prefetch abort) detail caps are widened so
EVERY abort of a run publishes its full tuple, and the faulting frame's USER pc is published beside
it as `xnu_live_sleh_upc`.  It keeps 1003's stalled-frame sampler, 1002's r3-save fix and 1001's
submap skip as regressions.  The section that carries the new reading is `[A]`.

numpy-free: this tool imports only os / re / sys (like its 1003 sibling).

1003's baseline (answered by 1003, kept as background): 1003's per-tick sampler settled that pid 1 is
NOT a user spin and NOT a passive sleep - the last samples are ONE kernel frame (pc=0x8001b1fd,
sp=0xcfdbbf00, lr=sleh_abort+0x468), i.e. pid 1's thread 0xc0bfa000 is in the data-abort RETURN path.
Total abort entries that boot reached 40 (`xnu_live_sleh_seen`), but the sleh caps published only the
first 8, and those 8 name DIFFERENT `far` values (dyld walking forward).  The 1002 r3-save fix holds
(the 230x `usermode data abort at 0x4` is GONE) and `cs_invalid_page` is a TOLERATED warning.

[A] THE ABORT TRACE - THE 1004 RUNG ITSELF.  Reads the full ordered sleh sequence, one record per
abort (`seq` opens a record; `type`/`fsr`/`far`/`thr`/`recover` + the frame's `pc`/`lr`/`sp`/`cpsr`/
`fsr_frame`/`far_frame`/`frame_ok`/`user` + the NEW `upc`), and CLASSIFIES the `far` walk:
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

WHAT 1004 CHECKS (the frozen block in `scripts/press_1004.sh` is the authority; `authority_line()`
reads THAT file):
  [R] THE 1002 r3-SAVE FIX AS A REGRESSION (verbatim from 1003): after `IOS7SHREG check_np ret=12`,
      the count of `usermode data abort ... at 0x00000004` must STILL be 0.  A recurrence is a
      REGRESSION - REBUILD, not a reading.
  [A] THE ABORT TRACE - THE 1004 RUNG ITSELF (see above).
  [S] THE 1003 STALL SAMPLE - KEPT AS A REGRESSION.  1003's `xnu_live_stall_*` (7 keys) must STILL be
      present; its SPIN/ASLEEP reading is restated by value (adjudicated in the 1003 tool's [S]).
  [F] THE CODE-SIGNING PAGE-VALIDATION WALL - `cs_invalid_page` is a TOLERATED warning (retval 0),
      recorded ONCE in the 1002 capture, NOT the frontier.  Verbatim from 1003.
  (2) SUBMAPSKIP, (3) VMHINT991, (4) THE SURVIVAL GATE, (5) THE USER-SVC FRONTIER, (6) THE PET,
  (7) THE CAP AND THE TICK, (8) THE FROZEN REGRESSION GUARDS - all VERBATIM from 1003 (1004 changes
  none of them).

METHOD.  Every reader (the `newline=""` file read, the de-doubling panic reader, the KV regex, the
`@:line` numbering, `groups_of`, the death gate) is the 1001/1002/1003 tool's, key for key.  This
tool is `tools/check_1003_capture.py` with the ONE new section [A] and the 1004 authority path.

Usage:  tools/check_1004_capture.py <capture.txt>

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

# ---- The kept instrument families (the census discriminates a 1000..1004 arm from its baselines).
NEW_FAMILIES = (
    ("xnu_live_tick_anom_", "(1) the late-delivery anomalies"),
    ("xnu_live_tick_sum_",  "(1) the tick aggregate summaries"),
    ("xnu_live_wdt_pet_",   "(2) the pet feed records (direct)"),
    ("xnu_live_usvc_",      "(4) the user-SVC frontier"),
    ("xnu_live_stall_",     "(S) 1003's stall sampler (kept)"),
    ("xnu_live_sleh_upc",   "(A) 1004's abort trace (the NEW instrument)"),
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


def authority_line():
    """Which 1004 pre-registration this run's expectations can be checked against - by value, at run
    time ([[mi4-self-written-record-is-not-a-constraint]])."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "scripts", "press_1004.sh")
    try:
        with open(p, "r", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return ("authority: scripts/press_1004.sh is NOT frozen yet (absent at run time) - the "
                "expectations below are the ones handed to the 1004 rung; RE-CHECK them against the "
                "pre-registration when it lands", False)
    has = "1004 PRESS PRE-REGISTRATION" in text
    # A SKELETON pre-reg is not a frozen one: the team-lead removes the `# 1004-SKELETON` sentinel when
    # the real block lands (scripts/press_1004.sh refuses to press while it is present).
    if has and "# 1004-SKELETON" in text:
        return ("authority: scripts/press_1004.sh (%d bytes) - the block is a SKELETON (the "
                "`# 1004-SKELETON` sentinel is present): the expectations below are the task's, and a "
                "pre-reg deviation must be REPORTED" % len(text), False)
    return ("authority: scripts/press_1004.sh (%d bytes, read at run time)%s" % (
        len(text), " - the frozen block IS embedded" if has else
        " - but the frozen 1004 PRESS PRE-REGISTRATION block is NOT in it: the expectations below are "
        "the task's, and a pre-reg deviation must be REPORTED"), has)


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
    say("READING", "THE FRONTIER = the LAST abort @:%d seq=%s type=%s far=%s upc=%s thr=%s "
                   "frame_ok=%s user=%s" % (
        last["ln"], fmt(lf.get("seq", (0, None))[1]), fmt(lf.get("type", (0, None))[1]),
        fmt(lf.get("far", (0, None))[1]), fmt(lf.get("upc", (0, None))[1]),
        fmt(lf.get("thr", (0, None))[1]), fmt(lf.get("frame_ok", (0, None))[1]),
        fmt(lf.get("user", (0, None))[1])))
    say("OBS", "records that carry the FRAME band (pc/lr/sp/cpsr/frame_ok/user): x%d of x%d (the "
               "pre-1004 `SLEH_LIVE_MAX` hid all but the first %d); the running `_seen` count x%d" % (
        n_frame, len(recs), SLEH_OLD_MAX, n_seen))
    # ---- DID EVERY ABORT RETURN?  `xnu_live_sleh_back` is the wrapper's post-`sleh_abort` note,
    # written for every abort <= the FRAMES band (512 here). An abort with a `_seq` but NO
    # corresponding `_back` entered `sleh_abort` and never completed it - the death episode,
    # measured.  Counted over the same band so a >512 abort is not miscounted.
    n_back = cap.family_count("xnu_live_sleh_back")
    if n_back and n_back < len(recs):
        say("READING", "ABORTS THAT NEVER RETURNED: x%d - `xnu_live_sleh_back` x%d for x%d abort "
                       "records. The LAST abort (seq @:%d, far=%s, user=%s) entered `sleh_abort` and "
                       "never reached its end = THE FRONTIER (the fault the kernel died on)."
            % (len(recs) - n_back, n_back, len(recs), _last_ln,
               fmt(recs[-1]["f"].get("far", (0, None))[1]), fmt(recs[-1]["f"].get("user", (0, None))[1])))
    elif n_back:
        say("PASS", "every abort returned (`xnu_live_sleh_back` x%d == `_seq` x%d) - no single abort "
                    "is the frontier by non-return" % (n_back, len(recs)))
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


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    cap = Capture(sys.argv[1])
    print("1004 capture adjudicator - %s (%d bytes, %d lines)" % (
        sys.argv[1], cap.nbytes, len(cap.lines)))
    auth, frozen = authority_line()
    print(auth)
    print("spec read here: 1003's switch set VERBATIM (STALL=1, USVC=1, SUBMAP_PMAP_SKIP=1) + the ONE "
          "new 1004 switch STAGE90_XNU_ABORT_TRACE (the abort trace: raised sleh caps + `%s`, section "
          "[A]); the tolerated wall `%s` is NOT the frontier; the 1002 regression is ZERO `%s` after "
          "`%s`" % (SLEH_UPC_KEY, CS_INVALID_LIT, ABORT_LIT, CHECK_NP_LIT))
    s0_gate(cap)
    s_identity(cap)
    s1_tick(cap)
    s2_direct_pet(cap)
    s3_submapskip(cap)
    s4_usvc(cap)
    s5_context(cap)
    s_rung(cap)
    s_abort(cap)
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
        print("  !! NO FROZEN 1004 PRE-REGISTRATION was found at run time - the expectations above are "
              "the task's; a deviation here is to REPORT")
    if SIGNATURES:
        print("  !! %d REBUILD-CLASS SIGNATURE(S) present (not counted):" % len(SIGNATURES))
        for s in SIGNATURES:
            print("    ! " + clip(s, 200))
    conj = " AND ".join("%s->%s" % (k, "holds" if GATE_CONJ.get(k) else "FAILS") for k in sorted(GATE_CONJ))
    print("DEATH GATE: %s (frozen conjuncts: %s)" % ("OPEN" if GATE_OPEN else "CLOSED", conj))
    if FAILURES:
        print("  => STOP and report per the pre-reg's FAILURE SIGNATURES")
        return 1
    print("  no hard anomaly; readings above are the evidence")
    return 0


if __name__ == "__main__":
    sys.exit(main())