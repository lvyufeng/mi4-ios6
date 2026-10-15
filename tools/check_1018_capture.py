#!/usr/bin/env python3
"""Mechanical reader for a **1018** press capture - the CAP-EXEMPT UNDEF WITNESS.

WHAT 1018 IS.  `docs/1018-design.md` (`0bc1051`).  1017 named, statically and by value, the
instruction pid 1's frame sits on: un-slid VA `0x3823aa54` = `libsystem_platform.dylib`
`__os_alloc_slow` + `0x108` = the Thumb `udf #254`, reached ONLY when `_mach_vm_map` returns non-zero
(`docs/1017-shared-cache-naming.md` §3).  1017 then wrote `# UNMEASURED` for the mechanism: does the
UNDEFINED-instantiation handler RETURN and re-trap, or does an outer retry re-enter the failure path?

`records/1018-recon.txt` (`a7bafd7`) found WHY that capture could not answer it, and it was an
instrument defect, not a device one: the recorder that would have named the `udf` - `__wrap_sleh_undef`
(`entry_trace.c:5537`) - writes `entry_live_write`, which is CAPPED (`ENTRY_LIVE_CAP 24576u`,
`entry_stubs.c:4254`).  The capture published `xnu_live_capped` at line `:29963`; the frontier sits at
`:33977`, ~4000 records LATER, on the DIRECT channel.  So `xnu_live_undef_seq` is ZERO in that capture
because the channel was exhausted, not because no `udf` happened.

1018's arm moves the six existing undef keys onto the DIRECT channel (`entry_write_kv`) under the new
switch `STAGE90_XNU_UNDEF_DIRECT`, so the witness survives the cap.  Its null control (design §1.2) is
the boot self-test `stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled`.

THIS READER announces EXACTLY ONE cell, and the cells are the design's §2/§3/§4:

    U-FIRES-DIES    the undef fires at the frontier and the run then STOPS   (the trap terminated pid 1)
    U-FIRES-LOOPS   the undef fires and the sampler pc stays put              (the handler returns, re-traps)
    U-FIRES-ONCE    the undef fires once and the sampler pc MOVES             (the trap is a transition)
    U-SILENT        1017's static id is CONTRADICTED - no undef at the frontier, control live
    U-CTL           the witness never fired at all / the control is absent    (INSTRUMENT_UNSOUND - withhold)

------------------------------------------------------------------------------
WHAT THIS READER READS BY VALUE, AND WHAT IT REFUSES TO ASSUME
------------------------------------------------------------------------------
Everything here is taken from the capture by value with the key named.  In particular:

  * **The frontier pc is not a constant in this file.**  1016-A (`records/1016a-read.txt` §2) is the
    lesson: the storm VA and the spin pc MOVE between runs (`0x61000` -> `0x2e000` -> `0xe6000` ->
    `0x27000` for the storm; `0x2be97...` -> `0x2beef...` for the poll).  So the reader derives the
    frontier pc from `xnu_live_stall_pc`'s LAST DISTINCT value and the undef pc from
    `xnu_live_undef_pc`'s rows - it holds NO literal for either.
  * **The run's slide is not assumed.**  1017 §1.1's arithmetic used `0x2e10000`, THIS run's slide
    from the capture's OWN `IOS7LAB cache mapping` rows.  A new run has a NEW slide, so a check that
    hard-coded "un-slid 0x3823aa54" would name the WRONG instruction
    ([[mi4-measurement-defects]] - a slid address read as un-slid).  This reader therefore compares in
    the SLID domain only (undef pc vs stall pc), and prints the slide it saw when it can derive it,
    flagging `# UNMEASURED` when the mapping rows are absent.
  * **A ladder's top row is not a frontier.**  `UNDEF_LIVE_MAX` is a BAND under the direct channel
    (design §1, `# UNMEASURED` its true ceiling); a `_seq` pinned at the band is the BAND, not the
    device (`records/1016a-read.txt` §3's defect).  The reader reports the band pin explicitly.
  * **A record's absence is a reading only if the channel was live.**  U-CTL withholds rather than
    concluding.

------------------------------------------------------------------------------
MECHANICAL ONLY.  This reader does not claim iOS 7 userspace launched.
------------------------------------------------------------------------------
"""

import argparse
import importlib.util
import os
import re
import sys

# The row parser is REUSED BY IDENTITY, not cloned - the same discipline
# `tools/check_1015b_capture.py` follows ([[mi4-one-value-two-definitions]]: two copies of one
# definition can drift).  `check_1015_capture.py` is imported by path (its filename is not a legal
# identifier - `1015` starts with a digit); its `if __name__ == "__main__"` guard means importing it
# does not run its reader.
_HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location("check_1015_capture",
                                              os.path.join(_HERE, "check_1015_capture.py"))
C15 = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(C15)

# The undef witness keys, in the order `__wrap_sleh_undef` writes them (entry_trace.c:5537-5605):
# `_seq` opens, then `_user_seq` when the mode is USR, then the four detail keys of the first entries.
UNDEF_KEYS = ("xnu_live_undef_seq", "xnu_live_undef_user_seq", "xnu_live_undef_pc",
              "xnu_live_undef_lr", "xnu_live_undef_spsr", "xnu_live_undef_user")

CAP_KEY = "xnu_live_capped"
STALL_PC_KEY = "xnu_live_stall_pc"
STALL_CPSR_KEY = "xnu_live_stall_cpsr"
STALL_N_KEY = "xnu_live_stall_n"
WDT_PETS_KEY = "xnu_live_wdt_pets"
BOOT_UNDEF_KEY = ("stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled")

# The user-mode test is Apple's own (`entry_trace.c` uses the same mask): CPSR bits[4:0] == 0x10.
PSR_MODE_MASK = 0x1F
PSR_USER_MODE = 0x10
UNDEF_BAND = 64          # UNDEF_LIVE_MAX under STAGE90_XNU_UNDEF_DIRECT (design §1; # UNMEASURED)


def say(tag, msg=""):
    print("[1018] %-18s %s" % (tag, msg))


def verdict(cell, msg):
    print("[1018] %-18s %s" % (cell, msg))
    return cell


def undef_rows(cap):
    """One dict per undef entry, bound to `__wrap_sleh_undef`'s ONE write order.

    The recorder writes `_seq` first, then `_user_seq` (only for a USR entry), then - for entries
    within the band - `_pc`/`_lr`/`_spsr`/`_user` in that order.  So a row OPENS at `_seq` and the
    detail keys that follow belong to it.  A `_seq` with no following `_pc` is reported (it is a
    real reading: an entry past the band).
    """
    rows = []
    cur = None
    for line_no, key, _raw, val in cap.toks:
        if key == "xnu_live_undef_seq":
            cur = {"line": line_no, "seq": val, "pc": None, "lr": None, "spsr": None, "user": None}
            rows.append(cur)
        elif cur is not None and key in ("xnu_live_undef_pc", "xnu_live_undef_lr",
                                         "xnu_live_undef_spsr", "xnu_live_undef_user"):
            if key == "xnu_live_undef_pc":
                cur["pc"] = val
            elif key == "xnu_live_undef_lr":
                cur["lr"] = val
            elif key == "xnu_live_undef_spsr":
                cur["spsr"] = val
            else:
                cur["user"] = val
    return rows


def stall_rows(cap):
    """The stall samples, one dict per `_n` (its ordinal).  The sampler writes `_n`/`_t` first,
    then pc/cpsr/sp/lr/nz (entry_stubs.c:4657-4663), so `_n` opens a row."""
    rows = []
    cur = None
    for line_no, key, _raw, val in cap.toks:
        if key == STALL_N_KEY:
            cur = {"line": line_no, "n": val, "pc": None, "cpsr": None}
            rows.append(cur)
        elif cur is not None and key == STALL_PC_KEY:
            cur["pc"] = val
        elif cur is not None and key == STALL_CPSR_KEY:
            cur["cpsr"] = val
    return rows


def boot_undef_row(cap):
    """The boot self-test row, read from the RAW TEXT - NOT from `cap.toks`.

    `check_1015_capture.py`'s `KV_RE` is `xnu_live_[a-z0-9_]+` by construction, so the boot key
    `stage90_xnu_arm_...` is NOT in `cap.toks` at all.  A check that looked for it there would be
    blind and would ALWAYS withhold ([[mi4-silence-is-a-reading-only-if-success-is-silent]]: the
    row's absence was really the parser's blindness).  So this reads the raw lines.  Returns
    `(line_no, value)` or None."""
    for i, line in enumerate(cap.lines, 1):
        if BOOT_UNDEF_KEY in line:
            m = re.search(re.escape(BOOT_UNDEF_KEY) + r"=([0-9A-Za-z_.+-]+)", line)
            if m:
                return i, C15.to_int(m.group(1)), m.group(1)
    return None


def resolve_slide(cap):
    """THIS run's slide, derived from the capture's OWN `IOS7LAB cache mapping` geometry, never
    assumed from a prior run.  Returns (slide, base) or (None, None) with `# UNMEASURED` when the
    rows are absent.  The artifact's mapping 0 is fixed at `0x2c000000` (the study's own value,
    `docs/reference/d13-dyld-shared-cache-study.md` §1); `slide = slid_va - 0x2c000000`."""
    ARTIFACT_BASE = 0x2C000000
    for line in cap.lines:
        if "cache mapping 0 " in line:
            for tok in line.split():
                if tok.startswith("va="):
                    try:
                        va = int(tok[3:], 16)
                    except ValueError:
                        break
                    if va >= ARTIFACT_BASE:
                        return va - ARTIFACT_BASE, ARTIFACT_BASE
                    break
    return None, None


def adjudicate(cap):
    rows = undef_rows(cap)
    stalls = stall_rows(cap)

    capped = cap.first(CAP_KEY)
    killed = cap.first("xnu_live_killed") or cap.first("xnu_live_resident_exit")

    say("CAPTURE", "%d bytes, %d lines, %d tokens"
        % (cap.nbytes, len(cap.lines), len(cap.toks)))
    if capped:
        say("CAP", "%s = %s at :%d -- the capped channel stopped HERE"
            % (CAP_KEY, C15.fmt(capped[3]), capped[0]))
        say("", "every entry_live_write record after this line is GONE (records/1018-recon.txt §1)")
    else:
        say("CAP", "no %s marker -- the capped channel was NOT exhausted this run" % CAP_KEY)

    # ---- the null control: was the direct CHANNEL proven live? -----------------------------------
    # Design §1.2: the boot self-test proves the VECTOR fires once at boot; the DIRECT witness must
    # MATCH it with a `_seq >= 1` row.  Both halves are required - the boot row alone is present even
    # in the pre-arm 1015b capture (capture :3480, where the witness was still CAPPED), so a control
    # that accepted it would read that OLD capture as U-SILENT - a falsification produced by the
    # instrument not being wired, which is the 1000 lesson exactly.  The boot row lives OUTSIDE the
    # `xnu_live_*` namespace the shared parser tokenizes, so it MUST be read from the raw text.
    boot = boot_undef_row(cap)
    ctl_live = bool(rows) and boot is not None and boot[1] == 1
    if boot is not None:
        say("CTL", "%s = %s (:%d)"
            % (BOOT_UNDEF_KEY, boot[2], boot[0]))
    else:
        say("CTL", "no %s row -- the vector's boot self-test is ABSENT" % BOOT_UNDEF_KEY)
    if not rows:
        say("CTL", "the boot self-test fired but the DIRECT witness published NO `_seq` row -- "
                   "the direct channel itself did not deliver (design §1.2: WITHHOLD)")

    # ---- the sampler: the LAST DISTINCT pc, and whether the tail is WEDGED (never a literal) --------
    pcs = []                       # distinct pcs in order of appearance
    for s in stalls:
        if s["pc"] is not None and (not pcs or pcs[-1] != s["pc"]):
            pcs.append(s["pc"])
    wedge_pc = pcs[-1] if pcs else None
    # WEDGED = every sample in the last contiguous run of that pc reads IT (>=2 samples).
    tail_run = 0
    for s in reversed(stalls):
        if s["pc"] == wedge_pc:
            tail_run += 1
        else:
            break
    wedged = wedge_pc is not None and tail_run >= 2
    wedge_cpsr = stalls[-1]["cpsr"] if stalls else None
    if wedge_pc is not None:
        say("FRONTIER", "sampler last distinct pc = %s (cpsr %s), tail run x%d, %d distinct pcs, "
                        "%d samples%s"
            % (C15.fmt(wedge_pc), C15.fmt(wedge_cpsr) if wedge_cpsr is not None else "-",
               tail_run, len(pcs), len(stalls), " WEDGED" if wedged else " MOVING"))
    else:
        say("FRONTIER", "no %s in this capture" % STALL_PC_KEY)

    slide, base = resolve_slide(cap)
    if slide is not None:
        say("SLIDE", "this run's cache slide = %s (from the capture's own mapping row)"
            % C15.fmt(slide))
    else:
        say("SLIDE", "%s -- no IOS7LAB cache mapping row; the slide is NOT derived" % "# UNMEASURED")

    say("WITNESS", "%d undef entry/entries recorded: %s"
        % (len(rows), ", ".join("seq=%d pc=%s" % (r["seq"], C15.fmt(r["pc"]))
                                if r["pc"] is not None else "seq=%d (past band)" % r["seq"]
                                for r in rows) if rows else "(none)"))

    # ---- the cells ---------------------------------------------------------------------------------
    if not ctl_live:
        return verdict("U-CTL", "WITHHOLD (design §1.2): the direct undef witness did NOT publish a "
                                "`_seq` row matched by the boot self-test -- the direct channel did "
                                "not run this arm, so the absence below is NOT a reading. Check the "
                                "CAP FIRST (the 1000 lesson); an OLD capture whose witness was still "
                                "on the capped channel reads here as U-CTL, never as U-SILENT.")

    # The pcs the undef witness fires at, and (as CORROBORATION ONLY) the pcs the sampler reads.
    # Both are THIS run's VAs - the slide is never assumed; a slid pc compared against a literal
    # would name the wrong instruction ([[mi4-measurement-defects]]).
    undef_pcs = {r["pc"] for r in rows if r["pc"] is not None}
    sampled_pcs = {s["pc"] for s in stalls if s["pc"] is not None}
    max_seq = rows[-1]["seq"] if rows else 0

    # The witness's DETAIL pcs are INVARIANT iff every row that carries a pc reads the SAME one.
    invariant_pc = (len(undef_pcs) == 1)

    band_pinned = rows[-1]["seq"] >= UNDEF_BAND and rows[-1]["pc"] is None
    if band_pinned:
        say("BAND", "the last undef seq (%d) is past UNDEF_LIVE_MAX (%d): the BAND may be the wall, "
                    "not the device -- re-size before reading the tail (records/1016a-read.txt §3)"
            % (rows[-1]["seq"], UNDEF_BAND))

    pets = cap.all(WDT_PETS_KEY)
    if len(pets) >= 2:
        say("RESIDENT", "%s: first %s last %s" % (WDT_PETS_KEY, C15.fmt(pets[0][3]),
                                                  C15.fmt(pets[-1][3])))

    n_at = len([r for r in rows if r["pc"] in sampled_pcs])
    if killed:
        return verdict("U-FIRES-DIES", "the undef FIRED and the run has a kill/exit marker -- XNU's "
                                       "sleh_undef user arm terminated pid 1: the frontier is a "
                                       "CRASH, not a spin. Next rung = the exception delivery.")

    # **THE RE-TRAP LOOP IS NAMED BY THE DESIGN'S OWN CRITERION (docs/1018-design.md §1.1), NOT BY
    # THE STALL SAMPLER.**  §1.1 says it in as many words: "`_seq` climbing with an invariant `_pc`
    # at the frontier is a re-trap loop".  The sampler was only ever CORROBORATION, and the
    # 2026-10-15 184618 run (arm c2168632) is the proof it CANNOT be the criterion: the sampler fires
    # on the timer tick's powers-of-two ladder and reads the INTERRUPTED context (pe_msm8974.c:529),
    # so every sample in that run is KERNEL mode and the last is capture :32958 - while the USER-mode
    # trap loop begins at :33993 and runs to the end of the console.  Reading the sampler as the
    # decider turned a proven 8183-entry re-trap loop at 1017's own `udf #254` into the false cell
    # U-SILENT: [[mi4-a-claim-in-a-comment-is-not-a-check]] in the reader itself.  So a climbing `_seq`
    # over an invariant detail pc IS the loop, sampler or no sampler.
    if invariant_pc and max_seq >= 2:
        return verdict("U-FIRES-LOOPS", "the undef FIRED at an INVARIANT pc %s and `_seq` climbed to "
                                        "%d (design §1.1 - the re-trap criterion) -- the handler "
                                        "RETURNS and RE-TRAPS (1017 §3.3 mechanism (i)). Same shape "
                                        "as 1010-1015, now at a udf in USER mode. Sampler "
                                        "corroboration: %d of %d detail entries at a sampled pc; "
                                        "the sampler read %d sample(s), last pc %s."
                      % (C15.fmt(next(iter(undef_pcs))), max_seq, n_at, len(rows), len(stalls),
                         C15.fmt(wedge_pc)))

    if not (undef_pcs & sampled_pcs):
        # The witness fired, but NOT at the frontier the sampler reads and NOT as a loop.  This is the
        # honest falsification §3 names - and it is reachable ONLY with the loop test above already
        # having failed, so a real loop can never be misfiled here (the 184618 defect).
        return verdict("U-SILENT", "1017's static identification is CONTRADICTED: the direct "
                                   "witness is live and fired, but never at a pc the sampler reads "
                                   "(undef pcs %s vs sampled pcs %s) and not as a climbing loop "
                                   "(`_seq` max %d). The sampler reads pc=%s but the CPU is not "
                                   "taking an undefined-instruction exception on it."
                      % (", ".join(sorted(C15.fmt(p) for p in undef_pcs)),
                         ", ".join(sorted(C15.fmt(p) for p in sampled_pcs)), max_seq,
                         C15.fmt(wedge_pc)))

    # The undef DID fire at a pc the sampler reads, but did not climb as a loop. Which of ONCE?
    return verdict("U-FIRES-ONCE", "the undef fired at a sampled pc (%d entries, max `_seq` %d) but "
                                   "the sampler's tail pc %s MOVES (tail run x%d, %d distinct pcs) "
                                   "-- the trap is a TRANSITION; the wall is downstream, not here."
                  % (n_at, max_seq, C15.fmt(wedge_pc), tail_run, len(pcs)))


def self_test():
    """A tiny synthetic capture per cell.  Each case asserts the EXACT cell string - asserting
    only "it returned False" is what let a previously-broken control (boot row unparsed => every
    case withholding as U-CTL) still print PASS.  Also proves the reader holds NO literal for the
    frontier pc: two different pcs must yield the same cell."""
    import io
    import contextlib
    import tempfile
    ok = True

    def mk(lines):
        fd, path = tempfile.mkstemp(suffix=".txt")
        with os.fdopen(fd, "w") as fh:
            fh.write("\n".join(lines) + "\n")
        return path

    def cell_of(lines):
        p = mk(lines)
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                return adjudicate(C15.Capture(p))
        finally:
            os.unlink(p)

    def want(name, lines, expected):
        nonlocal ok
        got = cell_of(lines)
        if got != expected:
            say("SELFTEST-FAIL", "%s: expected %s, got %s" % (name, expected, got))
            ok = False

    base = [" xnu_live_wdt_pets=0x00000061",
            " xnu_live_wdt_pets=0x00000062",
            " xnu_live_abt_nfar=0x000003bf",
            " xnu_live_capped=0x00006000"]
    BOOT = " stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x00000001"

    # U-CTL: nothing at all -> withhold.
    want("U-CTL-nothing", list(base), "U-CTL")

    # U-CTL: boot self-test present but the DIRECT witness never fired (the pre-arm 1015b shape:
    # boot row :3480 present, `_seq` zero because the witness was still CAPPED) -> withhold, NOT
    # U-SILENT.
    want("U-CTL-capped", list(base) + [BOOT,
                                       " xnu_live_stall_n=0x0000000b",
                                       " xnu_live_stall_pc=0x3b04aa54",
                                       " xnu_live_stall_cpsr=0x20000030"], "U-CTL")

    # U-SILENT: the witness IS live (boot row + a `_seq` row) but never at a sampled pc.
    want("U-SILENT", list(base) + [BOOT,
        " xnu_live_undef_seq=0x00000001", " xnu_live_undef_pc=0x3b0ccccc",
        " xnu_live_stall_n=0x0000000b", " xnu_live_stall_pc=0x3b04aa54",
        " xnu_live_stall_cpsr=0x20000030"], "U-SILENT")

    # U-FIRES-LOOPS: undef at the frontier, sampler pc frozen.
    loops = list(base) + [BOOT,
        " xnu_live_undef_seq=0x00000001", " xnu_live_undef_pc=0x3b04aa54",
        " xnu_live_undef_user=0x00000001",
        " xnu_live_undef_seq=0x00000002", " xnu_live_undef_pc=0x3b04aa54",
        " xnu_live_stall_n=0x0000000b", " xnu_live_stall_pc=0x3b04aa54",
        " xnu_live_stall_cpsr=0x20000030",
        " xnu_live_stall_n=0x0000000c", " xnu_live_stall_pc=0x3b04aa54",
        " xnu_live_stall_cpsr=0x20000030"]
    want("U-FIRES-LOOPS", loops, "U-FIRES-LOOPS")

    # The SAME shape at a DIFFERENT pc: the reader must not hold a literal.
    want("no-literal", [ln.replace("0x3b04aa54", "0x3c0bbbbb") for ln in loops], "U-FIRES-LOOPS")

    # **THE 2026-10-15 184618 REGRESSION GUARD (arm c2168632).**  A climbing `_seq` over an INVARIANT
    # pc, where the sampler reads ONLY a kernel-mode pc elsewhere and NEVER the trap - because the
    # sampler fires on the timer tick and reads the INTERRUPTED context, while this trap is USER mode
    # after the last tick.  This is a RE-TRAP LOOP (design §1.1: "`_seq` climbing with an invariant
    # `_pc`").  The reader that required a sampler intersection read all 8183 entries of that run as
    # U-SILENT - a falsification of 1017 produced by the reader, not by the device.  The sampler's
    # absence must NOT withhold the loop cell.
    loop_nosampler = list(base) + [BOOT,
        " xnu_live_undef_seq=0x00000001", " xnu_live_undef_pc=0x39cdfa54",
        " xnu_live_undef_user=0x00000001",
        " xnu_live_undef_seq=0x00000002", " xnu_live_undef_pc=0x39cdfa54",
        " xnu_live_undef_user=0x00000001",
        " xnu_live_undef_seq=0x00000003", " xnu_live_undef_pc=0x39cdfa54",
        " xnu_live_stall_n=0x0000000b", " xnu_live_stall_pc=0x8001cd5d",
        " xnu_live_stall_cpsr=0x60000113"]
    want("U-FIRES-LOOPS-nosampler", loop_nosampler, "U-FIRES-LOOPS")

    # ... and a SINGLE entry at that same out-of-sampler pc is NOT a loop: it is the honest
    # U-SILENT (the witness fired once, the sampler never read it, `_seq` did not climb).
    want("U-SILENT-single", list(base) + [BOOT,
        " xnu_live_undef_seq=0x00000001", " xnu_live_undef_pc=0x39cdfa54",
        " xnu_live_stall_n=0x0000000b", " xnu_live_stall_pc=0x8001cd5d",
        " xnu_live_stall_cpsr=0x60000113"], "U-SILENT")

    # U-FIRES-ONCE: one undef at the frontier, then the sampler MOVES.
    once = list(base) + [BOOT,
        " xnu_live_undef_seq=0x00000001", " xnu_live_undef_pc=0x3b04aa54",
        " xnu_live_stall_n=0x0000000b", " xnu_live_stall_pc=0x3b04aa54",
        " xnu_live_stall_cpsr=0x20000030",
        " xnu_live_stall_n=0x0000000c", " xnu_live_stall_pc=0x3b04fff0",
        " xnu_live_stall_cpsr=0x20000030"]
    want("U-FIRES-ONCE", once, "U-FIRES-ONCE")

    # U-FIRES-DIES: undef at the frontier AND a kill/exit marker.
    dies = list(base) + [BOOT,
        " xnu_live_undef_seq=0x00000001", " xnu_live_undef_pc=0x3b04aa54",
        " xnu_live_killed=0x00000001",
        " xnu_live_stall_n=0x0000000b", " xnu_live_stall_pc=0x3b04aa54",
        " xnu_live_stall_cpsr=0x20000030"]
    want("U-FIRES-DIES", dies, "U-FIRES-DIES")

    say("SELFTEST", "PASS" if ok else "FAIL")
    return ok


def main():
    ap = argparse.ArgumentParser(description="1018 cap-exempt undef witness reader")
    ap.add_argument("capture", nargs="?")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        sys.exit(0 if self_test() else 1)
    if not args.capture:
        ap.error("a capture path is required (or --selftest)")
    cap = C15.Capture(args.capture)
    adjudicate(cap)


if __name__ == "__main__":
    main()