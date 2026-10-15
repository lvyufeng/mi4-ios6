#!/usr/bin/env python3
"""Mechanical reader for the 1015 press capture - the AT/PAR permission-INPUTS witness.

**1015 IS ENTRY-ONLY.**  `docs/1015-design.md` §2 originally carried TWO halves; the lead removed the
ACTIVE one on 2026-10-15 and `scripts/build_1015.sh` no longer builds it.  1014's kernel pool goes to
the press byte for byte, so the arm is "1014's pool + 1015's entry objects":

  * THE REMOVED ACTIVE HALF (`STAGE90_XNU_USER_FULL_FLUSH`, pmap.c) WOULD have taken the FULL
    `arm_tlb_flushID()` (TLBIALL, `c8,c7,0`) instead of `arm_tlb_flushID_ASID` (TLBIASID) for a
    NON-KERNEL pmap in `pmap_flush_tlbs`'s `if (flush_self)` arm, and asked by BEHAVIOUR "does the
    storm STOP?".  It is NOT compiled, so **on a real 1015 arm `_wfkind` can never be 1** and the
    §3a V-G1/V-G2 branch below is STRUCTURALLY UNREACHABLE - every real 1015 capture reads V-G3, the
    null control.  The §3a table is KEPT and still adjudicated (it is the pre-registered table, the
    frozen-1014 negative control is certified through it, and it is what a LATER rung that does widen
    the flush would be read with) - but it is NOT 1015's verdict cell.
  * THE PASSIVE HALF (`STAGE90_XNU_PERM_WITNESS`, entry_stubs.c/entry_trace.c): the permission INPUTS
    witness (`_wd*`, §3b) plus THE AT/PAR CELL (§3c) - `armreg_va2pa_uw_ns` + `armreg_par_read`, the
    CPU describing at the abort the translation it would USE for the storm's VA.  **This is 1015's
    decisive cell** and it is press-independent: `par & 1` = the hardware itself faults that access,
    and set against the walked leaf (ap=0b11/apx=0/nG=0/domain=0) it decides H-G in ONE run.

MODELLED ON tools/check_1014_capture.py (its `Capture` reader, its `xnu_live_` KV regex, its
`@:line` numbering, its HARD/soft/REPORT split and its exit discipline).  It does NOT re-implement
the inherited readings: the 1015 capture IS ALSO a 1013+1014 capture (the same [WIT] leaf rows and
the same `_wf*` flush rows), so `tools/check_1013_capture.py` and `tools/check_1014_capture.py` are
run as SUBPROCESSES over the SAME capture and their `== VERDICT ==` blocks are re-printed under
`[LEAF via 1013]` / `[FLUSH via 1014]` headers.  The 1014 FLUSH CELL is load-bearing here (it is
how `_wfkind` is read), so the 1014 verdict is INHERITED as a cell, not merely printed.

THE ONE RULE THIS READER IS BUILT AROUND (design §0.4, [[mi4-measurement-defects]]):
  **every verdict is taken on the TAIL ROWS' OWN VALUES** - rows with `_pcn >= 0x400` (the storm's
  own steady state) - and never from a whole-run aggregate.  1014's tail is NOT uniform: the last
  two rows sit inside dyld's own image (`far` ~ `0x3a0xxxxx`).  So the storm is DEFINED as the rows
  whose own `_pcfar == 0x000e6000`, and the calm-time dyld rows are classified SEPARATELY rather
  than averaged into the same number.

THE DECISION CELLS.

  §3a INHERITED - the REMOVED active half's table, decided by the STORM (`[1015] VERDICT:`).  It is
  adjudicated for completeness and for the frozen-1014 negative control, but on a REAL 1015 arm the
  widening is not compiled, so V-G1/V-G2 CANNOT fire and the cell is V-G3 by construction - it is NOT
  1015's verdict.  **1015's verdict is the AT/PAR cell below.**
    * V-G1  storm STOPPED (fewer than 3 `far=0xe6000` tail rows AND none at `_pcn >= 0x4000`) AND
            the STORM's OWN tail rows publish `_wfkind == 1`  =>  H-G CONFIRMED - STALE_GLOBAL_TLB.
    * V-G2  storm PERSISTS (>= 3 such rows, or any at `_pcn >= 0x4000`) AND `_wfkind == 1` measured
            for the storm's pmap  =>  H-G REFUTED; fall to §3b's permission verdicts.
    * V-G3  `_wfseen == 0` or `_wfkind != 1` on the storm's tail rows  =>  EXPERIMENT VACUOUS
            (THE NULL CONTROL - a poke that never landed; MUST NOT be read as V-G2).  **On a real
            1015 arm this is the expected and only possible reading** - 1014's pool is linked
            unchanged, `_wfkind` is the measured 2 (the ASID arm), and no poke was armed.
    * V-G4  the symptom MOVED (the storm VA is gone from the tail but ANOTHER FS=15 VA appears at
            high `_pcn`)  =>  SYMPTOM_MOVED, NOT FIXED.
    * V-G5  the run no longer reaches the storm at all  =>  MEASUREMENT LOST; rebuild narrower.
    **Order matters and is the design's own**: V-G3 is evaluated FIRST (a vacuous poke cannot be
    read as a refutation); V-G5 is the no-evidence case; V-G4 is the "storm-gone-but-something-else"
    case; V-G1 vs V-G2 is the storm's own presence.

  §3c AT/PAR - **1015's DECISIVE CELL** `[1015 AT] VERDICT:` - FIVE cells published by the recorder:
  `_wdatpar` (AT user-WRITE of the STORM VA), `_wdatparpr` (AT privileged read of the STORM VA),
  `_wdctlat` (AT of the W-SYNC CONTROL VA - the T-CTL null control, its OWN key), `_wdatok` (the
  gate) and `_wdsctlr` (the CPU state).  Gated on `_wdatok=1`:
    * V-AT1  the user view FAULTS (`_wdatpar & 1 == 1`) while the walked leaf is ap=0b11 / apx=0 /
            nG=0 / domain=0  =>  H-G CONFIRMED AT THE HARDWARE, no press ambiguity.
    * V-AT2  the user view ALLOWS the write and its PA is the walked leaf's own (`0x841e9000`) yet
            the store still faults  =>  PROBE DEFECT (the fault is not from this translation).
    * V-AT3  the user view ALLOWS the write but its PA is a DIFFERENT frame  =>  a wrong translation
            in the TLB (same fix as V-AT1).
    * V-AT4  BOTH views fault  =>  BOTH-VIEWS-FAULT: re-rank to the permission-static cause #2.
    * V-AT0  the leg was VOID (no tail AT row, `_wdatok != 1`, the two STORM-VA PARs absent, or the
            T-CTL control read not satisfied)  =>  WITHHOLD; MUST NOT be read as V-AT1..4.
  **T-CTL is checked on `_wdctlat`, NOT on `_wdatparpr`** (the lead's 2026-10-15 ruling: the control
  read needs a key of its own; giving `_wdatparpr` two meanings is [[mi4-one-value-two-definitions]]).
  It is the leg's null control: if `_wdctlat` does not come back `par & 1 == 0` with
  `par & 0xfffff000 == 0x80080000`, the recorder is judged broken.  The recorder folds that into
  `_wdatok` (design §3c: "else `_wdatok=0` / V-AT0 withhold"); the reader ALSO checks `_wdctlat`
  directly, because a gate the reader cannot see is a status whose producer did not deliver one
  ([[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]).
  §3d STITCH - `[1015-AT x PRIMARY]`: V-AT1/V-AT3 + V-G1 = CONSISTENT (strongest); V-AT1/V-AT3 +
  V-G2 = CONFIRMED-TRANSLATION UNFIXED-BY-ONE-FLUSH; V-AT2 + V-G1 = CONTRADICTION; V-AT2 + V-G2 =
  H-G REFUTED FROM BOTH SIDES (the only clean refutation); V-AT0 + any = NO VERDICT.

  §3b SECONDARY - the passive ladder `[1015 P] VERDICT:` - read on the storm's own tail rows
  (`_wdn >= 0x400`, `_wdva == 0xe6000`) and WITHHELD unless `_wdctlok=1` AND `_wdseen=1` (and, for
  the lineage cells, `_wdmapok=1`).  P-CTL/P-D1/P-D2/P-D3/P-T1/P-T2/P-T3/P-M1/P-L1/P-L3/P-X1 are
  implemented exactly as the §3 table spells them.  The ladder DECIDES H-P only when V-G2 fired;
  otherwise it is measured, reported, and NOT adjudicated (a green ladder under V-G1 is consistent
  and is not a second verdict).

THE HARD ANOMALIES (a reading a correct recorder cannot emit):
  * a published `_wdleaf` decode that is NOT the recomputed decode (`_wdap`/`_wdapx`/`_wdtex`/
    `_wdcb`) - the design DERIVES those from the leaf, so a mismatch is a stale decode
    ([[mi4-one-value-two-definitions]]);
  * `_wdsmall == 1` with `_wdleaf` bit1 == 0, or `_wdsmall == 0` with bit1 == 1 - the present-test
    itself, published and recomputed.
`_wdsmall` is published directly by the recorder (design §2b) - it is NOT a leaf-derived cell and
is therefore checked against bit1 rather than excluded.

Usage:  tools/check_1015_capture.py <capture.txt>
        tools/check_1015_capture.py --self-test [<capture.txt>]

Exit: 0 = a clean read, whenever the capture parses.  A HARD anomaly is COUNTED and printed - the
inherited 1014 discipline: this reader ADJUDICATES a pre-registered table, it does not fail a build.
1 = the capture could not be read at all.  `--self-test` exits 0 iff every synthetic case printed
its expected verdict AND, when a capture argument is given, the reader REFUSES that capture for want
of `wd*` evidence (the frozen-1014 control).
"""

import os
import re
import subprocess
import sys

# The reader's ONE tokenizer - the 1013/1014 checkers' regex, key for key (the reader never assumes
# a `%p` width; a value may be hex or decimal).
KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

# ---- THE 1015 PASSIVE KEYSPACE (`src/entry/entry_stubs.c`).  Every key below is READ-ONLY here.
#      `_wdn` OPENS a row; the rest follow it in the recorder's ONE write order.
WD_N_KEY = "xnu_live_abt_wdn"
WD_LADDER_KEYS = (
    "xnu_live_abt_wdn", "xnu_live_abt_wdva", "xnu_live_abt_leafng", "xnu_live_abt_wdempty",
    "xnu_live_abt_wddacr", "xnu_live_abt_wddom", "xnu_live_abt_wdttbcr", "xnu_live_abt_wdsplit",
    "xnu_live_abt_wdttbr0", "xnu_live_abt_wdttbr1", "xnu_live_abt_wdl1", "xnu_live_abt_wdleaf",
    "xnu_live_abt_wdap", "xnu_live_abt_wdapx", "xnu_live_abt_wdtex", "xnu_live_abt_wdcb",
    "xnu_live_abt_wdsmall", "xnu_live_abt_wdpcpu", "xnu_live_abt_wdthrmap", "xnu_live_abt_wdmapok",
    "xnu_live_abt_wdseen", "xnu_live_abt_wdctl", "xnu_live_abt_wdctlok",
)
WD_ROW_FIELDS = tuple(k[len("xnu_live_abt_"):] for k in WD_LADDER_KEYS)

# The inherited 1014 flush keys - re-read here (the subprocess inheritance prints the 1014 verdict,
# but the V-G cells need `_wfkind`/`_wfseen`/`_wfva` AS VALUES, so they are read directly too).
WF_N_KEY = "xnu_live_abt_wfn"
WF_FIELDS = ("wfva", "wfttb", "wflive", "wfself", "wfshared", "wfncpus", "wfasid", "wfkind",
             "wfseen")
WF_KIND_NAMES = {0: "NO FLUSH RAN (the pmap.c gate was FALSE)",
                 1: "FULL arm_tlb_flushID (TLBIALL) - the widened arm",
                 2: "ASID arm_tlb_flushID_ASID (the 1014 steady state)"}

ABT_PCN_KEY = "xnu_live_abt_pcn"
ABT_PCFAR_KEY = "xnu_live_abt_pcfar"
ABT_FSR_KEY = "xnu_live_abt_fsr"
ABT_PC_KEY = "xnu_live_abt_pc"

# ---- THE 1015-AT EXTENSION (`b1015-entry`, `docs/1015-perm-static.md` Item 6 - "the killer key"):
#      the HARDWARE'S OWN ANSWER, not a host-side walk.  `AT` (Address Translate, `MRC p15,0,Rt,
#      c7,c8,0` for the user-WRITE / read variants) asks the MMU to walk the VA with the CURRENT TLB
#      state and return a PAR; `PAR` bit0 (`F`) = 1 means the MMU ITSELF faults that access, and 0
#      means the translation succeeded with `PA = PAR & 0xFFFFF000`.
#
#      WHY IT IS DECISIVE: every other cell is a host-side read of the PAGE TABLES.  If the walk
#      says user-read|write (`_wdap=0b11`, `_wdapx=0`) and the leaf is GLOBAL (`_leafng=0`) in
#      domain 0, and the MMU STILL faults an `AT` of that same user write, then the translation the
#      CPU is using is NOT the leaf the walk reads - a stale/other TLB entry, measured by the
#      hardware, with NO press ambiguity and no dependence on the flush widening.
#
#      `_wdatok` is the "did the reads run" flag and the WHOLE leg's gate (the design's withhold
#      rule).  **NOTE ON NAMING**: the team-lead's V-AT1 clause spells the nG precondition `wdng==0`;
#      the published keyspace's key for the RAW nG bit is `xnu_live_abt_leafng` (design §2b - its
#      NAME deliberately does not start with `wd`).  This reader reads `_leafng` for that clause and
#      says so rather than inventing a `_wdng` key.
#      **THE T-CTL KEY IS ITS OWN KEY (lead's ruling, 2026-10-15).**  The first draft of design §3c
#      gave `_wdatparpr` TWO meanings - the storm VA's privileged read in the table AND the W-SYNC
#      control VA's AT in the paragraph above it.  One key, two definitions
#      ([[mi4-one-value-two-definitions]]).  The ruling splits them: `_wdatparpr` keeps the STORM VA's
#      privileged read, and the control VA's AT is published as **`_wdctlat`**.  The reader reads
#      three PARs, not two, and the T-CTL check is a REAL check on `_wdctlat` rather than a collision
#      detector on a key that could not carry two readings at once.
AT_WRITE_KEY = "xnu_live_abt_wdatpar"       # AT user-WRITE for the STORM VA
AT_PR_KEY = "xnu_live_abt_wdatparpr"        # AT privileged READ for the STORM VA
AT_CTL_KEY = "xnu_live_abt_wdctlat"         # AT of the W-SYNC CONTROL VA (T-CTL) - its own key
AT_OK_KEY = "xnu_live_abt_wdatok"           # 1 iff the AT reads ran (AND T-CTL was satisfied)
AT_SCTLR_KEY = "xnu_live_abt_wdsctlr"       # SCTLR RAW AT THE ABORT (design §2b) - the CPU state
AT_KEY_TO_FIELD = {AT_WRITE_KEY: "wdatpar", AT_PR_KEY: "wdatparpr", AT_CTL_KEY: "wdctlat",
                   AT_OK_KEY: "wdatok", AT_SCTLR_KEY: "wdsctlr"}
AT_KEYS = (AT_OK_KEY, AT_WRITE_KEY, AT_PR_KEY, AT_CTL_KEY, AT_SCTLR_KEY)
PAR_FAULT_BIT = 1                           # PAR bit0: the MMU faults this access
PAR_PA_MASK = 0xFFFFF000                    # PAR[31:12] = the translated PA when bit0 == 0
AT_PA_EXPECT = 0x841E9000                   # §0.3: the PA the storm's own leaf 0x841e91bb names
AT_CTL_PA = 0x80080000                      # §3c T-CTL: the W-SYNC control VA's PA
SCTLR_M_BIT = 1 << 0                        # SCTLR.M - the MMU enable

# ---- THE CONSTANTS OF THE TABLE.  Each is the design's own number, spelled once.
TAIL_MIN_N = 0x400              # §3a: "rows with `_pcn >= 0x400`" (the SAME boundary 1013/1014 use)
STORM_MIN_ROWS = 3              # §3a: "fewer than 3 tail rows carry far=0xe6000" == stopped
STORM_MIN_ORD = 0x4000          # §3a: "no far=0xe6000 row appears at `_pcn >= 0x4000`"
STORM_VA = 0x000E6000           # §0: the storm's constant `far`
STORM_FSR = 0x0000080F          # §2b P5: FS=15 (FAULT_PERM_P), WnR (bit11) = 1
TTBR0_EXPECT = 0x81A86000       # §0.3: `_ptettbr` masked, the identity cell 1012/1013 measured
# The page-table window every lineage deref must land in (§2b `_wdmapok`'s own test).
PT_WINDOW_LO = 0x80000000
PT_WINDOW_HI = 0xFFFEFFFF
TTBCR_EXPECT_N = 2              # §3b P-T1: this port runs TTBCR.N = 2

# The inherited 1013/1014 readers, run as SUBPROCESSES over the SAME capture.
CHECK_1013_NAME = "check_1013_capture.py"
CHECK_1014_NAME = "check_1014_capture.py"
LEAF_LINE_PREFIX = "THE-LEAF-VALUE-WITNESS (the 1013 verdict):"
FLUSH_LINE_PREFIX = "1014 FLUSH CELL:"
SUBPROC_TIMEOUT = 600

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))

FAILURES = []                   # the HARD anomalies (counted, printed, never fatal)
SOFT = []                       # report-only findings
REPORT = []                     # the verdicts, for a machine-readable tail


def to_int(s):
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    except ValueError:
        return None


def fmt(v):
    return "?" if v is None else "0x%08x(%d)" % (v, v)


def hard(msg):
    FAILURES.append(msg)
    print("  [FAIL] " + msg)


def soft(msg):
    SOFT.append(msg)
    print("  [soft] " + msg)


def say(tag, msg=""):
    print(("  [%s] " % tag) + msg if msg else ("  [%s]" % tag))


def verdict(cell, msg):
    REPORT.append(cell)
    print("\n  [1015] VERDICT: %s - %s" % (cell, msg))


class Capture:
    """The 1013/1014 checker's reader, key for key: `newline=""` (no universal-newline translation,
    so line numbers are the reader's property), the KV regex over every line, and the tokens
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

        `pcn` advances ONE per abort (the 1009 pc ladder's counter), so this is the abort a row
        published during - the pairing key between a `wd*` row and the `wf*` row of the SAME abort.
        A row before the first published `pcn` gets None, which is REPORTED rather than assumed."""
        out, cur = {}, None
        for line_no, key, _raw, val in self.toks:
            if key == ABT_PCN_KEY:
                cur = val
            out[line_no] = cur
        return out

    def per_line(self, key):
        """The value of `key` for each line at or after its own first occurrence (a RUNNING value).

        Used for the two ROW-CARRYING ladder keys the tail rows need beside their own `wd*` fields -
        `_pcn` (the abort ordinal) and `_pcfar` (the abort's own `far`) - because the recorder reads
        the fault state at the abort and publishes the `wd*` row beside it.  The DAILY values are
        per-abort (one per `pcn`), so a running read is the row's own abort."""
        out, cur = {}, None
        for line_no, k, _raw, val in self.toks:
            if k == key:
                cur = val
            out[line_no] = cur
        return out


def wd_records(cap):
    """THE 1015 PASSIVE LADDER - one row per PUBLISHED `entry_note_abt_wd*` ladder row.

    The recorder writes its keys in ONE fixed order - `_wdn` OPENS the row, then the rest of
    `WD_LADDER_KEYS` - so the reader binds to the recorder's ONE write order rather than to a
    channel layout it cannot see ([[mi4-one-value-two-definitions]]).  Each row also carries `pcn`
    and `pcfar` as READ AT ITS OWN LINE (the abort it published under), so the tail filter and the
    storm filter are the ROW'S OWN abort state and never a whole-run property."""
    fkeys = {k: k[len("xnu_live_abt_"):] for k in WD_LADDER_KEYS}
    blank = {v: None for v in fkeys.values()}
    pcn_line = cap.pcn_by_line()
    pcfar_line = cap.per_line(ABT_PCFAR_KEY)
    rows, cur = [], None

    def close():
        if cur is not None:
            rows.append(cur)

    for ln, key, _raw, val in cap.toks:
        if key == WD_N_KEY:
            close()
            cur = {"ln": ln, "order": ["wdn"], "pcn": pcn_line.get(ln), "pcfar": pcfar_line.get(ln)}
            cur.update(blank)
            cur["wdn"] = val
        elif cur is not None and key in fkeys:
            f = fkeys[key]
            cur[f] = val
            cur["order"].append(f)
    close()
    return rows


def wf_records(cap):
    """THE INHERITED 1014 FLUSH LADDER - one row per PUBLISHED `entry_note_abt_wf*` row, plus `pcn`
    and `pcfar` as read at the row's own line.  Needed AS VALUES here (not only as the 1014
    subprocess's verdict) because `_wfkind` is what V-G1/V-G2/V-G3 turn on."""
    fkeys = {"xnu_live_abt_" + f: f for f in WF_FIELDS}
    blank = {v: None for v in fkeys.values()}
    pcn_line = cap.pcn_by_line()
    pcfar_line = cap.per_line(ABT_PCFAR_KEY)
    rows, cur = [], None

    def close():
        if cur is not None:
            rows.append(cur)

    for ln, key, _raw, val in cap.toks:
        if key == WF_N_KEY:
            close()
            cur = {"ln": ln, "order": ["wfn"], "pcn": pcn_line.get(ln), "pcfar": pcfar_line.get(ln)}
            cur.update(blank)
            cur["wfn"] = val
        elif cur is not None and key in fkeys:
            f = fkeys[key]
            cur[f] = val
            cur["order"].append(f)
    close()
    return rows


def at_records(cap):
    """THE 1015-AT ROWS - one row per PUBLISHED `_wdatok`, carrying the three PARs.

    The AT keys are a FLAT set published at one ordinal (not a ladder), so the row is opened by
    `_wdatok` (the leg's own gate) and the PARs follow it; `pcn`/`pcfar` are read at the row's own
    line for the same tail/storm attribution the `wd*`/`wf*` ladders use.  Rows are returned in log
    order; `_wdatok` may recur, and ALL occurrences are kept ([[mi4-measurement-defects]]: the last
    one is not privileged over the first without a reason, so both are reasoned about).

    **THREE PARs, and that is the lead's ruling.** `wdatpar` = the storm VA, user write;
    `wdatparpr` = the storm VA, privileged read; `wdctlat` = the W-SYNC CONTROL VA's AT (the T-CTL
    null control).  The control is NOT read out of `wdatparpr` - that was the collision this reader
    reported and the ruling fixed by giving the control its own key."""
    fkeys = {k: f for k, f in AT_KEY_TO_FIELD.items()}
    blank = {f: None for f in AT_KEY_TO_FIELD.values()}
    pcn_line = cap.pcn_by_line()
    pcfar_line = cap.per_line(ABT_PCFAR_KEY)
    # `_wdva` IS THE ROW'S OWN ABORT KEY and it does NOT lag the AT keys: the recorder publishes the
    # `wd*` ladder row with `_wdva` at its HEAD, and the AT keys (`_wdatok`..) after it, whereas
    # `_pcfar`/`_pcn` are published at the TAIL of the block.  A running read of `_pcfar` therefore
    # returns the PREVIOUS abort's far at an AT row (measured: the 0x3b3d31e8 AT row @:34895 read
    # `_pcfar=0x27000`, the storm's, and was mis-attributed to the storm).  `_wdva` at the AT row is
    # the block's own VA, so the storm filter below uses it.
    wdva_line = cap.per_line("xnu_live_abt_wdva")
    rows, cur = [], None
    for ln, key, _raw, val in cap.toks:
        if key == AT_OK_KEY:
            if cur is not None:
                rows.append(cur)
            cur = {"ln": ln, "pcn": pcn_line.get(ln), "pcfar": pcfar_line.get(ln),
                   "wdva": wdva_line.get(ln)}
            cur.update(blank)
            cur["wdatok"] = val
        elif cur is not None and key in fkeys:
            cur[fkeys[key]] = val
    if cur is not None:
        rows.append(cur)
    return rows


def aborts(cap):
    """THE ABORT LADDER - one entry per published `_pcn`, carrying the abort's OWN `far`/`fsr`/`pc`
    as read at (or after) that ordinal's line.

    This is the reader the STORM CLASSIFIER runs on, because the storm is defined as rows with
    `far == 0xe6000` - and 1014's tail is a MIX of two different processes' faults (design §0.4),
    so a storm count taken from the `wd*`/`wf*` rows alone would silently merge them.  Every entry
    is `{"ln","pcn","far","fsr","pc"}` with the values AS OF that ordinal's line."""
    # **THE PAIRING IS FORWARD, AND THAT MATTERS.**  The recorder publishes the abort's ordinal
    # (`_pcn`) and THEN its own state (`_pcfar`/`_fsr`/`_pc`), so a running-value read at the `_pcn`
    # line returns the PREVIOUS abort's `far` - MEASURED on a synthetic two-abort capture: the second
    # abort's own `far` came back as the first's.  So an abort OPENS at its `_pcn` and its three
    # fields are filled by the FIRST occurrence of each AFTER it (a field's first value wins, and a
    # second occurrence at the same ordinal is reported rather than silently overwritten by being
    # counted separately).  This is the 1010c lesson read the right way round: the row is the unit,
    # not the key's running value.
    fields = {ABT_PCFAR_KEY: "far", ABT_FSR_KEY: "fsr", ABT_PC_KEY: "pc"}
    out, cur = [], None
    for ln, key, _raw, val in cap.toks:
        if key == ABT_PCN_KEY:
            cur = {"ln": ln, "pcn": val, "far": None, "fsr": None, "pc": None}
            out.append(cur)
        elif cur is not None and key in fields:
            f = fields[key]
            if cur[f] is None:
                cur[f] = val
    return out


def read_cell_via(checker_name, path, line_prefix):
    """Run another rung's checker over the SAME capture and return (cell, block, note).

    `cell` is the value of the line prefixed `line_prefix` (None if absent); `block` is its LAST
    `== VERDICT ==` block; `note` is a non-None string ONLY when the reading could not be obtained
    (checker absent / could not run / timed out) or the checker exited non-zero (it found hard
    anomalies of its own - a caveat that TRAVELS with the cell, it does not hide it)."""
    checker = os.path.join(TOOLS_DIR, checker_name)
    if not os.path.isfile(checker):
        return None, "", "checker absent (%s does not exist)" % checker
    repo_root = os.path.dirname(TOOLS_DIR)
    try:
        proc = subprocess.run([sys.executable, checker, path], cwd=repo_root,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True, timeout=SUBPROC_TIMEOUT)
    except OSError as exc:
        return None, "", "could not run %s: %s" % (checker_name, exc)
    except subprocess.TimeoutExpired:
        return None, "", "%s TIMED OUT after %ds" % (checker_name, SUBPROC_TIMEOUT)
    out = proc.stdout or ""
    cell = None
    for line in out.split("\n"):
        if line.startswith(line_prefix):
            cell = line[len(line_prefix):].strip()
    idx = out.rfind("== VERDICT ==")
    block = out[idx:].rstrip("\n") if idx >= 0 else ""
    if proc.returncode != 0:
        return cell, block, ("%s exited %d: it found hard anomalies of its own in THIS SAME log; the "
                             "cell is carried forward (its checker prints it before its failure "
                             "return) but the caveat travels with it" % (checker_name, proc.returncode))
    return cell, block, None


# ====================================================================================================
# §3a THE PRIMARY TABLE - the experiment, decided by the STORM on the TAIL.
# ====================================================================================================
def classify_storm(abt_rows, tail_min):
    """THE STORM, BY DEFINITION (design §3a).  Returns a dict describing the tail's `far=0xe6000`
    population and the OTHER high-ordinal aborts, each from its ROW'S OWN values.

    `storm_tail`  - tail rows (`pcn >= tail_min`) whose own `far == 0xe6000` (THE STORM)
    `storm_high`  - those at `pcn >= STORM_MIN_ORD` (the 1014 storm's rows run to 0x40000)
    `other_high`  - tail rows at `pcn >= STORM_MIN_ORD` with a DIFFERENT `far` (the dyld rows of
                    §0.4, and anything else) - never merged into the storm count
    `other_fs15`  - the subset of `other_high` with FS=15 WnR=1 (the V-G4 siblings)
    `reached`     - whether ANY tail row exists at all (V-G5's own evidence)"""
    tail = [r for r in abt_rows if r["pcn"] is not None and r["pcn"] >= tail_min]
    # THE STORM VA IS COMPUTED BY VALUE, NEVER HARDCODED.  The storm's `far` MOVES per run
    # (1011 0x61000 -> 1012 0x2e000 -> 1014 0xe6000 -> 1015 0x27000): it is the page the COW/CS
    # retry picks.  Hardcoding it (the original `STORM_VA` constant) turned a COMPLETE 1015 verdict
    # into V-AT0 + a vacuous "STOPPED" on the 1015 capture - a measurement defect ([[mi4-measurement-
    # defects]]): the number was an artifact of how it was taken.  The storm is BY DEFINITION the
    # tail fault that RECURS (the most rows); ties break toward the highest ordinal reached.
    va = STORM_VA
    if tail:
        counts, minord, maxord = {}, {}, {}
        for r in tail:
            f = r["far"]
            counts[f] = counts.get(f, 0) + 1
            minord[f] = min(minord.get(f, 1 << 62), r["pcn"])
            maxord[f] = max(maxord.get(f, -1), r["pcn"])
        # MOST tail rows wins; then EARLIEST first appearance (a storm RECURS from low ordinals up, so
        # a lone row that first appears at a high ordinal is NOT the storm - it is the V-G4 "moved"
        # sibling); then the highest ordinal reached.
        va = max(counts, key=lambda f: (counts[f], -minord[f], maxord[f]))
    storm_tail = [r for r in tail if r["far"] == va]
    storm_high = [r for r in storm_tail if r["pcn"] >= STORM_MIN_ORD]
    other_high = [r for r in tail if r["pcn"] >= STORM_MIN_ORD and r["far"] != va]
    other_fs15 = [r for r in other_high if r["fsr"] is not None and (r["fsr"] & 0x00000F0F) ==
                  (STORM_FSR & 0x00000F0F)]
    return {"tail": tail, "va": va, "storm_tail": storm_tail, "storm_high": storm_high,
            "other_high": other_high, "other_fs15": other_fs15,
            "reached": bool(tail)}


def s_primary(cap, storm, wf_rows):
    """[1015] THE REMOVED ACTIVE HALF - the §3a cells, in the design's OWN order.

    **THIS IS NOT 1015's VERDICT CELL.** The widening is not compiled (1015 is entry-only), so the
    storm cannot have stopped BECAUSE of it and `_wfkind=1` cannot be published: a real 1015 arm
    reads V-G3 here every time. Kept because it is the pre-registered table the frozen-1014 negative
    control is certified through, and because a LATER rung that does widen the flush is read by it.
    """
    print("\n== [1015] THE INHERITED §3a QUESTION - DID THE STORM STOP WHEN THE FLUSH BECAME A FULL "
          "TLBIALL?  (the widening is NOT compiled in this rung: V-G3 is expected) ==")
    storm_tail, storm_high = storm["storm_tail"], storm["storm_high"]
    STORM_VA = storm["va"]   # the storm's OWN far, computed by value (never the module literal)
    stopped = len(storm_tail) < STORM_MIN_ROWS and not storm_high

    say("OBS", "TAIL (abort `_pcn` >= %s): x%d abort ordinal(s); of them `far=%s` x%d (THE STORM), "
               "`far != %s` x%d; at `_pcn >= %s`: storm x%d, other x%d" % (
                   fmt(TAIL_MIN_N), len(storm["tail"]), fmt(STORM_VA), len(storm_tail),
                   fmt(STORM_VA), len(storm["tail"]) - len(storm_tail), fmt(STORM_MIN_ORD),
                   len(storm_high), len(storm["other_high"])))
    say("OBS", "the STOP DEFINITION (§3a): fewer than %d `far=%s` tail rows AND none at `_pcn >= %s`"
               " - MEASURED: %d row(s), %d at/above %s  =>  %s" % (
                   STORM_MIN_ROWS, fmt(STORM_VA), fmt(STORM_MIN_ORD), len(storm_tail),
                   len(storm_high), fmt(STORM_MIN_ORD),
                   "STOPPED" if stopped else "PERSISTS"))
    for r in storm_tail[-8:]:
        say("OBS", "  storm tail @:%d pcn=%s far=%s fsr=%s pc=%s" % (
            r["ln"], fmt(r["pcn"]), fmt(r["far"]), fmt(r["fsr"]), fmt(r["pc"])))
    for r in storm["other_high"][-4:]:
        say("OBS", "  other tail @:%d pcn=%s far=%s fsr=%s pc=%s" % (
            r["ln"], fmt(r["pcn"]), fmt(r["far"]), fmt(r["fsr"]), fmt(r["pc"])))

    # ---- THE FLUSH EVIDENCE, restricted to the STORM'S OWN tail rows (never a whole-run aggregate).
    # The 1015 recorder (`entry_note_abt_wf`) is called from `pmap_flush_tlbs` keyed on the CURRENT
    # fault's `far`, so a `wf*` row published under a storm abort carries the storm's `far` in its
    # own `_wfva` - and its `pcfar` is that same abort.  Rows are attributed to the storm if EITHER
    # key says so; the two are reported separately so a keying defect is visible rather than merged.
    wf_tail = [r for r in wf_rows if r["wfn"] is not None and r["wfn"] >= TAIL_MIN_N]
    wf_storm = [r for r in wf_tail if r["wfva"] == STORM_VA or r["pcfar"] == STORM_VA]
    say("OBS", "flush witness at the tail: x%d `_wf*` row(s); x%d carry the storm (`_wfva=%s` or "
               "abort `far=%s`)" % (len(wf_tail), len(wf_storm), fmt(STORM_VA), fmt(STORM_VA)))
    if not wf_storm:
        # NO FLUSH ROW FOR THE STORM: fall back to the whole tail's kind vocabulary, REPORTED as a
        # fallback (a keying defect must not silently become a V-G2).
        kinds_all = sorted({r["wfkind"] for r in wf_tail if r["wfkind"] is not None})
        say("OBS", "  FALLBACK (no storm-keyed flush row): the tail's `_wf*` rows publish `_wfkind` "
                   "{%s} over x%d row(s); x%d carry `_wfseen=0`" % (
                       ",".join(str(k) for k in kinds_all), len(wf_tail),
                       sum(1 for r in wf_tail if r["wfseen"] == 0)))
    kinds = sorted({r["wfkind"] for r in wf_storm if r["wfkind"] is not None})
    seen_vals = sorted({r["wfseen"] for r in wf_storm if r["wfseen"] is not None})
    full_rows = [r for r in wf_storm if r["wfkind"] == 1]
    say("PASS" if full_rows else "OBS", "the storm's own flush rows publish `_wfkind` {%s} "
                                        "(1 = the widened TLBIALL) over x%d row(s), `_wfseen` {%s}" % (
        ",".join(str(k) for k in kinds), len(wf_storm), ",".join(str(k) for k in seen_vals)))

    # ---- V-G5 FIRST when there is no tail evidence at all: a branch over zero samples is not a
    # verdict ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
    if not storm["reached"]:
        pcn_all = [r["pcn"] for r in cap.all(ABT_PCN_KEY) if r[3] is not None] if False else None
        maxd = [t[3] for t in cap.all("xnu_live_tick_sum_maxd") if t[3] is not None]
        note = ("no abort row at `_pcn >= %s` at all - the run did not reach the tail; "
                "`xnu_live_tick_sum_maxd` MAX=%s" % (
                    fmt(TAIL_MIN_N), fmt(max(maxd) if maxd else None)))
        verdict("V-G5", "MEASUREMENT LOST - %s.  The widened flush may have broken a different path "
                        "(design §3a V-G5/§7): rebuild with the flush scoped NARROWER rather than "
                        "concluding." % note)
        return "V-G5", stopped

    # ---- V-G3 THE NULL CONTROL, before any conclusion: a poke that never landed cannot refute H-G.
    # The design spells this cell TWICE - `_wfseen = 0` or `_wfkind = 0` in the §3a table, and
    # `_wfkind != 1` in the §3a null-control paragraph.  The reader takes the UNION (withhold if
    # EITHER is true), which is the conservative direction: it refuses a V-G2 in every case either
    # spelling refuses (see docs/1015-harness.md).
    wf_vacuous = (not wf_storm) or any(r["wfseen"] == 0 or r["wfkind"] != 1 for r in wf_storm)
    if wf_vacuous:
        why = []
        if not wf_storm:
            why.append("no `_wf*` row carries the storm's VA at the tail at all")
        else:
            n_null = sum(1 for r in wf_storm if r["wfseen"] == 0)
            n_notfull = sum(1 for r in wf_storm if r["wfkind"] != 1)
            if n_null:
                why.append("x%d of x%d storm flush row(s) carry `_wfseen=0` (the in-loop TTBR read "
                           "never executed, so `_wfself` is not a decision)" % (n_null, len(wf_storm)))
            if n_notfull:
                why.append("x%d of x%d carry `_wfkind != 1` {%s}" % (
                    n_notfull, len(wf_storm), ",".join(str(k) for k in kinds)))
        verdict("V-G3", "EXPERIMENT VACUOUS - NO VERDICT: %s.  The widened arm was never reached for "
                        "the storm's pmap, so the poke never landed and this MUST NOT be read as "
                        "V-G2 (design §3a's own null control: 'the flush was refused, so of course "
                        "it still faults' is a different sentence from 'a full flush ran and it "
                        "still faults').  Rebuild and check `flush_self` at the storm." % ("; ".join(why)))
        return "V-G3", stopped

    # ---- V-G4: nothing to move if the storm is still here; if the storm is GONE but a DIFFERENT
    # FS=15 VA sits at high ordinal, the symptom MOVED rather than stopped.
    if stopped and storm["other_fs15"]:
        r = storm["other_fs15"][-1]
        verdict("V-G4", "SYMPTOM_MOVED, NOT FIXED - the storm's `far=%s` is gone from the tail, but "
                        "another FS=15 WnR=1 USER fault appears at `_pcn=%s` with `far=%s` (pc=%s): "
                        "the invalidate changed the fault's address, so the entry was TLB-resident "
                        "but there are siblings; the next rung enumerates them (design §3a V-G4). "
                        "This is NOT a confirmation of H-G as *the* cause." % (
                            fmt(STORM_VA), fmt(r["pcn"]), fmt(r["far"]), fmt(r["pc"])))
        return "V-G4", stopped

    if stopped:
        verdict("V-G1", "H-G CONFIRMED - STALE_GLOBAL_TLB.  The storm STOPPED: x%d `far=%s` tail "
                        "row(s) (< %d) and none at `_pcn >= %s`, while the storm's own flush rows "
                        "publish `_wfkind=1` (the widened `arm_tlb_flushID()`, TLBIALL).  The "
                        "widened invalidate evicted the stale GLOBAL translation and the current "
                        "RW leaf then served the store - the mechanism is the never-`nG` leaf "
                        "(`L2_NG_BIT` is ORed nowhere) plus the ASID-only steady-state flush.  The "
                        "fix is design §7 (not built in 1015)." % (
                            len(storm_tail), fmt(STORM_VA), STORM_MIN_ROWS, fmt(STORM_MIN_ORD)))
        return "V-G1", stopped

    verdict("V-G2", "H-G REFUTED - a FULL TLBIALL ran for this pmap (`_wfkind=1`, x%d storm tail "
                    "row(s), x%d at `_pcn >= %s`) and the store STILL faults with `far=%s`.  No "
                    "stale TLB entry is the cause: run the §3b passive ladder (already measured in "
                    "this same capture - see the P block below)." % (
                        len(full_rows), len(storm_high), fmt(STORM_MIN_ORD), fmt(STORM_VA)))
    return "V-G2", stopped


# ====================================================================================================
# §3b THE SECONDARY LADDER - the permission INPUTS witness.
# ====================================================================================================
def s_p_leaves(rows):
    """THE HARD ANOMALIES on the published `wd*` rows - the design DERIVES `_wdap`/`_wdapx`/`_wdtex`/
    `_wdcb` from the ONE `_wdleaf` read, so a published decode that is not the recomputed decode is a
    stale decode and is HARD ([[mi4-one-value-two-definitions]]).  `_wdsmall` is published directly
    by the recorder, so it is checked against bit1 rather than counted as a derived cell."""
    for r in rows:
        leaf = r["wdleaf"]
        if leaf is None:
            continue
        dec = {"wdap": (leaf >> 4) & 3, "wdapx": (leaf >> 9) & 1, "wdtex": (leaf >> 6) & 7,
               "wdcb": (leaf >> 2) & 3}
        for field, want in dec.items():
            got = r[field]
            if got is not None and got != want:
                hard("a `_wd*` row @:%d publishes `_%s=%s`, but `_wdleaf=%s` decodes to %s - the "
                     "cell is not the decode of the ONE leaf read the design derives it from (the "
                     "stale-decode class; design §2b rows `_wdap`..`_wdcb`)" % (
                         r["ln"], field, fmt(got), fmt(leaf), fmt(want)))
        if r["wdsmall"] is not None and r["wdsmall"] != ((leaf >> 1) & 1):
            hard("a `_wd*` row @:%d publishes `_wdsmall=%s`, but `_wdleaf=%s` bit1 = %d - the "
                 "present-test cell disagrees with the leaf it tests" % (
                     r["ln"], fmt(r["wdsmall"]), fmt(leaf), (leaf >> 1) & 1))
        if r["wdmapok"] is not None and r["wdthrmap"] is not None and r["wdmapok"] == 1:
            if not (PT_WINDOW_LO <= r["wdthrmap"] <= PT_WINDOW_HI):
                hard("a `_wd*` row @:%d publishes `_wdmapok=1` while `_wdthrmap=%s` is OUTSIDE the "
                     "page-table window [%s, %s] the cell is defined as 'every deref landed in' - a "
                     "status whose producer did not deliver it" % (
                         r["ln"], fmt(r["wdthrmap"]), fmt(PT_WINDOW_LO), fmt(PT_WINDOW_HI)))


def s_secondary(cap, storm, decide, primary_cell):
    """[1015 P] THE §3b TABLE.  Read on the storm's own tail rows (`_wdn >= 0x400` AND
    `_wdva == 0xe6000`), WITHHELD unless `_wdctlok=1` and `_wdseen=1` (and `_wdmapok=1` for the
    lineage cells).  DECIDES H-P only when `decide` (i.e. V-G2 fired); otherwise it is measured and
    printed WITHOUT a P verdict, because a green ladder under V-G1 is consistent and is not a
    second verdict."""
    print("\n== [1015 P] THE PASSIVE LADDER - the permission INPUTS at the abort (design §3b) ==")
    STORM_VA = storm["va"]   # the storm's OWN far, computed by value (never the module literal)
    wd_rows = wd_records(cap)
    wd_storm = [r for r in wd_rows
                if r["wdn"] is not None and r["wdn"] >= TAIL_MIN_N
                and r["wdva"] == STORM_VA]
    if not wd_storm:   # a capture whose witness's OWN `_wdva` differs from the abort's `far`
        wd_storm = [r for r in wd_rows
                    if r["wdn"] is not None and r["wdn"] >= TAIL_MIN_N
                    and r["pcfar"] == STORM_VA]
    say("OBS", "`%s` rows x%d; at the tail with `_wdva=%s`: x%d" % (
        WD_N_KEY, len(wd_rows), fmt(STORM_VA), len(wd_storm)))
    if not wd_rows:
        say("VERDICT", "[1015 P] VERDICT: P-CTL NO_PERM_WITNESS - the capture carries NO `%s` row "
                       "at all: `STAGE90_XNU_PERM_WITNESS` did not reach the image (or the recorder "
                       "never fired).  No passive verdict can be read; rebuild." % WD_N_KEY)
        return "P-CTL"
    if not wd_storm:
        say("VERDICT", "[1015 P] VERDICT: P-CTL NO_PERM_WITNESS - x%d `%s` row(s) exist but NONE is "
                       "at the tail keyed on `_wdva=%s`; the passive ladder's own population is "
                       "empty, so no P-cell can be read (a branch over zero samples is not a "
                       "verdict).  Rebuild." % (len(wd_rows), WD_N_KEY, fmt(STORM_VA)))
        return "P-CTL"

    # ---- THE WITHHOLD GATES, read on the LAST storm row (the row the cell is taken from).
    last = wd_storm[-1]
    for r in wd_storm[-4:]:
        say("OBS", "  storm wd @:%d wdn=%s wdva=%s leaf=%s ap=%s apx=%s small=%s leafng=%s dom=%s "
                   "dacr=%s ttbcr=%s split=%s ttbr0=%s apok=%s seen=%s ctlok=%s ctl=%s" % (
                       r["ln"], fmt(r["wdn"]), fmt(r["wdva"]), fmt(r["wdleaf"]), fmt(r["wdap"]),
                       fmt(r["wdapx"]), fmt(r["wdsmall"]), fmt(r["leafng"]), fmt(r["wddom"]),
                       fmt(r["wddacr"]), fmt(r["wdttbcr"]), fmt(r["wdsplit"]), fmt(r["wdttbr0"]),
                       fmt(r["wdmapok"]), fmt(r["wdseen"]), fmt(r["wdctlok"]), fmt(r["wdctl"])))
    s_p_leaves(wd_storm)

    if last["wdctlok"] != 1 or last["wdseen"] != 1:
        say("VERDICT", "[1015 P] VERDICT: P-CTL NO_PERM_WITNESS - the storm's own tail row @:%d "
                       "publishes `_wdctlok=%s` / `_wdseen=%s` (the design withholds EVERY passive "
                       "verdict unless BOTH are 1): the control read was not taken, or the cells "
                       "were not produced, so nothing here is a reading.  Rebuild." % (
                           last["ln"], fmt(last["wdctlok"]), fmt(last["wdseen"])))
        return "P-CTL"

    # ---- THE H-G PRECONDITION, by value (design §3b's note): `_leafng == 0` (GLOBAL) is the
    # precondition of H-G; `_leafng == 1` would KILL H-G before any press, because TLBIASID would
    # then evict the stale entry.  Published so the reader sees it without decoding the packed leaf.
    ng_vals = sorted({r["leafng"] for r in wd_storm if r["leafng"] is not None})
    if ng_vals and all(v == 0 for v in ng_vals):
        say("PASS", "the H-G PRECONDITION holds BY VALUE: `_leafng=0` (GLOBAL) on every storm tail "
                    "row - the leaf's bit11 is 0, exactly the 1014 decode of 0x841e91bb")
    elif ng_vals:
        say("VERDICT", "[1015 P] VERDICT: LEAF_NOT_GLOBAL (`_leafng` {%s}) - the storm leaf is "
                       "ASID-tagged, so TLBIASID WOULD have evicted its stale entry and H-G dies "
                       "before the press (design §3b's note).  This does not change the primary "
                       "cell; it is the precondition read back." % ",".join(str(v) for v in ng_vals))

    # ---- P-CTL is a null-control cell in the §3b table; report its value alongside the ladder (the
    # `_wdctl` publish is the control VA's domain+DACR bits at the SAME abort).
    say("OBS", "  the null control's own reading at the storm abort: `_wdctl=%s` (`_wdctlok=1`) - the "
               "control VA's domain+DACR bits, read AT THE SAME ABORT" % fmt(last["wdctl"]))

    if not decide:
        say("OBS", "[1015 P] the ladder is MEASURED but NOT ADJUDICATED: the primary cell is %s, and "
                   "the P-table decides H-P only when V-G2 fires (design §3b: 'decides H-P only if "
                   "V-G2').  A green ladder here is CONSISTENT, not a second verdict." % primary_cell)

    # ---- THE TABLE, in the §3b order.  Each condition is the design's own spelling.
    pcell = None
    if last["wddom"] != 0:
        pcell = ("P-D1", "L1_DOMAIN_NONZERO - the faulting L1 descriptor @:%d names domain %s "
                         "(DESIGN: != 0); the DACR must mark that domain client and this port's L1 "
                         "entries are expected in domain 0 (design §0.2)" % (
                             last["ln"], fmt(last["wddom"])))
    elif (last["wddacr"] & 3) != 1:
        pcell = ("P-D2", "DACR_NOT_CLIENT - `_wddom=0` and `(_wddacr & 3) = %d != 1` (DESIGN: "
                         "client): H-P CONFIRMED - domain 0 is NOT marked client, so the CPU takes "
                         "a permission fault on a leaf that is user-writable.  The only DACR writer "
                         "is src/mmu.c:4921." % (last["wddacr"] & 3))
    elif last["wdttbcr"] is None or (last["wdttbcr"] & 7) != TTBCR_EXPECT_N:
        pcell = ("P-T1", "TTBCR_NOT_N2 - `(_wdttbcr & 7) = %s != %d` (DESIGN): the split the whole "
                         "`entry_ttbr_base` derivation (entry_stubs.c:2150) presumes is not in "
                         "force, so every TTBR0/TTBR1 derivation must be re-read" % (
                             fmt(None if last["wdttbcr"] is None else last["wdttbcr"] & 7),
                             TTBCR_EXPECT_N))
    elif last["wdsplit"] is None or last["wdva"] >= last["wdsplit"]:
        pcell = ("P-T2", "ROUTED_TO_TTBR1 - `(TTBCR.N)=2` and `_wdva=%s >= _wdsplit=%s`: the storm "
                         "VA routes to the high-half table, so the TTBR0 lineage is not the one the "
                         "MMU walks for it" % (fmt(last["wdva"]), fmt(last["wdsplit"])))
    elif last["wdttbr0"] is None or (last["wdttbr0"] & 0xFFFFF000) != TTBR0_EXPECT:
        pcell = ("P-T3", "TTBR0_MOVED_AT_ABORT - the live TTBR0 at the abort, masked "
                         "`2^(14-n)` = 0xFFFFF000, is %s != %s: the walker's table is not the one "
                         "1012/1013 measured at the storm" % (
                             fmt(None if last["wdttbr0"] is None else last["wdttbr0"] & 0xFFFFF000),
                             fmt(TTBR0_EXPECT)))
    elif last["wdmapok"] == 1 and last["wdthrmap"] != last["wdttbr0"]:
        pcell = ("P-M1", "THREAD_PMAP_NE_TTBR0 - `_wdmapok=1` and the aborting thread's own pmap L1 "
                         "base `_wdthrmap=%s` != the live `_wdttbr0=%s`: the thread that faulted is "
                         "not running on the table being walked" % (
                             fmt(last["wdthrmap"]), fmt(last["wdttbr0"])))
    elif last["wdsmall"] == 0:
        pcell = ("P-L3", "NOT_A_SMALL_PAGE - `_wdsmall=0`: `_wdleaf=%s` is not an L2 small page, so "
                         "the AP bits this ladder reads are not the page's protection at all" % (
                             fmt(last["wdleaf"])))
    elif last["wdapx"] != 0 or last["wdap"] != 0b11:
        pcell = ("P-L1", "LEAF_NOT_USER_WRITABLE - `_wdleaf=%s` decodes `_wdap=%s _wdapx=%s` (want "
                         "0b11 / 0): the leaf is NOT user read|write, which CONTRADICTS 1014's "
                         "stable double-read of 0x841e91bb - so either the leaf changed between the "
                         "two captures or the recorder's ONE-read rule is broken" % (
                             fmt(last["wdleaf"]),
                             "%s" % (fmt(last["wdap"]) + " (0b%s)" % bin(last["wdap"])[2:]
                                      if last["wdap"] is not None else "?"),
                             fmt(last["wdapx"])))
    else:
        pcell = ("P-X1", "INPUTS_ALL_CORRECT_STILL_FAULTS - every §3b clause is killed BY VALUE: the "
                         "L1 descriptor @:%d names domain 0, the DACR marks it CLIENT "
                         "(`(_wddacr&3)=1`), `TTBCR.N=2` and the VA is below the split "
                         "(`_wdva=%s < _wdsplit=%s`), the live TTBR0 is %s, `_wdmapok=1` and the "
                         "thread's pmap agrees with it, and the leaf is a present user-RW small "
                         "page (`_wdleaf=%s`, `_wdap=0b11`, `_wdapx=0`).  With V-G2 (a full TLBIALL "
                         "ran and it still faults) this is H-X: the port's model of the "
                         "leaf-to-permission step is WRONG, and the next rung must read the CPU's "
                         "OWN state (SCTLR.M / a vector-time DFSR shadow), never another table read "
                         "(design §3b P-X1/§1 H-X)." % (
                             last["ln"], fmt(last["wdva"]), fmt(last["wdsplit"]),
                             fmt(None if last["wdttbr0"] is None else last["wdttbr0"] & 0xFFFFF000),
                             fmt(last["wdleaf"])))

    if decide:
        say("VERDICT", "[1015 P] VERDICT: %s %s" % pcell)
    else:
        say("OBS", "[1015 P] the §3b table would read %s (MEASURED, NOT ADJUDICATED - see above)" %
                   pcell[0])
        say("OBS", "  %s %s" % pcell)
    return pcell[0]


# ====================================================================================================
# [1015-AT] THE HARDWARE'S OWN ANSWER - `AT`/`PAR` (docs/1015-perm-static.md Item 6, "the killer key").
#
# Every other 1015 cell is a HOST-SIDE READ.  `_wd*` walks the page tables from the abort context;
# `_wf*` reads the pmap's flush decision; the storm itself is read off the abort ladder.  None of
# them can see the TRANSLATION THE CPU IS ACTUALLY USING.  `AT` can: it asks the MMU to walk the VA
# with its CURRENT TLB state and hands back a PAR, and PAR bit0 = 1 means the MMU faults that
# access.  So the cell below is the one that does not need the press at all - and the one whose
# failure mode (`_wdatok=0` / T-CTL unsatisfied on `_wdctlat`, V-AT0) is "the leg was void", never
# "H-G refuted".  V-AT4 is a READING - both views fault - not a withhold.
# ====================================================================================================
def s_at(cap, storm):
    """[1015-AT] THE §3c TABLE - did the HARDWARE translate `0xe6000` for a user write?

    The table has FIVE cells and is keyed on BOTH PARs, which is what separates "the CPU denies the
    user view" from "the CPU denies everything":

    | cell  | `_wdatpar` (user WRITE) | `_wdatparpr` (priv READ) | reading |
    |-------|-------------------------|--------------------------|---------|
    | V-AT0 | --                      | --                       | AT_INSTRUMENT_BROKEN - WITHHOLD |
    | V-AT1 | bit0 = 1 (FAULTS)       | bit0 = 0 (allows)        | H-G CONFIRMED AT THE HARDWARE |
    | V-AT2 | bit0 = 0, PA = leaf PA  | bit0 = 0                 | translation CORRECT yet the store faulted |
    | V-AT3 | bit0 = 0, PA != leaf PA | bit0 = 0                 | WRONG TRANSLATION IN THE TLB |
    | V-AT4 | bit0 = 1 (FAULTS)       | bit0 = 1 (FAULTS)        | BOTH VIEWS FAULT - the user-AP story is not the whole story |

    `_wdatok=1` and a tail row gate everything (V-AT0).  The row used is the LAST tail row (the same
    discipline the §3b ladder uses).  Returns `(cell, detail)`, or `(None, ...)` when the keyspace is
    absent ENTIRELY - a pre-AT capture gets NO cell, exactly as a pre-1015 capture gets no P cell.

    **THE `_wdatparpr` COLLISION - REPORTED, THEN FIXED BY RULING (2026-10-15).**  The first draft of
    `docs/1015-design.md` §3c named `_wdatparpr` TWICE with two different meanings: the table's
    "priv. read" column (the storm VA's privileged view), and the paragraph above it (the W-SYNC
    CONTROL VA's AT) - one key, two definitions ([[mi4-one-value-two-definitions]]).  The lead's
    ruling split them: the control VA's AT is **`_wdctlat`**, `_wdatparpr` keeps the storm VA's
    privileged read, and the recorder folds the control's success into `_wdatok`.

    So the reader's job here changed from *detecting a collision* to *checking the control*.  It
    checks `_wdctlat` **directly** and withholds (V-AT0) if it is absent or unsatisfied - even though
    the recorder is also supposed to have folded it into `_wdatok`, because a gate published as a
    single bit is a status the reader cannot audit, and a status is a verdict only if its producer
    delivered one ([[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]).  The two
    checks are independent readings of the same fact, which is the point.

    A DEFENSIVE CARRY-OVER, kept deliberately: if a row's `_wdatparpr` still names the control VA's PA
    (`0x80080000`), the recorder has not made the change, the key carries two meanings again, and the
    priv-read discriminator that separates V-AT1 from V-AT4 is unavailable - V-AT0, with the reason
    named.  It costs one comparison and it refuses a silently-unfixed recorder."""
    print("\n== [1015-AT] THE HARDWARE'S OWN ANSWER - `AT`/`PAR` at the storm (design §3c) ==")
    STORM_VA = storm["va"]   # the storm's OWN far, computed by value (never the module literal)
    _wd_hint = [r for r in wd_records(cap)
                if r["wdva"] == STORM_VA and r["wdleaf"] is not None
                and (r["wdleaf"] & 0x03) == 0x03]
    if _wd_hint:
        AT_PA_EXPECT = _wd_hint[-1]["wdleaf"] & 0xFFFFF000
    rows = at_records(cap)
    if not rows:
        say("OBS", "no `%s` row in this capture - the AT leg was not armed on this build (a pre-AT "
                   "capture carries no cell here, exactly as a pre-1015 capture carries no P cell)"
            % AT_OK_KEY)
        return None, {"rows": 0}
    tail = [r for r in rows if r["pcn"] is not None and r["pcn"] >= TAIL_MIN_N]
    say("OBS", "`%s` rows x%d; at the tail (`_pcn >= %s`): x%d" % (
        AT_OK_KEY, len(rows), fmt(TAIL_MIN_N), len(tail)))
    for r in rows[-3:]:
        say("OBS", "  AT row @:%d pcn=%s atok=%s sctlr=%s | user-WRITE par=%s priv-READ par=%s "
                   "T-CTL(control VA) par=%s" % (
            r["ln"], fmt(r["pcn"]), fmt(r["wdatok"]), fmt(r["wdsctlr"]), fmt(r["wdatpar"]),
            fmt(r["wdatparpr"]), fmt(r["wdctlat"])))

    # ---- V-AT0, FIRST AND UNCONDITIONALLY: nothing below is a reading unless the reads ran.  Every
    # spelling of "void" folds here, EACH WITH ITS OWN REASON - a withhold that does not say why is a
    # status whose producer did not deliver one.
    if not tail:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: x%d `%s` row(s) exist but NONE is at a "
                         "tail ordinal (`_pcn >= %s`), so there is no pre-storm AT reading to "
                         "adjudicate.  WITHHOLD V-AT1..4 (design §3c: a void leg must not be read as "
                         "a confirmation OR a refutation)." % (
                             len(rows), AT_OK_KEY, fmt(TAIL_MIN_N)))
        return "V-AT0", {"rows": len(rows)}

    # ---- THE STORM'S OWN AT ROW, SELECTED BY VALUE.  The tail's LAST AT row is NOT necessarily the
    # storm's: the tail is a MIX of faults (doc 1014 §2 - the run's last samples are a DIFFERENT
    # proc's fault), and the AT keys are published once per abort, so `tail[-1]` was a DIFFERENT VA
    # (`pcfar=0x3b2d28e0`) than the storm (`pcfar=0x27000`).  Select on the row's OWN `_pcfar`
    # matching the storm's computed VA, falling back to the last tail row only when no storm-keyed
    # AT row exists (then it is said so, not silently substituted).
    # `_wdva` FIRST (it is the row's own block VA and does not lag); `_pcfar` only as a fallback for
    # a capture whose AT rows carry no `_wdva` (an older spelling), where the lag is acknowledged.
    storm_at = [r for r in tail if r.get("wdva") == STORM_VA]
    if not storm_at:
        storm_at = [r for r in tail if r["pcfar"] == STORM_VA]
    if not storm_at:
        say("OBS", "  NO tail AT row carries `_pcfar=%s` (the storm's own VA): the cell below is "
                   "read off a DIFFERENT fault's AT row - report the mismatch, do not attribute it "
                   "to the storm" % fmt(STORM_VA))
        last = tail[-1]
    else:
        last = storm_at[-1]
    if last["wdatok"] != 1:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: the storm's own tail row @:%d publishes "
                         "`_wdatok=%s`, so the AT reads did not run - or the T-CTL control read did "
                         "not come back as the design requires, which the recorder folds into this "
                         "bit - and there is NO hardware answer on this run.  WITHHOLD V-AT1..4; "
                         "rebuild and confirm the AT leg fires before the handler AND that "
                         "`_wdctlat` satisfies T-CTL (design §3c's V-AT0)." % (
                             last["ln"], fmt(last["wdatok"])))
        return "V-AT0", {"rows": len(rows)}
    par, pr = last["wdatpar"], last["wdatparpr"]
    if par is None or pr is None:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: the storm's tail row @:%d carries "
                         "`_wdatpar=%s` / `_wdatparpr=%s` - the STORM VA's two views are not both "
                         "published, so the cell cannot be read (every §3c cell below V-AT0 needs "
                         "BOTH columns).  WITHHOLD." % (last["ln"], fmt(par), fmt(pr)))
        return "V-AT0", {"rows": len(rows), "par": par, "pr": pr}
    if (pr & PAR_PA_MASK) == AT_CTL_PA and (pr & PAR_FAULT_BIT) == 0:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: the storm row @:%d publishes "
                         "`_wdatparpr=%s`, which is the W-SYNC **CONTROL** VA's PA (`%s`), not the "
                         "storm VA's privileged view.  `docs/1015-design.md` §3c's first draft gave "
                         "`_wdatparpr` TWO definitions (the table's priv-read column AND the T-CTL "
                         "control read) and the lead's ruling gave the control its own key "
                         "`_wdctlat` - this row still carries the OLD shape, so the priv-read "
                         "discriminator that separates V-AT1 from V-AT4 cannot be read from it "
                         "([[mi4-one-value-two-definitions]]).  WITHHOLD; the recorder must publish "
                         "the control under `_wdctlat`." % (last["ln"], fmt(pr), fmt(AT_CTL_PA)))
        return "V-AT0", {"rows": len(rows), "par": par, "pr": pr, "collision": True}
    # ---- T-CTL, THE LEG'S OWN NULL CONTROL, READ ON ITS OWN KEY `_wdctlat` (the lead's ruling).
    # The recorder folds the same fact into `_wdatok`; the reader checks it here as well because a
    # one-bit gate is not auditable and the AT instrument's whole claim is that its silence is
    # readable ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
    ctl = last["wdctlat"]
    if ctl is None:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: the storm's tail row @:%d publishes no "
                         "`_wdctlat`, so the T-CTL control read (the W-SYNC control VA's AT, which "
                         "must return `par & 1 == 0` with PA `%s`) was not published and the AT "
                         "instrument's own null control is unavailable.  WITHOUT the control a "
                         "faulting user view cannot be distinguished from a broken recorder.  "
                         "WITHHOLD V-AT1..4." % (last["ln"], fmt(AT_CTL_PA)))
        return "V-AT0", {"rows": len(rows), "par": par, "pr": pr, "ctl": None}
    ctl_pa, ctl_fault = ctl & PAR_PA_MASK, (ctl & PAR_FAULT_BIT) == 1
    ctl_ok = (not ctl_fault) and ctl_pa == AT_CTL_PA
    say("PASS" if ctl_ok else "OBS",
        "  T-CTL (`_wdctlat=%s`, the W-SYNC control VA): MMU %s -> PA=%s; the design requires "
        "`par & 1 == 0` with PA `%s`  =>  %s" % (
            fmt(ctl), "FAULTS" if ctl_fault else "allows", fmt(ctl_pa), fmt(AT_CTL_PA),
            "CONTROL SATISFIED (the instrument works, so the storm VA's reading is meaningful)"
            if ctl_ok else
            "CONTROL NOT SATISFIED - the AT instrument itself is broken on this run"))
    if not ctl_ok:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: T-CTL FAILED on the storm's own tail "
                         "row @:%d.  `_wdctlat=%s` (MMU %s, PA %s) where the W-SYNC control VA's AT "
                         "must allow with PA `%s`.  The control read is the leg's NULL CONTROL: "
                         "without it a faulting user view is indistinguishable from a recorder that "
                         "wrote the wrong VA, used the wrong register, or skipped its ISB, so the "
                         "reading cannot be attributed and MUST NOT be read as V-AT1..4.  (The "
                         "recorder folds this into `_wdatok`; the reader checks it independently - "
                         "design §3c's T-CTL.)" % (
                             last["ln"], fmt(ctl), "FAULTS" if ctl_fault else "allows",
                             fmt(ctl_pa), fmt(AT_CTL_PA)))
        return "V-AT0", {"rows": len(rows), "par": par, "pr": pr, "ctl": ctl, "ctl_ok": False}
    fault_u, fault_p = (par & PAR_FAULT_BIT) == 1, (pr & PAR_FAULT_BIT) == 1
    pa = par & PAR_PA_MASK
    say("PASS" if (fault_u and not fault_p) else "OBS",
        "the AT leg RAN at the storm (@:%d): user-WRITE par=%s (MMU %s), priv-READ par=%s (MMU %s "
        "-> PA=%s)" % (last["ln"], fmt(par), "FAULTS" if fault_u else "allows", fmt(pr),
                       "FAULTS" if fault_p else "allows", fmt(pr & PAR_PA_MASK)))
    if last["wdsctlr"] is not None:
        say("OBS" if (last["wdsctlr"] & SCTLR_M_BIT) else "PASS",
            "  SCTLR at the abort (`_wdsctlr=%s`): M=%d - %s" % (
                fmt(last["wdsctlr"]), last["wdsctlr"] & SCTLR_M_BIT,
                "the MMU was ENABLED, so a PAR that faults is a permission/translation decision and "
                "not 'the MMU was off'" if (last["wdsctlr"] & SCTLR_M_BIT) else
                "THE MMU WAS DISABLED (M=0) AT THE ABORT - every PAR reading is void"))

    # ---- V-AT4: BOTH views fault.  Placed BEFORE V-AT1 because it is the one case where the user
    # view faulting does NOT isolate a user-AP decision (the design: "the entry is not merely
    # user-denied; a domain/invalid mechanism is in play").
    if fault_u and fault_p:
        verdict("V-AT4", "BOTH VIEWS FAULT - the user-AP story is NOT the whole story.  The MMU "
                         "refuses BOTH the user write (par=%s) and the PRIVILEGED read (par=%s) of "
                         "the storm VA `%s`, so the entry is not merely user-denied: a domain / "
                         "invalid / SCTLR mechanism is in play.  Re-rank toward `docs/1015-perm-"
                         "static.md` cause #2 (the CPU's own state - SCTLR/PRRR, now measured as "
                         "`_wdsctlr`) before `docs/1015-design.md` §7." % (
                             fmt(par), fmt(pr), fmt(STORM_VA)))
        return "V-AT4", {"rows": len(rows), "par": par, "pr": pr}

    if not fault_u:
        # V-AT2 / V-AT3 - the user view SUCCEEDS, so the store's FS=15 is not produced by this
        # translation either way.  Which one turns on the PA, and the design's own PA is §0.3's.
        if pa == AT_PA_EXPECT:
            verdict("V-AT2", "TRANSLATION IS CORRECT YET THE STORE FAULTED - PROBE DEFECT, NOT H-G. "
                             "The MMU ALLOWS the user write to `%s` (par=%s) with the privileged "
                             "read also allowed (par=%s), and it names PA %s - exactly the PA the "
                             "storm's own leaf (`_wdleaf=0x841e91bb`) names.  So the store's FS=15 "
                             "WnR=1 is NOT produced by this translation and the DFSR/DFAR pair and "
                             "the frame describe a DIFFERENT access from the one this probe "
                             "translates: re-examine the probe's ordering and key (the "
                             "`g_abtpte_curva` write in `__wrap_sleh_abort`).  This is NOT a "
                             "page-table defect and NOT evidence about a stale TLB entry." % (
                                 fmt(STORM_VA), fmt(par), fmt(pr), fmt(pa)))
            return "V-AT2", {"rows": len(rows), "par": par, "pr": pr}
        verdict("V-AT3", "WRONG TRANSLATION IN THE TLB - the MMU translates the storm VA `%s` "
                         "SUCCESSFULLY (par=%s, priv read par=%s) but to PA %s, NOT the PA the "
                         "storm's own leaf (`_wdleaf=0x841e91bb`) names (%s).  A TLB entry is "
                         "serving a translation the tables no longer hold - the same conclusion as "
                         "V-AT1, reached from the PA side, and it names the STALE PA BY VALUE." % (
                             fmt(STORM_VA), fmt(par), fmt(pr), fmt(pa), fmt(AT_PA_EXPECT)))
        return "V-AT3", {"rows": len(rows), "par": par, "pr": pr}

    # ---- V-AT1: the user view FAULTS while the privileged view SUCCEEDS.  That asymmetry is the
    # signature, but it only identifies a stale entry if the WALK independently says the access MUST
    # be allowed - otherwise "user-AP denies it" is the honest reading and calling it H-G would be
    # the one-value-two-definitions error ([[mi4-one-value-two-definitions]]).  The design's own V-AT1
    # text states this precondition ("even though the walked leaf is `ap=0b11`, `apx=0`,
    # `domain=0`, `nG=0`"), so it is checked rather than assumed.
    #
    # **ON THE `wdng` SPELLING**: the clause as handed over is `wdng==0`; the published keyspace's key
    # for the raw nG bit is `xnu_live_abt_leafng` (design §2b deliberately gives it a name that does
    # NOT start with `wd`).  This reader reads `_leafng` and says so rather than inventing a key.
    wd_storm = [r for r in wd_records(cap)
                if r["wdn"] is not None and r["wdn"] >= TAIL_MIN_N
                and r["wdva"] == STORM_VA]
    if not wd_storm:
        wd_storm = [r for r in wd_records(cap)
                    if r["wdn"] is not None and r["wdn"] >= TAIL_MIN_N
                    and r["pcfar"] == STORM_VA]
    if not wd_storm:
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: the user view FAULTS and the privileged "
                         "view succeeds at the storm, but no `wd*` storm row is present to show the "
                         "walk calls that access user-RW, so the asymmetry the cell needs is only "
                         "half-established.  WITHHOLD (V-AT0), not V-AT1.")
        return "V-AT0", {"rows": len(rows), "par": par, "pr": pr, "wd": 0}
    w = wd_storm[-1]
    clause = {"wdap==0b11": w["wdap"] == 0b11, "wdapx==0": w["wdapx"] == 0,
              "leafng==0 (GLOBAL)": w["leafng"] == 0, "wddom==0": w["wddom"] == 0}
    if not all(clause.values()):
        bad = ", ".join(k for k, v in clause.items() if not v)
        verdict("V-AT0", "AT_INSTRUMENT_BROKEN - NO VERDICT: the user view FAULTS and the privileged "
                         "view succeeds, but the walk does NOT clear every precondition (failing: "
                         "%s) on the paired storm row @:%d (`_wdleaf=%s`, `_wdap=%s`, `_wdapx=%s`, "
                         "`_leafng=%s`, `_wddom=%s`) - so the hardware and the walk may simply "
                         "AGREE on a genuine user-AP denial, and reading H-G out of that would be "
                         "the one-value-two-definitions error.  WITHHOLD (V-AT0), not V-AT1." % (
                             bad, w["ln"], fmt(w["wdleaf"]), fmt(w["wdap"]), fmt(w["wdapx"]),
                             fmt(w["leafng"]), fmt(w["wddom"])))
        return "V-AT0", {"rows": len(rows), "par": par, "pr": pr, "clause": clause}
    verdict("V-AT1", "H-G CONFIRMED AT THE HARDWARE - STALE_GLOBAL_TLB, by MEASUREMENT.  The MMU "
                     "FAULTS an `AT` user-WRITE of the storm VA `%s` (par=%s, bit0=1) while the SAME "
                     "`AT` pair's privileged read SUCCEEDS (par=%s) and the walk from the SAME abort "
                     "context says the access MUST be allowed: `_wdleaf=%s` is present, "
                     "`_wdap=0b11`, `_wdapx=0`, GLOBAL (`_leafng=0`) and in domain 0.  A USER-denied "
                     "but PRIVILEGED-allowed translation of a leaf the tables call user-RW is only "
                     "satisfiable by a translation that is NOT the leaf the page tables hold - a "
                     "stale/other TLB entry.  This is the ONE cell that reads the CPU's own "
                     "TRANSLATION rather than a page table, so it does not depend on the flush "
                     "widening and it is DECIDED WITHOUT the press's ambiguity (design §3c).  The "
                     "fix is `docs/1015-design.md` §7: make the user-pmap flush global/VA-scoped, or "
                     "set `nG` on user leaves." % (fmt(STORM_VA), fmt(par), fmt(pr), fmt(w["wdleaf"])))
    return "V-AT1", {"rows": len(rows), "par": par, "pr": pr, "clause": clause, "wd": w["wdn"]}


def s_at_cross(primary_cell, at_cell):
    """THE CROSS-CELL READING: what the hardware half and the experiment half say TOGETHER.

    Added because the two halves answer DIFFERENT questions and their combination is stronger (or
    more alarming) than either alone.  The design's own framing is that V-AT1 is press-independent,
    so it is the cell a V-G2 must be reconciled with: 'a full TLBIALL ran and it still faults' and
    'the hardware translates this VA differently from the walked leaf' cannot both be true unless
    the full flush failed to evict, which is itself the H-G mechanism surviving the fix."""
    print("\n== [1015-AT x PRIMARY] THE TWO HALVES TOGETHER ==")
    if at_cell is None:
        say("OBS", "no AT cell on this capture - the cross-reading is unavailable (nothing to "
                   "reconcile)")
        return
    # THE §3d STITCH TABLE, in the design's own order.  `V-AT1`/`V-AT3` are the "the hardware's
    # translation is NOT the leaf" pair; the design stitches them identically against V-G.
    not_leaf = at_cell in ("V-AT1", "V-AT3")
    if not_leaf and primary_cell == "V-G1":
        say("PASS", "H-G CONFIRMED, CONSISTENT - THE STRONGEST CELL IN THE RUNG.  The hardware "
                    "disagreed with the page table (%s) AND widening the flush to a full TLBIALL "
                    "removed the fault (V-G1).  Two independent instruments, one mechanism: take "
                    "design §7(b); §7(a) is the principled follow-on." % at_cell)
    elif not_leaf and primary_cell == "V-G2":
        say("VERDICT", "[1015-AT x PRIMARY] CONFIRMED-TRANSLATION, UNFIXED-BY-ONE-FLUSH.  The stale "
                       "entry is REAL (the hardware's translation is not the walked leaf - %s) and "
                       "it SURVIVES a full `arm_tlb_flushID()`.  So a global invalidate at "
                       "`pmap_flush_tlbs` does not reach it: wrong moment, another CPU, or a second "
                       "entry.  **V-G2's 'H-G REFUTED' MUST NOT be read alone here** - do not "
                       "discard H-G; enumerate the entries (design §3d)." % at_cell)
    elif not_leaf and primary_cell == "V-G3":
        say("PASS", "THE HARDWARE HALF CARRIES THE RUNG: the experiment half is VACUOUS (the "
                    "widened flush never landed - V-G3), but the AT leg was read BEFORE any flush "
                    "and it shows the hardware's translation is not the leaf (%s).  The two are "
                    "consistent: V-G3 is about whether the POKE landed, %s is about what the CPU's "
                    "TLB held, and the hardware reading does not depend on the poke." % (
                        at_cell, at_cell))
    elif at_cell == "V-AT2" and primary_cell == "V-G1":
        say("VERDICT", "[1015-AT x PRIMARY] CONTRADICTION - the hardware translated the write "
                       "CORRECTLY yet the storm stopped when the flush widened (V-G1).  The stop "
                       "was caused by something other than this translation: re-read the observer "
                       "before trusting either cell (design §3d).")
    elif at_cell == "V-AT2" and primary_cell == "V-G2":
        say("PASS", "H-G REFUTED FROM BOTH SIDES - the ONLY clean refutation in the rung: the "
                    "hardware translates the write to the leaf's own PA (V-AT2) AND the storm "
                    "survives a full TLBIALL (V-G2).  Go to §3b's P-X1 and then to the CPU's own "
                    "state (SCTLR/PRRR, measured as `_wdsctlr`).")
    elif at_cell == "V-AT0":
        say("VERDICT", "[1015-AT x PRIMARY] NO VERDICT - the AT never ran (V-AT0).  The behaviour "
                       "column (%s) cannot carry the rung alone; rebuild with the AT leg armed "
                       "(design §3d)." % primary_cell)
    elif at_cell == "V-AT4":
        say("OBS", "the AT's BOTH-VIEWS-FAULT reading (V-AT4) at primary %s: the user-AP story is "
                   "not the whole story, so re-rank toward perm-static cause #2 (the CPU's own "
                   "state, `_wdsctlr`) before design §7.  §3d stitches V-AT4 only through V-AT0's "
                   "'no verdict' rule and the §3b ladder." % primary_cell)
    else:
        say("OBS", "AT cell %s with primary %s: both measured; §3d's stitch names no combined cell "
                   "for this pair." % (at_cell, primary_cell))


# ====================================================================================================
# THE SELF-TEST - synthetic captures, each asserting the cell a wrong table would get wrong.
# ====================================================================================================
def _mk_capture(tmpdir, name, lines):
    path = os.path.join(tmpdir, name)
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def _row(n, far, fsr=STORM_FSR, pc=0x2BED61A0, wfkind=None, wfseen=1, leaf=0x841E91BB,
         dacr=0x55555555, dom=0, ttbcr=2, ttbr0=0x81A86000, thrmap=0x81A86000, mapok=1,
         seen=1, ctlok=1, ctl=0x11, leafng=0, small=1, wdva=None, atpar=None, atpr=None,
         atctl=None, atok=None, sctlr=0x00C5387D):
    """One synthetic tail abort at ordinal `n`, with the flush row (if `wfkind` is not None), the
    passive row (if `seen` is not None) and the AT row (if `atok` is not None) published beside it -
    the SAME `_pcn` ordinal, which is how the recorders' rows pair in a real capture."""
    out = ["xnu_live_abt_pcn=0x%08x" % n, "xnu_live_abt_pcfar=0x%08x" % far,
           "xnu_live_abt_fsr=0x%08x" % fsr, "xnu_live_abt_pc=0x%08x" % pc]
    if wfkind is not None:
        out.append("xnu_live_abt_wfn=0x%08x xnu_live_abt_wfva=0x%08x xnu_live_abt_wfttb=0x81a86000 "
                   "xnu_live_abt_wflive=0x81a86000 xnu_live_abt_wfself=1 xnu_live_abt_wfshared=0 "
                   "xnu_live_abt_wfncpus=1 xnu_live_abt_wfasid=0x02 xnu_live_abt_wfkind=%d "
                   "xnu_live_abt_wfseen=%d" % (n, far, wfkind, wfseen))
    if seen is not None:
        split = 0 if (ttbcr & 7) == 0 else (1 << (32 - (ttbcr & 7)))
        out.append("xnu_live_abt_wdn=0x%08x xnu_live_abt_wdva=0x%08x xnu_live_abt_leafng=%d "
                   "xnu_live_abt_wdempty=0 xnu_live_abt_wddacr=0x%08x xnu_live_abt_wddom=0x%x "
                   "xnu_live_abt_wdttbcr=0x%08x xnu_live_abt_wdsplit=0x%08x "
                   "xnu_live_abt_wdttbr0=0x%08x xnu_live_abt_wdttbr1=0x80004018 "
                   "xnu_live_abt_wdl1=0x81bb2001 xnu_live_abt_wdleaf=0x%08x "
                   "xnu_live_abt_wdap=%d xnu_live_abt_wdapx=%d xnu_live_abt_wdtex=%d "
                   "xnu_live_abt_wdcb=%d xnu_live_abt_wdsmall=%d xnu_live_abt_wdpcpu=0x80000000 "
                   "xnu_live_abt_wdthrmap=0x%08x xnu_live_abt_wdmapok=%d "
                   "xnu_live_abt_wdseen=%d xnu_live_abt_wdctl=0x%08x xnu_live_abt_wdctlok=%d" % (
                       n, (far if wdva is None else wdva), leafng, dacr, dom, ttbcr, split, ttbr0,
                       leaf,
                       (leaf >> 4) & 3, (leaf >> 9) & 1, (leaf >> 6) & 7, (leaf >> 2) & 3,
                       ((leaf >> 1) & 1) if small is None else small, thrmap, mapok, seen, ctl,
                       ctlok))
    if atok is not None:
        out.append("xnu_live_abt_wdatok=%s xnu_live_abt_wdatpar=0x%08x "
                   "xnu_live_abt_wdatparpr=0x%08x xnu_live_abt_wdctlat=0x%08x "
                   "xnu_live_abt_wdsctlr=0x%08x" % (
                       atok,
                       0x841E9000 if atpar is None else atpar,
                       0x841E9000 if atpr is None else atpr,
                       AT_CTL_PA if atctl is None else atctl,
                       sctlr))
    return out


def _storm_lines(storm_ns=(0x400, 0x800, 0x1000, 0x4000), **kw):
    """A synthetic tail whose `far=0xe6000` rows sit at the given ordinals - the storm's own shape
    (the 1014 capture's nine rows run 0x400..0x40000)."""
    lines = []
    for n in storm_ns:
        lines += _row(n, STORM_VA, **kw)
    return lines


def _run_reader_on(lines, tmpdir, name):
    """Run the WHOLE reader over a synthetic capture, capturing its verdicts.  The subprocess
    inheritance is disabled for the synthetic runs (their captures carry no 1013/1014 rows), so this
    exercises exactly the 1015 tables."""
    path = _mk_capture(tmpdir, name, lines)
    cap = Capture(path)
    storm = classify_storm(aborts(cap), TAIL_MIN_N)
    wf_rows = wf_records(cap)
    FAILURES[:] = []
    SOFT[:] = []
    REPORT[:] = []
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        cell, stopped = s_primary(cap, storm, wf_rows)
        pcell = s_secondary(cap, storm, cell == "V-G2", cell)
        atcell, _atdet = s_at(cap, storm)
        s_at_cross(cell, atcell)
    return cell, pcell, stopped, buf.getvalue(), atcell


def self_test(capture_arg=None):
    import tempfile
    cases = []
    tmpdir = tempfile.mkdtemp(prefix="check1015-selftest-")

    def check(desc, got, want, extra=""):
        ok = got == want
        cases.append((desc, got, want, ok, extra))
        print("  [%s] %s: got %s, want %s%s" % ("ok" if ok else "FAIL", desc, got, want,
                                                (" - " + extra) if extra else ""))

    # 1. V-G1: the storm STOPPED (one row, low ordinal) with the widened arm measured.
    cell, pcell, stopped, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400,), wfkind=1), tmpdir, "vg1.txt")
    check("V-G1 storm stopped + _wfkind=1 => CONFIRMED", cell, "V-G1", "stopped=%s" % stopped)

    # 2. V-G2: the storm PERSISTS at high ordinal with the widened arm.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000, 0x40000), wfkind=1), tmpdir, "vg2.txt")
    check("V-G2 storm persists at pcn>=0x4000 + _wfkind=1 => REFUTED", cell, "V-G2")

    # 3. V-G3: the storm persists BUT the flush witness never fired (`_wfseen=0`) - MUST NOT be V-G2.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000, 0x40000), wfkind=1, wfseen=0), tmpdir,
        "vg3-seen0.txt")
    check("V-G3 _wfseen=0 with a persisting storm => VACUOUS, NOT refuted", cell, "V-G3")
    # 3b. the OTHER spelling: `_wfkind = 0` (the gate was FALSE) - the same cell.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000, 0x40000), wfkind=0), tmpdir, "vg3-kind0.txt")
    check("V-G3 _wfkind=0 (flush refused) => VACUOUS", cell, "V-G3")
    # 3c. `_wfkind = 2` (the ASID arm - the widened arm was NOT taken) => also vacuous.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000, 0x40000), wfkind=2), tmpdir, "vg3-kind2.txt")
    check("V-G3 _wfkind=2 (ASID arm, poke never widened) => VACUOUS", cell, "V-G3")

    # 4. V-G4: the storm is GONE but another FS=15 VA sits at high ordinal.
    lines = _storm_lines(storm_ns=(0x400,), wfkind=1) + _row(0x40000, 0x3A1141E8, wfkind=1)
    cell, pcell, _s, _out, _at = _run_reader_on(lines, tmpdir, "vg4.txt")
    check("V-G4 storm gone, another FS=15 VA at high pcn => SYMPTOM_MOVED", cell, "V-G4")

    # 5. V-G5: no tail at all.
    cell, pcell, _s, _out, _at = _run_reader_on(["xnu_live_abt_pcn=0x1 xnu_live_abt_pcfar=0x1000"],
                                           tmpdir, "vg5.txt")
    check("V-G5 no tail rows => MEASUREMENT LOST", cell, "V-G5")

    # 6. P-D2: a persisting storm (V-G2) whose storm row has `_wddacr & 3 != 1` => DACR_NOT_CLIENT.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1, dacr=0x55555550), tmpdir,
        "pd2.txt")
    check("P-D2 (V-G2 + wddacr&3 != 1) => DACR_NOT_CLIENT", pcell, "P-D2", "primary=%s" % cell)

    # 7. P-X1: a persisting storm with every input correct => INPUTS_ALL_CORRECT_STILL_FAULTS.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1), tmpdir, "px1.txt")
    check("P-X1 (V-G2 + every §3b clause killed) => INPUTS_ALL_CORRECT", pcell, "P-X1")

    # 8. P-CTL: the control read was never taken.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1, ctlok=0), tmpdir, "pctl.txt")
    check("P-CTL (_wdctlok=0) => NO_PERM_WITNESS", pcell, "P-CTL")

    # 9. P-D1 / P-L1 / P-L3 / P-T2 - one row each, on a persisting storm.
    for want, kw, desc in (
            ("P-D1", dict(dom=3), "P-D1 (_wddom != 0) => L1_DOMAIN_NONZERO"),
            ("P-L3", dict(small=0), "P-L3 (_wdsmall=0) => NOT_A_SMALL_PAGE"),
            ("P-T2", dict(ttbr0=0x81A86000, wdva=0xC0001000), "P-T2 (the witness's OWN `_wdva` is at/"
                                                      "above the N=2 split 0x40000000) => "
                                                      "ROUTED_TO_TTBR1"),
    ):
        cell, pcell, _s, _out, _at = _run_reader_on(
            _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1, **kw), tmpdir,
            "p-%s.txt" % want)
        check(desc, pcell, want)
    # P-L1 needs a leaf whose AP/APX are NOT user-writable; the `_wdap`/`_wdapx` cells are then
    # consistent with the leaf, so this must reach P-L1 rather than a HARD stale-decode.
    cell, pcell, _s, _out, _at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1, leaf=0x841E91AB), tmpdir, "pl1.txt")
    check("P-L1 (leaf not user-writable) => LEAF_NOT_USER_WRITABLE", pcell, "P-L1")

    # 10. THE HARD STALE-DECODE: a published `_wdap` that is NOT the decode of `_wdleaf`.
    lines = _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1)
    lines = [ln.replace("xnu_live_abt_wdap=3", "xnu_live_abt_wdap=1") for ln in lines]
    path = _mk_capture(tmpdir, "staledecode.txt", lines)
    cap = Capture(path)
    FAILURES[:] = []
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        s_p_leaves([r for r in wd_records(cap) if r["wdn"] is not None and r["wdn"] >= TAIL_MIN_N])
    check("HARD: a published _wdap that is not the decode of _wdleaf is HARD",
          "yes" if FAILURES else "no", "yes", buf.getvalue().strip().split("\n")[0][:90])

    # 12. [1015-AT] THE HARDWARE'S OWN ANSWER - the §3c table.
    # 12a. V-AT1: user view FAULTS, PRIV view SUCCEEDS, and the paired walk clears every clause.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x00000001,
                     atpr=0x841E9000), tmpdir, "vat1.txt")
    check("V-AT1 (user view faults, priv view succeeds, walk says user-RW/GLOBAL/dom0) => H-G at "
          "the hardware", at, "V-AT1", "primary=%s" % cell)

    # 12b. V-AT2: both views allow AND the PA is the leaf's own => the fault is not this translation.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=AT_PA_EXPECT,
                     atpr=0x841E9000), tmpdir, "vat2.txt")
    check("V-AT2 (both views allow, PA == the walked leaf's own, store still faults) => PROBE "
          "DEFECT", at, "V-AT2")

    # 12c. V-AT3: both views allow but the PA is a DIFFERENT one => present-but-wrong translation.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x80000000,
                     atpr=0x841E9000), tmpdir, "vat3.txt")
    check("V-AT3 (both views allow, PA != the walked leaf's) => WRONG TRANSLATION", at, "V-AT3")

    # 12d. V-AT4: BOTH views FAULT => the user-AP story is not the whole story.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x00000001,
                     atpr=0x00000001), tmpdir, "vat4.txt")
    check("V-AT4 (user AND priv views both fault) => BOTH VIEWS FAULT, not V-AT1", at, "V-AT4")

    # 12e. V-AT0: the leg was void (`_wdatok=0`) => WITHHELD, explicitly NOT V-AT1..4.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=0, atpar=0x00000001,
                     atpr=0x841E9000), tmpdir, "vat0.txt")
    check("V-AT0 (_wdatok=0 => the AT leg was void) => WITHHELD, NOT read as V-AT1", at, "V-AT0")

    # 12f. V-AT0's second spelling: an AT row exists but NONE at a tail ordinal.
    lines = _row(0x1, STORM_VA, atok=1, atpar=0x00000001, atpr=0x841E9000)
    lines += _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2)
    cell, pcell, _s, _out, at = _run_reader_on(lines, tmpdir, "vat0b.txt")
    check("V-AT0 (an AT row exists but none at the tail) => WITHHELD", at, "V-AT0")

    # 12g. V-AT0's third spelling: the `_wdatparpr` COLLISION - the OLD shape, checked on the
    #      CORRECTED TWO-KEY layout.  The row publishes BOTH the ruling's new `_wdctlat` AND a
    #      `_wdatparpr` that carries the W-SYNC CONTROL VA's PA (i.e. a recorder that still gives the
    #      one key two definitions).  MUST withhold and NAME the collision, not guess V-AT1/V-AT4 off
    #      a control reading.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x00000001,
                     atpr=AT_CTL_PA, atctl=AT_CTL_PA), tmpdir, "vat0coll.txt")
    check("V-AT0 (_wdatparpr carries the CONTROL VA's PA - one key, two definitions) => WITHHELD",
          at, "V-AT0")

    # 12g-bis. THE CORRECTED SHAPE, THE HOLE THE COLLISION TEST DOES NOT COVER: a row whose
    #      `_wdatparpr` is CORRECT (the storm VA's privileged read) but which publishes NO `_wdctlat`
    #      at all - the recorder never landed the ruling's new key.  The T-CTL null control is
    #      unavailable, so the leg's silence is not readable and this MUST withhold rather than pass
    #      on `_wdatok`'s one bit (a status is a verdict only if its producer delivered one -
    #      [[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]).
    lines = _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x00000001,
                         atpr=0x841E9000)
    lines = [re.sub(r' xnu_live_abt_wdctlat=0x[0-9a-f]+', '', ln) for ln in lines]
    cell, pcell, _s, _out, at = _run_reader_on(lines, tmpdir, "vat0noctl.txt")
    check("V-AT0 (no `_wdctlat` published - the T-CTL control is unavailable) => WITHHELD, "
          "NOT read off `_wdatok`'s one bit", at, "V-AT0")

    # 12g-ter. THE CORRECTED SHAPE'S CONTROL FAILS: `_wdctlat` IS published, but the control VA's AT
    #      FAULTS (or names a different PA) - the AT instrument itself is broken on this run, so the
    #      storm VA's faulting user view is indistinguishable from a mis-aimed probe.  WITHHOLD.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x00000001,
                     atpr=0x841E9000, atctl=0x00000001), tmpdir, "vat0ctlbad.txt")
    check("V-AT0 (T-CTL FAILED: the control VA's AT faults) => WITHHELD, NOT V-AT1", at, "V-AT0")

    # 12h. A FURTHER V-AT0 spelling (the conservative half of V-AT1): the hardware's user view faults
    #      and the priv view succeeds, but the walk does NOT clear every clause (leaf not
    #      user-writable) - the hardware and the walk may simply AGREE, so this MUST be V-AT0.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2, atok=1, atpar=0x00000001,
                     atpr=0x841E9000, leaf=0x841E91AB), tmpdir, "vat1-no.txt")
    check("V-AT0 (user view faults but the walk does NOT say user-RW: hardware and walk agree) => "
          "WITHHELD, NOT V-AT1", at, "V-AT0")

    # 12i. THE FROZEN-1014-SHAPED CONTROL: NO AT keys at all gets NO cell (None), never a withhold.
    cell, pcell, _s, _out, at = _run_reader_on(
        _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=2), tmpdir, "vat-absent.txt")
    check("no AT keys at all => NO AT CELL (None), not a withhold", at, None)

    # 12j-12m. THE §3d STITCH - each of the design's four stitched rows, plus the V-AT0 row.
    def _stitch(atpar, atpr, storm_ns, wfkind, tag):
        lines = _storm_lines(storm_ns=storm_ns, wfkind=wfkind, atok=1, atpar=atpar, atpr=atpr)
        path = _mk_capture(tmpdir, tag + ".txt", lines)
        cap = Capture(path)
        storm = classify_storm(aborts(cap), TAIL_MIN_N)
        FAILURES[:] = []
        SOFT[:] = []
        REPORT[:] = []
        b = io.StringIO()
        with contextlib.redirect_stdout(b):
            g, _st = s_primary(cap, storm, wf_records(cap))
            a, _d = s_at(cap, storm)
            s_at_cross(g, a)
        return g, a, b.getvalue()

    # 12j. V-AT1/V-AT3 + V-G1 => the design's strongest cell.
    g, a, txt = _stitch(0x00000001, 0x841E9000, (0x400,), 1, "stitch-1")
    check("§3d: V-AT1 + V-G1 => H-G CONFIRMED, CONSISTENT (the strongest cell)",
          "yes" if ("STRONGEST CELL" in txt and g == "V-G1" and a == "V-AT1") else "no", "yes",
          "primary=%s at=%s" % (g, a))
    # 12k. V-AT1/V-AT3 + V-G2 => confirmed translation, unfixed by one flush (NOT "H-G REFUTED").
    g, a, txt = _stitch(0x00000001, 0x841E9000, (0x400, 0x1000, 0x4000, 0x40000), 1, "stitch-2")
    check("§3d: V-AT1 + V-G2 => CONFIRMED-TRANSLATION, UNFIXED-BY-ONE-FLUSH (not 'H-G REFUTED')",
          "yes" if ("UNFIXED-BY-ONE-FLUSH" in txt and g == "V-G2" and a == "V-AT1") else "no", "yes",
          "primary=%s at=%s" % (g, a))
    # 12l. V-AT2 + V-G1 => the design's CONTRADICTION row.
    g, a, txt = _stitch(AT_PA_EXPECT, 0x841E9000, (0x400,), 1, "stitch-3")
    check("§3d: V-AT2 + V-G1 => CONTRADICTION (the stop was not caused by this translation)",
          "yes" if ("CONTRADICTION" in txt and g == "V-G1" and a == "V-AT2") else "no", "yes",
          "primary=%s at=%s" % (g, a))
    # 12m. V-AT2 + V-G2 => the ONLY clean refutation in the rung.
    g, a, txt = _stitch(AT_PA_EXPECT, 0x841E9000, (0x400, 0x1000, 0x4000, 0x40000), 1, "stitch-4")
    check("§3d: V-AT2 + V-G2 => H-G REFUTED FROM BOTH SIDES (the only clean refutation)",
          "yes" if ("REFUTED FROM BOTH SIDES" in txt and g == "V-G2" and a == "V-AT2") else "no",
          "yes", "primary=%s at=%s" % (g, a))
    # 12n. V-AT0 + any => NO VERDICT (the behaviour column cannot carry the rung alone).
    g, a, txt = _stitch(0x00000001, 0x841E9000, (0x400, 0x1000, 0x4000), 1, "stitch-5")
    # force V-AT0 by making the leg void
    lines = _storm_lines(storm_ns=(0x400, 0x1000, 0x4000), wfkind=1, atok=0, atpar=0x00000001,
                         atpr=0x841E9000)
    path = _mk_capture(tmpdir, "stitch-6.txt", lines)
    cap = Capture(path)
    storm = classify_storm(aborts(cap), TAIL_MIN_N)
    FAILURES[:] = []
    SOFT[:] = []
    REPORT[:] = []
    b = io.StringIO()
    with contextlib.redirect_stdout(b):
        g, _st = s_primary(cap, storm, wf_records(cap))
        a, _d = s_at(cap, storm)
        s_at_cross(g, a)
    check("§3d: V-AT0 + any => NO VERDICT (the behaviour column cannot carry the rung alone)",
          "yes" if ("NO VERDICT" in b.getvalue() and a == "V-AT0") else "no", "yes",
          "primary=%s at=%s" % (g, a))
    # 12o. THE TLSHY-ABSENT `_wdsctlr`: an AT row that publishes no SCTLR must still read, with the
    #      `_wdsctlr` line simply absent (the design flags it as a plumbing gap, not a verdict).
    lines = _storm_lines(storm_ns=(0x400,), wfkind=2, atok=1, atpar=0x00000001, atpr=0x841E9000)
    lines = [re.sub(r' xnu_live_abt_wdsctlr=0x[0-9a-f]+', '', ln) for ln in lines]
    cell, pcell, _s, _out, at = _run_reader_on(lines, tmpdir, "vat-nosctlr.txt")
    check("an AT row with no `_wdsctlr` still reads (the SCTLR line is a plumbing gap, not a gate)",
          at, "V-AT1")

    # 11. THE FROZEN-1014 CONTROL (the task's own requirement): the reader must FAIL on a capture with
    #     NO `wd*` keys.  The frozen 1014 capture IS the 2026-10-15 recovery; if no capture argument
    #     was given, the real one is used when present.
    ctl = capture_arg
    if ctl is None:
        cand = os.path.join(os.path.dirname(TOOLS_DIR), "out", "stage90", "captures",
                            "recovered-unknown-arm-20261015-092217-last_kmsg.txt")
        ctl = cand if os.path.isfile(cand) else None
    if ctl and os.path.isfile(ctl):
        ccap = Capture(ctl)
        n_wd = ccap.family_count("xnu_live_abt_wd")
        n_leafng = len(ccap.all("xnu_live_abt_leafng"))
        check("frozen 1014 capture carries ZERO `wd*` keys (the control is a control)",
              "yes" if (n_wd == 0 and n_leafng == 0) else "no", "yes",
              "wd*=x%d leafng=x%d" % (n_wd, n_leafng))
        ccell, cpcell, _s, _o = _run_reader_on(ccap.lines[:0] + ccap.lines, tmpdir,
                                               "unused-ctl.txt") if False else (None, None, None, "")
        storm = classify_storm(aborts(ccap), TAIL_MIN_N)
        FAILURES[:] = []
        REPORT[:] = []
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            pcell = s_secondary(ccap, storm, True, "V-G2")
        check("the reader REFUSES the frozen 1014 capture for want of wd* evidence (P-CTL)",
              pcell, "P-CTL", "it measures the storm fine (x%d storm tail rows)" % len(
                  storm["storm_tail"]))
    else:
        print("  [warn] no frozen-1014 capture found - the CONTROL case was skipped")

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
        cap = Capture(path)
    except (OSError, IOError) as exc:
        print("CANNOT READ %s: %s" % (path, exc))
        return 1

    print("1015 capture reader - %s (%d bytes, %d lines)" % (path, cap.nbytes, len(cap.lines)))
    print("1015 IS ENTRY-ONLY (design §2 as amended 2026-10-15): the design's ACTIVE half - which "
          "would have widened the storm's flush to a full TLBIALL and asked 'does the storm STOP?' - "
          "is NOT built; 1014's kernel pool goes to the press unchanged, so the `_wfkind=1` widened "
          "arm does not exist and the inherited §3a cell reads V-G3 by construction.  THE VERDICT "
          "CELL IS THE AT/PAR CELL (§3c): the CPU's OWN translation of the storm's VA, measured in "
          "the abort context, decisive in one run and independent of any press.")

    storm = classify_storm(aborts(cap), TAIL_MIN_N)
    wf_rows = wf_records(cap)
    primary_cell, _stopped = s_primary(cap, storm, wf_rows)
    pcell = s_secondary(cap, storm, primary_cell == "V-G2", primary_cell)
    atcell, _atdet = s_at(cap, storm)
    s_at_cross(primary_cell, atcell)

    # ---- THE INHERITED READINGS, re-printed rather than re-implemented.
    print("\n== [LEAF via 1013] THE INHERITED 1013 READING (the same capture) ==")
    leaf, block, note = read_cell_via(CHECK_1013_NAME, path, LEAF_LINE_PREFIX)
    if note:
        print("[LEAF via 1013] NOTITE: %s" % note)
    for line in (block or "(no `== VERDICT ==` block)").split("\n"):
        print("  | " + line)
    print("  THE-LEAF-VALUE-WITNESS: %s" % (leaf if leaf else "NOT READ"))

    print("\n== [FLUSH via 1014] THE INHERITED 1014 READING (the same capture) ==")
    flush, fblock, fnote = read_cell_via(CHECK_1014_NAME, path, FLUSH_LINE_PREFIX)
    if fnote:
        print("[FLUSH via 1014] NOTE: %s" % fnote)
    for line in (fblock or "(no `== VERDICT ==` block)").split("\n"):
        print("  | " + line)
    print("  1014 FLUSH CELL: %s" % (flush if flush else "NOT READ"))

    print("\n== 1015 VERDICT ==")
    print("1015 AT/PAR CELL (THE VERDICT - the CPU's own translation): %s" % (atcell if atcell else "n/a"))
    print("1015 PASSIVE CELL (the permission ladder): %s" % pcell)
    print("1015 INHERITED PRIMARY CELL (the REMOVED active half - V-G3 by construction): %s" % primary_cell)
    # ^ THE LABEL IS STABLE AND THE BUILD GREPS IT (`scripts/build_1015.sh`'s frozen-capture control).
    #   It is NOT named "1015 PRIMARY CELL": that name would assert the widened flush is this rung's
    #   verdict cell, which is exactly what the entry-only decision removed.
    print("1014 FLUSH CELL (inherited): %s" % (flush if flush else "NOT READ"))
    print("HARD ANOMALIES: %d" % len(FAILURES))
    for f in FAILURES:
        print("    - " + f)
    if SOFT:
        print("  %d report-only finding(s) above (never counted, never concluded from)" % len(SOFT))
    print("  => 1015 IS ENTRY-ONLY: the INHERITED PRIMARY CELL can only be V-G3 (no widened flush is "
          "compiled, so `_wfkind=1` is unreachable), and V-G3 MUST NOT be read as V-G2.  THE VERDICT "
          "IS THE AT/PAR CELL: V-AT1 confirms H-G (STALE_GLOBAL_TLB) AT THE HARDWARE - the CPU's own "
          "translation of the storm's VA is not the walked user-writable leaf - and it does not depend "
          "on the press.  V-AT2 is a PROBE defect, not a refutation; V-AT3 is a wrong translation in "
          "the TLB; V-AT4 is BOTH VIEWS FAULT (a domain/invalid/SCTLR mechanism, re-rank toward "
          "perm-static cause #2) and MUST NOT be read as V-AT1..3; V-AT0 is the WITHHOLD (the AT "
          "leg did not run, or the T-CTL null control on `_wdctlat` was absent or unsatisfied) and "
          "MUST NOT be read as any of V-AT1..4.  A HARD anomaly is "
          "REPORTED, not a fix adjudication - the exit code stays 0 for any capture that parses")
    return 0


if __name__ == "__main__":
    sys.exit(main())