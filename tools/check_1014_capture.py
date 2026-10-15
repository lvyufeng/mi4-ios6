#!/usr/bin/env python3
"""Mechanical reader for the 1014 press capture - the MEASUREMENT rung that reads the TLB-FLUSH
WITNESS (`docs/1014-design.md` §2): WHICH flush arm ran, for WHICH pmap, under WHICH ASID, with
WHICH live TTBR0, and under which CONTEXTIDR the fault was taken.

1014 IS A PURE MEASUREMENT RUNG - IT INSTALLS NO FIX AND CHANGES NO KERNEL BEHAVIOUR (design §2.6).
The run is therefore EXPECTED to still freeze at the SAME ~24.4 s shape 1013 measured; a 1014 verdict
is a SUCCESSFUL READ, never a fix adjudication - a distinction that runs through every clause below.

MODELLED ON tools/check_1013_capture.py (its `Capture` reader, its `xnu_live_` KV regex, its ladder
rule `n <= 4 || n & (n-1) == 0`, its `@:line` numbering and its VERDICT/exit discipline).  This tool
does NOT re-implement the 1013 reading: 1014's capture IS ALSO a 1013 capture (the same [WIT] leaf
witness rows are in it), so the 1013 checker is RUN AS A SUBPROCESS over the SAME capture and its
`== VERDICT ==` block is re-printed under a `[LEAF via 1013]` header.

[WF] THE 1014 TLB-FLUSH WITNESS - the NEW section (the ONE `[WF] VERDICT:` line).

TWO RECORDERS PUBLISH THE ROWS (`src/entry/entry_stubs.c`):
  * `entry_note_abt_wf` (called from `pmap_flush_tlbs`, keyed on the CURRENT fault's `far` -
    1013's `g_abtpte_curva`).  Keys: `_wfn` (the ladder ordinal, OPENS the row), `_wfva` (`startv`),
    `_wfttb` (`pmap->pm_l1_phys`), `_wflive` (the in-loop `armreg_ttbr_read() & 0xFFFFFF00`, RAW),
    `_wfself` (`flush_self`: 0 = the flush did NOT run for this pmap), `_wfshared`, `_wfncpus`
    (`pmap_asid_ncpus` at runtime), `_wfasid` (`pmap->pm_asid & 0xFF`), `_wfkind`
    (0 = NO FLUSH RAN / 1 = `arm_tlb_flushID()` full / 2 = `arm_tlb_flushID_ASID`), `_wfseen`
    (THE NULL CONTROL: 1 iff the loop's `pmap.c:1295` TTBR read executed), plus the change-count
    `_wfnx` (DISTINCT `(self,kind,asid)` triples, written WHERE IT CHANGES, OUTSIDE the ladder).
  * `entry_note_abt_wctx` (called from `__wrap_sleh_abort`, before `__real_sleh_abort`).  Keys:
    `_wctxn` (the ladder ordinal, OPENS the row), `_wctx` (`CONTEXTIDR & 0xFF` at the abort),
    `_wctxva` (the `far` the row is keyed on).

THE LADDER, AND WHY THE TAIL.  Both recorders publish at `n <= 4 || n & (n-1) == 0` - the SAME rule
(A)/(B)/(E')/[PMP]/[ABT2..4]/[WIT] use.  The storm settles by ~ordinal 0x200 and the rows at/after
0x400 are the run's OWN steady state: THE TAIL.  The decision is read from the TAIL ROWS' OWN
`_wfkind`/`_wfasid`/`_wctx` and NEVER from the whole-run change-count `_wfnx` (the 1008/1009
measurement defect - a running max is not a value; [[mi4-measurement-defects]]).

THE DECISION CELLS (design §2.5 - the ONE `[WF] VERDICT:` line):
  * F1 STALE_TLB_NO_FLUSH   - tail `_wfkind == 0`: the gate at `pmap.c:1298` was FALSE, so NO ARM
    flush instruction executed for this pmap.
  * F2 FLUSH_RAN_ASID_MATCH - tail `_wfkind == 2` AND `_wfasid == _wctx` (paired by the abort ordinal
    the two rows share): the ASID-scoped invalidate ran under the ASID the fault was tagged with.
  * F3 ASID_MISMATCH        - tail `_wfkind == 2` AND `_wfasid != _wctx`: the flush was for one ASID
    while the CPU ran the fault under another.
  * F4 FLUSH_RAN_FULL       - tail `_wfkind == 1`: the full `arm_tlb_flushID` (TLBIALL) ran.
  * F5 NULL_CONTROL         - EVERY tail row carries `_wfseen == 0`: the loop's `pmap.c:1295` read
    never executed, so `_wfself = 0` is NOT a decision and the row must NOT be read as F1.  REPORT
    ONLY - this is the designed null control, never a conclusion (design §2.2).
  * F6 NO_FLUSH_WITNESS     - no tail row at all: the flush recorder never fired on the storm VA (a
    provenance/keying failure).  REPORT ONLY - rebuild, do not conclude (design §2.5).
  * `F3_UNREAD_NO_PAIRING`  - REPORT ONLY, and NOT a conclusion: a tail `_wfkind == 2` row exists but
    NO `_wctx` row pairs to its abort ordinal, so the ASID comparison could not be MADE.  A branch
    over zero samples is not a verdict ([[mi4-silence-is-a-reading-only-if-success-is-silent]]), so
    F2 and F3 are WITHHELD rather than guessed.  (This is the one cell this reader adds beyond the
    task's F1-F6 table; it is reported, never concluded from.)

THE ANOMALIES (HARD - a reading that could not be true of a correct recorder):
  * a recorded row whose published `_wfkind` is not in {0, 1, 2} - a published decode that is not the
    decode of what ran (design §2.5, the stale-decode class);
  * a `_wfself == 1` row whose `_wfttb` and `_wflive` do not agree masked to `0xFFFFFF00` - the row
    claims the flush RAN for this pmap while its own two TTBR readings disagree with the comparison
    the run made.
EVERYTHING ELSE IS A REPORT (`[soft]`/`[OBS]`), including a partial row and a mixed tail.

Usage:  tools/check_1014_capture.py <capture.txt>

Exit: 0 = a clean READ, ALWAYS, whenever the capture parses - including F1-F6, which are all
SUCCESSFUL READS of the ONE question 1014 asked (design §2.6: no fix was built or pressed).  A HARD
anomaly is COUNTED and printed, it does not fail the read (a measurement reader is not a fix
adjudication, the 1013 checker's discipline).  1 = the capture could not be read at all.
"""

import os
import re
import subprocess
import sys

# The reader's ONE tokenizer - the 1013 checker's regex, key for key (a `xnu_live_` key and a value
# that may be hex or decimal; the reader never assumes a `%p` width).
KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

# ---- [WF] THE 1014 KEYSPACE.  Every key below is READ-ONLY here: this tool adjudicates, it never
#      builds.  `_wfn`/`_wctxn` OPEN a row; `_wfnx` is a change-count and is NEVER part of a row.
WF_N_KEY = "xnu_live_abt_wfn"
WF_ROW_KEYS = ("xnu_live_abt_wfva", "xnu_live_abt_wfttb", "xnu_live_abt_wflive",
               "xnu_live_abt_wfself", "xnu_live_abt_wfshared", "xnu_live_abt_wfncpus",
               "xnu_live_abt_wfasid", "xnu_live_abt_wfkind", "xnu_live_abt_wfseen")
WFNX_KEY = "xnu_live_abt_wfnx"
WCTX_N_KEY = "xnu_live_abt_wctxn"
WCTX_ROW_KEYS = ("xnu_live_abt_wctx", "xnu_live_abt_wctxva")

# The shared abort ordinal both recorders' rows sit under (the 1009 pc ladder's counter, one per
# abort).  It is how a `_wf*` row and a `_wctx*` row are PAIRED: they are published during the SAME
# abort, so the most recent `xnu_live_abt_pcn` at or before a row's line is that row's abort ordinal.
ABT_PCN_KEY = "xnu_live_abt_pcn"

# The ladder rule - spelled ONCE (`n <= 4 || power-of-two`), the SAME rule the recorders use.
LADDER_HEAD = 4

# THE TAIL: the SAME ordinal [ABT3]/[ABT4]/[CFFIX]/[PMP]/[WIT] use (0x400).  The storm settles by
# ~0x200, so the rows at/after 0x400 are the storm's OWN steady state - and THE DECISION READS THOSE
# ROWS' OWN VALUES, never a whole-run `max()`/`first()`.
WF_TAIL_MIN_N = 0x400

# The ttb-vs-live comparison's mask - the RECORDER's own (`pmap.c:1295` masks with 0xFFFFFF00,
# `:1298` compares against that).  Masking coarser would manufacture agreement exactly at the F1/F2
# cell this rung turns on ([[mi4-one-value-two-definitions]]).
TTBR_CMP_MASK = 0xFFFFFF00

# ---- A CLAIM IN A COMMENT IS NOT A CHECK ([[mi4-a-claim-in-a-comment-is-not-a-check]]): the reader
#      strips `xnu_live_abt_` off each declared key to name its field, so the declaration and the row
#      dicts must agree BY CONSTRUCTION.  A wrong prefix here would silently produce a row dict whose
#      keys the classifier does not use.
assert {k[len("xnu_live_abt_"):] for k in WF_ROW_KEYS} == {
    "wfva", "wfttb", "wflive", "wfself", "wfshared", "wfncpus", "wfasid", "wfkind", "wfseen"}, \
    "WF_ROW_KEYS must be exactly the nine published row fields (the recorder's ONE write order)"

WF_KIND_NAMES = {
    0: "NO FLUSH RAN (the pmap.c:1298 gate was FALSE)",
    1: "FULL arm_tlb_flushID (TLBIALL)",
    2: "ASID arm_tlb_flushID_ASID",
}

# ---- THE INHERITED 1013 READING.  The 1013 checker is a SCRIPT, not a library, so it is run as a
#      SUBPROCESS over the SAME capture - the capture 1014 is read from IS a 1013 capture (its [WIT]
#      rows are the apex/walk/control ladders).  Its LAST `== VERDICT ==` block is re-printed and the
#      `THE-LEAF-VALUE-WITNESS (the 1013 verdict): ` line is the LEAF CELL this tool carries forward.
LEAF_LINE_PREFIX = "THE-LEAF-VALUE-WITNESS (the 1013 verdict):"
CHECK_1013_NAME = "check_1013_capture.py"
CHECK_1013_TIMEOUT = 600            # seconds; the 1013 checker is a reader, not a solver

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))

FAILURES = []                        # the HARD anomalies (counted, printed, never fatal - see docstring)
SOFT = []                            # the report-only findings


def to_int(s):
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    except ValueError:
        return None


def fmt(v):
    return "?" if v is None else "0x%08x(%d)" % (v, v)


def is_pow2(v):
    return v is not None and v != 0 and (v & (v - 1)) == 0


def on_ladder(n):
    """The recorders' ONE ladder rule, spelled the same way in every rung's reader."""
    return n is not None and (n <= LADDER_HEAD or (n & (n - 1)) == 0)


def hard(msg):
    FAILURES.append(msg)
    print("  [FAIL] " + msg)


def soft(msg):
    SOFT.append(msg)
    print("  [soft] " + msg)


def say(tag, msg=""):
    print(("  [%s] " % tag) + msg if msg else ("  [%s]" % tag))


class Capture:
    """The 1013 checker's reader, key for key: `newline=""` (no universal-newline translation, so
    line numbers are the reader's property - m997), the KV regex over every line, and the tokens
    collected IN ORDER (a key may recur; ALL occurrences are kept, in log order)."""

    def __init__(self, path):
        with open(path, "r", errors="replace", newline="") as fh:
            self.text = fh.read()
        self.nbytes = len(self.text)
        self.lines = self.text.split("\n")
        if self.lines and self.lines[-1] == "":
            self.lines.pop()
        self.toks = []                  # (line_no, key, raw, int_value), in log order
        for line_no, line in enumerate(self.lines, 1):
            for m in KV_RE.finditer(line):
                self.toks.append((line_no, m.group(1), m.group(2), to_int(m.group(2))))

    def all(self, key):
        return [t for t in self.toks if t[1] == key]

    def first(self, key):
        a = self.all(key)
        return a[0] if a else None

    def family_count(self, prefix):
        return sum(1 for t in self.toks if t[1].startswith(prefix))

    def pcn_by_line(self):
        """The abort ordinal governing EACH LINE: the most recent `xnu_live_abt_pcn` at or before it.

        `xnu_live_abt_pcn` advances ONE per abort (the 1009 pc ladder's counter), so this is the abort
        a row published during - the pairing key between a `_wf*` row and a `_wctx*` row.  A row that
        appears before the first published `pcn` gets None, which the classifier REPORTS rather than
        assumes ([[mi4-one-value-two-definitions]])."""
        out, cur = {}, None
        for line_no, key, _raw, val in self.toks:
            if key == ABT_PCN_KEY:
                cur = val
            out[line_no] = cur
        return out


def wf_records(cap):
    """[WF] THE FLUSH-WITNESS LADDER - one row per PUBLISHED `entry_note_abt_wf` ladder row.

    `{"ln","wfn","wfva","wfttb","wflive","wfself","wfshared","wfncpus","wfasid","wfkind","wfseen",
    "pcn","order"}`.  Modelled on the 1013 checker's `wit_apex_records()`: the recorder writes its
    ladder keys in ONE fixed order - `_wfn` OPENS the row, then
    `_wfva`/`_wfttb`/`_wflive`/`_wfself`/`_wfshared`/`_wfncpus`/`_wfasid`/`_wfkind`/`_wfseen` - so the
    reader binds to the recorder's ONE write order rather than to a channel layout it cannot see
    ([[mi4-one-value-two-definitions]]).  The field set is KNOWN, so a key outside it (the
    change-count `_wfnx`, written WHERE IT CHANGES, or the `wctx*`/`wk*`/`wc*`/`pmp*`/`vme*`/`pte*`
    keys, which pair to these rows BY KEY EQUALITY, never by position) is skipped rather than
    trusted."""
    rows, cur = [], None
    # `fkeys` maps the PHYSICAL key to the SHORT field name used in the row dict; the keyspace is the
    # ONE declaration above (WF_ROW_KEYS), never a second spelling of it.
    fkeys = {k: k[len("xnu_live_abt_"):] for k in WF_ROW_KEYS}
    blank = {v: None for v in fkeys.values()}
    pcn_by_line = cap.pcn_by_line()

    def close():
        if cur is not None:
            rows.append(cur)

    for ln, key, _raw, val in cap.toks:
        if key == WF_N_KEY:
            close()
            cur = {"ln": ln, "wfn": val, "pcn": pcn_by_line.get(ln), "order": ["wfn"]}
            cur.update(blank)
            cur["wfn"] = val
        elif cur is not None and key in fkeys:
            f = fkeys[key]
            cur[f] = val
            cur["order"].append(f)
    close()
    return rows


def wctx_records(cap):
    """[WF] THE CONTEXTIDR-WITNESS LADDER - one row per PUBLISHED `entry_note_abt_wctx` ladder row.

    `{"ln","wctxn","wctx","wctxva","pcn","order"}`.  `_wctxn` OPENS the row, then `_wctx`/`_wctxva`.
    `pcn` is the abort ordinal this row published under - the key that PAIRS it to the `_wf*` row of
    the same abort."""
    rows, cur = [], None
    fkeys = {"xnu_live_abt_wctx": "wctx", "xnu_live_abt_wctxva": "wctxva"}
    pcn_by_line = cap.pcn_by_line()

    def close():
        if cur is not None:
            rows.append(cur)

    for ln, key, _raw, val in cap.toks:
        if key == WCTX_N_KEY:
            close()
            cur = {"ln": ln, "wctxn": val, "wctx": None, "wctxva": None,
                   "pcn": pcn_by_line.get(ln), "order": ["wctxn"]}
        elif cur is not None and key in fkeys:
            f = fkeys[key]
            cur[f] = val
            cur["order"].append(f)
    close()
    return rows


def ctx_by_pcn(ctx_rows):
    """The `_wctx` row for each abort ordinal (LAST wins, and the list is ordered)."""
    out = {}
    for r in ctx_rows:
        if r["pcn"] is not None:
            out[r["pcn"]] = r
    return out


def leaf_via_1013(capture_path):
    """Run tools/check_1013_capture.py over the SAME capture and return (leaf, block, skipped, hard).

    `leaf` is the value of the `THE-LEAF-VALUE-WITNESS (the 1013 verdict):` line (None if the line is
    absent); `block` is its LAST `== VERDICT ==` block ("" if absent); `skipped` is a NON-None string
    only when the reading could not be OBTAINED at all (the checker is absent, cannot be run, or timed
    out) - that is the case the task names as SKIPPED; `hard` is a NON-None string when the checker
    RAN and READ but exited non-zero (it found hard anomalies of its own - a 1013 measurement defect
    or a broken witness control in the same log).

    A non-zero exit does NOT discard the reading: the `THE-LEAF-VALUE-WITNESS` line is a READING of
    the same capture, and the 1013 checker's own discipline is that a verdict it could READ is
    reported even when a regression clause in the same log is hard (check_1013_capture.py prints the
    leaf line BEFORE its failure return, for exactly this reason).  The hard note is carried forward
    as a CAVEAT on the inherited cell, never as a reason to hide it."""
    checker = os.path.join(TOOLS_DIR, CHECK_1013_NAME)
    if not os.path.isfile(checker):
        return None, "", "checker absent (%s does not exist)" % checker, None
    repo_root = os.path.dirname(TOOLS_DIR)
    try:
        proc = subprocess.run([sys.executable, checker, capture_path], cwd=repo_root,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True, timeout=CHECK_1013_TIMEOUT)
    except OSError as exc:
        return None, "", "could not run the checker: %s" % exc, None
    except subprocess.TimeoutExpired:
        return None, "", "the checker TIMED OUT after %ds" % CHECK_1013_TIMEOUT, None
    out = proc.stdout or ""
    leaf = None
    for line in out.split("\n"):
        if line.startswith(LEAF_LINE_PREFIX):
            leaf = line[len(LEAF_LINE_PREFIX):].strip()
    idx = out.rfind("== VERDICT ==")
    block = out[idx:].rstrip("\n") if idx >= 0 else ""
    if proc.returncode != 0:
        return leaf, block, None, ("the 1013 checker exited %d: it found hard anomalies of its own in "
                                   "THIS SAME log (a 1013 measurement defect, or a witness control it "
                                   "refused).  The leaf cell IS carried forward - it is a READING "
                                   "that checker made of THIS capture, and the 1013 checker prints it "
                                   "before its failure return for exactly this reason - but the "
                                   "caveat travels with it: read the [LEAF via 1013] block above "
                                   "before concluding anything from the leaf" % proc.returncode)
    if not block:
        return leaf, block, "the checker exited 0 but printed no `== VERDICT ==` block", None
    return leaf, block, None, None


def s_wf(cap):
    """[WF] THE 1014 TLB-FLUSH WITNESS - the ONE headline (`[WF] VERDICT: <cell>`; design §2.5)."""
    print("\n== [WF] THE 1014 TLB-FLUSH WITNESS - WHICH flush arm ran, for WHICH pmap, under WHICH "
          "ASID ==")

    wf_rows = wf_records(cap)
    ctx_rows = wctx_records(cap)
    n_wf = cap.family_count("xnu_live_abt_wf")
    n_ctx = cap.family_count("xnu_live_abt_wctx")
    pcn_vals = [t[3] for t in cap.all(ABT_PCN_KEY) if t[3] is not None]
    pcn_max = max(pcn_vals) if pcn_vals else None
    maxd_vals = [t[3] for t in cap.all("xnu_live_tick_sum_maxd") if t[3] is not None]
    maxd = max(maxd_vals) if maxd_vals else None

    # ---- (wf-0) THE INSTRUMENT, BY COUNT.  A count here is RECORDS, not literals.
    say("OBS", "`%s` rows x%d (the flush-witness ladder); `%s` x%d (the change-count: a whole-run "
               "OBSERVATION, never the decision)" % (WF_N_KEY, len(wf_rows), WFNX_KEY,
                                                     cap.family_count(WFNX_KEY)))
    say("OBS", "`%s` rows x%d (the CONTEXTIDR ladder); `xnu_live_abt_wf*` tokens x%d, "
               "`xnu_live_abt_wctx*` tokens x%d" % (WCTX_N_KEY, len(ctx_rows), n_wf, n_ctx))
    say("OBS", "the advance signals, read as RUNNING MAXIMA (`max()`, never `first()`): "
               "`%s` MAX=%s, `xnu_live_tick_sum_maxd` MAX=%s" % (ABT_PCN_KEY, fmt(pcn_max), fmt(maxd)))

    # ---- (wf-1) THE ROW INTEGRITY.  A partial row (the ordinal opened a row and its fields did not
    # all follow) is REPORTED, never failed: the task's HARD set is exactly the two anomalies below.
    for label, rows in (("F-witness", wf_rows), ("CTX-witness", ctx_rows)):
        partial = [r for r in rows if any(r[f] is None for f in r if f not in ("order", "pcn"))]
        if partial:
            miss = sorted({f for r in partial for f in r
                           if f not in ("order", "pcn") and r[f] is None})
            soft("x%d %s row(s) are INCOMPLETE (missing %s) - the channel may be truncated; first "
                 "@:%d.  REPORTED, not failed (a partial row is not one of the two HARD anomalies)"
                 % (len(partial), label, "/".join(miss), partial[0]["ln"]))
        elif rows:
            say("PASS", "%s: every record carries its full field tuple (x%d)" % (label, len(rows)))

    # ---- (wf-2) THE HARD ANOMALIES - the two shapes a correctly-built recorder cannot emit.
    bad_kind = [r for r in wf_rows if r["wfkind"] is not None and r["wfkind"] not in (0, 1, 2)]
    for r in bad_kind[:4]:
        hard("a `%s` row publishes `_wfkind=%s`, which is NOT one of {0,1,2} - a published decode "
             "that is not the decode of what ran (design §2.5, the stale-decode class); @:%d" % (
                 WF_N_KEY, fmt(r["wfkind"]), r["ln"]))
    if len(bad_kind) > 4:
        hard("and x%d MORE row(s) carry an out-of-set `_wfkind` (x%d in total; the first 4 are named "
             "above) - the count is EXACT, the listing is capped" % (len(bad_kind) - 4, len(bad_kind)))
    bad_bases = [r for r in wf_rows if r["wfself"] == 1 and r["wfttb"] is not None
                 and r["wflive"] is not None
                 and (r["wfttb"] & TTBR_CMP_MASK) != (r["wflive"] & TTBR_CMP_MASK)]
    for r in bad_bases[:4]:
        hard("a `_wfself=1` row's TWO TTBR readings disagree masked to 0x%X: `_wfttb=%s` vs "
             "`_wflive=%s` - the row claims the flush RAN for this pmap (the `pmap.c:1298` comparison "
             "is `ttb == ttbr_pmap` with BOTH sides read at flush time) while its own two readings "
             "differ, so the published `_wfself` is not the decision the run made; @:%d" % (
                 TTBR_CMP_MASK, fmt(r["wfttb"]), fmt(r["wflive"]), r["ln"]))
    if len(bad_bases) > 4:
        hard("and x%d MORE row(s) carry a `_wfself=1` with disagreeing bases (x%d in total; the first "
             "4 are named above) - the count is EXACT, the listing is capped" % (
                 len(bad_bases) - 4, len(bad_bases)))

    # ---- (wf-3) THE TAIL.  The storm's OWN steady state (ordinal >= 0x400), the SAME boundary
    # [ABT3]/[ABT4]/[CFFIX]/[PMP]/[WIT] use.  THE DECISION READS THESE ROWS' OWN VALUES.
    tail = [r for r in wf_rows if r["wfn"] is not None and r["wfn"] >= WF_TAIL_MIN_N]
    off_ladder = [r for r in wf_rows if not on_ladder(r["wfn"])]
    if off_ladder:
        soft("x%d `%s` row(s) sit OFF the ladder (`n<=%d || n&(n-1)==0`) - the recorder publishes "
             "only on the ladder, so an off-ladder ordinal is a channel/decoder finding; first @:%d "
             "n=%s" % (len(off_ladder), WF_N_KEY, LADDER_HEAD, off_ladder[0]["ln"],
                       fmt(off_ladder[0]["wfn"])))
    ctx_by_ord = ctx_by_pcn(ctx_rows)
    ctx_tail = [r for r in ctx_rows if r["wctxn"] is not None and r["wctxn"] >= WF_TAIL_MIN_N]
    say("OBS", "TAIL (ordinal >= %s): F-witness x%d row(s), CTX-witness x%d row(s)" % (
        fmt(WF_TAIL_MIN_N), len(tail), len(ctx_tail)))
    for r in tail[-6:]:
        ctx = ctx_by_ord.get(r["pcn"])
        say("OBS", "  tail F-witness @:%d wfn=%s wfkind=%s wfself=%s wfseen=%s wfva=%s wfttb=%s "
                   "wflive=%s wfncpus=%s wfasid=%s | abort pcn=%s, paired wctx=%s" % (
                       r["ln"], fmt(r["wfn"]),
                       "%s = %s" % (fmt(r["wfkind"]), WF_KIND_NAMES.get(r["wfkind"], "?"))
                       if r["wfkind"] is not None else "?",
                       fmt(r["wfself"]), fmt(r["wfseen"]), fmt(r["wfva"]), fmt(r["wfttb"]),
                       fmt(r["wflive"]), fmt(r["wfncpus"]), fmt(r["wfasid"]),
                       fmt(r["pcn"]), fmt(ctx["wctx"]) if ctx else "no wctx row at that ordinal"))
    for r in ctx_tail[-4:]:
        say("OBS", "  tail CTX-witness @:%d wctxn=%s wctx=%s wctxva=%s (abort pcn=%s)" % (
            r["ln"], fmt(r["wctxn"]), fmt(r["wctx"]), fmt(r["wctxva"]), fmt(r["pcn"])))

    # ---- (wf-4) THE DECISION - the §2.5 table over the TAIL rows' OWN values.  ONE headline.
    # The cell is read from the tail's LAST decision row (`wfseen != 0`), and a tail whose rows
    # DISAGREE on `_wfkind` is reported (a mixed tail is a finding, not a value to average).
    kind = self_ = None
    if tail:
        decision_rows = [r for r in tail if r["wfseen"] != 0]
        null_rows = [r for r in tail if r["wfseen"] == 0]
        if not decision_rows:
            # EVERY tail row is the NULL CONTROL: the loop's `:1295` read never executed, so
            # `_wfself = 0` is not a decision and F1 must NOT be read from it (design §2.2).
            say("VERDICT", "[WF] VERDICT: F5 NULL_CONTROL - all x%d tail F-witness row(s) carry "
                           "`_wfseen == 0` (the `pmap.c:1295` in-loop TTBR read never executed on any "
                           "of them), so `_wfself = 0` is NOT a flush decision.  REPORT ONLY - this "
                           "is the designed null control; the F1-F4 cells are WITHHELD (design §2.2). "
                           "First @:%d" % (len(tail), tail[0]["ln"]))
            return "F5", None
        if null_rows:
            soft("x%d of x%d tail row(s) are NULL CONTROLS (`_wfseen == 0`) and are EXCLUDED from "
                 "the decision; the cell is read from the x%d row(s) that reached the loop" % (
                     len(null_rows), len(tail), len(decision_rows)))
        kinds = sorted({r["wfkind"] for r in decision_rows if r["wfkind"] is not None})
        if len(kinds) > 1:
            soft("the tail's DECISION rows DISAGREE on `_wfkind` {%s} - the cell is read from the "
                 "LAST tail decision row (@:%d), not averaged" % (
                     ",".join(str(k) for k in kinds), decision_rows[-1]["ln"]))
        last = decision_rows[-1]
        kind, self_ = last["wfkind"], last["wfself"]
    else:
        # NO TAIL ROW: the decision cannot be made from THIS log.  A branch over zero samples is not
        # a verdict ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
        ran_storm = (pcn_max is not None and pcn_max >= WF_TAIL_MIN_N) or \
                    (maxd is not None and maxd >= 0x10000000)
        if wf_rows:
            note = ("the F-witness ladder FIRED (x%d rows, max ordinal %s) but carries NO row at the "
                    "TAIL ordinal >= %s - the recorder publishes only on the ladder "
                    "(`n<=%d || n&(n-1)==0`), so either the flush does not sit on the storm's retry "
                    "path often enough to reach the tail, or the row keying is wrong" % (
                        len(wf_rows),
                        fmt(max((r["wfn"] for r in wf_rows if r["wfn"] is not None), default=None)),
                        fmt(WF_TAIL_MIN_N), LADDER_HEAD))
        elif ran_storm:
            note = ("the storm RAN (pcn_max=%s / maxd=%s) yet the log carries NO `%s` row at all: the "
                    "flush recorder NEVER FIRED on the storm's VA" % (
                        fmt(pcn_max), fmt(maxd), WF_N_KEY))
        else:
            note = ("NO `%s` rows AND no storm-scale advance (pcn_max=%s, maxd=%s) - this run did "
                    "not reach the tail to measure" % (WF_N_KEY, fmt(pcn_max), fmt(maxd)))
        say("VERDICT", "[WF] VERDICT: F6 NO_FLUSH_WITNESS - no tail F-witness row: %s.  REPORT ONLY - "
                       "this is a provenance/keying finding, not a conclusion (design §2.5: rebuild, "
                       "do not conclude)." % note)
        return "F6", None

    if kind == 0:
        say("VERDICT", "[WF] VERDICT: F1 STALE_TLB_NO_FLUSH - the tail row @:%d publishes `_wfkind=0` "
                       "(`_wfself=%s`, `_wfseen=%s`): the gate at `pmap.c:1298` was FALSE for this "
                       "pmap at flush time, so NO ARM flush instruction executed (`_wfttb=%s` vs "
                       "`_wflive=%s`)" % (
                           decision_rows[-1]["ln"], fmt(self_), fmt(decision_rows[-1]["wfseen"]),
                           fmt(decision_rows[-1]["wfttb"]), fmt(decision_rows[-1]["wflive"])))
        return "F1", kind
    if kind == 1:
        say("VERDICT", "[WF] VERDICT: F4 FLUSH_RAN_FULL - the tail row @:%d publishes `_wfkind=1`: "
                       "the FULL `arm_tlb_flushID()` (TLBIALL) ran for this pmap (design §2.5 F4; "
                       "cross-check the 1013 [WIT] V3 cell `_wwl2pa` vs `_wkl2pa` in the [LEAF via "
                       "1013] block below)" % decision_rows[-1]["ln"])
        return "F4", kind
    if kind == 2:
        # THE ASID CELL: pair each tail `_wfkind == 2` row to the `_wctx` row of the SAME abort
        # ordinal and compare.  Pairing by the abort ordinal is the design's own rule; the change
        # count is never consulted.
        kind2 = [r for r in decision_rows if r["wfkind"] == 2]
        pairs = [(r, ctx_by_ord.get(r["pcn"])) for r in kind2]
        paired = [(r, c) for r, c in pairs if c is not None and c["wctx"] is not None]
        unpaired = [r for r, c in pairs if c is None or c["wctx"] is None]
        for r, c in paired[-3:]:
            say("OBS", "  ASID pair @:%d (abort pcn=%s): `_wfasid=%s` vs `_wctx=%s` (CTX-witness "
                       "@:%d)" % (r["ln"], fmt(r["pcn"]), fmt(r["wfasid"]), fmt(c["wctx"]), c["ln"]))
        if unpaired:
            soft("x%d tail `_wfkind=2` row(s) have NO `_wctx` row at their abort ordinal - the ASID "
                 "comparison could not be MADE for those rows; first @:%d pcn=%s" % (
                     len(unpaired), unpaired[0]["ln"], fmt(unpaired[0]["pcn"])))
        if not paired:
            say("VERDICT", "[WF] VERDICT: F3_UNREAD_NO_PAIRING - the tail publishes `_wfkind=2` (the "
                           "ASID-scoped flush ran) but NO `_wctx` row pairs to the abort ordinal, so "
                           "`_wfasid` has nothing to be compared AGAINST.  REPORT ONLY - this is NOT "
                           "a conclusion: F2 and F3 are WITHHELD rather than guessed (a branch over "
                           "zero samples is not a verdict)")
            return "F3_UNREAD_NO_PAIRING", kind
        mismatch = [(r, c) for r, c in paired if r["wfasid"] != c["wctx"]]
        if mismatch:
            r, c = mismatch[-1]
            say("VERDICT", "[WF] VERDICT: F3 ASID_MISMATCH - the tail row @:%d published `_wfkind=2` "
                           "with `_wfasid=%s`, but the CONTEXTIDR at that abort (pcn=%s) was "
                           "`_wctx=%s`: the ASID-scoped invalidate ran for a DIFFERENT ASID than the "
                           "one the faulting store was tagged with (design §2.5 F3); @:%d" % (
                               r["ln"], fmt(r["wfasid"]), fmt(r["pcn"]), fmt(c["wctx"]), c["ln"]))
            return "F3", kind
        r, c = paired[-1]
        say("VERDICT", "[WF] VERDICT: F2 FLUSH_RAN_ASID_MATCH - the tail row @:%d published "
                       "`_wfkind=2` with `_wfasid=%s`, and the CONTEXTIDR at that abort (pcn=%s) was "
                       "`_wctx=%s` - the ASID-scoped flush ran under the ASID the fault was tagged "
                       "with (design §2.5 F2)" % (
                           r["ln"], fmt(r["wfasid"]), fmt(r["pcn"]), fmt(c["wctx"])))
        return "F2", kind
    # A tail row whose `_wfkind` was never published at all (a partial row): REPORT, never guess.
    say("VERDICT", "[WF] VERDICT: F6 NO_FLUSH_WITNESS (partial) - the tail row @:%d published no "
                   "`_wfkind`, so no cell can be read from it.  REPORT ONLY" % decision_rows[-1]["ln"])
    return "F6", None


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if flags:
        print("unknown flag(s): %s" % " ".join(flags))
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    if len(argv) != 1:
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    path = argv[0]
    try:
        cap = Capture(path)
    except (OSError, IOError) as exc:
        print("CANNOT READ %s: %s" % (path, exc))
        return 1

    print("1014 capture reader - %s (%d bytes, %d lines)" % (path, cap.nbytes, len(cap.lines)))
    print("1014 IS A PURE MEASUREMENT RUNG (design §2.6): it installs no fix and changes no kernel "
          "behaviour, so the run is EXPECTED to still freeze at the ~24.4 s shape 1013 measured and "
          "every verdict below is a SUCCESSFUL READ, never a fix adjudication.")

    cell, kind = s_wf(cap)

    # ---- [LEAF via 1013] THE INHERITED READING.  1014's capture IS a 1013 capture (the same [WIT]
    # witness rows), so the 1013 checker is run as a subprocess over the SAME capture and its LAST
    # `== VERDICT ==` block is re-printed here rather than re-implemented.
    leaf, block, skipped, hard_note = None, "", None, None
    try:
        leaf, block, skipped, hard_note = leaf_via_1013(path)
    except Exception as exc:                       # never let the subprocess crash the reader
        skipped = "the 1013 checker raised %s: %s" % (type(exc).__name__, exc)
        block = ""
    print("\n== [LEAF via 1013] THE INHERITED 1013 READING (the same capture) ==")
    if skipped:
        print("[LEAF via 1013] SKIPPED (checker absent or failed) - %s" % skipped)
        if leaf:
            print("  its `THE-LEAF-VALUE-WITNESS` line READ: %s" % leaf)
        if block:
            print("  - the checker's last `== VERDICT ==` block, PRINTED for the record but NOT "
                  "inherited:")
            for line in block.split("\n"):
                print("    | " + line)
        leaf_cell = "NOT INHERITED (checker absent/failed: %s); its own reading: %s" % (
                    skipped, leaf if leaf else "no THE-LEAF-VALUE-WITNESS line")
    else:
        if hard_note:
            print("[LEAF via 1013] READ WITH A CAVEAT - %s" % hard_note)
        if block:
            for line in block.split("\n"):
                print("  | " + line)
        else:
            print("  (the checker printed no `== VERDICT ==` block)")
        leaf_cell = leaf if leaf else "NOT READ (the 1013 block carries no THE-LEAF-VALUE-WITNESS " \
                                      "line)"

    print("\n== 1014 VERDICT ==")
    print("1014 FLUSH CELL: %s" % cell)
    print("1014 LEAF CELL (inherited from 1013): %s" % leaf_cell)
    print("HARD ANOMALIES: %d" % len(FAILURES))
    for f in FAILURES:
        print("    - " + f)
    if SOFT:
        print("  %d report-only finding(s) above (never counted, never concluded from)" % len(SOFT))
    print("  => 1014 is a MEASUREMENT (design §2.6): the cell above is the evidence; do NOT build or "
          "press a fix from an assumed cause.  A HARD anomaly is REPORTED, not a fix adjudication - "
          "the exit code stays 0 for any capture that parses")
    return 0


if __name__ == "__main__":
    sys.exit(main())