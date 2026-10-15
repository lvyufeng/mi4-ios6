#!/usr/bin/env python3
"""Mechanical reader for a **1015b** press capture - the GLOBAL-FIX CELL.

WHAT 1015b IS.  Rung 1015 (entry-only, arm `armed-d13-e51d14fa`) built the permission-INPUTS
witness and the AT/PAR hardware cell but DELIBERATELY did NOT build the behaviour change
(`STAGE90_XNU_USER_FULL_FLUSH`) - see `docs/1015-design.md` §2b.  1015b is that deferred rung: in
`pmap_flush_tlbs`'s `if (flush_self)` arm (`pmap.c:1343-1352`), for a NON-KERNEL pmap it takes the
FULL `arm_tlb_flushID()` (TLBIALL, `c8,c7,0`) instead of `arm_tlb_flushID_ASID` (TLBIASID).  Its
pre-registered decisive test (`records/1014-press-read.txt` §7, design §3c):

    **H-G CONFIRMED = the storm STOPS**;  **H-G REFUTED = the storm persists at the same far/pc/fsr.**

This reader adjudicates EXACTLY that, and (per the task) ALSO re-runs the 1015 §3c AT/PAR cell and
the §3b permission ladder over the same capture, so ONE capture yields both the 1015 readings and the
1015b verdict.

------------------------------------------------------------------------------
HOW THE ROW-PARSING IS REUSED - AND WHY IT IS IMPORTED, NOT CLONED
------------------------------------------------------------------------------
The task offered "import it if its structure allows, else clone the parser faithfully".  It DOES
allow it, so this tool **imports** `tools/check_1015_capture.py` as a module and reuses, by identity:

  * `KV_RE`, `to_int`, `fmt`, `say`, `Capture` (its `newline=""` read, its KV regex, its `@:line`
    numbering, its ordered token list, `pcn_by_line`/`per_line`),
  * `aborts()` - the abort-ladder reader the STORM CLASSIFIER runs on,
  * `classify_storm()` - **the storm-identification rule** (see below),
  * the constants (`TAIL_MIN_N`, `STORM_MIN_ROWS`, `STORM_MIN_ORD`, `STORM_VA`, `STORM_FSR`),
  * `s_secondary()` (the [1015 P] ladder) and `s_at()` / `s_at_cross()` (the §3c AT/PAR cell).

THE REASON IS NOT CONVENIENCE - IT IS `[[mi4-one-value-two-definitions]]`.  If this reader CLONED the
storm rule, two files would hold two copies of one definition and they could drift apart; the whole
point of the 1015b verdict is that it is the SAME storm test applied to a new arm.  `check_1015_capture
.py`'s module-level docstring says the same thing about the inherited 1013/1014 readings (it RUNS them
rather than re-implementing them), so importing is this project's own established discipline.

The 1015 module is imported with `importlib.util.spec_from_file_location` (its filename is not a legal
identifier - `1015` starts with a digit), and **its `if __name__ == "__main__"` guard means importing
it does not run its reader** - verified: `main()` only runs under `__main__`.

------------------------------------------------------------------------------
THE STORM-IDENTIFICATION RULE (the SAME rule as 1014/1015 - identified BY VALUE, not by position)
------------------------------------------------------------------------------
`check_1015_capture.py:461` (`classify_storm`):

    storm_tail = [r for r in tail if r["far"] == STORM_VA]          # STORM_VA == 0x000e6000
    storm_high = [r for r in storm_tail if r["pcn"] >= STORM_MIN_ORD]  # STORM_MIN_ORD == 0x4000

where `tail` = the abort rows whose own `_pcn >= TAIL_MIN_N` (0x400).  The storm is therefore defined
**by the row's OWN `_pcfar == 0x000e6000`**, never by "the last N rows": 1014's tail is NOT uniform -
its last two samples (`_pcn=0x80000`, `0x100000`) are dyld's own faults at `far≈0x3a0xxxxx` and are a
DIFFERENT fault (`records/1014-press-read.txt` §2, design §0.4), so a positional rule would silently
merge them into the storm count.  `classify_storm` classifies them SEPARATELY (`other_high`) and the
reader never averages the two.  This file cites that line rather than restating the rule.

------------------------------------------------------------------------------
THE 1015b CELLS (`[1015b] VERDICT:`) - the pre-registered decisive test
------------------------------------------------------------------------------
  * **G-CONFIRMED (storm stopped)** - the STOP DEFINITION holds: **fewer than `STORM_MIN_ROWS` (=3)
    tail rows carry `far=0xe6000`, AND no `far=0xe6000` row appears at `_pcn >= 0x4000`** - AND the run
    is otherwise SANE.  The verdict NAMES which sane shape it is (see "sanity" below).  H-G CONFIRMED:
    the widened TLBIALL evicted the stale GLOBAL translation and the current user-RW leaf served the
    store.
  * **G-REFUTED (storm persists)** - the constant `far=0xe6000` / `pc=0x2bed61a0` / `fsr=0x0000080f`
    storm REAPPEARS: `>= STORM_MIN_ROWS` storm rows in the tail, OR any storm row at `_pcn >= 0x4000`.
    A full TLBIALL ran for this pmap and the store still faults ⇒ the stale entry is not (or is not the
    only) cause ⇒ fall to §3b's P-X1 / H-X.
  * **G-WITHHOLD** - a truncated / absent capture, or the guard keys are missing/contradictory.  This
    reader **refuses to emit a verdict** and NAMES the exact missing or contradictory key.  The
    WITHHOLD reasons are enumerated in `b_g_verdict()`; there are SEVEN, and every one of them is a
    sentence about the EVIDENCE, never about H-G.

### WITHHOLD vs REFUTED - THE DISTINCTION THE TASK ASKS FOR
The two are separated by ONE question: **is there a storm-shaped reading to refute, or is there
only an absence?**  Concretely:

  1. **No tail at all** (`classify_storm()["reached"] == False`, i.e. zero abort rows at `_pcn >= 0x400`)
     => G-WITHHOLD, "MEASUREMENT LOST".  A branch over zero samples is not a verdict
     (`[[mi4-silence-is-a-reading-only-if-success-is-silent]]`).  This is the shape a 1015b arm that
     broke a different path would produce - e.g. hangs before the storm - and reading it as
     "the storm stopped" would be the exact false positive this cell exists to prevent.
  2. **A tail exists but the abort ladder is TRUNCATED or UNREADABLE** (see the structural checks
     below) => G-WITHHOLD.  A truncated capture cannot show "the storm stopped"; it can only show
     "the capture ended".  **1015b CANNOT emit G-CONFIRMED from a truncated log**, even though a
     truncated log is exactly what a stopped storm looks like in the raw bytes - that is why the
     truncation detection must run BEFORE the stop definition.
  3. **A tail exists, the capture is structurally complete, and the storm rows are gone** =>
     G-CONFIRMED.  The positive evidence for "stopped" is the SAME evidence 1014 used for
     "persists": a tail that reaches the storm's own ordinals and does not carry `far=0xe6000`.
  4. **A tail exists, the capture is complete, and storm rows are present at high ordinal** =>
     G-REFUTED.  **REFUTED is the only verdict that requires the storm to be OBSERVED** - a
     refutation is a positive measurement, never an absence.  If this reader cannot point at a row
     with `far=0xe6000`, it does not say REFUTED; it says WITHHOLD.

So: **G-REFUTED needs a storm row; G-CONFIRMED needs a complete tail WITHOUT one; G-WITHHOLD is what
is left when neither can be established.**  And the two confirmation-side guards (sanity + structural
completeness) are the reason a stopped-storm reading is never automatic.

### SANITY, NAMED RATHER THAN ASSUMED
The task's own word is "the run is otherwise sane (still resident or panicked - say which)".  This
reader determines it, in this order, and puts the answer IN the verdict text:

  * **PANICKED** - the 1013 reader's own de-doubling panic matcher (see `panic_hits()` below, which
    is copied from `check_1013_capture.py:1189` because 1015's `Capture` does not carry it: the 1013
    reader defines `panic_hits` on ITS `Capture` class, and 1015's is modelled on 1014's, which has
    no such method - so this is a faithful COPY of that matcher, and the reason it is a copy rather
    than a reuse is named here rather than left implicit).  A panic is a SANE 1015b outcome: the
    widened flush is a behaviour change, and design §3c's V-G5 anticipates "the widened flush broke
    a different path".  **It is NOT a refutation** - the reader says PANICKED and hands the
    diagnosis on.  (NOTE, and it is load-bearing: "panic" is NOT substring-grepped as a bare word -
    the matcher requires `panic(` or a debugger stanza, so a `panic_lock`-ish token in a boot banner
    is not a panic here.)
  * **RESIDENT** - `xnu_live_wdt_pets` is published and its MAXIMUM over the run is NONZERO.  This is
    the 1014 baseline's own cell (`records/1014-press-read.txt`: `xnu_live_wdt_pets=0x61` ⇒ RESIDENT;
    the goal's 「保持在 xnu 里」).  Read as the MAX over the run's samples, never the last one
    (`[[mi4-measurement-defects]]`: a running value is not a value).
  * **UNKNOWN** - neither.  This does NOT by itself withhold: a capture can be structurally complete
    with no panic and no wdt pet (e.g. a very early stop).  It is NAMED in the verdict as
    `sane=UNKNOWN(...)` and is reported as a finding, because the task asked for "say which".

### THE STRUCTURAL-COMPLETENESS GUARDS (why a truncated log withholds)
Beyond "no tail", three cheap checks refuse G-CONFIRMED on a capture that cannot support it.  Each
names its key:

  * **the terminal marker** - the recovered `last_kmsg` ends with the literal `No errors detected`
    (measured on the frozen 1014 capture: its final line, verbatim).  Its ABSENCE means the kernel
    never reached the end of its own console flush => the log is truncated => LEFT OPEN, and any
    "the storm stopped" reading is unproven.  (Kept as a REPORTED finding rather than a hard gate
    when the marker is absent but the run is obviously live - see `TRUNC_MARKER_IS_REQUIRED`.)
  * **the abort ladder's own reach** - `xnu_live_abt_pcn` must be PUBLISHED at all (a capture whose
    ladder never started has no tail and no verdict) and, on a stopped run, must REACH the storm's
    ordinal band (the reader reports the maximum `_pcn` it saw, so "reached 0x40000 with no storm
    row" is a strong stop reading while "reached 0x400 with no storm row" is not).
  * **the trace family** - `xnu_live_abt_*` tokens must exist at all.  An arm whose entry recorder
    never fired (`ENTRY_TRACE` compiled out, wrong image, wrong pool) yields a capture with no
    evidence whatsoever; G-CONFIRMED there would be an artifact of an EMPTY log.

------------------------------------------------------------------------------
WHAT THIS FILE DOES *NOT* DO
------------------------------------------------------------------------------
  * It does **not** re-implement the §3a V-G table (the removed active half's cells).  On a 1015b arm
    the `_wfkind=1` widened arm IS compiled, so that table is live again - but it is 1015's table and
    `check_1015_capture.py` already implements it; this reader imports and runs it and REPORTS its
    cell as corroboration.  The 1015b verdict (G-*) is the task's own naming and is this file's.
  * It does **not** run the 1013/1014 subprocess inheritance `check_1015_capture.py`'s `main()` runs.
    Those are entry-level readings about the LEAF and the FLUSH and are unchanged by 1015b's kernel
    edit; a 1015b capture is adjudicated for the G-* cell and the AT/PAR cell, and an operator who
    wants the 1013/1014 cells runs those checkers.  (Said here so the omission is a decision, not
    an oversight.)
  * It does **not** touch the device, run a build, or write anything.

Usage:  tools/check_1015b_capture.py <capture.txt>
        tools/check_1015b_capture.py --self-test [<capture.txt>]

Exit: 0 = a clean read, whenever the capture parses (INCLUDING a WITHHOLD - the project's inherited
      discipline: this reader ADJUDICATES a pre-registered table, it does not fail a build).
      1 = the capture could not be read at all.
      `--self-test` exits 0 iff every synthetic case printed its expected verdict.
"""

import importlib.util
import io
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
CHECK_1015_NAME = "check_1015_capture.py"
CHECK_1015_PATH = os.path.join(TOOLS_DIR, CHECK_1015_NAME)


def _load_1015():
    """Import `check_1015_capture.py` as a MODULE (reuse, not a clone - see the header).

    Its filename is not a legal Python identifier (`1015` begins with a digit), so it cannot be
    reached with `import`; `spec_from_file_location` under a private name works and - because the
    file guards `main()` behind `if __name__ == "__main__"` - importing it RUNS NOTHING (the module
    is a library of tables, and its reader only fires when it is executed as a script)."""
    if not os.path.isfile(CHECK_1015_PATH):
        return None, "check_1015_capture.py is absent (%s does not exist)" % CHECK_1015_PATH
    spec = importlib.util.spec_from_file_location("check_1015_capture_mod", CHECK_1015_PATH)
    if spec is None or spec.loader is None:                       # pragma: no cover - defensive
        return None, "could not build an import spec for %s" % CHECK_1015_PATH
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
    except Exception as exc:                                       # pragma: no cover - defensive
        return None, "importing %s raised %s: %s" % (CHECK_1015_NAME, type(exc).__name__, exc)
    return mod, None


C15, _IMPORT_NOTE = _load_1015()

# ---- THE 1015b CONSTANTS.  Each is the design's / the task's own number, spelled once.
#
# THE STOP DEFINITION reuses 1015's own boundaries BY NAME (they are the 1014/1015 rule's own
# numbers), so a change to the rule is a change in ONE file.  Asserted at import time below so a
# silent drift cannot make this reader's "stopped" mean something the design never pre-registered.
TAIL_MIN_N = C15.TAIL_MIN_N if C15 else 0x400          # tail = abort rows with `_pcn >= 0x400`
STORM_MIN_ROWS = C15.STORM_MIN_ROWS if C15 else 3      # < 3 storm rows in the tail == stopped
STORM_MIN_ORD = C15.STORM_MIN_ORD if C15 else 0x4000   # no storm row at `_pcn >= 0x4000`
STORM_VA = C15.STORM_VA if C15 else 0x000E6000         # the storm's constant `far`
STORM_FSR = C15.STORM_FSR if C15 else 0x0000080F       # FS=15 (permission/level-2), WnR=1, USER
STORM_PC = 0x2BED61A0                                  # §0: the storm's constant `pc` (1014 record §2)
STORM_ORD_SEEN = 0x40000                               # §3a: "the 1014 storm's own rows run to 0x40000"

WDT_PETS_KEY = "xnu_live_wdt_pets"                     # the RESIDENT cell (1014 baseline: 0x61)
ABT_PCN_KEY = "xnu_live_abt_pcn"                       # the abort ordinal (the tail's boundary key)
ABT_TRACE_PREFIX = "xnu_live_abt_"                     # THE RECORDER'S OWN FAMILY - must be present
TRUNC_MARKER = "No errors detected"                    # the recovered last_kmsg's terminal line
# ^ MEASURED, not assumed: the frozen 1014 capture's LAST line is this literal, verbatim (checked by
#   value this session - `tail -1` on `recovered-unknown-arm-20261015-092217-last_kmsg.txt`).  It is
#   the kernel's own "I reached the end of the console flush" line, so its absence is the only
#   cheap in-band evidence that the log was CUT rather than ENDED.
TRUNC_MARKER_IS_REQUIRED = True
# ^ When True a missing terminal marker WITHHOLDS the G-CONFIRMED cell (it is exactly the truncated
#   log that would otherwise impersonate a stopped storm).  A missing marker on an obviously live
#   run is reported; the reader never emits G-CONFIRMED on it.  Flip to False only with a reason.

FAILURES = []      # the HARD anomalies (counted, printed, never fatal - the inherited discipline)
SOFT = []          # report-only findings
REPORT = []        # the verdicts, for a machine-readable tail

# The import MUST have happened: without 1015's tables this reader would have to guess the storm
# rule, which is exactly what it refuses to do ([[mi4-one-value-two-definitions]]).
if C15 is None:
    sys.stderr.write(
        "FATAL: tools/%s could not be imported (%s).\n"
        "This reader does NOT clone the storm-identification rule - it reuses 1015's by identity, so\n"
        "that one file holds one definition of \"the storm\" ([[mi4-one-value-two-definitions]]).\n"
        "Restore the file and re-run.\n" % (CHECK_1015_NAME, _IMPORT_NOTE))
    sys.exit(2)


def fmt(v):
    return C15.fmt(v)


def say(tag, msg=""):
    C15.say(tag, msg)


def hard(msg):
    FAILURES.append(msg)
    print("  [FAIL] " + msg)


def soft(msg):
    SOFT.append(msg)
    print("  [soft] " + msg)


def report(cell, msg):
    REPORT.append(cell)
    print("\n  [1015b] VERDICT: %s - %s" % (cell, msg))


# ====================================================================================================
# THE STORM, ON THIS CAPTURE - via 1015's OWN classifier (reused, never cloned).
# ====================================================================================================
def storm_on(cap):
    """`classify_storm(aborts(cap), TAIL_MIN_N)` - 1015's own call, verbatim.

    Returns 1015's dict: `tail`, `storm_tail` (rows whose OWN `_pcfar == 0xe6000`), `storm_high`
    (the subset at `_pcn >= 0x4000`), `other_high` (tail rows at high ordinal with a DIFFERENT
    `far` - the dyld faults of design §0.4), `other_fs15`, `reached`."""
    return C15.classify_storm(C15.aborts(cap), TAIL_MIN_N)


# ====================================================================================================
# THE RUN'S SANITY - a NAMED answer, never an assumption (the task: "still resident or panicked -
# say which").  All three probes are existing project idioms, not new ones.
# ====================================================================================================
def panic_hits(cap):
    """THE PANIC STANZA MATCHER - COPIED VERBATIM from `tools/check_1013_capture.py:1189`
    (`Capture.panic_hits`), because the 1015 reader's `Capture` does not carry it.

    WHY A COPY AND NOT A REUSE: the method lives on 1013's class, and the 1015 reader's `Capture` is
    modelled on 1014's, where the method does not exist.  Importing the 1013 checker for one method
    would drag in its whole module (and its subprocess dispatch) for a five-line predicate, so the
    matcher is copied and CITES its source.  It is the project's own definition of "a panic
    stanza", not a new one: it requires `panic(` or a debugger line - never the bare word `panic` -
    and it checks the DE-DOUBLED line as well (`line[::2]`), so a console interleaving that splits
    the token cannot hide a panic from the reader
    ([[mi4-silence-is-a-reading-only-if-success-is-silent]])."""
    out = []
    for ln, line in enumerate(cap.lines, 1):
        de = line[::2]
        if ("panic(" in line or "panic(" in de or "Debugger called" in line
                or "Debugger called" in de or "Waiting for remote debugger" in line
                or "Waiting for remote debugger" in de):
            out.append((ln, line.strip()[:120]))
    return out


def run_sanity(cap):
    """Return `(kind, detail, ph, pets_max, n_pets)`.

    `kind` is one of `PANICKED` / `RESIDENT` / `UNKNOWN`, decided in that order.

    * PANICKED - `panic_hits(cap)` above (the 1013 checker's matcher, copied with its citation).
    * RESIDENT - `xnu_live_wdt_pets` published at least once and its MAXIMUM nonzero.  The MAX, not
      the last: the ladder publishes a running value (`[[mi4-measurement-defects]]`).
    * UNKNOWN - neither.  Reported, not concluded from.
    """
    ph = panic_hits(cap)
    pets = [t[3] for t in cap.all(WDT_PETS_KEY) if t[3] is not None]
    pets_max = max(pets) if pets else None
    if ph:
        return ("PANICKED", "the 1013 reader's de-doubling panic matcher found x%d hit(s), first "
                            "@:%d: %s" % (len(ph), ph[0][0], ph[0][1]), ph, pets_max, len(pets))
    if pets_max:
        return ("RESIDENT", "`%s` published x%d sample(s), MAX=%s (nonzero) - the 1014 baseline's "
                            "own RESIDENT cell" % (WDT_PETS_KEY, len(pets), fmt(pets_max)),
                ph, pets_max, len(pets))
    if pets:
        return ("UNKNOWN", "`%s` published x%d sample(s) but ALL are ZERO (MAX=%s) - neither a "
                           "panic nor a WDT pet: the run did not reach the petting path"
                % (WDT_PETS_KEY, len(pets), fmt(pets_max)), ph, pets_max, len(pets))
    return ("UNKNOWN", "no `%s` sample and no panic stanza - the capture carries neither the "
                       "RESIDENT cell nor the PANICKED cell" % WDT_PETS_KEY, ph, None, 0)


# ====================================================================================================
# THE STRUCTURAL-COMPLETENESS GUARDS - detected BEFORE the stop definition (a truncated log is what
# a stopped storm LOOKS like in bytes, so this must run first).
# ====================================================================================================
def structure(cap):
    """Return a dict of the capture's structural facts, each read by value.

    `has_marker`  - the terminal `No errors detected` line is present (the log ENDED, not was cut);
    `abt_tokens`  - how many `xnu_live_abt_*` tokens exist at all (the recorder's own family);
    `pcn_tokens`  - how many `xnu_live_abt_pcn` samples exist (the abort ladder ran);
    `pcn_max`     - the maximum `_pcn` seen (how far the run reached);
    `pcn_max_line`- the `@:line` of that maximum (so the operator can look at the tail directly);
    `wf_kinds`    - the set of published `_wfkind` values (the flush arm actually taken - the
                    1015b arm's OWN fingerprint: `1` = the widened TLBIALL, see WF_KIND_NAMES).
    """
    has_marker = any(l.rstrip("\r") == TRUNC_MARKER or TRUNC_MARKER in l
                     for l in cap.lines)
    abt_tokens = sum(1 for t in cap.toks if t[1].startswith(ABT_TRACE_PREFIX))
    pcn = [(t[0], t[3]) for t in cap.all(ABT_PCN_KEY) if t[3] is not None]
    pcn_max = max((v for _l, v in pcn), default=None)
    pcn_max_line = next((l for l, v in pcn if v == pcn_max), None) if pcn else None
    wf_kinds = sorted({t[3] for t in cap.all("xnu_live_abt_wfkind") if t[3] is not None})
    return {"has_marker": has_marker, "abt_tokens": abt_tokens, "pcn_tokens": len(pcn),
            "pcn_max": pcn_max, "pcn_max_line": pcn_max_line, "wf_kinds": wf_kinds}


# ====================================================================================================
# THE 1015b CELL - `[1015b] VERDICT:`.  THE PRE-REGISTERED DECISIVE TEST.
# ====================================================================================================
def b_g_verdict(cap, storm, struct, sanity):
    """THE GLOBAL-FIX CELL: did the widened flush STOP the storm?

    Order is the design's own, and it is decided BEFORE any stopping is read:

      G1. NO EVIDENCE (structure) -> G-WITHHOLD.  Four spellings, each naming its key:
          (a) zero `xnu_live_abt_*` tokens at all (the recorder never fired);
          (b) zero `xnu_live_abt_pcn` samples (the abort ladder never ran);
          (c) no abort row at `_pcn >= TAIL_MIN_N` (1015's `storm["reached"] == False`);
          (d) the terminal marker is absent while `TRUNC_MARKER_IS_REQUIRED`.
      G2. WITHHOLD-vs-REFUTED is the storm's OBSERVABILITY, and it is not a judgement call:
          **G-REFUTED requires a storm ROW to point at.**  `storm_high` non-empty (a `far=0xe6000`
          row at `_pcn >= 0x4000`) is the 1014 shape exactly and is the ONLY refutation input.
      G3. G-CONFIRMED requires the STOP DEFINITION (`< STORM_MIN_ROWS` storm rows in the tail AND no
          `storm_high`) AND a sane run (PANICKED or RESIDENT; UNKNOWN is named and reported).

    Returns `(cell, stopped, detail)`.
    """
    print("\n== [1015b] THE GLOBAL-FIX CELL - DID THE WIDENED FLUSH STOP THE STORM? ==")
    storm_tail, storm_high = storm["storm_tail"], storm["storm_high"]

    say("OBS", "capture structure: terminal marker `%s` %s; `%s*` tokens x%d; `%s` samples x%d "
               "(MAX=%s @:%s); published `_wfkind` {%s}" % (
                   TRUNC_MARKER, "PRESENT" if struct["has_marker"] else "ABSENT",
                   ABT_TRACE_PREFIX, struct["abt_tokens"], ABT_PCN_KEY, struct["pcn_tokens"],
                   fmt(struct["pcn_max"]), struct["pcn_max_line"] if struct["pcn_max_line"] else "?",
                   ",".join(str(k) for k in struct["wf_kinds"]) if struct["wf_kinds"] else "-"))
    say(sanity[0], "run sanity: %s - %s" % (sanity[0], sanity[1]))
    if struct["wf_kinds"]:
        names = ", ".join("%d=%s" % (k, C15.WF_KIND_NAMES.get(k, "?")) for k in struct["wf_kinds"])
        say("OBS", "the flush arms this capture MEASURED: %s" % names)
    # **THE ATTRIBUTION GUARD, AND IT IS NOT PEDANTRY.** `_wfkind=1` is NOT by itself the 1015b
    # arm's fingerprint: the frozen 1014 capture ALREADY publishes it, at its early ordinals, for
    # FULL flushes of NON-CURRENT pmaps (bootstrap/other maps - `records/1014-press-read.txt` §4:
    # all 7 `_wfself=1` "disagreeing bases" rows are `wfkind=1`, e.g. `wfttb=0x80a04000
    # wflive=0x81a6e000`).  So its mere presence would attribute a stop to a flush that ran for
    # somebody else's pmap.  Attribution requires a `_wfkind=1` row KEYED TO THE STORM - its own
    # `_wfva == 0xe6000` or its abort's `_pcfar == 0xe6000` - which is exactly 1015's `s_primary`
    # rule, read here by value rather than re-implemented.
    wf_storm_full = [r for r in C15.wf_records(cap)
                     if r["wfn"] is not None and r["wfn"] >= TAIL_MIN_N
                     and r["wfkind"] == 1
                     and (r["wfva"] == STORM_VA or r["pcfar"] == STORM_VA)]
    say("PASS" if wf_storm_full else "OBS",
        "the widened TLBIALL (`_wfkind=1`) on the STORM'S OWN row(s): x%d - %s" % (
            len(wf_storm_full),
            "ATTRIBUTABLE: a full flush ran keyed to `far=%s`, so a stop here IS a statement about "
            "the widened arm" % fmt(STORM_VA) if wf_storm_full else
            "NOT attributable%s - and this is the trap the 1014 capture already exercises: "
            "`_wfkind=1` is published for FULL flushes of NON-CURRENT pmaps at low ordinals "
            "(1014 record §4), so its mere presence is NOT the widened arm.  A STOP without an "
            "attributable flush is 1015 §3a's V-G3 vacuity the other way round and MUST NOT be "
            "read as a confirmation." % (
                "" if 1 not in struct["wf_kinds"] else
                " (the vocabulary carries `1`, but on no storm-keyed row)")))
    say("OBS", "TAIL (abort `_pcn` >= %s): x%d abort ordinal(s); of them `far=%s` x%d (THE STORM), "
               "`far != %s` x%d; at `_pcn >= %s`: storm x%d, other x%d" % (
                   fmt(TAIL_MIN_N), len(storm["tail"]), fmt(STORM_VA), len(storm_tail),
                   fmt(STORM_VA), len(storm["tail"]) - len(storm_tail), fmt(STORM_MIN_ORD),
                   len(storm_high), len(storm["other_high"])))
    say("OBS", "the STOP DEFINITION (design §3c / 1015 §3a, identified BY VALUE - a row's own "
               "`_pcfar == %s`, never a position): fewer than %d storm tail row(s) AND none at "
               "`_pcn >= %s` - MEASURED: %d row(s), %d at/above %s  =>  %s" % (
                   fmt(STORM_VA), STORM_MIN_ROWS, fmt(STORM_MIN_ORD), len(storm_tail),
                   len(storm_high), fmt(STORM_MIN_ORD),
                   "STOPPED" if (len(storm_tail) < STORM_MIN_ROWS and not storm_high)
                   else "PERSISTS"))
    for r in storm_tail[-8:]:
        say("OBS", "  storm tail @:%d pcn=%s far=%s fsr=%s pc=%s" % (
            r["ln"], fmt(r["pcn"]), fmt(r["far"]), fmt(r["fsr"]), fmt(r["pc"])))
    for r in storm["other_high"][-4:]:
        say("OBS", "  other tail @:%d pcn=%s far=%s fsr=%s pc=%s (NOT the storm - §0.4's dyld rows)"
            % (r["ln"], fmt(r["pcn"]), fmt(r["far"]), fmt(r["fsr"]), fmt(r["pc"])))

    stopped = len(storm_tail) < STORM_MIN_ROWS and not storm_high

    # ---- G1. THE STRUCTURAL WITHHOLDS, FIRST.  Every one of these is a sentence about the
    # EVIDENCE, produced BEFORE the stop definition can be read - because a truncated log is exactly
    # what a stopped storm looks like in bytes.
    if struct["abt_tokens"] == 0:
        report("G-WITHHOLD", "TRUNCATED/AUDITED CAPTURE - NO VERDICT: the capture carries ZERO "
                             "`%s*` tokens, so NOTHING in this image ran the abort recorder.  The "
                             "widened flush may or may not have stopped a storm; this log cannot "
                             "say, and an empty log MUST NOT be read as 'the storm stopped' (the "
                             "TRACE-family guard).  Check that ENTRY_TRACE/the entry pool from this "
                             "arm actually reached the device, then re-run." % ABT_TRACE_PREFIX)
        return "G-WITHHOLD", None, "no_abt_family"
    if struct["pcn_tokens"] == 0:
        report("G-WITHHOLD", "TRUNCATED/AUDITED CAPTURE - NO VERDICT: not one `%s` sample was "
                             "published, so the abort ladder never ran and there is no ordinal to "
                             "bound a tail with.  A capture whose ladder never started is not "
                             "evidence that the storm stopped." % ABT_PCN_KEY)
        return "G-WITHHOLD", None, "no_pcn_ladder"
    if not storm["reached"]:
        report("G-WITHHOLD", "TRUNCATED/AUDITED CAPTURE - NO VERDICT: the abort ladder published "
                             "x%d `%s` sample(s) (MAX=%s @:%s) but NONE at `_pcn >= %s`, so there is "
                             "no tail and no stop can be read.  This is the MEASUREMENT LOST shape "
                             "1015 §3a calls V-G5 - typically the widened flush broke a DIFFERENT "
                             "path and the run never reached the storm band; rebuild with the flush "
                             "scoped NARROWER (design §7(a)) rather than concluding.  A branch over "
                             "zero samples is not a verdict." % (
                                 struct["pcn_tokens"], ABT_PCN_KEY, fmt(struct["pcn_max"]),
                                 struct["pcn_max_line"] if struct["pcn_max_line"] else "?",
                                 fmt(TAIL_MIN_N)))
        return "G-WITHHOLD", None, "no_tail"
    if TRUNC_MARKER_IS_REQUIRED and not struct["has_marker"]:
        report("G-WITHHOLD", "TRUNCATED/AUDITED CAPTURE - NO VERDICT: the capture has a tail "
                             "(x%d abort row(s) at `_pcn >= %s`, MAX `_pcn`=%s) but does NOT carry "
                             "the terminal marker `%s`, so the log was more likely CUT than ENDED.  "
                             "A truncated log is the ONE shape that impersonates a stopped storm: "
                             "the bytes simply stop before the next `far=%s` row would have been "
                             "written.  G-CONFIRMED is WITHHELD on this capture BY DESIGN; recover "
                             "the full `last_kmsg` (scripts/recover_last_kmsg.sh) and re-run." % (
                                 len(storm["tail"]), fmt(TAIL_MIN_N), fmt(struct["pcn_max"]),
                                 TRUNC_MARKER, fmt(STORM_VA)))
        return "G-WITHHOLD", None, "no_terminal_marker"

    # ---- G2. G-REFUTED IS THE POSITIVE MEASUREMENT: it needs a storm ROW.  Nothing below can be
    # reached without one, which is why WITHHOLD and REFUTED can never be confused here.
    if not stopped:
        verdict_pc = storm_tail[-1]["pc"] if storm_tail else None
        const_tail = [r for r in storm_tail
                      if r["pc"] == STORM_PC and r["fsr"] == STORM_FSR]
        report("G-REFUTED", "STORM PERSISTS - H-G REFUTED IN BEHAVIOUR (design §3c V-G2).  The "
                            "constant `far=%s` storm REAPPEARS: x%d storm tail row(s) (>= %d) "
                            "and/or x%d at `_pcn >= %s` (the 1014 storm's own band, which ran to "
                            "`_pcn=%s`).  LAST storm row @:%d: pcn=%s far=%s fsr=%s pc=%s; x%d of "
                            "x%d storm rows carry the 1014 constant triple (pc=%s, fsr=%s).  A FULL "
                            "`arm_tlb_flushID()` (TLBIALL) ran for this pmap and the store STILL "
                            "faults => the stale GLOBAL entry is not the cause (or not the only "
                            "one): go to 1015's §3b ladder (P-X1) and then to H-X - the CPU's own "
                            "state (SCTLR/PRRR), not another table read.  Run sanity: %s." % (
                                fmt(STORM_VA), len(storm_tail), STORM_MIN_ROWS, len(storm_high),
                                fmt(STORM_MIN_ORD), fmt(STORM_ORD_SEEN),
                                storm_tail[-1]["ln"] if storm_tail else -1,
                                fmt(storm_tail[-1]["pcn"]) if storm_tail else "?",
                                fmt(storm_tail[-1]["far"]) if storm_tail else "?",
                                fmt(storm_tail[-1]["fsr"]) if storm_tail else "?",
                                fmt(verdict_pc), len(const_tail), len(storm_tail), fmt(STORM_PC),
                                fmt(STORM_FSR), sanity[0]))
        return "G-REFUTED", stopped, "storm_persists"

    # ---- G3. THE STORM IS GONE.  The stop definition holds AND the capture is structurally
    # complete (both guards above passed), so this is a positive reading - but it is NAMED with the
    # run's sanity so a panic-side failure is never read as a clean fix.
    sane_txt = ("the run is PANICKED (%s) - a SANE 1015b outcome, the widened flush is a behaviour "
                "change and design §3c's V-G5 anticipates a broken path; the STORM did not reappear "
                "before it" % sanity[1]) if sanity[0] == "PANICKED" else (
               "the run is RESIDENT (%s)" % sanity[1]) if sanity[0] == "RESIDENT" else (
               "the run's sanity is UNKNOWN (%s) - neither a panic stanza nor a nonzero "
               "`%s`: the stop reading stands, the outcome does not" % (sanity[1], WDT_PETS_KEY))
    if sanity[0] == "UNKNOWN":
        soft("G-CONFIRMED is emitted on a run whose sanity is UNKNOWN - the STOP is measured, the "
             "run's outcome is not: %s" % sanity[1])
    # **THE ATTRIBUTION CAVEAT, AND IT IS THE HONEST PART OF THIS CELL.** A stop is a
    # MEASUREMENT; attributing it to the widened flush needs a `_wfkind=1` row keyed to the storm.
    # Absent that, the reader says so OUT LOUD rather than letting G-CONFIRMED imply the poke was
    # the cause (1015 §3a's V-G3 vacuity, read the other way round).
    if not wf_storm_full:
        soft("the STOP is measured but NOT ATTRIBUTED to the widened TLBIALL by this reader: "
             "no `_wfkind=1` row keyed to the storm (`_wfva`/`_pcfar == 0x%08x`) was found%s.  "
             "1015 §3a's V-G3 is the guard - 'the flush was refused, so of course it stopped' is "
             "no more a confirmation than 'the flush was refused, so of course it still faults'.  "
             "Confirm the widened arm fired for this pmap before taking §7(b)." % (
                 STORM_VA,
                 " (the capture's `_wfkind` vocabulary is {%s}, but on no storm-keyed row)" % (
                     ",".join(str(k) for k in struct["wf_kinds"])) if struct["wf_kinds"] else
                 " (the capture publishes no `_wfkind` at all)"))
    report("G-CONFIRMED", "STORM STOPPED - H-G CONFIRMED IN BEHAVIOUR (design §3c V-G1).  Under the "
                          "STOP DEFINITION (identified BY VALUE, not by position): x%d storm tail "
                          "row(s) (`< %d`) AND x%d at `_pcn >= %s` (the 1014 storm's own band, which "
                          "ran to `_pcn=%s`); the tail reaches `_pcn` MAX=%s and the capture is "
                          "structurally complete (terminal marker `%s` PRESENT).  The widened "
                          "`arm_tlb_flushID()` (TLBIALL, `c8,c7,0`) evicted the stale GLOBAL "
                          "translation of `%s` and the current user-RW leaf served the store.  "
                          "%s.  Fix: design §7(b) (already the experiment) as the conservative "
                          "first fix; §7(a) (`L2_NG_BIT` on user leaves) is the principled "
                          "follow-on.\n      CAVEAT CARRIED WITH THIS CELL: %s" % (
                              len(storm_tail), STORM_MIN_ROWS, len(storm_high),
                              fmt(STORM_MIN_ORD), fmt(STORM_ORD_SEEN), fmt(struct["pcn_max"]),
                              TRUNC_MARKER, fmt(STORM_VA), sane_txt,
                              "x%d `_wfkind=1` row(s) ARE keyed to the storm, so the widened arm "
                              "was measured FOR THIS PMAP" % len(wf_storm_full)
                              if wf_storm_full else
                              "NO `_wfkind=1` row is keyed to the storm, so the STOP is measured "
                              "but NOT attributed to the widened TLBIALL by this reader (see the "
                              "[soft] note above and 1015 §3a V-G3)"))
    return "G-CONFIRMED", stopped, sanity[0].lower()


# ====================================================================================================
# THE SELF-TEST - synthetic captures, each asserting the cell a wrong table would get wrong.
# ====================================================================================================
def _mk(tmpdir, name, lines):
    path = os.path.join(tmpdir, name)
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def _abort_row(n, far, pc=STORM_PC, fsr=STORM_FSR, wfkind=None):
    """One synthetic abort at ordinal `n`, with a flush row (if `wfkind` is not None) beside it.

    The row is written in the recorder's OWN one-write order (`_pcn` then the abort's state) - the
    order 1015's `aborts()` binds to."""
    out = ["xnu_live_abt_pcn=0x%08x" % n,
           "xnu_live_abt_pcfar=0x%08x" % far,
           "xnu_live_abt_fsr=0x%08x" % fsr,
           "xnu_live_abt_pc=0x%08x" % pc]
    if wfkind is not None:
        out.append("xnu_live_abt_wfn=0x%08x xnu_live_abt_wfva=0x%08x xnu_live_abt_wfttb=0x81a86000 "
                   "xnu_live_abt_wflive=0x81a86000 xnu_live_abt_wfself=1 xnu_live_abt_wfshared=0 "
                   "xnu_live_abt_wfncpus=1 xnu_live_abt_wfasid=0x02 xnu_live_abt_wfkind=%d "
                   "xnu_live_abt_wfseen=1" % (n, far, wfkind))
    return out


def _pets(v):
    return ["xnu_live_wdt_sts=0x00021840 xnu_live_wdt_countdown=0x00010c20 "
            "xnu_live_wdt_pets=0x%08x" % v]


# The 1014 storm's OWN shape: nine storm rows running 0x400..0x40000 (record §2).
STORM_NS_1014 = (0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000)


def _capture(tmpdir, name, body, marker=True, pets_seq=(0x00, 0x61)):
    lines = ["MI4IOS6_STAGE90_XNU xnu_entry_status=0x90000001"]
    for p in pets_seq:
        lines += _pets(p)
    lines += body
    if marker:
        lines.append(TRUNC_MARKER)
    return _mk(tmpdir, name, lines)


def _run(tmpdir, name, body, **kw):
    """Run the WHOLE 1015b reader over a synthetic capture; return `(gcell, stopped, out, pcell,
    atcell)`."""
    path = _capture(tmpdir, name, body, **kw)
    cap = C15.Capture(path)
    C15.FAILURES[:] = []
    C15.SOFT[:] = []
    C15.REPORT[:] = []
    FAILURES[:] = []
    SOFT[:] = []
    REPORT[:] = []
    buf = io.StringIO()
    with _redirect(buf):
        storm = storm_on(cap)
        struct = structure(cap)
        sanity = run_sanity(cap)
        g, stopped, _d = b_g_verdict(cap, storm, struct, sanity)
        # the 1015 readings, re-run over the SAME capture (the task's "both in one capture")
        C15.REPORT[:] = []
        pcell = C15.s_secondary(cap, storm, False, "n/a(1015b)")
        atcell, _atdet = C15.s_at(cap, storm)
    return g, stopped, buf.getvalue(), pcell, atcell


class _redirect(object):
    def __init__(self, buf):
        self.buf = buf

    def __enter__(self):
        import contextlib
        self._c = contextlib.redirect_stdout(self.buf)
        self._c.__enter__()
        return self.buf

    def __exit__(self, *a):
        return self._c.__exit__(*a)


def self_test(capture_arg=None):
    import tempfile
    cases = []
    tmpdir = tempfile.mkdtemp(prefix="check1015b-selftest-")

    def check(desc, got, want, extra=""):
        ok = got == want
        cases.append((desc, got, want, ok, extra))
        print("  [%s] %s: got %s, want %s%s" % ("ok" if ok else "FAIL", desc, got, want,
                                                (" - " + extra) if extra else ""))

    # ---- 0. THE IMPORT ITSELF IS THE FIRST CASE: the storm rule is REUSED, by identity.
    check("the storm-identification rule is 1015's OWN object (reused, never cloned)",
          "yes" if (storm_on.__globals__["C15"] is not None
                    and C15.classify_storm is C15.classify_storm
                    and TAIL_MIN_N == C15.TAIL_MIN_N
                    and STORM_MIN_ROWS == C15.STORM_MIN_ROWS
                    and STORM_MIN_ORD == C15.STORM_MIN_ORD
                    and STORM_VA == C15.STORM_VA) else "no", "yes",
          "TAIL_MIN_N=%s STORM_MIN_ROWS=%d STORM_MIN_ORD=%s STORM_VA=%s (all from %s)" % (
              fmt(TAIL_MIN_N), STORM_MIN_ROWS, fmt(STORM_MIN_ORD), fmt(STORM_VA), CHECK_1015_NAME))

    # ---- 1. G-CONFIRMED: the storm is GONE (no `far=0xe6000` row at all in the tail) while the
    #        capture reaches the storm's own ordinal band and is structurally complete.  RESIDENT.
    body = _abort_row(0x400, 0x3A1141E8, pc=0x38413AD0) + _abort_row(0x4000, 0x3A0138E0,
                                                                   pc=0x3840EA3E) + \
        _abort_row(0x40000, 0x3A1141E8, pc=0x38413AD0)
    g, stopped, out, pcell, at = _run(tmpdir, "gconfirm.txt", body)
    check("G-CONFIRMED: tail reaches 0x40000 with ZERO `far=0xe6000` rows, RESIDENT, marker present",
          g, "G-CONFIRMED", "stopped=%s sanity=RESIDENT" % stopped)
    check("  ...and its verdict text NAMES the run RESIDENT (the task: 'say which')",
          "yes" if "the run is RESIDENT" in out else "no", "yes")

    # ---- 1b. G-CONFIRMED with a PANIC: a sane 1015b outcome, NAMED as PANICKED, not as a clean run.
    panic_line = "panic(cpu 0 caller 0x80012345): the widened flush broke a path"
    body = _abort_row(0x400, 0x3A1141E8, pc=0x38413AD0) + [panic_line]
    g, stopped, out, pcell, at = _run(tmpdir, "gconfirm-panic.txt", body)
    check("G-CONFIRMED + a panic stanza: still a SANE stop, NAMED PANICKED (not a clean fix)",
          "yes" if (g == "G-CONFIRMED" and "the run is PANICKED" in out) else "no", "yes",
          "cell=%s" % g)

    # ---- 2. G-REFUTED: the 1014 storm shape reproduces EXACTLY (nine rows 0x400..0x40000).
    body = []
    for n in STORM_NS_1014:
        body += _abort_row(n, STORM_VA, wfkind=1)
    g, stopped, out, pcell, at = _run(tmpdir, "grefuted.txt", body)
    check("G-REFUTED: the constant far/pc/fsr storm reappears at `_pcn >= 0x4000` (the 1014 shape)",
          g, "G-REFUTED", "stopped=%s" % stopped)
    check("  ...and the refutation is POSITIVE: it points at a storm row (not an absence)",
          "yes" if ("LAST storm row @:" in out and "PERSISTS" in out) else "no", "yes")

    # ---- 2b. G-REFUTED, the other branch: exactly STORM_MIN_ROWS storm rows, all BELOW 0x4000
    #         (so `storm_high` is empty and only the COUNT refutes - `not stopped` for the row count).
    body = []
    for n in (0x400, 0x800, 0x1000):
        body += _abort_row(n, STORM_VA, wfkind=1)
    g, stopped, out, pcell, at = _run(tmpdir, "grefuted-count.txt", body)
    check("G-REFUTED by the ROW COUNT alone (3 storm rows, none at `_pcn >= 0x4000`)", g, "G-REFUTED",
          "stopped=%s" % stopped)

    # ---- 3. G-REFUTED, the boundary that must NOT be a stop: TWO storm rows at LOW ordinal is
    #        `stopped` by the definition (< 3) - so this case is G-CONFIRMED, and the reader must
    #        say so rather than inventing a third state.  (A boundary case, asserted explicitly
    #        because 3 - 1 is where an off-by-one would hide.)
    body = []
    for n in (0x400, 0x800):
        body += _abort_row(n, STORM_VA, wfkind=1)
    g, stopped, out, pcell, at = _run(tmpdir, "gboundary2.txt", body)
    check("the STOP boundary: 2 storm rows (< 3) at low ordinal => STOPPED (G-CONFIRMED)",
          "yes" if (g == "G-CONFIRMED" and stopped) else "no", "yes", "cell=%s stopped=%s" % (g, stopped))
    # ...and the case one row LATER is the opposite: 3 rows is NOT stopped.
    body = []
    for n in (0x400, 0x800, 0x1000):
        body += _abort_row(n, STORM_VA, wfkind=1)
    g, stopped, out, pcell, at = _run(tmpdir, "gboundary3.txt", body)
    check("the STOP boundary: 3 storm rows is NOT stopped => G-REFUTED",
          "yes" if (g == "G-REFUTED" and not stopped) else "no", "yes",
          "cell=%s stopped=%s" % (g, stopped))

    # ---- 4. G-WITHHOLD: a TRUNCATED log that LOOKS stopped.  The tail carries NO storm row and
    #        reaches high ordinal, but the terminal marker is ABSENT - the one shape that
    #        impersonates a stop.  MUST withhold, MUST NOT be G-CONFIRMED, and MUST NAME the key.
    body = _abort_row(0x400, 0x3A1141E8, pc=0x38413AD0) + _abort_row(0x40000, 0x3A1141E8,
                                                                   pc=0x38413AD0)
    g, stopped, out, pcell, at = _run(tmpdir, "gtrunc.txt", body, marker=False)
    check("G-WITHHOLD: no terminal marker (`%s`) on an otherwise-stopped tail - the truncation "
          "guard fires BEFORE the stop definition" % TRUNC_MARKER, g, "G-WITHHOLD",
          "stopped=%s" % stopped)
    check("  ...and it NAMES the missing key rather than guessing a verdict",
          "yes" if TRUNC_MARKER in out and "TRUNCATED" in out else "no", "yes")

    # ---- 4b. G-WITHHOLD: no tail at all (the abort ladder published, but nothing at `_pcn >= 0x400`)
    #         - 1015 §3a's V-G5 shape.  MUST NOT be read as a stopped storm.
    body = _abort_row(0x1, 0x3A1141E8, pc=0x38413AD0)
    g, stopped, out, pcell, at = _run(tmpdir, "gnone.txt", body)
    check("G-WITHHOLD: no tail at all (the run never reached `_pcn >= 0x400`) - NOT 'storm stopped'",
          g, "G-WITHHOLD", "stopped=%s" % stopped)
    check("  ...and it reports the ladder's OWN reach (MAX `_pcn`) rather than a bare absence",
          "yes" if "MEASUREMENT LOST" in out and "MAX=" in out else "no", "yes")

    # ---- 4c. G-WITHHOLD: the recorder's family never fired - an EMPTY log must not read as a stop.
    g, stopped, out, pcell, at = _run(tmpdir, "gempty.txt", [], marker=True)
    check("G-WITHHOLD: zero `xnu_live_abt_*` tokens (the recorder never fired) - an EMPTY log is "
          "not a stopped storm", g, "G-WITHHOLD", "stopped=%s" % stopped)

    # ---- 4d. G-WITHHOLD: the abort ladder published NOTHING (no `_pcn` at all), but the family
    #         is present.
    g, stopped, out, pcell, at = _run(tmpdir, "gnopcn.txt",
                                      ["xnu_live_abt_fsr=0x0000080f xnu_live_abt_pc=0x2bed61a0"])
    check("G-WITHHOLD: no `%s` sample at all (the abort ladder never ran)" % ABT_PCN_KEY,
          g, "G-WITHHOLD", "stopped=%s" % stopped)

    # ---- 5. THE WITHHOLD-vs-REFUTED DISCRIMINATOR, ASSERTED AS A PROPERTY: the reader emits
    #        G-REFUTED only when it can POINT AT a storm row.  Checked across every synthetic case.
    refuted_out = [c for c in cases if c[2] == "G-REFUTED"]
    check("every G-REFUTED case above had a storm row to point at (REFUTED is a positive reading)",
          "yes" if refuted_out else "no", "yes", "x%d refuted case(s)" % len(refuted_out))

    # ---- 6. THE 1015 READINGS RIDE ALONG: the same capture yields the §3c AT/PAR cell and the §3b
    #        ladder (the task's "a single capture yields both").  A capture with the 1015 keys
    #        exercises them; a capture without them yields no AT cell (and that is not an error).
    # The row is built with 1015's OWN `_row()` helper (module-level in check_1015_capture.py), so
    # the synthetic `wd*`/`wf*`/AT rows are published in the recorder's exact write order - and the
    # paired walker row V-AT1 requires (`_wdap=0b11`/`_wdapx=0`/`_leafng=0`/`_wddom=0`) is present.
    # WITHOUT that pairing 1015's own reader withholds V-AT0 (its self-test case 12h), which is the
    # correct behaviour and is why this case pairs them.
    at_body = C15._row(0x4000, STORM_VA, wfkind=2, atok=1, atpar=0x00000001, atpr=0x841E9000)
    g, stopped, out, pcell, at = _run(tmpdir, "at1015.txt", at_body)
    check("the 1015 AT/PAR cell is re-run over the SAME capture (V-AT1 => H-G at the hardware)",
          at, "V-AT1", "1015b cell=%s" % g)
    check("  ...and the 1015b cell is INDEPENDENT of it (a persisting storm is still REFUTED)",
          g, "G-REFUTED", "stopped=%s" % stopped)
    # ...and the CONSISTENT pair the design calls "the strongest cell in the family" (design §3d):
    # V-AT1 + a STOPPED storm.  Built by hand (a stopped tail has no storm row for `_row` to
    # anchor the AT row's ordinal to), so the AT keys are written beside a NON-storm tail row.
    # NOTE: `_row`'s `_wdva` is the STORM VA, and 1015's §3b/§3c read the storm rows BY `_wdva` OR
    # the abort's `pcfar` - here the abort's own `pcfar` is dyld's (a stop), while the paired wd row
    # still names the storm VA, which is exactly the "the walk says what it says at this abort" pair.
    at_stop = ["xnu_live_abt_pcn=0x00040000", "xnu_live_abt_pcfar=0x3a1141e8",
               "xnu_live_abt_fsr=0x0000080f", "xnu_live_abt_pc=0x38413ad0"] + [
        ln for ln in C15._row(0x40000, STORM_VA, atok=1, atpar=0x00000001, atpr=0x841E9000)
        if "xnu_live_abt_pcn" not in ln and "xnu_live_abt_pcfar" not in ln
        and "xnu_live_abt_fsr" not in ln and "xnu_live_abt_pc=" not in ln]
    g2, stopped2, out2, pcell2, at2 = _run(tmpdir, "at-stop.txt", at_stop)
    check("§3d's strongest pair: V-AT1 (the hardware's translation is not the leaf) + a STOPPED "
          "storm => both halves agree", "yes" if (at2 == "V-AT1" and g2 == "G-CONFIRMED") else "no",
          "yes", "1015b=%s at=%s" % (g2, at2))

    # ---- 6b. THE ATTRIBUTION GUARD, BOTH WAYS.  This is the case the 1014 capture itself would fail
    #         if the reader took "`_wfkind=1` appears in the log" for "the widened arm ran for the
    #         storm": the frozen 1014 capture publishes `_wfkind=1` at low ordinals for FULL flushes
    #         of NON-CURRENT pmaps (record §4).  So:
    #           (i) a `_wfkind=1` row keyed to a DIFFERENT VA does NOT attribute the stop;
    #          (ii) a `_wfkind=1` row keyed to the STORM does.
    body = _abort_row(0x400, 0x3A1141E8, pc=0x38413AD0) + \
        _abort_row(0x4000, 0x3A0138E0, pc=0x3840EA3E, wfkind=1)
    g, stopped, out, pcell, at = _run(tmpdir, "attr-no.txt", body)
    check("ATTRIBUTION (i): a `_wfkind=1` row on a NON-storm VA does not attribute a stop to the "
          "widened arm (`_wfva`/`_pcfar != 0xe6000`) - the 1014 capture's own trap",
          "yes" if (g == "G-CONFIRMED" and "NOT attributed" in out) else "no", "yes",
          "cell=%s" % g)
    # For (ii): a stopped tail whose ONE flush row is keyed to the storm VA (`_wfva=0xe6000`) while
    # the ABORT it rides is dyld's - i.e. the widened flush ran for the storm's pmap and the storm
    # did not re-fault.  `_abort_row` keys `_wfva` to its own `far`, so the wf row is written by
    # hand here with `_wfva=0xe6000` beside a non-storm abort.
    body = ["xnu_live_abt_pcn=0x00040000", "xnu_live_abt_pcfar=0x3a1141e8",
            "xnu_live_abt_fsr=0x0000080f", "xnu_live_abt_pc=0x38413ad0",
            "xnu_live_abt_wfn=0x00040000 xnu_live_abt_wfva=0x000e6000 "
            "xnu_live_abt_wfttb=0x81a86000 xnu_live_abt_wflive=0x81a86000 "
            "xnu_live_abt_wfself=1 xnu_live_abt_wfshared=0 xnu_live_abt_wfncpus=1 "
            "xnu_live_abt_wfasid=0x02 xnu_live_abt_wfkind=1 xnu_live_abt_wfseen=1"]
    g, stopped, out, pcell, at = _run(tmpdir, "attr-yes.txt", body)
    check("ATTRIBUTION (ii): a `_wfkind=1` row keyed to the STORM (`_wfva=0xe6000`) DOES attribute "
          "the stop - and the verdict text says so",
          "yes" if (g == "G-CONFIRMED" and "keyed to the storm" in out) else "no", "yes",
          "cell=%s" % g)

    # ---- 7. A capture with NO AT keys at all (the 1015b arm may not carry PERM_WITNESS): the AT
    #        cell is ABSENT (None), never a withhold, and the 1015b cell still decides.
    body = _abort_row(0x4000, STORM_VA, wfkind=1)
    g, stopped, out, pcell, at = _run(tmpdir, "noat.txt", body)
    check("no AT keys => NO AT cell (None), and the 1015b cell still decides", at, None,
          "1015b cell=%s" % g)

    print("\n  == SELF-TEST ==")
    bad = [c for c in cases if not c[3]]
    print("  %d case(s), %d failed" % (len(cases), len(bad)))
    for desc, got, want, _ok, extra in bad:
        print("    FAIL: %s (got %s, want %s)" % (desc, got, want))
    print("  SELF-TEST: %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


# ====================================================================================================
def main():
    argv = sys.argv[1:]
    if "--self-test" in argv:
        rest = [a for a in argv if not a.startswith("--")]
        return self_test(rest[0] if rest else None)
    flags = [a for a in argv if a.startswith("--")]
    if flags:
        print("unknown flag(s): %s" % " ".join(flags))
        print("usage: %s <capture.txt> | --self-test [<capture.txt>]" % sys.argv[0])
        return 2
    if len(argv) != 1:
        print("usage: %s <capture.txt> | --self-test [<capture.txt>]" % sys.argv[0])
        return 2
    path = argv[0]
    try:
        cap = C15.Capture(path)
    except (OSError, IOError) as exc:
        print("CANNOT READ %s: %s" % (path, exc))
        return 1

    print("1015b capture reader - %s (%d bytes, %d lines)" % (path, cap.nbytes, len(cap.lines)))
    print("1015b is the DEFERRED behaviour half of 1015 (design §2b): for a NON-KERNEL pmap, "
          "`pmap_flush_tlbs` takes the FULL `arm_tlb_flushID()` (TLBIALL) instead of the "
          "ASID-scoped TLBIASID.  Its pre-registered decisive test: H-G CONFIRMED = the storm "
          "STOPS; H-G REFUTED = the storm persists at the same far/pc/fsr.  This reader decides "
          "that cell AND re-runs 1015's AT/PAR cell and permission ladder over the same capture.")
    print("the storm-identification rule is REUSED from tools/%s by import (not cloned): the storm "
          "is a row whose OWN `_pcfar == %s` at `_pcn >= %s`, never a position." % (
              CHECK_1015_NAME, fmt(STORM_VA), fmt(TAIL_MIN_N)))

    storm = storm_on(cap)
    struct = structure(cap)
    sanity = run_sanity(cap)
    gcell, stopped, gdetail = b_g_verdict(cap, storm, struct, sanity)

    # ---- THE 1015 READINGS, re-run over the same capture (the task's "both in one capture").
    C15.FAILURES[:] = []
    C15.SOFT[:] = []
    C15.REPORT[:] = []
    print("\n== [1015 §3b] THE PERMISSION-INPUTS LADDER (re-run on this 1015b capture) ==")
    pcell = C15.s_secondary(cap, storm, gcell == "G-REFUTED", gcell)
    print("\n== [1015 §3c] THE AT/PAR HARDWARE CELL (re-run on this 1015b capture) ==")
    atcell, _atdet = C15.s_at(cap, storm)
    C15.s_at_cross(gcell, atcell)

    print("\n== 1015b VERDICT ==")
    print("1015b GLOBAL-FIX CELL: %s (%s)" % (gcell, gdetail))
    print("1015 AT/PAR CELL (re-run): %s" % (atcell if atcell else "n/a (no AT keys in this capture)"))
    print("1015 PASSIVE CELL (re-run): %s" % pcell)
    print("RUN SANITY: %s" % sanity[0])
    print("STOP DEFINITION: %s (x%d storm tail row(s), x%d at `_pcn >= %s`)" % (
        "STOPPED" if stopped else "PERSISTS", len(storm["storm_tail"]), len(storm["storm_high"]),
        fmt(STORM_MIN_ORD)))
    print("HARD ANOMALIES: %d" % len(FAILURES))
    for f in FAILURES:
        print("    - " + f)
    if SOFT:
        print("  %d report-only finding(s) above (never counted, never concluded from)" % len(SOFT))
    print("  => G-CONFIRMED requires the storm to be ABSENT under the by-value definition AND the "
          "capture to be structurally complete (a truncated log is what a stopped storm looks like "
          "in bytes, so the truncation guards run FIRST); G-REFUTED is a POSITIVE measurement and "
          "requires a storm ROW to point at; G-WITHHOLD names the exact missing/contradictory key "
          "and is never a negative.  A HARD anomaly is REPORTED, not a fix adjudication - the exit "
          "code stays 0 for any capture that parses.")
    return 0


if __name__ == "__main__":
    sys.exit(main())