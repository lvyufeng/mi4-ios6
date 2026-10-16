#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_1023_capture.py -- the 1023 press's capture reader.

WHAT 1023 IS, AND WHAT THIS CHECKER IS FOR
==========================================
1022 (records/1022-press-read.txt) installed `mach_trap_table[15]` (mach_vm_map) and read
T15-FIRES: the row is installed, `mach_vm_map` is called 7 times and returns `result=0` every
time, and the udf wall 1021d stopped on is GONE.  The frontier MOVED to libxpc's
`__xpc_mach_port_allocate`-family wrapper (`0x3824dfc8`), whose thumb `udf #254` sits at file VA
`0x3824e022`.

Rung 1023 opens `mach_trap_table[24]` (`mach_port_construct`) the same way 1022 opened row 15,
INCLUDING retargeting the row's runtime selector (`mach_kernelrpc.c:74`:
`ios7_compat_vmabi_enabled()` -> `ios7_traps_enabled()`; on CANCRO the former is a compile-time 0,
so an installed row without the retarget would return the shim's OWN sentinel and be strictly worse
than absent).  It also adds a bounded `IOS7LAB trap24 frame ...` register-frame dump.

THE RUNG'S PREMISE WAS RE-AIMED BY VALUE (records/1023-adjudication.txt).  The original identity --
"the frontier udf IS trap 24 returning an unaccepted rv" -- is REFUTED: a dispatched trap-24 with
row 24 == kern_invalid would PANIC (`swi_mach` compares the row's fn against `kern_invalid` and
branches to `swi_mach_error` -> `irqvec_panic` -> `panic`, no return path), and the 1022 capture has
ZERO panics.  Therefore trap 24 was NOT dispatched in that run, and the true frontier is probably the
helper call one instruction EARLIER (`blx 0x38260ee0` -> cache-rel `0x3815bc38`).  What survives is
the EXPERIMENT: install the row and MEASURE whether trap 24 is dispatched at all.

THE HEADLINE CELL IS THEREFORE *DISPATCH*, NOT "THE WALL MOVED" (docs/1023 section 0.2; the
adjudication's "STATUS OF STAGED ARTIFACTS").  The decisive measurement is:
    - the `IOS7LAB trap24 frame ...` dump APPEARS  => trap 24 IS dispatched; the frontier IS the row,
      and the row now has a real body (read what `IOS7LAB port_construct ... result=` says);
    - the dump is ABSENT                          => the trap row is EXONERATED; the frontier is the
      wrapper's helper call `0x3815bc38`, and this rung's next step is that helper, NOT a wider gate.

THE WITNESSES
=============
  (i)  THE DISPATCH WITNESS -- `IOS7LAB trap24 frame r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x
       r5=%08x r8=%08x`, printed at `mach_kernelrpc.c:98` by the row's body itself, BEFORE the
       selector gate's `return rv` and before the `done:` printf.  Its presence is the ONLY
       un-inferred proof that trap 24 dispatched.  Its ABSENCE, unlike most absences, is a sound
       reading IFF the kernel printf channel was live -- and this printf is a raw `printf`, NOT the
       capped live channel, so it rides the console and is always live once the row runs.  To keep
       the absence honest the control ALSO requires a positive signal from the same run (the live
       undef witness OR the 1022 wall), so a fully-dead console is WITHHOLD, never "not dispatched".
  (ii) THE BODY WITNESS -- `IOS7LAB port_construct flags=%x qlimit=%u context=%08x:%08x
       result=%d name=%x bypass_hints=%x` (`mach_kernelrpc.c:144`, the `done:` printf).  When the
       dispatch witness is present this is the row's VERDICT: `result=0` = success (WALL MOVED),
       `result=4` (KERN_INVALID_ARGUMENT) = the frame/wire was refused, `result=268435459`
       (0x10000003) = the sentinel, i.e. the selector retarget did NOT land.

CELLS (pre-registered; docs/1023 section 7, re-aimed per the adjudication)
==========================================================================
  G-CTL             neither witness live AND no panic/stop        => WITHHOLD (INSTRUMENT_UNSOUND)
  T24-FIRES         the dispatch witness is PRESENT and `port_construct result=0`  => ROW USED,
                    THE WALL MOVED (the frontier is now DOWNSTREAM; read its new stop)
  T24-SENTINEL      the dispatch witness is PRESENT and `port_construct result=268435459` (sentinel)
                                                                   => ROW INSTALLED, SELECTOR
                    STILL 0 (the `mach_kernelrpc.c:74` retarget did not land)
  T24-BADFRAME      the dispatch witness is PRESENT and `port_construct result=4` (KERN_INVALID_ARGUMENT)
                                                                   => ROW USED, THE WIRE IS REFUSED
                    (read the printed frame words -- the design's own falsifier)
  T24-PANICS        a `swi_mach_error`-shaped panic (0x81)         => STALE POOL (the -D did not
                    reach a pool object, or a stale object was linked)
  T24-NOT-DISPATCHED the dispatch witness is ABSENT, the console WAS live (a live witness or the
                    1022 wall is present), and there is no panic => the trap row is EXONERATED;
                    the frontier is the wrapper's helper call 0x3815bc38
  T24-DEFERRAL-BROKEN rows 25/41/42 read something other than kern_invalid; the press's own by-value
                    read of the linked image fails => COMPOSITION (a build defect)
  NO-CELL           nothing in the pre-registered table fires (report what was present; never
                    fabricate a verdict)

⚠ THE `T24-NOT-DISPATCHED` CELL IS THE HONEST REFRAME.  The design §7 originally offered
`T24-FIRES` keyed on "the 1022 frontier pc 0x3b418022 appears 0 times AND >=1 port_construct
result=0".  But the adjudication showed the frontier udf is a USER-mode udf that pid 1 SURVIVES, so
its absence (or presence) is not by itself the dispatch reading -- the DISPATCH reading is the frame
dump.  This reader keys the cells on the frame dump first, and reports the wall pc as corroboration.

The by-value build-side clause is `tools/check_1023_trap24.py <elf> --expect on|off`; it is NOT
re-run here (this is a CAPTURE reader).

⚠ TWO PARSERS, TWO KEYS: this reader keys ONLY on the two `IOS7LAB` printf marks.  The live-channel
witnesses (`xnu_live_*`) are read via `raw_kv` because `C15.Capture`'s KV regex is
`xnu_live_[a-z0-9_]+` by construction.
"""

import argparse
import contextlib
import importlib.util
import io
import os
import re
import sys

# The capture parser is REUSED BY IDENTITY, not cloned ([[mi4-one-value-two-definitions]]).
_HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location("check_1015_capture",
                                              os.path.join(_HERE, "check_1015_capture.py"))
C15 = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(C15)

# ---------------------------------------------------------------------------------------------------
# THE 1022 WALL'S IDENTITY (slide-invariant FILE OFFSET, NOT a linked VA: pid 1's udf is in the dyld
# shared cache and its runtime pc moves with the slide EVERY RUN).  Reported as corroboration only.
# ---------------------------------------------------------------------------------------------------
REF_WALL_OFFSET = 0x3824E022        # libxpc __xpc_mach_port_allocate wrapper, `udf #254`
REF_WALL_NAME = "__xpc_mach_port_allocate+0x5a (libxpc)"
REF_1022_PC = 0x3B418022            # the 1022 capture's raw pc (recorded for the arithmetic check)
REF_1022_SLIDE = 0x031CA000

# The slide is published by the shreg wire, e.g.
# `IOS7SHREG map_slide pid=1 fd=0 count=4 mappings=0x2bdd2848 slide=0x31ca000 ...`.
MAP_SLIDE_MARK = "map_slide"
MAP_SLIDE_RE = re.compile(r"\bmap_slide\b.*?\bpid=(\d+)\b.*?\bslide=(0x[0-9A-Fa-f]+)\b")

# ---- the live undef witness (`entry_trace.c:5578`, `__wrap_sleh_undef`) ---------------------------
UNDEF_SEQ_KEY = "xnu_live_undef_seq"
UNDEF_PC_KEY = "xnu_live_undef_pc"

# ---- the row's own two prints, VERBATIM from the staged body -------------------------------------
DISPATCH_MARK = "IOS7LAB trap24 frame"
DISPATCH_RE = re.compile(
    r"IOS7LAB trap24 frame r0=([0-9A-Fa-f]{8}) r1=([0-9A-Fa-f]{8}) r2=([0-9A-Fa-f]{8}) "
    r"r3=([0-9A-Fa-f]{8}) r4=([0-9A-Fa-f]{8}) r5=([0-9A-Fa-f]{8}) r8=([0-9A-Fa-f]{8})")

BODY_MARK = "IOS7LAB port_construct"
BODY_RE = re.compile(
    r"IOS7LAB port_construct flags=([0-9A-Fa-f]+) qlimit=(-?\d+) context=([0-9A-Fa-f]{8}):"
    r"([0-9A-Fa-f]{8}) result=(-?\d+) name=([0-9A-Fa-f]+) bypass_hints=([0-9A-Fa-f]+)")

KERN_INVALID_ARGUMENT = 4           # `osfmk/mach/kern_return.h`
MACH_SEND_INVALID_DEST = 0x10000003  # `osfmk/mach/message.h:692` - the shim's own sentinel

# ---- panic evidence ------------------------------------------------------------------------------
PANIC_ENTERED_KEY = "xnu_entry_panic_entered"
VEC_PANIC_MARK = "fleh_vectors: exception in exception vectors"
VEC_PANIC_CODE_RE = re.compile(r"exception in exception vectors,\s*0x([0-9A-Fa-f]+)")
SWI_MACH_ERROR_CODE = 0x81          # `traps_lo.s:261`
PANIC_TEXT_RE = re.compile(r"\bpanic\b[:(]")

# Residency.
WDT_PETS_KEY = "xnu_live_wdt_pets"
MEMSIZE_KEY = "xnu_entry_args_memSize"

# The 1022 recovery, used as the `--baseline` DEFAULT.
DEFAULT_BASELINE = os.path.join(os.path.dirname(_HERE), "out", "stage90", "captures",
                                "recovered-unknown-arm-20261016-060342-last_kmsg.txt")

# The cells, in the brief's order.  `NO-CELL` is the else-arm and is NOT a verdict.
# ⚠ `T24-DEFERRAL-BROKEN` is NOT here: it is a BUILD-SIDE cell (rows 25/41/42 changed, or the row did
# not install), decided by the by-value clause `tools/check_1023_trap24.py <elf> --expect on` on the
# LINKED image, and never by a capture.  A capture reader that listed it could never reach it.
CELLS = ("G-CTL", "T24-FIRES", "T24-SENTINEL", "T24-BADFRAME", "T24-PANICS",
         "T24-NOT-DISPATCHED", "NO-CELL")

DISPLAY = {
    "G-CTL": "WITHHOLD (INSTRUMENT_UNSOUND)",
    "T24-FIRES": "T24-FIRES (ROW INSTALLED, USED, SUCCESSFUL - THE WALL MOVED)",
    "T24-SENTINEL": "T24-SENTINEL (ROW INSTALLED, SELECTOR STILL 0 - the retarget did not land)",
    "T24-BADFRAME": "T24-BADFRAME (ROW USED; THE WIRE IS REFUSED)",
    "T24-PANICS": "T24-PANICS (STALE POOL - NOT A BEHAVIOUR READING)",
    "T24-NOT-DISPATCHED": "T24-NOT-DISPATCHED (TRAP 24 EXONERATED - the frontier is 0x3815bc38)",
    "NO-CELL": "NO-CELL (no pre-registered cell fires - nothing is concluded)",
}


def say(tag, msg=""):
    print("[1023] %-9s %s" % (tag, msg))


def verdict(cell, msg):
    print("[1023] %-16s %s" % (cell, msg))
    return cell


# ---------------------------------------------------------------------------------------------------
# THE RAW-TEXT READER (the `xnu_entry_*` keys are invisible to `C15.Capture.toks`).
# ---------------------------------------------------------------------------------------------------
def raw_kv(cap, key):
    out = []
    pat = re.compile(re.escape(key) + r"=([0-9A-Za-z_.+-]+)")
    for i, line in enumerate(cap.lines, 1):
        for m in pat.finditer(line):
            out.append((i, C15.to_int(m.group(1)), m.group(1)))
    return out


def raw_last(cap, key):
    rows = raw_kv(cap, key)
    return rows[-1] if rows else None


def raw_lines_with(cap, mark):
    return [i for i, line in enumerate(cap.lines, 1) if mark in line]


def max_key(cap, key):
    vals = [t[3] for t in cap.all(key) if t[3] is not None]
    return max(vals) if vals else None


# ---------------------------------------------------------------------------------------------------
# THE READINGS
# ---------------------------------------------------------------------------------------------------
def slide_of(cap, pid=1):
    hit = (None, None)
    for i, line in enumerate(cap.lines, 1):
        if MAP_SLIDE_MARK not in line:
            continue
        m = MAP_SLIDE_RE.search(line)
        if m is not None and C15.to_int(m.group(1)) == pid:
            hit = (C15.to_int(m.group(2)), i)
    return hit


def undef_rows(cap):
    rows, cur = [], None
    for line_no, key, _raw, val in cap.toks:
        if key == UNDEF_SEQ_KEY:
            cur = {"line": line_no, "seq": val, "pc": None}
            rows.append(cur)
        elif cur is not None and key == UNDEF_PC_KEY:
            cur["pc"] = val
    return rows


def dispatch_events(cap):
    """`(rows, malformed)`: the row's own frame dump, parsed.  `malformed` are `(line, text)` for
    lines carrying the MARK that the field regex could NOT parse - the distinction matters at
    `G-CTL`, which counts the MARK (a line the reader cannot parse is still evidence the row RAN)."""
    rows, malformed = [], []
    for i, line in enumerate(cap.lines, 1):
        if DISPATCH_MARK not in line:
            continue
        m = DISPATCH_RE.search(line)
        if m is None:
            malformed.append((i, line.strip()[:200]))
            continue
        rows.append({"line": i,
                     "r0": C15.to_int("0x" + m.group(1)),
                     "r1": C15.to_int("0x" + m.group(2)),
                     "r2": C15.to_int("0x" + m.group(3)),
                     "r3": C15.to_int("0x" + m.group(4)),
                     "r4": C15.to_int("0x" + m.group(5)),
                     "r5": C15.to_int("0x" + m.group(6)),
                     "r8": C15.to_int("0x" + m.group(7)),
                     "raw": line.strip()[:200]})
    return rows, malformed


def body_events(cap):
    """`(rows, malformed)`: the row's `done:` printf, parsed."""
    rows, malformed = [], []
    for i, line in enumerate(cap.lines, 1):
        if BODY_MARK not in line:
            continue
        m = BODY_RE.search(line)
        if m is None:
            malformed.append((i, line.strip()[:200]))
            continue
        rows.append({"line": i,
                     "flags": C15.to_int("0x" + m.group(1)),
                     "qlimit": int(m.group(2), 10),
                     "context_hi": C15.to_int("0x" + m.group(3)),
                     "context_lo": C15.to_int("0x" + m.group(4)),
                     "result": int(m.group(5), 10),
                     "name": C15.to_int("0x" + m.group(6)),
                     "bypass_hints": C15.to_int("0x" + m.group(7)),
                     "raw": line.strip()[:200]})
    return rows, malformed


def panic_reading(cap):
    """The panic evidence, by value.  `evidence` is the boolean the cells key on."""
    d = {}
    pe = raw_last(cap, PANIC_ENTERED_KEY)
    d["panic_entered"], d["panic_entered_line"] = (pe[1] if pe else None), (pe[0] if pe else None)
    d["vec_panic"] = [(i, line.strip()[:200]) for i, line in enumerate(cap.lines, 1)
                      if VEC_PANIC_MARK in line]
    d["panic_text"] = [(i, line.strip()[:200]) for i, line in enumerate(cap.lines, 1)
                       if PANIC_TEXT_RE.search(line)]
    codes = []
    for i, line in enumerate(cap.lines, 1):
        for m in VEC_PANIC_CODE_RE.finditer(line):
            codes.append((i, C15.to_int("0x" + m.group(1))))
    d["vec_codes"] = codes
    d["swi_mach_shaped"] = any(c == SWI_MACH_ERROR_CODE for _i, c in codes)
    # `evidence` is the design's panic cell EXACTLY as written ("a `swi_mach_error`-shaped panic").
    d["evidence"] = bool(d["vec_panic"] or d["panic_text"])
    # `stopped` is the weaker AFFIRMATIVE "the run ended in the kernel": a panic-entered record.
    d["stopped"] = bool(d["panic_entered"] not in (0, None))
    return d


def panic_why(d):
    out = []
    if d["vec_panic"]:
        out.append("a vector-panic string at :%d%s" % (
            d["vec_panic"][0][0],
            " reading 0x%02x = swi_mach_error's own code (traps_lo.s:261)" % SWI_MACH_ERROR_CODE
            if d["swi_mach_shaped"] else ""))
    if d["panic_text"]:
        out.append("panic console text at :%d (%r)" % (d["panic_text"][0][0], d["panic_text"][0][1]))
    if d["panic_entered"] not in (0, None):
        out.append("`%s=%s` (:%s)" % (PANIC_ENTERED_KEY, C15.fmt(d["panic_entered"]),
                                      d["panic_entered_line"]))
    return "; ".join(out) if out else "(none)"


def wall_shape(cap, slide, rows):
    """Where the 1022 wall udf is, RELATIVE TO THE WALL.  Every field is `None` when the slide is
    unknown.  REPORTED as corroboration - the cells key on the DISPATCH witness, not on this."""
    wall_pc = (REF_WALL_OFFSET + slide) if slide is not None else None
    user_pcs = sorted({r["pc"] for r in rows if r["pc"] is not None})
    wall_rows = [r for r in rows if wall_pc is not None and r["pc"] == wall_pc]
    other_user = sorted({p for p in user_pcs if wall_pc is None or p != wall_pc})
    if slide is None:
        present = None
    else:
        present = bool(wall_rows)
    return {"wall_pc": wall_pc, "present": present, "wall_rows": wall_rows,
            "wall_last": (max(r["line"] for r in wall_rows) if wall_rows else None),
            "other_user": other_user, "pcs_seen": user_pcs}


def baseline_section(cap, base, base_path, slide, rows):
    """The `--baseline` side-by-side (REPORT-ONLY; no cell reads it).

    ⚠ THE RAW PC IS NOT COMPARABLE ACROSS RUNS: the live undef pc is POST-slide and the slide varies
    per run.  The invariant is the FILE OFFSET (`pc - slide`); both are printed."""
    if base is None:
        say("BASE", "no baseline - the side-by-side is SKIPPED (no cell depends on it)")
        return None
    cpself = os.path.realpath(getattr(cap, "path", "") or "")
    bpath = os.path.realpath(base_path or "")
    if cpself and cpself == bpath:
        say("BASE", "REFUSING the baseline: it IS this capture (a comparison a file makes with "
                    "itself cannot fail - [[mi4-a-claim-in-a-comment-is-not-a-check]])")
        return None
    bslide, bline = slide_of(base)
    brows = undef_rows(base)
    bset = sorted({r["pc"] for r in brows if r["pc"] is not None})
    bdisp = raw_lines_with(base, DISPATCH_MARK)
    bbody = raw_lines_with(base, BODY_MARK)
    name = os.path.basename(base_path or "(given)")
    say("BASE", "%s: slide=%s (:%s); its undef pc(s): %s; `%s` x%d; `%s` x%d"
        % (name, C15.fmt(bslide), bline,
           ("{%s}" % ", ".join(C15.fmt(p) for p in bset)) if bset else "(none published)",
           DISPATCH_MARK, len(bdisp), BODY_MARK, len(bbody)))
    if bset and bslide is not None:
        ex = [p - bslide for p in bset]
        say("BASE", "its FILE OFFSET(s): %s%s"
            % (", ".join(C15.fmt(o) for o in ex),
               "  <- == %s == the 1022 wall" % C15.fmt(REF_WALL_OFFSET)
               if REF_WALL_OFFSET in ex else "  <- NOT the 1022 wall %s" % C15.fmt(REF_WALL_OFFSET)))
    ours_pcs = [r["pc"] for r in rows if r["pc"] is not None]
    for p in bset:
        say("BASE", "  raw pc %s in THIS capture: x%d row(s)"
            % (C15.fmt(p), sum(1 for q in ours_pcs if q == p)))
    if bslide is not None and slide is not None:
        boffs = {p - bslide for p in bset}
        n_off = sum(1 for q in ours_pcs if (q - slide) in boffs)
        say("BASE", "  the SAME FILE OFFSET(s) in THIS capture: x%d row(s)%s"
            % (n_off, (" (the pcs are post-slide; the slide differs: %s vs %s)"
                       % (C15.fmt(bslide), C15.fmt(slide))) if bslide != slide else
               " (same slide this run)"))
    return {"base_pcs": bset, "base_slide": bslide, "base_name": name}


# ---------------------------------------------------------------------------------------------------
# THE ADJUDICATION.  DISPATCH first (the headline cell), then the row's verdict, then panic.
# ---------------------------------------------------------------------------------------------------
def adjudicate(cap, baseline=None, baseline_path=None):
    slide, slide_line = slide_of(cap)
    rows = undef_rows(cap)
    disp, disp_bad = dispatch_events(cap)
    body, body_bad = body_events(cap)
    pan = panic_reading(cap)
    wall = wall_shape(cap, slide, rows)
    disp_lines = raw_lines_with(cap, DISPATCH_MARK)
    body_lines = raw_lines_with(cap, BODY_MARK)

    say("CAPTURE", "%d bytes, %d lines, %d live tokens" % (cap.nbytes, len(cap.lines), len(cap.toks)))
    baseline_section(cap, baseline, baseline_path, slide, rows)

    # ---- SLIDE and the 1022 WALL ----------------------------------------------------------------
    if slide is None:
        say("SLIDE", "NO `%s` line for pid 1 - the wall's pc CANNOT be computed (never hard-coded: "
                     "the slide varies per run)" % MAP_SLIDE_MARK)
    else:
        say("SLIDE", "`%s` pid=1 at :%d reads slide=%s => this run's 1022-wall pc = %s (%s)"
            % (MAP_SLIDE_MARK, slide_line, C15.fmt(slide),
               C15.fmt(REF_WALL_OFFSET + slide), REF_WALL_NAME))
        say("SLIDE", "cross-check vs the 1022 record: %s - %s = %s (want %s)"
            % (C15.fmt(REF_1022_PC), C15.fmt(REF_1022_SLIDE),
               C15.fmt(REF_1022_PC - REF_1022_SLIDE), C15.fmt(REF_WALL_OFFSET)))
    if slide is not None:
        say("WALL", "the 1022 wall udf (%s): %s - %d live row(s), last :%s; OTHER user undef pcs: %s"
            % (C15.fmt(wall["wall_pc"]), "PRESENT" if wall["present"] else "ABSENT",
               len(wall["wall_rows"]), wall["wall_last"],
               ("{%s}" % ", ".join(C15.fmt(p) for p in wall["other_user"]))
               if wall["other_user"] else "(none)"))

    # ---- THE TWO WITNESSES ----------------------------------------------------------------------
    seql = raw_lines_with(cap, UNDEF_SEQ_KEY)
    say("LIVE", "witness (i, liveness) `%s`: x%d row(s); witness (ii, DISPATCH) `%s`: x%d printf "
                "line(s) (of which parsed x%d); the body's `%s`: x%d line(s) (parsed x%d)"
        % (UNDEF_SEQ_KEY, len(rows), DISPATCH_MARK, len(disp_lines), len(disp),
           BODY_MARK, len(body_lines), len(body)))
    for i, txt in disp_bad[:4]:
        say("LIVE", "  ⚠ UNPARSED `%s` line at :%d - the row RAN but this reader cannot read its "
                    "slots: %s" % (DISPATCH_MARK, i, txt))
    for i, txt in body_bad[:4]:
        say("LIVE", "  ⚠ UNPARSED `%s` line at :%d: %s" % (BODY_MARK, i, txt))
    if disp:
        d = disp[0]
        say("DISPATCH", "FIRST frame dump at :%d: r0=%s r1=%s r2=%s r3=%s r4=%s r5=%s r8=%s  "
                        "(these are the ADAPTER's regs slots; the design §4 reads regs[1] as a "
                        "pointer to 24 option bytes and regs[4] as the name-out pointer)"
            % (d["line"], C15.fmt(d["r0"]), C15.fmt(d["r1"]), C15.fmt(d["r2"]), C15.fmt(d["r3"]),
               C15.fmt(d["r4"]), C15.fmt(d["r5"]), C15.fmt(d["r8"])))
    if body:
        results = {}
        for r in body:
            results[r["result"]] = results.get(r["result"], 0) + 1
        say("DISPATCH", "the body's own results: %s"
            % ", ".join("result=%d x%d" % (k, v) for k, v in sorted(results.items())))
    say("LIVE", "`%s` max=%s; `%s`=%s"
        % (WDT_PETS_KEY, C15.fmt(max_key(cap, WDT_PETS_KEY)), MEMSIZE_KEY,
           (raw_last(cap, MEMSIZE_KEY)[2] if raw_last(cap, MEMSIZE_KEY) else "absent")))

    # ---- PANIC ----------------------------------------------------------------------------------
    if pan["evidence"]:
        say("PANIC", "evidence: %s" % panic_why(pan))
    elif pan["stopped"]:
        say("PANIC", "NO design panic evidence, but the run STOPPED IN THE KERNEL: `%s`=%s (:%s)"
            % (PANIC_ENTERED_KEY, C15.fmt(pan["panic_entered"]), pan["panic_entered_line"]))
    else:
        say("PANIC", "no panic evidence and no kernel stop: vector-panic x%d, panic text x%d, "
                     "`%s`=%s"
            % (len(pan["vec_panic"]), len(pan["panic_text"]), PANIC_ENTERED_KEY,
               C15.fmt(pan["panic_entered"])))

    # =============================================================================================
    # 1. G-CTL - THE CONTROL, AND THE MANDATORY-FIRST CELL.
    #    With NEITHER a positive dispatch reading NOR a liveness witness, an ABSENCE of the frame
    #    dump is not a reading ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).  Two
    #    carve-outs, both POSITIVE observations that do not depend on any absence: (a) a PANIC, (b) a
    #    kernel-stop record.
    # =============================================================================================
    if not disp_lines and not rows and not pan["evidence"] and not pan["stopped"]:
        missing = []
        if not rows:
            missing.append("witness (i) `%s` (the live undef witness - the liveness signal that the "
                           "console was delivering)" % UNDEF_SEQ_KEY)
        if not disp_lines:
            missing.append("witness (ii) `%s` (the row's own frame dump - the DISPATCH signal)"
                           % DISPATCH_MARK)
        return verdict("G-CTL", "WITHHOLD (INSTRUMENT_UNSOUND).  MISSING: %s.  With NEITHER the "
                                "frame dump NOR any live-channel witness, the console's silence "
                                "cannot be read as 'trap 24 was not dispatched' - a dead instrument "
                                "produces exactly the same silence.  This is the 1018 defect read "
                                "forward.  Fix the instrument (rebuild with the row + the live "
                                "witness armed) before adjudicating anything." % "; ".join(missing))

    # =============================================================================================
    # 2. T24-PANICS - tested before the dispatch cells because a panic is not a behaviour reading.
    #    A dispatched trap-24 with row 24 == kern_invalid would panic via swi_mach_error; a panic
    #    therefore means the -D did not reach a pool object (or a stale object was linked).
    # =============================================================================================
    if pan["evidence"]:
        swi = ("\n    NOTE: the vector-panic code reads 0x%02x, which is EXACTLY r0 at "
               "`swi_mach_error` (traps_lo.s:261) - the `swi_mach_error`-shaped panic the design "
               "names." % SWI_MACH_ERROR_CODE if pan["swi_mach_shaped"] else "")
        return verdict("T24-PANICS", "STALE POOL - NOT A BEHAVIOUR READING.  Panic evidence: %s.%s  "
                                     "A dispatched trap-24 whose row is STILL `kern_invalid` panics "
                                     "(`swi_mach` -> `swi_mach_error` -> `irqvec_panic`, no return), "
                                     "so this means the row did NOT install OR a stale pool object "
                                     "(osfmk_kern_syscall_sw.o / osfmk_ipc_mach_kernelrpc.o) was "
                                     "linked.  READ `nm` ON THE OBJECTS AND ON THE LINKED IMAGE, not "
                                     "the env (records/1020-build-defect.txt is the precedent)."
                     % (panic_why(pan), swi))

    # =============================================================================================
    # 3. T24-SENTINEL - the row ran, the body printed the SENTINEL (0x10000003).
    # =============================================================================================
    sent = [r for r in body if r["result"] == MACH_SEND_INVALID_DEST]
    if disp and sent:
        r = sent[0]
        return verdict("T24-SENTINEL", "ROW INSTALLED, SELECTOR STILL 0.  The dispatch witness IS "
                                       "present (%d frame dump(s)) AND the body printed "
                                       "`result=%d` = 0x%08x = MACH_SEND_INVALID_DEST, the shim's "
                                       "OWN sentinel.  So edit 2 (`mach_kernelrpc.c:74`: "
                                       "`ios7_compat_vmabi_enabled()` -> `ios7_traps_enabled()`) did "
                                       "NOT land: the row dispatched, but its runtime selector "
                                       "answered 0 and it returned the sentinel before doing the "
                                       "copyin.  The shim takes its slow leg on exactly this word, so "
                                       "the row is strictly worse than absent.  READ `nm`/the "
                                       "disassembly of `mach_kernelrpc.o` to confirm the selector.  "
                                       "Frame at :%d: %s"
                      % (len(disp), r["result"], MACH_SEND_INVALID_DEST, r["line"], r["raw"]))

    # =============================================================================================
    # 4. T24-BADFRAME - the row ran, the body returned KERN_INVALID_ARGUMENT (the frame refused).
    # =============================================================================================
    bad = [r for r in body if r["result"] == KERN_INVALID_ARGUMENT]
    if disp and bad:
        r = bad[0]
        d = disp[0]
        return verdict("T24-BADFRAME", "ROW USED; THE WIRE IS REFUSED.  The dispatch witness is "
                                       "present AND the body read `result=%d` = KERN_INVALID_ARGUMENT, "
                                       "the adapter's own guard.  THE OFFENDING FRAME WORDS (the "
                                       "design §4's own falsifier):\n"
                                       "    r0=%s r1=%s r2=%s r3=%s r4=%s r5=%s r8=%s\n"
                                       "    flags=%s qlimit=%d name=%s bypass_hints=%s result=%d\n"
                                       "    :%d  %s\n"
                                       "  The design §4 predicts `regs[1]` (the 24-option-bytes "
                                       "pointer) or `regs[4]` (the name-out pointer) is the word that "
                                       "is wrong.  RE-DERIVE the frame from these printed words; do "
                                       "NOT re-press the same arm."
                      % (KERN_INVALID_ARGUMENT, C15.fmt(d["r0"]), C15.fmt(d["r1"]), C15.fmt(d["r2"]),
                         C15.fmt(d["r3"]), C15.fmt(d["r4"]), C15.fmt(d["r5"]), C15.fmt(d["r8"]),
                         C15.fmt(r["flags"]), r["qlimit"], C15.fmt(r["name"]),
                         C15.fmt(r["bypass_hints"]), r["result"], r["line"], r["raw"]))

    # =============================================================================================
    # 5. T24-FIRES - the row ran AND the body returned SUCCESS (result=0).
    # =============================================================================================
    ok = [r for r in body if r["result"] == 0]
    if disp and ok:
        r = ok[0]
        return verdict("T24-FIRES", "ROW INSTALLED, USED, SUCCESSFUL - THE WALL MOVED.  The dispatch "
                                    "witness IS present (%d frame dump(s)) AND the body returned "
                                    "`result=0` (first at :%d: name=%s).  So trap 24 dispatched, the "
                                    "selector answered 1, and `mach_port_construct` succeeded - the "
                                    "libxpc wrapper's `udf #254` at %s should be GONE and the frontier "
                                    "is now DOWNSTREAM.  Read WHAT the next stop is (the 1015b/1016a "
                                    "style) - this reader makes NO claim about it.  Residency: `%s` "
                                    "max=%s, `%s`=%s."
                      % (len(disp), r["line"], C15.fmt(r["name"]), REF_WALL_NAME, WDT_PETS_KEY,
                         C15.fmt(max_key(cap, WDT_PETS_KEY)), MEMSIZE_KEY,
                         (raw_last(cap, MEMSIZE_KEY)[2] if raw_last(cap, MEMSIZE_KEY) else "absent")))

    # =============================================================================================
    # 6. T24-NOT-DISPATCHED - the row's witness is ABSENT and the console WAS live.
    #    This is the rung's own falsifier, stated as a predicate: the trap row is EXONERATED and the
    #    frontier is the wrapper's helper call 0x3815bc38.
    # =============================================================================================
    live_signal = bool(rows) or (wall["present"] is True)
    if not disp_lines and live_signal:
        ev = []
        if rows:
            ev.append("the live undef witness `%s` x%d row(s)" % (UNDEF_SEQ_KEY, len(rows)))
        if wall["present"] is True:
            ev.append("the 1022 wall udf IS present (%s at :%s)"
                      % (C15.fmt(wall["wall_pc"]), wall["wall_last"]))
        if wall["other_user"]:
            ev.append("other user undef pc(s) {%s}"
                      % ", ".join(C15.fmt(p) for p in wall["other_user"]))
        return verdict("T24-NOT-DISPATCHED", "TRAP 24 EXONERATED - the frontier is NOT the trap row.  "
                                             "The row's own frame dump (`%s`) is ABSENT x0, and the "
                                             "console WAS delivering: %s.  So trap 24 was NEVER "
                                             "dispatched in this run, and installing the row (with the "
                                             "selector retarget) did NOT change the frontier.  The "
                                             "true frontier is the libxpc wrapper's HELPER call one "
                                             "instruction EARLIER (`blx 0x38260ee0` -> cache-rel "
                                             "0x3815bc38, the 476-byte-frame libxpc routine), whose "
                                             "rv the wrapper refuses.  RUNG 1024 targets THAT helper - "
                                             "NOT a wider gate and NOT another table row "
                                             "(records/1023-adjudication.txt sections (f)/(g))."
                      % (DISPATCH_MARK, "; ".join(ev)))

    # =============================================================================================
    # 7. NO-CELL - no pre-registered cell fires.  NAME what was present; NEVER fabricate a verdict.
    # =============================================================================================
    failed = []
    if not disp_lines:
        failed.append("`%s` x0 (no dispatch to classify)" % DISPATCH_MARK)
    if disp and not body:
        failed.append("the dispatch witness fired (x%d) but NO `%s` line follows - the row entered "
                      "but its `done:` printf did not print (a fault inside the handler? read the "
                      "panic/fault evidence for a bad `regs[1]` pointer)"
                      % (len(disp_lines), BODY_MARK))
    if body:
        others = sorted({r["result"] for r in body} - {0, KERN_INVALID_ARGUMENT,
                                                       MACH_SEND_INVALID_DEST})
        if others:
            failed.append("the body ran with result value(s) {%s} that are neither 0, %d, nor %d"
                          % (", ".join(str(o) for o in others), KERN_INVALID_ARGUMENT,
                             MACH_SEND_INVALID_DEST))
    if wall["present"] is None:
        failed.append("the 1022 wall is UNREADABLE (no `%s` for pid 1)" % MAP_SLIDE_MARK)
    return verdict("NO-CELL", "NO PRE-REGISTERED CELL FIRES - nothing is concluded.  PRESENT: `%s` "
                              "x%d, `%s` x%d, `%s` x%d.  PREDICATES THAT FAILED: %s.  Do NOT read "
                              "this as a pass: it means this capture is outside the pre-registered "
                              "table, so the rung must be described in a new cell BEFORE its data is "
                              "interpreted ([[mi4-measurement-defects]])."
                  % (DISPATCH_MARK, len(disp_lines), BODY_MARK, len(body_lines), UNDEF_SEQ_KEY,
                     len(rows), "; ".join(failed) if failed else "(none - a reader defect)"))


# ---------------------------------------------------------------------------------------------------
# THE SELF-TEST.  Every fixture is SYNTHETIC, generated in a fresh tempfile.mkdtemp().
# ---------------------------------------------------------------------------------------------------
def _mk(tmpdir, name, lines):
    path = os.path.join(tmpdir, name)
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def self_test():
    import tempfile
    ok = True
    tmpdir = tempfile.mkdtemp(prefix="check1023-selftest-")
    cases = []

    def cell_of(lines, base_lines, tag):
        p = _mk(tmpdir, tag + ".txt", lines)
        cap = C15.Capture(p)
        cap.path = p
        base, bpath = None, None
        if base_lines is not None:
            bpath = _mk(tmpdir, tag + "-base.txt", base_lines)
            base = C15.Capture(bpath)
            base.path = bpath
        with contextlib.redirect_stdout(io.StringIO()):
            return adjudicate(cap, base, bpath)

    def want(name, lines, expected, base_lines=None):
        nonlocal ok
        got = cell_of(lines, base_lines, name)
        cases.append((name, got, expected))
        if got != expected:
            say("SELFTEST-FAIL", "%s: expected %s, got %s" % (name, expected, got))
            ok = False

    SLIDE = 0x031CA000
    WALL_PC = REF_WALL_OFFSET + SLIDE            # 0x3b418022 - the 1022 run's own pc
    SLIDE_LINE = ("IOS7SHREG map_slide pid=1 fd=0 count=4 mappings=0x2bdd2848 slide=0x%x "
                  "slide_start=0x3ddaa000 size=0x00089000" % SLIDE)
    MEM = "MI4IOS6_STAGE90_XNU xnu_entry_args_memSize=0x1e400000"
    UNDEF_AT_WALL = [" xnu_live_undef_seq=0x00000001",
                     " xnu_live_undef_pc=0x%08x" % WALL_PC]

    def disp(r0=0x00000007, r1=0x00800000, r2=0x00000000, r3=0x00000003, r4=0x00800100,
             r5=0x00000000, r8=0x00000000):
        return ("IOS7LAB trap24 frame r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r8=%08x"
                % (r0, r1, r2, r3, r4, r5, r8))

    def body(result, flags=0, qlimit=0, ctx_hi=0, ctx_lo=0, name=0x00800100, bypass=0):
        return ("IOS7LAB port_construct flags=%x qlimit=%d context=%08x:%08x result=%d name=%x "
                "bypass_hints=%x" % (flags, qlimit, ctx_hi, ctx_lo, result, name, bypass))

    # ================= G-CTL =================
    want("G-CTL-empty", [], "G-CTL")
    want("G-CTL-no-witnesses", ["MI4IOS6_STAGE90_XNU booting", MEM], "G-CTL")
    # One witness is enough to pass the control.
    want("G-CTL-one-witness-live", [" xnu_live_undef_seq=0x00000001"], "T24-NOT-DISPATCHED")
    want("G-CTL-one-witness-dispatch", [disp()], "NO-CELL")

    # ================= T24-FIRES =================
    want("FIRES-success", [SLIDE_LINE, MEM, disp(), body(0)], "T24-FIRES")
    want("FIRES-with-wall-present", [SLIDE_LINE, MEM] + UNDEF_AT_WALL + [disp(), body(0)],
         "T24-FIRES")

    # ================= T24-SENTINEL =================
    want("SENTINEL", [SLIDE_LINE, MEM, disp(), body(MACH_SEND_INVALID_DEST)], "T24-SENTINEL")
    # Sentinel dominates success (most-severe first).
    want("SENTINEL-over-FIRES", [SLIDE_LINE, disp(), body(MACH_SEND_INVALID_DEST), body(0)],
         "T24-SENTINEL")

    # ================= T24-BADFRAME =================
    want("BADFRAME", [SLIDE_LINE, MEM, disp(), body(KERN_INVALID_ARGUMENT)], "T24-BADFRAME")
    # Refused dominates success.
    want("BADFRAME-over-FIRES", [SLIDE_LINE, disp(), body(KERN_INVALID_ARGUMENT), body(0)],
         "T24-BADFRAME")

    # ================= T24-PANICS =================
    want("PANICS-vec-81",
         [SLIDE_LINE, "panic: fleh_vectors: exception in exception vectors, 0x00000081"],
         "T24-PANICS")
    want("PANICS-panic-text", [SLIDE_LINE, "panic(\"sleh_undef: current thread is NULL\")"],
         "T24-PANICS")
    # ⚠ A panic dominates even when the row SUCCEEDED ("NOT A BEHAVIOUR READING").
    want("PANICS-over-FIRES", [SLIDE_LINE, disp(), body(0), "panic: x"], "T24-PANICS")
    # A non-zero `xnu_entry_panic_entered` alone is NOT the panic cell - it is a kernel-stop signal,
    # which (with no live witness) is a carve-out that avoids G-CTL but still does not conclude.
    want("PANICS-NOT-entered-only", [SLIDE_LINE, " xnu_entry_panic_entered=0x00000001"], "NO-CELL")

    # ================= T24-NOT-DISPATCHED =================
    want("NOT-DISPATCHED-wall-present", [SLIDE_LINE, MEM] + UNDEF_AT_WALL, "T24-NOT-DISPATCHED")
    want("NOT-DISPATCHED-live-only", [SLIDE_LINE, " xnu_live_undef_seq=0x00000001"],
         "T24-NOT-DISPATCHED")

    # ================= NO-CELL =================
    # The row entered but its `done:` printf did not follow.
    want("NO-CELL-entered-no-body", [SLIDE_LINE, disp()], "NO-CELL")
    # The body returned a result that is neither 0, 4, nor the sentinel.
    want("NO-CELL-other-result", [SLIDE_LINE, disp(), body(6)], "NO-CELL")
    # The row's MARK is present but the line is UNPARSEABLE: it still counts (the row RAN) and
    # cannot be classified - NO-CELL, with the warning printed.
    want("NO-CELL-unparsed-dispatch",
         [SLIDE_LINE, "IOS7LAB trap24 frame r0=00000007 (truncated)",
          " " + UNDEF_SEQ_KEY + "=0x00000001"], "NO-CELL")

    # ================= the BASELINE section (never a cell) =================
    p = _mk(tmpdir, "selfbase.txt", [SLIDE_LINE, MEM] + UNDEF_AT_WALL)
    cap = C15.Capture(p)
    cap.path = p
    with contextlib.redirect_stdout(io.StringIO()) as buf:
        got = adjudicate(cap, cap, p)
    cases.append(("BASELINE-refuses-self", got, "T24-NOT-DISPATCHED"))
    if "REFUSING the baseline" not in buf.getvalue():
        say("SELFTEST-FAIL", "BASELINE-refuses-self: the reader did not refuse a self-baseline")
        ok = False
    if got != "T24-NOT-DISPATCHED":
        say("SELFTEST-FAIL", "BASELINE-refuses-self: cell moved to %s" % got)
        ok = False

    print("\n  == SELF-TEST ==")
    bad = [(n, g, e) for n, g, e in cases if g != e]
    print("  %d case(s), %d failed" % (len(cases), len(bad)))
    for n, g, e in bad:
        print("    FAIL: %s (got %s, want %s)" % (n, g, e))
    got_cells = {g for _n, g, _e in cases}
    missing = [c for c in CELLS if c not in got_cells]
    if missing:
        print("    FAIL: no fixture reached %s" % ", ".join(missing))
        bad.append(("coverage", None, missing))
    print("  SELF-TEST: %s" % ("PASS" if not bad else "FAIL"))
    return 0 if not bad else 1


# ---------------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="1023 mach trap-24 row / libxpc port-allocation "
                                             "wrapper capture reader")
    ap.add_argument("capture", nargs="?")
    ap.add_argument("--baseline", help="a capture to compare against (default: the 1022 recovery, "
                                       "when on disk)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        sys.exit(self_test())
    if not args.capture:
        ap.error("a capture path is required (or --selftest)")
    try:
        cap = C15.Capture(args.capture)
    except (OSError, IOError) as exc:
        print("CANNOT READ %s: %s" % (args.capture, exc))
        sys.exit(1)
    cap.path = args.capture

    base, bpath = None, None
    cand = args.baseline
    if cand is None and os.path.isfile(DEFAULT_BASELINE):
        cand = DEFAULT_BASELINE
        say("BASE", "no --baseline given; using the 1022 recovery on disk (%s)" % cand)
    if cand is not None:
        if not os.path.isfile(cand):
            say("BASE", "the named baseline does not exist (%s) - the side-by-side is SKIPPED" % cand)
        else:
            try:
                base = C15.Capture(cand)
                base.path = cand
                bpath = cand
            except (OSError, IOError) as exc:
                say("BASE", "cannot read the baseline (%s): %s" % (cand, exc))

    cell = adjudicate(cap, base, bpath)
    print("\n[1023] VERDICT: %s" % DISPLAY.get(cell, cell))
    return 0


if __name__ == "__main__":
    sys.exit(main())