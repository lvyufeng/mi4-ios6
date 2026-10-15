#!/usr/bin/env python3
"""Mechanical reader for a **1020** press capture - DID THE GENUINE `udf` RAISE, OR STILL LOOP?

WHAT 1020 IS.  `docs/1020-design.md`.  1019 (`records/1019-rootcause.txt`) named the ROOT CAUSE of
pid 1's frontier, statically and by value: `sleh_undef`'s user-mode arm
(`external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:1108-1126`) tests the undefined instruction against
one hard-coded value (`!= 0xfedeffe7`) as a STAND-IN for "is this a coprocessor/VFP instruction", and
for EVERY other undefined instruction it enables VFP and then `return;`s with the PC still on the
`udf` - a perfect RE-TRAP LOOP the moment VFP is already on.  The 2026-10-15 184618 capture
(`out/stage90/captures/recovered-unknown-arm-20261015-184618-last_kmsg.txt`) measured that loop:
`xnu_live_undef_seq` climbs to `0x1ff7` (8183) at ONE invariant pc `0x39cdfa54`, with ZERO
`usermode undefined instruction` printfs.

1020's rung (`STAGE90_XNU_UNDEF_RAISE`) moves the `return;` INSIDE the `if (!thread->machine.vfp_enable)`
block, so the first trip is the lazy-VFP-enable (a real VFP insn re-executes and advances) and a
SECOND trip - VFP already on and the instruction STILL undefined - falls through to the handler's own
`printf("...[%d]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x%08x ...")` and
`exception_type = EXC_BAD_INSTRUCTION` + `doexception(...)`.

THE WITNESS.  The six pre-existing keys `xnu_live_undef_{seq,user_seq,pc,lr,spsr,user}` (written by
`__wrap_sleh_undef`, `entry_trace.c:5537`) ride the DIRECT channel under the 1018 arm's
`STAGE90_XNU_UNDEF_DIRECT=1`, which THIS arm carries - so they survive `ENTRY_LIVE_CAP` and are read
BY VALUE here.  The POSITIVE signal that the raise path was taken is the handler's OWN kernel printf
`usermode undefined instruction, EXC_BAD_INSTRUCTION ...`.

THIS READER announces EXACTLY ONE cell, and the cells are the design's §5 table:

    R-RAISES-ONCE    `_seq` advances to >= 2 at ONE pc and STOPS (<= UNDEF_LOOP_MAX) + printf >= 1  => FIXED
    R-RAISES-CLEAN   `_seq` max == 1 (VFP was already on) + printf >= 1, no loop                    => FIXED (cleaner)
    R-STILL-LOOPS    `_seq` climbs PAST UNDEF_LOOP_MAX at one invariant pc + printf == 0            => NOT FIXED
    R-VFP-CONTINUES  no undef loop (bounded) + printf == 0 (the boot advanced past the udf)         => MOVED
    R-CTL            the witness was NOT live (control absent) - WITHHOLD                          => INSTRUMENT_UNSOUND

------------------------------------------------------------------------------
WHAT THIS READER READS BY VALUE, AND WHAT IT REFUSES TO ASSUME
------------------------------------------------------------------------------
  * **The udf pc is not a constant in this file.**  1016-A (`records/1016a-read.txt` §2) is the
    lesson: the storm VA and the spin pc MOVE between runs.  So the reader derives the invariant pc
    from `xnu_live_undef_pc`'s OWN distinct values and holds NO literal for it (the self-test proves
    this: two different pcs must yield the same cell).
  * **The control is TWO halves.**  The boot self-test row
    `stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1` proves the VECTOR fires once at
    boot; the DIRECT witness must MATCH it with a `_seq >= 1` row.  Both are required - the boot row
    alone is present even in the pre-arm 1015b capture, where the witness was still CAPPED
    ([[mi4-a-claim-in-a-comment-is-not-a-check]]: a control that accepted the boot row alone would
    read an OLD capture as a falsification the instrument produced).  The boot row lives OUTSIDE the
    `xnu_live_*` namespace the shared parser tokenizes, so it MUST be read from the raw text.
  * **The printf is the raise path's OWN signal.**  It is counted on the raw text (the printf in
    trap.c carries no `xnu_live_` key).  A capture with a climbing `_seq` and ZERO printfs is the
    re-trap LOOP, not a raise - the 184618 shape.

------------------------------------------------------------------------------
MECHANICAL ONLY.  This reader does not claim iOS 7 userspace launched.
------------------------------------------------------------------------------
"""

import argparse
import importlib.util
import os
import re
import sys

# The row parser is REUSED BY IDENTITY, not cloned - the same discipline `tools/check_1018_capture.py`
# follows ([[mi4-one-value-two-definitions]]: two copies of one definition can drift).  Its filename
# is not a legal identifier (`1015` starts with a digit), so it is imported by path; its
# `if __name__ == "__main__"` guard means importing it does not run its reader.
_HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location("check_1015_capture",
                                              os.path.join(_HERE, "check_1015_capture.py"))
C15 = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(C15)

# The undef witness keys, in the order `__wrap_sleh_undef` writes them (entry_trace.c:5537-5605):
# `_seq` opens, then `_user_seq` when the mode is USR, then the four detail keys of the first entries.
UNDEF_KEYS = ("xnu_live_undef_seq", "xnu_live_undef_user_seq", "xnu_live_undef_pc",
              "xnu_live_undef_lr", "xnu_live_undef_spsr", "xnu_live_undef_user")

SEQ_KEY = "xnu_live_undef_seq"
PC_KEY = "xnu_live_undef_pc"

# The boot self-test row (the control's first half).  It is NOT in the `xnu_live_*` namespace the
# shared parser tokenizes, so it is read from the raw text (checked below).
BOOT_UNDEF_KEY = "stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled"

# The handler's OWN printf (`osfmk/arm/trap.c`, design §0).  Its tail varies (`in map %p (pmap %p)`),
# so only the invariant prefix is matched.  This is the POSITIVE signal that the raise path was taken.
PRINTF_MARK = "usermode undefined instruction"

# A panic may follow a raise (the design's §5 R-RAISES-ONCE: "either the process is gone or a panic
# follows"); it is REPORTED, never a gate.
PANIC_MARK = "panic"

# The loop bound (design §5, `UNDEF_LOOP_MAX`).  A `_seq` at/under this at ONE invariant pc is a
# BOUNDED sequence (a raise that took one or a few lazy-VFP trips); a `_seq` PAST it at one pc is the
# re-trap LOOP the defect produces (the 184618 capture measured 8183).
UNDEF_LOOP_MAX = 100


def say(tag, msg=""):
    print("[1020] %-18s %s" % (tag, msg))


def verdict(cell, msg):
    print("[1020] %-18s %s" % (cell, msg))
    return cell


def undef_rows(cap):
    """One dict per undef entry, bound to `__wrap_sleh_undef`'s ONE write order.

    The recorder writes `_seq` first, then `_user_seq` (only for a USR entry), then - for entries
    within the band - `_pc`/`_lr`/`_spsr`/`_user` in that order.  So a row OPENS at `_seq` and the
    detail keys that follow belong to it.  A `_seq` with no following `_pc` is reported (it is a real
    reading: an entry past the band)."""
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


def boot_undef_row(cap):
    """The boot self-test row, read from the RAW TEXT - NOT from `cap.toks`.

    `check_1015_capture.py`'s `KV_RE` is `xnu_live_[a-z0-9_]+` by construction, so the boot key
    `stage90_xnu_arm_...` is NOT in `cap.toks` at all.  A check that looked for it there would be
    blind and would ALWAYS withhold ([[mi4-silence-is-a-reading-only-if-success-is-silent]]: the
    row's absence would really be the parser's blindness).  So this reads the raw lines.  Returns
    `(line_no, int_value, raw_value)` or None."""
    for i, line in enumerate(cap.lines, 1):
        if BOOT_UNDEF_KEY in line:
            m = re.search(re.escape(BOOT_UNDEF_KEY) + r"=([0-9A-Za-z_.+-]+)", line)
            if m:
                return i, C15.to_int(m.group(1)), m.group(1)
    return None


def printf_count(cap):
    """The number of handler `usermode undefined instruction` printf lines in the capture.

    The handler's printf carries NO `xnu_live_` key (it is a plain kernel `printf`), so it is counted
    on the raw text.  This is the raise path's OWN positive signal (design §5)."""
    return sum(line.count(PRINTF_MARK) for line in cap.lines)


def adjudicate(cap):
    rows = undef_rows(cap)
    seqs = [r["seq"] for r in rows if r["seq"] is not None]
    max_seq = max(seqs) if seqs else 0
    # The witness's DETAIL pcs are INVARIANT iff every pc row present reads the SAME one value
    # (design: "the pc is invariant iff every distinct `xnu_live_undef_pc` value ... is the same
    # single value").  An EMPTY set is NOT invariant - a pc value is required to call a sequence a
    # single-pc sequence (mirrors check_1018's `invariant_pc = (len(pcs) == 1)`).
    pc_vals = sorted({r["pc"] for r in rows if r["pc"] is not None})
    invariant_pc = (len(pc_vals) == 1)
    n_print = printf_count(cap)
    n_panic = sum(line.count(PANIC_MARK) for line in cap.lines)

    say("CAPTURE", "%d bytes, %d lines, %d tokens" % (cap.nbytes, len(cap.lines), len(cap.toks)))

    # ---- THE CONTROL (design §5, R-CTL), read FIRST: the boot self-test half and the DIRECT half.
    boot = boot_undef_row(cap)
    if boot is not None:
        say("CTL", "%s = %s (:%d)" % (BOOT_UNDEF_KEY, boot[2], boot[0]))
    else:
        say("CTL", "no %s row -- the vector's boot self-test is ABSENT" % BOOT_UNDEF_KEY)
    if boot is not None and boot[1] != 1:
        say("CTL", "the boot self-test row is present but reads %s (want 0x1) -- the vector's boot "
                   "self-test did not PASS" % boot[2])
    if rows:
        say("CTL", "the DIRECT undef witness published x%d `_seq` row(s): max `_seq` = %d"
            % (len(rows), max_seq))
    else:
        say("CTL", "the DIRECT undef witness published NO `_seq` row -- the direct channel did not "
                   "deliver (design §5: WITHHOLD)")

    ctl_live = bool(rows) and boot is not None and boot[1] == 1

    say("WITNESS", "%d undef entry/entries; max `_seq` = %d; `_pc` %s; printf x%d; `%s`-string x%d"
        % (len(rows), max_seq,
           (C15.fmt(pc_vals[0]) + " (INVARIANT)") if invariant_pc else
           ("{%s}" % ", ".join(C15.fmt(p) for p in pc_vals) if pc_vals else "(none)"),
           n_print, PANIC_MARK, n_panic))

    # ---- R-CTL: WITHHOLD.  The witness was not live, so NOTHING below is a reading.
    # [[mi4-a-claim-in-a-comment-is-not-a-check]] / the 1000 lesson: an absent effect whose channel
    # was wrong is not a falsification.
    if not ctl_live:
        why = []
        if boot is None:
            why.append("the boot self-test row `%s` is ABSENT" % BOOT_UNDEF_KEY)
        elif boot[1] != 1:
            why.append("the boot self-test row reads %s (want 0x1)" % boot[2])
        if not rows:
            why.append("the DIRECT witness published NO `xnu_live_undef_seq` row")
        return verdict("R-CTL", "WITHHOLD (design §5, the mandatory-first cell): %s.  The control is "
                                "TWO halves - the boot self-test row AND at least one `_seq` row - and "
                                "the witness was not live, so the absence below is NOT a reading and "
                                "MUST NOT be read as R-STILL-LOOPS or R-VFP-CONTINUES."
                      % ("; ".join(why)))

    # ---- R-RAISES-ONCE: `_seq` reaches >= 2 at ONE invariant pc AND does NOT climb past the bound
    #      AND the handler's own printf fired.  The lazy-VFP first trip returned; the second trip
    #      raised.
    if invariant_pc and 2 <= max_seq <= UNDEF_LOOP_MAX and n_print >= 1:
        return verdict("R-RAISES-ONCE", "FIXED - the genuine `udf` RAISED after the lazy-VFP first "
                                        "trip.  `_seq` advanced to %d at the SINGLE invariant pc %s "
                                        "and then STOPPED (<= UNDEF_LOOP_MAX %d - no lockstep climb "
                                        "to thousands), and the handler's own `%s` printf fired x%d. "
                                        "The moved `return;` took effect: trip 1 enabled VFP and "
                                        "returned, trip 2 found VFP on and fell through to "
                                        "EXC_BAD_INSTRUCTION (design §1/§5).  The infinite re-trap "
                                        "loop is dead."
                      % (max_seq, C15.fmt(pc_vals[0]), UNDEF_LOOP_MAX, PRINTF_MARK, n_print))

    # ---- R-RAISES-CLEAN: the very FIRST trip fell through (VFP was already on for this thread), so
    #      `_seq` is exactly 1 and the printf fired.  Even cleaner than R-RAISES-ONCE.
    if max_seq == 1 and n_print >= 1:
        return verdict("R-RAISES-CLEAN", "FIXED (VFP already on) - the very FIRST trip RAISED.  "
                                         "`_seq` max == 1 (VFP was already enabled for this thread, so "
                                         "the lazy-VFP branch was never taken) and the handler's own "
                                         "`%s` printf fired x%d.  The `udf` never re-trapped at all - "
                                         "the cleanest possible reading of the fix (design §5 "
                                         "R-RAISES-CLEAN)." % (PRINTF_MARK, n_print))

    # ---- R-STILL-LOOPS: `_seq` climbs PAST the bound at ONE invariant pc and the printf NEVER fired.
    #      The moved `return;` did not take effect - check the pool `-D` reached `osfmk_arm_trap.o`
    #      (design §6: an absent effect whose channel was wrong - the 1010 lesson).
    if invariant_pc and max_seq > UNDEF_LOOP_MAX and n_print == 0:
        return verdict("R-STILL-LOOPS", "NOT FIXED - the re-trap LOOP is still here.  `_seq` climbed "
                                        "to %d at the SINGLE invariant pc %s (> UNDEF_LOOP_MAX %d) "
                                        "with the handler's printf firing x0 - the handler RETURNS "
                                        "and RE-TRAPS, exactly the 184618 shape.  The moved `return;` "
                                        "did not take effect: check that `-DSTAGE90_XNU_UNDEF_RAISE=1` "
                                        "reached the KERNEL POOL object (`osfmk_arm_trap.o`), not "
                                        "just the env (design §6, the 1010 lesson)."
                      % (max_seq, C15.fmt(pc_vals[0]), UNDEF_LOOP_MAX))

    # ---- R-VFP-CONTINUES: no undef loop (bounded `_seq`) AND the printf NEVER fired - the boot
    #      advanced PAST the udf to a DIFFERENT frontier without raising (design §5).
    return verdict("R-VFP-CONTINUES", "MOVED - no undef LOOP and NO raise: `_seq` is bounded (max %d, "
                                      "<= UNDEF_LOOP_MAX %d) and the handler's `%s` printf fired x%d.  "
                                      "The boot advanced PAST the udf to a DIFFERENT frontier without "
                                      "taking the raise path; the next frontier is downstream (the "
                                      "`_mach_vm_map`/allocator question, records/1019-rootcause.txt "
                                      "§6)."
                  % (max_seq, UNDEF_LOOP_MAX, PRINTF_MARK, n_print))


def self_test():
    """A tiny synthetic capture per cell.  Each case asserts the EXACT cell string - asserting only
    "it returned False" is what let a previously-broken control (the boot row lives outside the
    `xnu_live_*` namespace the shared parser tokenizes, so it was never seen) pass a wrong cell.
    Also proves the reader holds NO literal for the udf pc: two different pcs must yield the same
    cell."""
    import contextlib
    import io
    import tempfile
    ok = True

    def mk(lines):
        fd, path = tempfile.mkstemp(suffix=".txt")
        with os.fdopen(fd, "w") as f:
            f.write("\n".join(lines) + "\n")
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

    BOOT = " stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x00000001"
    BOOT_BAD = " stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x00000000"
    # The handler's own printf, matched by its invariant prefix (trap.c:1137).
    PRINTF = (" launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x39cdfa54 in map "
              "0x3a0f2000 (pmap 0x...)",)

    def seq_lines(n, pc=0x39CDFA54):
        out = []
        for i in range(1, n + 1):
            out.append(" xnu_live_undef_seq=0x%08x" % i)
            out.append(" xnu_live_undef_user_seq=0x%08x" % i)
            out.append(" xnu_live_undef_pc=0x%08x" % pc)
        return out

    # R-CTL-noselftest: the DIRECT witness fired, but the boot self-test row is ABSENT -> withhold.
    want("R-CTL-noselftest", seq_lines(3), "R-CTL")

    # R-CTL-noseq: the boot self-test row is present, but the DIRECT witness never published a `_seq`
    # row (the pre-arm shape: the witness was still CAPPED) -> withhold, NOT R-VFP-CONTINUES.
    want("R-CTL-noseq", [BOOT, " xnu_live_stall_pc=0x8001cd5d",
                         " xnu_live_stall_cpsr=0x60000113"], "R-CTL")

    # R-CTL: the boot row is present but reads 0 (the vector's self-test did not PASS) -> withhold.
    want("R-CTL-bootbad", [BOOT_BAD] + seq_lines(3), "R-CTL")

    # R-RAISES-ONCE: seq 1..5 at ONE pc + the handler's printf.
    want("R-RAISES-ONCE", [BOOT] + seq_lines(5) + list(PRINTF), "R-RAISES-ONCE")

    # The SAME shape at a DIFFERENT pc: the reader must not hold a literal.
    want("R-RAISES-ONCE-noliteral", [BOOT] + seq_lines(5, pc=0x2BE7E0C4) + list(PRINTF),
         "R-RAISES-ONCE")

    # R-RAISES-CLEAN: `_seq` max == 1 (VFP already on) + the printf.
    want("R-RAISES-CLEAN", [BOOT] + seq_lines(1) + list(PRINTF), "R-RAISES-CLEAN")

    # R-STILL-LOOPS: seq 1..200 at ONE pc, NO printf (the 184618 shape).
    want("R-STILL-LOOPS", [BOOT] + seq_lines(200), "R-STILL-LOOPS")

    # R-VFP-CONTINUES: seq 1..3 at ONE pc, NO printf, bounded.
    want("R-VFP-CONTINUES", [BOOT] + seq_lines(3), "R-VFP-CONTINUES")

    # R-VFP-CONTINUES: a SINGLE bounded entry, no printf, is NOT a loop and NOT a raise.
    want("R-VFP-CONTINUES-single", [BOOT] + seq_lines(1), "R-VFP-CONTINUES")

    # The BOUNDARY is EXACT: `_seq` == UNDEF_LOOP_MAX with a printf is R-RAISES-ONCE, not loops.
    want("boundary-max-once", [BOOT] + seq_lines(UNDEF_LOOP_MAX) + list(PRINTF), "R-RAISES-ONCE")
    # ... and `_seq` == UNDEF_LOOP_MAX + 1 with NO printf is R-STILL-LOOPS.
    want("boundary-over-loops", [BOOT] + seq_lines(UNDEF_LOOP_MAX + 1), "R-STILL-LOOPS")

    # A climbing `_seq` over a NON-invariant pc is NOT the single-pc loop cell: with a printf it is
    # NOT R-RAISES-ONCE either (the criterion requires ONE invariant pc) - it falls to the residual.
    two_pc = [BOOT]
    for i in range(1, 5):
        two_pc += [" xnu_live_undef_seq=0x%08x" % i, " xnu_live_undef_pc=0x39cdfa54"]
    two_pc += [" xnu_live_undef_seq=0x00000006", " xnu_live_undef_pc=0x2be7e0c4"]
    want("two-pc-not-once", two_pc + list(PRINTF), "R-VFP-CONTINUES")

    say("SELFTEST", "PASS" if ok else "FAIL")
    return ok


def main():
    ap = argparse.ArgumentParser(description="1020 undef-raise reader")
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