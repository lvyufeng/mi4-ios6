#!/usr/bin/env python3
"""Mechanical reader for a **1021d** press capture - DID REMOVING THE KERNEL POOL'S COMPILER-EMITTED
FP/NEON RETIRE THE KERNEL-MODE NEON UNDEF AT `0x8047b0ac`?

WHAT 1021d IS.  `docs/1021c-general-regs-design.md`.  1020b (`records/1020b-press-read.txt`) raised
pid 1's `udf` (the 1019 loop is dead) and the frontier MOVED to a **kernel-mode** undefined
instruction at `xnu_entry_undef_pc=0x8047b0ac` / `xnu_entry_undef_lr=0x8047b0b0` (`spsr=0x40000093`,
`_user=0`), inside `mach_msg_send`.  `records/1021-fpu-rootcause.txt` proved the mechanism by value:
the instruction is compiler-emitted NEON (`vld1.32 {d16-d17},[r0]!`, a vectorised struct copy of
`msgh_sender`/`msgh_audit`), it executes when `FPEXC.EN` is 0, and `FPEXC.EN` goes to 0 because
1020b's OWN success (reaching pid 1's VFP trap) sets that thread's `machine.vfp_enable`, so the next
`save_vfp_context` clears it - and nothing ever restores it on switch-in.

1021d's rung builds the kernel pool with **`-msoft-float`** (`docs/1021c-general-regs-design.md` §2.1;
the exact spelling Apple's own ARM kernel uses, `MakeInc.def:377-379`) so that no compiler-emitted
FP/NEON remains in the pool.  The device question (§4.3) is exactly:

    Does removing compiler-emitted FP/NEON from the kernel pool retire the kernel-mode NEON undef at
    `0x8047b0ac`, and does the boot then move past `mach_msg_send`?

THIS READER announces EXACTLY ONE cell, and the cells are the design's §4.4 table:

    A-RETIRED            no kernel-mode undef at `0x8047b0ac`; the run reached the 1020b frontier's
                         trigger point and moved past it (the frontier moved, or the run ends at
                         `entry_epilogue`)                                    => THE FIX STANDS
    B-STILL-THERE        the same kernel-mode undef at `0x8047b0ac` / `lr=0x8047b0b0`  => NOT FIXED
    C-NEW-FAULT          a DIFFERENT kernel-mode undef pc, or a link/undefined-name refusal naming an
                         `__aeabi_f*`/`__aeabi_d*`                            => NEW, nameable, fixable
    D-DIFFERENT-FAILURE  the run stops EARLIER than the mach_msg path (a `panic` in `arm_init`'s VFP
                         self-test, a `panic` from `init_vfp`, or any stop before the frontier)
                                                                             => the flag broke the FPU
    E-NO-CHANGE          byte-for-byte the 1020b frontier (same undef pc and lr AND the same frontier
                         record), with no other progress signal              => the switch did not land
    R-CTL                the CONTROLS are absent / the run's own silence is unreadable - WITHHOLD

------------------------------------------------------------------------------
WHAT THIS READER READS BY VALUE, AND WHAT IT REFUSES TO ASSUME
------------------------------------------------------------------------------
  * **THE TRAP RECORD IS CAP-EXEMPT, AND THAT IS WHY THE `0x8047b0ac` ABSENCE IS READABLE.**
    `xnu_entry_undef_*` is written by `fleh_undef` through `entry_panic_kv` into `g_panic_buf` - a
    buffer of its own, printed under a `trap record:` heading whenever `g_panic_entered != 0`
    (`src/entry/entry_stubs.c:6382-6389`).  It is NOT `entry_live_write`, so it is NOT subject to
    `ENTRY_LIVE_CAP`.  So "no `xnu_entry_undef_pc`" can be a reading - provided the record's own
    channel reached DRAM, which is a CASE OF ITS OWN the reader checks first.
  * **THE LIVE CHANNEL IS CAPPED, AND THE ANCHOR RIDES IT.**  The 1020 raise witness
    (`xnu_live_undef_{seq,user_seq,pc,lr,spsr,user}`) rides the DIRECT channel under
    `STAGE90_XNU_UNDEF_DIRECT=1` (`entry_trace.c:5515-5605`); with that switch off it rides
    `entry_live_write` and dies at `ENTRY_LIVE_CAP` (`entry_stubs.c:4443`).  1018's whole defect was
    reading its absence as "the `udf` never happened" when the channel had simply been exhausted
    (`records/1018-recon.txt`).  So the reader computes whether the live channel was still delivering
    AFTER the `xnu_live_capped` marker; a capped-out channel is a WITHHOLD, never a "it did not
    happen".  `[[mi4-silence-is-a-reading-only-if-success-is-silent]]`.
  * **A-RETIRED IS CONCLUDED FROM SILENCE, SO IT NEEDS A PROGRESS ANCHOR - AND THE READER NAMES IT.**
    `[[mi4-measurement-defects]]`: a verdict read off an absence must show the run reached at least as
    far as the run it is being compared to.  The anchor here is **the 1020b frontier's own trigger
    point**: the run must show it HANDLED pid 1's user `udf` - either the kernel's own printf
    `usermode undefined instruction` (raw console text, cap-exempt) or a BOUNDED, invariant, user-mode
    `xnu_live_undef_*` sequence (the DIRECT witness).  `records/1021-fpu-rootcause.txt` §3 step 4 puts
    the `mach_msg_send` NEON "immediately after the printing", so that anchor is the nearest thing to
    the frontier that a capture can measure.  The channel used is printed on every run.
  * **E IS A COMPARISON, NOT A REMEMBERED NUMBER.**  `docs/1021c-general-regs-design.md` §5.2 warns
    against asserting a remembered value.  So E is decided by comparing the capture against a BASELINE
    capture (`--baseline`, defaulting to the 1020b recovery when it is on disk) and is UNAVAILABLE when
    there is no baseline.  **A capture is never its own baseline**: the reader refuses that identity
    explicitly, because a comparison a file makes with itself cannot fail
    (`[[mi4-a-claim-in-a-comment-is-not-a-check]]`).
  * **THE `0x8047b0ac` LITERAL IS THE DESIGN'S, AND IT IS A GATE.**  Unlike the 1015 storm VA - which
    moves per run, so `tools/check_1015_capture.py` holds no literal - the design's §4.4 cell B NAMES
    this address and its `lr`, and the frontier's identity is the question.  So the literal is held,
    cited, and the self-test proves the boundary is EXACT (ref pc -> B/E; ref pc + 4 -> C).  The reader
    also checks the recorder's own arithmetic: `xnu_entry_undef_pc` is written as `lr_undef - 4`
    (`entry_stubs.c:11322`), so `pc == lr - 4` is a tautology a real capture cannot violate - a capture
    that does violate it has a broken producer, and a status is a verdict only if its producer
    delivered one (`[[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]`).
  * **D IS DEFINED BY ORDER, NOT BY SPELLING.**  "Stops EARLIER than the mach_msg path" is measured as:
    there IS stop evidence (a trap record, a non-zero `xnu_entry_panic_entered`, a VFP self-test panic
    string, or an `xnu_entry_why_byte` naming an epilogue reason) AND the progress anchor is ABSENT.  A
    `panic("Leo VFP access unavailable CPACR=...")` in `arm_init` (`arm_init.c:237`) happens at boot,
    before pid 1 ever runs, so it cannot coexist with the anchor.

------------------------------------------------------------------------------
MECHANICAL ONLY.  This reader does not claim iOS 7 userspace launched, and it does not read the build's
own by-value cell (the pool's FP-instruction count, design §5) - that is a property of the objects, not
of a capture.  A-RETIRED says the DEVICE question was answered; §5's clause still owes the object
measurement.
------------------------------------------------------------------------------
"""

import argparse
import contextlib
import importlib.util
import io
import os
import re
import sys

# The row parser is REUSED BY IDENTITY, not cloned - the same discipline `tools/check_1020_capture.py`
# follows ([[mi4-one-value-two-definitions]]: two copies of one definition can drift).  Its filename is
# not a legal identifier (`1015` starts with a digit), so it is imported by path; its
# `if __name__ == "__main__"` guard means importing it does not run its reader.
_HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location("check_1015_capture",
                                              os.path.join(_HERE, "check_1015_capture.py"))
C15 = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(C15)

# ---------------------------------------------------------------------------------------------------
# THE FRONTIER'S IDENTITY - the design's own literal (docs/1021c §4.4 cell B).  These are LINKED-image
# VAs, which do not slide.
# ---------------------------------------------------------------------------------------------------
REF_UNDEF_PC = 0x8047B0AC
REF_UNDEF_LR = 0x8047B0B0
KERNEL_MODE_SVC = 0x13              # spsr mode field 0x13 = SVC (kernel); 0x10 = USR

# The four keys `fleh_undef` writes into `g_panic_buf` (entry_stubs.c:11321-11323, :11366).  They are
# NOT in the `xnu_live_*` namespace the shared parser tokenises, so they are read from the raw text.
KERNEL_UNDEF_KEYS = ("xnu_entry_undef_pc", "xnu_entry_undef_lr",
                     "xnu_entry_undef_spsr", "xnu_entry_undef_user")
TRAP_RECORD_MARK = "trap record:"
PANIC_ENTERED_KEY = "xnu_entry_panic_entered"
WHY_KEY = "xnu_entry_why"
WHY_BYTE_KEY = "xnu_entry_why_byte"

# The control's first half: the vector's boot self-test (raw text - the same key check_1020_capture.py
# reads, and for the same reason).
BOOT_UNDEF_KEY = "stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled"

# The 1020 raise's OWN positive signal: the handler's kernel printf (`osfmk/arm/trap.c`; the tail after
# the invariant prefix varies per run).  Cap-exempt: a plain kernel `printf`, not a live record.
PRINTF_MARK = "usermode undefined instruction"

# The live witness keys, in `__wrap_sleh_undef`'s ONE write order (entry_trace.c:5578-5605).
UNDEF_SEQ_KEY = "xnu_live_undef_seq"
UNDEF_PC_KEY = "xnu_live_undef_pc"

# The live channel's cap, and the marker it publishes once (and only once) on crossing.
CAP_EVENT_KEY = "xnu_live_capped"
CAP_VALUE_KEY = "xnu_live_cap"      # the DECLARED capacity - a different thing from the event

# The loop bound, carried from 1020 (`UNDEF_LOOP_MAX`): a `_seq` at/under this at ONE invariant pc is a
# BOUNDED sequence (the lazy-VFP trip(s) plus the raise); past it, it is the re-trap loop.
UNDEF_LOOP_MAX = 100

# The 1015b storm fingerprint - the same test 1020's reader applies: the good shape is a small
# `xnu_live_abt_seq` with MANY distinct `xnu_live_abt_far`; the defect shape is ONE far retried
# hundreds of thousands of times.
ABT_SEQ_KEY = "xnu_live_abt_seq"
ABT_FAR_KEY = "xnu_live_abt_far"

# Residency (「保持在 xnu 里」) and the window the run was given.
WDT_PETS_KEY = "xnu_live_wdt_pets"
MEMSIZE_KEY = "xnu_entry_args_memSize"

# The NEW signature this rung could introduce (§3.2/§4.4 cell C): soft-float legitimately adds libgcc
# references, and the rung's expected fix is "$LIBGCC resolves them" - so a refusal or an unresolved
# name NAMING one of these is the cell, not a nuisance.
AEABI_RE = re.compile(r"__aeabi_[fdFD]")
LINK_UNDEF_COUNT_KEY = "stage90_xnu_link_undefined_symbol_count"
STUB_HIT_COUNT_KEY = "xnu_entry_stub_hit_count"
REFUSAL_MARK = "REFUSING"

# The pre-mach panic spellings, VERBATIM from the sites §4.4 cell D names (`arm_init.c:237`, `:242`,
# `:248`).  Their presence forbids A and is direct evidence for D.
VFP_PANIC_MARKS = ("Leo VFP access unavailable CPACR=",
                   "Leo VFP architecture unsupported FPSID=",
                   "Leo VFP D32 unavailable MVFR0=")

# The `entry_epilogue` reason byte `e` = "exception: ..." (entry_stubs.c:11726 and its siblings).  Any
# other byte is an epilogue reason that is NOT the exception path - stop evidence for D.
WHY_BYTE_EXCEPTION = 0x65           # 'e'

# The 1020b recovery, used as E's baseline when the operator does not name one.  It is a DEFAULT, not
# an assumption: if it is absent, E is reported UNAVAILABLE rather than guessed.
DEFAULT_BASELINE = os.path.join(os.path.dirname(_HERE), "out", "stage90", "captures",
                                "recovered-unknown-arm-20261016-014316-last_kmsg.txt")

# The order the cells are announced in, for the machine-readable tail.
CELLS = ("A-RETIRED", "B-STILL-THERE", "C-NEW-FAULT", "D-DIFFERENT-FAILURE", "E-NO-CHANGE", "R-CTL")


def say(tag, msg=""):
    print("[1021d] %-10s %s" % (tag, msg))


def verdict(cell, msg):
    print("[1021d] %-16s %s" % (cell, msg))
    return cell


# ---------------------------------------------------------------------------------------------------
# THE RAW-TEXT READER.  `C15.Capture`'s KV regex is `xnu_live_[a-z0-9_]+` by construction, so EVERY
# `xnu_entry_*` and `stage90_*` key is invisible to `cap.toks`.  A check that looked for them there
# would be blind and would ALWAYS withhold - the exact defect `check_1020_capture.py` documents for its
# boot self-test row ([[mi4-silence-is-a-reading-only-if-success-is-silent]]: the row's absence would
# really be the parser's blindness).  So these keys are read off the raw lines.
# ---------------------------------------------------------------------------------------------------
def raw_kv(cap, key):
    """Every occurrence of `key=value` in the raw text, in log order: [(line_no, int_or_None, raw)]."""
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


# ---------------------------------------------------------------------------------------------------
# THE READINGS
# ---------------------------------------------------------------------------------------------------
def undef_rows(cap):
    """One dict per live undef entry, bound to `__wrap_sleh_undef`'s ONE write order.

    The recorder writes `_seq` first, then `_user_seq` (only for a USR entry), then - for entries within
    `UNDEF_LIVE_MAX` - `_pc`/`_lr`/`_spsr`/`_user` in that order.  So a row OPENS at `_seq` and the
    detail keys that follow belong to it.  A `_seq` with no following `_pc` is a real reading: an entry
    PAST `UNDEF_LIVE_MAX`, where only the count advances (identical to `check_1020_capture.py`'s
    reader - the same recorder, the same order)."""
    rows, cur = [], None
    field = {"xnu_live_undef_pc": "pc", "xnu_live_undef_lr": "lr",
             "xnu_live_undef_spsr": "spsr", "xnu_live_undef_user": "user"}
    for line_no, key, _raw, val in cap.toks:
        if key == UNDEF_SEQ_KEY:
            cur = {"line": line_no, "seq": val, "pc": None, "lr": None, "spsr": None, "user": None}
            rows.append(cur)
        elif cur is not None and key in field:
            cur[field[key]] = val
    return rows


def printf_count(cap):
    return sum(line.count(PRINTF_MARK) for line in cap.lines)


def frontier(cap):
    """THE KERNEL-MODE UNDEF, read off the trap record.  The LAST occurrence of each key wins (a second
    trap appends a second record); the COUNT is kept so a multi-trap capture is visible rather than
    silently collapsed."""
    f = {}
    for k in KERNEL_UNDEF_KEYS:
        rows = raw_kv(cap, k)
        f[k] = rows[-1][1] if rows else None
        f[k + "__n"] = len(rows)
    f["record_lines"] = raw_lines_with(cap, TRAP_RECORD_MARK)
    f["record"] = bool(f["record_lines"])
    pe = raw_last(cap, PANIC_ENTERED_KEY)
    f["panic_entered"] = pe[1] if pe else None
    f["panic_entered_line"] = pe[0] if pe else None
    wb = raw_last(cap, WHY_BYTE_KEY)
    f["why_byte"] = wb[1] if wb else None
    wh = raw_last(cap, WHY_KEY)
    f["why"] = wh[1] if wh else None
    return f


def live_after_cap(cap):
    """Was the live channel still DELIVERING after it hit its cap?

    `entry_live_write` publishes `xnu_live_capped` exactly once, on the record that crosses
    `ENTRY_LIVE_CAP`, and drops every `entry_live_write` after it (`entry_stubs.c:4443-4448`).  If ANY
    `xnu_live_*` key appears after that line, those keys did not come through the capped channel - they
    came through the DIRECT one (`entry_write_kv`), which is cap-exempt.  Returns
    `(capped_line_or_None, bool)`: `True` means a live-channel ABSENCE after that line is still a
    reading, `False` means it is the instrument, not the machine."""
    capped = raw_lines_with(cap, CAP_EVENT_KEY)
    if not capped:
        return None, True
    first = capped[0]
    for line_no, key, _raw, _val in cap.toks:
        if line_no > first and key not in (CAP_EVENT_KEY, CAP_VALUE_KEY):
            return first, True
    return first, False


def anchor_reading(cap, rows):
    """THE PROGRESS ANCHOR - did this run reach the 1020b frontier's trigger point?

    The trigger point is pid 1's user `udf`, which `records/1021-fpu-rootcause.txt` §3 puts IMMEDIATELY
    before the `mach_msg_send` NEON.  Two independent channels can witness it, and the reader reports
    which one carried the reading:

      * the kernel's own printf `usermode undefined instruction` - raw console text, cap-exempt, and
        the POSITIVE signal that the 1020 raise path was taken (design 1020 §5);
      * a BOUNDED, invariant, user-mode `xnu_live_undef_*` sequence - the DIRECT witness.

    Returns `(ok, why_string, channel_string)`."""
    n_print = printf_count(cap)
    seqs = [r["seq"] for r in rows if r["seq"] is not None]
    max_seq = max(seqs) if seqs else 0
    pc_vals = sorted({r["pc"] for r in rows if r["pc"] is not None})
    users = [r["user"] for r in rows if r["user"] is not None]
    user_rows = [r for r in rows if r["user"] == 1]
    bounded = (1 <= max_seq <= UNDEF_LOOP_MAX)
    invariant = (len(pc_vals) == 1)
    if n_print >= 1:
        return True, ("the handler's own `%s` printf fired x%d" % (PRINTF_MARK, n_print)), "printf"
    if rows and bounded and invariant and bool(user_rows) and all(u == 1 for u in users):
        return True, ("a BOUNDED user-mode `%s` sequence: x%d row(s), max `_seq`=%d at the invariant "
                      "pc %s" % (UNDEF_SEQ_KEY, len(rows), max_seq, C15.fmt(pc_vals[0]))), "witness"
    return False, ("neither channel carries the anchor: printf x%d, `%s` row(s) x%d, max `_seq`=%d, "
                   "invariant pc=%s, user rows x%d" % (
                       n_print, UNDEF_SEQ_KEY, len(rows), max_seq, invariant, len(user_rows))), "none"


def storm_reading(cap):
    """The 1015b storm fingerprint, by value: (max `_seq`, number of DISTINCT `far`)."""
    seqs = [t[3] for t in cap.all(ABT_SEQ_KEY) if t[3] is not None]
    fars = {t[3] for t in cap.all(ABT_FAR_KEY) if t[3] is not None}
    return (max(seqs) if seqs else None), len(fars)


def max_key(cap, key):
    vals = [t[3] for t in cap.all(key) if t[3] is not None]
    return max(vals) if vals else None


def new_signature(cap):
    """The NEW signature this rung could introduce (§3.2/§4.4 cell C): a compile/link refusal naming an
    `__aeabi_f*`/`__aeabi_d*` symbol, or a non-zero undefined-symbol count.

    Returns `(hits, notes)`: `hits` is the (line, text) of every line that NAMES such a symbol anywhere
    in the capture; `notes` names the link counters as read."""
    hits = [(i, line.strip()[:160]) for i, line in enumerate(cap.lines, 1) if AEABI_RE.search(line)]
    notes = []
    for k in (LINK_UNDEF_COUNT_KEY, STUB_HIT_COUNT_KEY):
        rows = raw_kv(cap, k)
        if rows:
            notes.append("%s=%s" % (k, rows[-1][2]))
    return hits, notes


def frontier_sig(cap):
    """E's gate: the FRONTIER'S OWN identity, plus the keys that say WHICH stop this was.

    **What is deliberately NOT in the gate, and why.**  The 1020 raise's `_seq` (1 vs 2 depends on
    whether VFP happened to be on at trip 1 - 1020's own R-RAISES-CLEAN cell is the `_seq == 1` case)
    and the 1015b storm's `_seq`/distinct-far counts are RUN-VARIABLE.  Gating E on them would make E a
    cell that can essentially never fire on hardware, i.e. a pre-registered cell that is dead
    ([[mi4-measurement-defects]]: the tool's own reading must not be the artifact).  They are compared
    and REPORTED as soft divergences instead."""
    f = frontier(cap)
    return {
        "undef_pc": f["xnu_entry_undef_pc"],
        "undef_lr": f["xnu_entry_undef_lr"],
        "undef_spsr": f["xnu_entry_undef_spsr"],
        "undef_user": f["xnu_entry_undef_user"],
        "trap_record": f["record"],
        "panic_entered": f["panic_entered"],
        "why_byte": f["why_byte"],
    }


def compare_to_baseline(cap, base):
    """(equal, [(gated_key, ours, theirs)], [(soft_key, ours, theirs)]).  The first list is the E gate;
    the second is the run-variable comparison, reported and never concluded from."""
    gate_k = ("undef_pc", "undef_lr", "undef_spsr", "undef_user", "trap_record", "panic_entered",
              "why_byte")
    a, b = frontier_sig(cap), frontier_sig(base)
    diffs = [(k, a[k], b[k]) for k in gate_k if a[k] != b[k]]
    soft = []
    am, bm = anchor_reading(cap, undef_rows(cap)), anchor_reading(base, undef_rows(base))
    if am[0] != bm[0]:
        soft.append(("anchor", am[0], bm[0]))
    ap, bp = printf_count(cap), printf_count(base)
    if ap != bp:
        soft.append(("printf x", ap, bp))
    aq, bq = storm_reading(cap), storm_reading(base)
    for label, x, y in (("abt_seq max", aq[0], bq[0]), ("abt_far distinct", aq[1], bq[1]),
                        ("wdt_pets max", max_key(cap, WDT_PETS_KEY), max_key(base, WDT_PETS_KEY))):
        if x != y:
            soft.append((label, x, y))
    wm, bmm = raw_last(cap, MEMSIZE_KEY), raw_last(base, MEMSIZE_KEY)
    if (wm[1] if wm else None) != (bmm[1] if bmm else None):
        soft.append(("args_memSize", wm[1] if wm else None, bmm[1] if bmm else None))
    return (not diffs), diffs, soft


# ---------------------------------------------------------------------------------------------------
# THE ADJUDICATION - one cell, in the design's §4.4 order as the OBSERVATIONS force it.
#
# The reader's order, and why:
#   1. the link half of C - a build refusal is not a device observation and needs no control;
#   2. R-CTL: the boot self-test half of the control, and the recorder's own arithmetic;
#   3. R-CTL: the trap record's own channel (the "buffer did not reach DRAM" case);
#   4. B / E  - the same undef at the NAMED pc AND lr (E first: it is the STRICTER reading);
#   5. C      - a DIFFERENT undef pc;
#   6. A      - silence, but only with the anchor held AND the witness half of the control present;
#   7. D      - a stop BEFORE the anchor;
#   8. R-CTL  - nothing at all.
# ---------------------------------------------------------------------------------------------------
def adjudicate(cap, baseline=None, baseline_path=None):
    f = frontier(cap)
    rows = undef_rows(cap)
    capped_line, live_ok = live_after_cap(cap)
    boot = raw_last(cap, BOOT_UNDEF_KEY)
    anchor_ok, anchor_why, anchor_chan = anchor_reading(cap, rows)
    abt_seq, abt_fars = storm_reading(cap)
    wdt = max_key(cap, WDT_PETS_KEY)
    ms = raw_last(cap, MEMSIZE_KEY)
    hits, link_notes = new_signature(cap)
    n_print = printf_count(cap)
    seqs = [r["seq"] for r in rows if r["seq"] is not None]
    max_seq = max(seqs) if seqs else 0
    pc_vals = sorted({r["pc"] for r in rows if r["pc"] is not None})
    upc, ulr = f["xnu_entry_undef_pc"], f["xnu_entry_undef_lr"]

    say("CAPTURE", "%d bytes, %d lines, %d live tokens" % (cap.nbytes, len(cap.lines), len(cap.toks)))

    # ---- THE KERNEL FRONTIER -----------------------------------------------------------------
    if f["record"]:
        say("FRONTIER", "a `%s` block IS present (first at :%d)" % (TRAP_RECORD_MARK,
                                                                   f["record_lines"][0]))
    else:
        say("FRONTIER", "NO `%s` block - the run published no trap record.  `%s`=%s"
            % (TRAP_RECORD_MARK, PANIC_ENTERED_KEY,
               "absent" if f["panic_entered"] is None else C15.fmt(f["panic_entered"])))
    say("FRONTIER", "kernel undef: pc=%s lr=%s spsr=%s user=%s (x%d occurrence(s)); `%s`=%s%s"
        % (C15.fmt(upc), C15.fmt(ulr), C15.fmt(f["xnu_entry_undef_spsr"]),
           C15.fmt(f["xnu_entry_undef_user"]), f["xnu_entry_undef_pc__n"], WHY_BYTE_KEY,
           C15.fmt(f["why_byte"]),
           (" (%r)" % chr(f["why_byte"]))
           if (f["why_byte"] is not None and 0x20 <= f["why_byte"] < 0x7f) else ""))

    # ---- THE 1020 RAISE WITNESS (the anchor's second channel) and the storm fingerprint ---------
    say("LIVE", "`%s` rows x%d; max `_seq`=%d; `_pc` %s; printf x%d"
        % (UNDEF_SEQ_KEY, len(rows), max_seq,
           (C15.fmt(pc_vals[0]) + " (INVARIANT)") if len(pc_vals) == 1 else
           ("{%s}" % ", ".join(C15.fmt(p) for p in pc_vals) if pc_vals else "(none)"), n_print))
    cval = raw_last(cap, CAP_VALUE_KEY)
    if capped_line is None:
        say("LIVE", "cap: declared `%s`=%s; the capped EVENT is ABSENT - the live channel was never "
                    "exhausted" % (CAP_VALUE_KEY, cval[2] if cval else "absent"))
    elif live_ok:
        say("LIVE", "cap: declared `%s`=%s; the capped EVENT is PRESENT at :%d - and DIRECT keys appear "
                    "AFTER it, so the channel was still DELIVERING and a live-channel absence after "
                    "that line is readable" % (CAP_VALUE_KEY, cval[2] if cval else "absent",
                                               capped_line))
    else:
        say("LIVE", "cap: declared `%s`=%s; the capped EVENT is PRESENT at :%d and NOTHING live appears "
                    "after it - a live-channel ABSENCE after that line is the INSTRUMENT, not the "
                    "machine (1018's defect)" % (CAP_VALUE_KEY, cval[2] if cval else "absent",
                                                 capped_line))
    say("LIVE", "1015b storm fingerprint: max `%s`=%s, DISTINCT `%s` x%d%s"
        % (ABT_SEQ_KEY, C15.fmt(abt_seq), ABT_FAR_KEY, abt_fars,
           " - the GOOD shape (a small seq with many distinct fars)" if (abt_seq is not None and
                                                                         abt_fars > 1) else
           " - read the numbers: the defect shape is ONE far retried hundreds of thousands of times"))
    say("LIVE", "residency `%s` max=%s; `%s`=%s" % (WDT_PETS_KEY, C15.fmt(wdt), MEMSIZE_KEY,
                                                    ms[2] if ms else "absent"))
    say("LIVE", "NEW-SIGNATURE scan (`%s`): x%d line(s) naming an `__aeabi_f/d` symbol; link counters: %s"
        % (AEABI_RE.pattern, len(hits), ", ".join(link_notes) if link_notes else "(none published)"))
    for ln, txt in hits[:4]:
        say("LIVE", "  :%d %s" % (ln, txt))

    # ---- THE ANCHOR -----------------------------------------------------------------------------
    say("ANCHOR", "%s - %s (channel: %s)" % ("HELD" if anchor_ok else "ABSENT", anchor_why,
                                             anchor_chan))

    # =============================================================================================
    # 1. C-NEW-FAULT, THE LINK HALF - first, because a build/refusal capture is not a device
    #    observation and needs no control.  Design §3.2/§4.4: soft-float legitimately adds libgcc
    #    references; an unresolved one is EXPECTED-BUT-UNHANDLED - name it and add `$LIBGCC`.
    #    A bare mention of an `__aeabi_*` symbol is NOT the cell: the refusal (or a non-zero
    #    undefined-symbol count with no trap record) is what makes it one.
    # =============================================================================================
    refused = [i for i, line in enumerate(cap.lines, 1) if REFUSAL_MARK in line]
    undef_count = raw_last(cap, LINK_UNDEF_COUNT_KEY)
    if hits and refused:
        return verdict("C-NEW-FAULT", "SOFT-FLOAT ADDED A REFERENCE THE LINK DOES NOT RESOLVE.  The "
                                      "capture names an `__aeabi_f*`/`__aeabi_d*` symbol (x%d line(s), "
                                      "first at :%d) AND the build REFUSED (x%d `%s` line(s), first at "
                                      ":%d).  This is the design's §3.2 expectation - a pool source "
                                      "really does FP after all, or a soft-float TU calls a libgcc "
                                      "helper - and the fix is to let `$LIBGCC` "
                                      "(`build_entry.sh:2483`) resolve it exactly as "
                                      "`__aeabi_uldivmod` is resolved today, NOT to revert the flag.  "
                                      "§4.5's second falsifier, named." % (
                                          len(hits), hits[0][0], len(refused), REFUSAL_MARK,
                                          refused[0]))
    if undef_count is not None and undef_count[1] != 0 and not f["record"]:
        return verdict("C-NEW-FAULT", "the entry build recorded `%s=%s` - a NON-ZERO pass-1 "
                                      "undefined-symbol count, with no trap record to say otherwise.  "
                                      "The link did not resolve every name; name them (design §3.3) "
                                      "before pressing anything."
                      % (LINK_UNDEF_COUNT_KEY, undef_count[2]))

    # =============================================================================================
    # 2. R-CTL, THE CONTROL'S FIRST HALF, and the recorder's own arithmetic.
    # =============================================================================================
    if boot is None:
        say("CTL", "no `%s` row -- the vector's boot self-test is ABSENT" % BOOT_UNDEF_KEY)
    else:
        say("CTL", "`%s` = %s (:%d)" % (BOOT_UNDEF_KEY, boot[2], boot[0]))
    say("CTL", "the undef witness published x%d `%s` row(s)" % (len(rows), UNDEF_SEQ_KEY))
    if boot is None or boot[1] != 1:
        why = ("the boot self-test row `%s` is ABSENT" % BOOT_UNDEF_KEY if boot is None else
               "the boot self-test row reads %s (want 0x1)" % boot[2])
        return verdict("R-CTL", "WITHHOLD (the mandatory-first cell): %s.  Without it the "
                                "undefined-instruction vector is not proven ARMED on this build, so "
                                "the ABSENCE of a kernel-mode undef is not a reading and MUST NOT be "
                                "adjudicated as A-RETIRED." % why)
    if upc is not None and ulr is not None and ulr != upc + 4:
        return verdict("R-CTL", "WITHHOLD - THE TRAP RECORD IS INTERNALLY INCONSISTENT: "
                                "`xnu_entry_undef_pc=%s` but `xnu_entry_undef_lr=%s`, and the recorder "
                                "writes the pc AS `lr - 4` (`entry_stubs.c:11322`), so these two "
                                "cannot disagree in a capture whose producer ran.  A status is a "
                                "verdict only if its producer delivered one; nothing downstream of a "
                                "broken record is a reading."
                      % (C15.fmt(upc), C15.fmt(ulr)))

    # =============================================================================================
    # 3. THE TRAP RECORD'S OWN CHANNEL.  `entry_stubs.c:6382` states the case in so many words: "a
    #    report with no `trap record:` line and a non-zero `xnu_entry_panic_entered` is a trap whose
    #    buffer did not reach DRAM".  A-RETIRED rests on the ABSENCE of `xnu_entry_undef_pc`, so this
    #    must withhold BEFORE A.
    # =============================================================================================
    if (not f["record"]) and f["panic_entered"] not in (0, None):
        return verdict("R-CTL", "WITHHOLD - THE TRAP BUFFER DID NOT REACH DRAM.  `%s`=%s (:%s) says the "
                                "undefined-instruction handler RAN, but there is no `%s:` heading in "
                                "the report - the case `entry_stubs.c:6382` names.  So the ABSENCE of "
                                "an `xnu_entry_undef_pc` is the record's silence, not the kernel's, and "
                                "MUST NOT be read as A-RETIRED." % (
                                    PANIC_ENTERED_KEY, C15.fmt(f["panic_entered"]),
                                    f["panic_entered_line"], TRAP_RECORD_MARK))
    if (not anchor_ok) and capped_line is not None and not live_ok:
        return verdict("R-CTL", "WITHHOLD - the live channel was CAPPED at :%d and NOTHING live appears "
                                "after it, so the anchor's absence (%s) is the CHANNEL, not the "
                                "machine (1018's defect, `records/1018-recon.txt`).  A-RETIRED is "
                                "concluded from silence and this silence is not readable."
                      % (capped_line, anchor_why))
    if f["panic_entered"] is None and not f["record"] and not anchor_ok:
        return verdict("R-CTL", "WITHHOLD - `%s` is ABSENT from the report and there is no trap record "
                                "and no anchor, so the report did not carry the run's end at all.  "
                                "Nothing here is a reading." % PANIC_ENTERED_KEY)

    # =============================================================================================
    # 4. B-STILL-THERE / E-NO-CHANGE - the SAME undef, at the design's own pc AND lr.
    #    E is the STRICTER reading and is evaluated first: the same frontier AND the same record.
    # =============================================================================================
    if upc is not None and upc == REF_UNDEF_PC and ulr == REF_UNDEF_LR:
        spi = f["xnu_entry_undef_spsr"] if f["xnu_entry_undef_spsr"] is not None else 0
        if f["xnu_entry_undef_user"] != 0:
            say("SOFT", "`xnu_entry_undef_user=%s` is NOT 0, so this undef was NOT taken in kernel mode "
                        "- the design's cell B is a KERNEL-mode undef.  The cell is still B (the pc and "
                        "lr are the named frontier) but the mode reading is reported."
                % C15.fmt(f["xnu_entry_undef_user"]))
        if (spi & 0x1F) != KERNEL_MODE_SVC:
            say("SOFT", "`xnu_entry_undef_spsr=%s` has mode 0x%02x, not SVC (0x13)"
                % (C15.fmt(f["xnu_entry_undef_spsr"]), spi & 0x1F))
        base = baseline
        if base is not None:
            cp = os.path.realpath(getattr(cap, "path", "") or "")
            bp = os.path.realpath(baseline_path or "")
            if cp and cp == bp:
                say("BASE", "REFUSING the baseline: it IS this capture (a comparison a file makes with "
                            "itself cannot fail - [[mi4-a-claim-in-a-comment-is-not-a-check]])")
                base = None
        if base is None:
            say("BASE", "no baseline capture available, so E-NO-CHANGE ('byte-for-byte the 1020b "
                        "frontier') cannot be decided - it is a COMPARISON, and §5.2 forbids asserting "
                        "a remembered number.  Returning the weaker, always-true-here B.")
        else:
            equal, diffs, soft = compare_to_baseline(cap, base)
            say("BASE", "frontier record vs %s: %s"
                % (os.path.basename(baseline_path or "(given)"),
                   "IDENTICAL on every gated key" if equal else
                   "DIFFERS on %s" % ", ".join("%s %s->%s" % (k, C15.fmt(b), C15.fmt(a))
                                               for k, a, b in diffs)))
            for k, a, b in soft:
                say("BASE", "  (run-variable, reported not gated) %s: %s vs baseline %s" % (k, a, b))
            if equal:
                return verdict("E-NO-CHANGE", "NO CHANGE AT ALL, INCLUDING THE UNDEF PC.  The run "
                                              "stopped on the SAME kernel-mode undef at pc=%s / lr=%s, "
                                              "and the whole gated frontier record (spsr, the user "
                                              "bit, trap-record presence, panic-entered, why byte) is "
                                              "IDENTICAL to the baseline.  Per §4.4 this is the cell "
                                              "that says the switch did NOT reach the compile "
                                              "(OFF-spelling, or the wrong builder), and per §4.4 the "
                                              "BY-VALUE CLAUSE (§5) should have caught it - if it "
                                              "passed, the clause is wrong.  Read §5.1's named object "
                                              "(`osfmk_ipc_mach_msg.o`) by hand, then re-check §3.5's "
                                              "eight builders." % (C15.fmt(upc), C15.fmt(ulr)))
        return verdict("B-STILL-THERE", "NOT FIXED - the SAME kernel-mode undef is still here.  "
                                        "`xnu_entry_undef_pc=%s` and `xnu_entry_undef_lr=%s` are the "
                                        "design's §4.4 cell B, to the byte: the well-formed NEON "
                                        "`vld1.32 {d16-d17},[r0]!` inside `mach_msg_send` "
                                        "(`records/1021-fpu-rootcause.txt` §1) is STILL in the linked "
                                        "image and STILL executing with the FPU ungranted.  Per §4.4 "
                                        "this means the pool was NOT rebuilt, OR the object at that "
                                        "address came from ANOTHER BUILDER (§3.5's eight scripts) - "
                                        "READ THE BY-VALUE CELL (§5) FIRST; do not re-press."
                      % (C15.fmt(upc), C15.fmt(ulr)))

    # =============================================================================================
    # 5. C-NEW-FAULT - a DIFFERENT kernel-mode undef pc.  Expected if soft-float moved the first
    #    FP-touching instruction elsewhere (a scalar-VFP spill: §3.1's 451 sites), and the NAME is the
    #    whole point of the cell.
    # =============================================================================================
    if upc is not None:
        return verdict("C-NEW-FAULT", "A DIFFERENT UNDEFINED INSTRUCTION.  `xnu_entry_undef_pc=%s` / "
                                      "`lr=%s` (spsr=%s, user=%s) is NOT the 1020b frontier %s.  The "
                                      "undef at `mach_msg_send` is gone, so the flag DID remove that "
                                      "site - and something ELSE is undefined.  §3.1 predicts exactly "
                                      "this shape if the pool still carries FP: soft-float removes the "
                                      "FP register file from the ALLOCATOR (`-msoft-float`), but a site "
                                      "that legitimately needs the coprocessor - or an object from "
                                      "another builder (§3.5) - would fault first somewhere else.  "
                                      "Read the pc by value: `nm`-bracket it in "
                                      "`out/stage90/xnu_arm_entry.elf` and disassemble it, exactly as "
                                      "1020b did for %s." % (
                                          C15.fmt(upc), C15.fmt(ulr),
                                          C15.fmt(f["xnu_entry_undef_spsr"]),
                                          C15.fmt(f["xnu_entry_undef_user"]),
                                          C15.fmt(REF_UNDEF_PC), C15.fmt(REF_UNDEF_PC)))

    # =============================================================================================
    # 6. A-RETIRED - the undef is absent, the anchor holds, and the control's SECOND half (the witness
    #    rows) is present.  Every case where that absence is unreadable has been refused above.
    # =============================================================================================
    if not anchor_ok:
        why_byte_stop = (f["why_byte"] is not None and f["why_byte"] != WHY_BYTE_EXCEPTION)
        stop_evidence = []
        if f["record"]:
            stop_evidence.append("a `%s` block at :%d carrying no undefined-instruction pc"
                                 % (TRAP_RECORD_MARK, f["record_lines"][0]))
        if f["panic_entered"] not in (0, None):
            stop_evidence.append("`%s=%s`" % (PANIC_ENTERED_KEY, C15.fmt(f["panic_entered"])))
        vfp_panic = [(i, m) for i, line in enumerate(cap.lines, 1)
                     for m in VFP_PANIC_MARKS if m in line]
        if vfp_panic:
            stop_evidence.append("a VFP self-test panic at :%d (%r)" % (vfp_panic[0][0],
                                                                        vfp_panic[0][1]))
        if why_byte_stop:
            stop_evidence.append("`%s=0x%02x` (%r) - an epilogue reason that is NOT the exception path"
                                 % (WHY_BYTE_KEY, f["why_byte"], chr(f["why_byte"])))
        if stop_evidence:
            return verdict("D-DIFFERENT-FAILURE", "THE RUN STOPPED BEFORE THE FRONTIER.  There is stop "
                                                  "evidence (%s) and the anchor is ABSENT (%s), so "
                                                  "this run never reached pid 1's `udf` - the stop is "
                                                  "EARLIER than the `mach_msg_send` path, which "
                                                  "§4.3/§4.4 makes cell D.  Design §3.2 names what "
                                                  "legitimately needs the FPU and survives "
                                                  "(`proc_reg.h`'s `mrc p10,7,…`, `arm_init`'s VFP "
                                                  "self-test, `init_vfp`), and §3.4 notes the ENTRY "
                                                  "image's own objects already default to `__SOFTFP__` "
                                                  "- so read what THIS stop names before touching the "
                                                  "flag." % ("; ".join(stop_evidence), anchor_why))
        return verdict("R-CTL", "WITHHOLD - NO STOP EVIDENCE AND NO ANCHOR.  There is no `%s` block, "
                                "no panic, no VFP-panic string, and no non-exception `%s`, and the run "
                                "never shows pid 1's `udf` (%s): nothing here says the kernel reached "
                                "the frontier, so A-RETIRED MUST NOT be read from it."
                      % (TRAP_RECORD_MARK, WHY_BYTE_KEY, anchor_why))

    if not rows:
        return verdict("R-CTL", "WITHHOLD - THE CONTROL'S SECOND HALF IS ABSENT while the anchor holds "
                                "on the printf channel.  The kernel reported pid 1's `udf` yet the "
                                "DIRECT witness published NO `%s` row and the live channel was not "
                                "capped, which is an instrument shape, not a machine one (1018's "
                                "lesson read the other way).  Rebuild and confirm the witness is armed "
                                "before A-RETIRED is claimed from this run." % UNDEF_SEQ_KEY)

    named = []
    if f["record"]:
        named.append("a trap record IS present at :%d carrying no undefined-instruction pc (the "
                     "frontier is no longer an undef)" % f["record_lines"][0])
    if f["why_byte"] is not None:
        named.append("`%s=0x%02x`%s" % (WHY_BYTE_KEY, f["why_byte"],
                                        (" (%r)" % chr(f["why_byte"]))
                                        if 0x20 <= f["why_byte"] < 0x7f else ""))
    return verdict("A-RETIRED", "RETIRED - THE FIX STANDS.  There is NO kernel-mode undefined "
                                "instruction at %s anywhere in the capture, and the run demonstrably "
                                "reached the 1020b frontier's own trigger point: the ANCHOR holds on "
                                "channel `%s` (%s).  `records/1021-fpu-rootcause.txt` §3 puts the "
                                "`mach_msg_send` NEON IMMEDIATELY after that point, so the run moved "
                                "past it.  %s  Residency for the record: `%s` max=%s, `%s`=%s, storm "
                                "`%s` max=%s / distinct `%s` x%d.  **The build's own by-value cell "
                                "(design §5) still owes the pool's FP-instruction count** - a device "
                                "reading is not that measurement." % (
                                    C15.fmt(REF_UNDEF_PC), anchor_chan, anchor_why,
                                    ("Frontier named by this capture: " + "; ".join(named) + ".")
                                    if named else
                                    "The capture names NO further frontier: the run ends without a "
                                    "trap record, which is the 'ends at `entry_epilogue`' branch of "
                                    "cell A.",
                                    WDT_PETS_KEY, C15.fmt(wdt), MEMSIZE_KEY,
                                    ms[2] if ms else "absent", ABT_SEQ_KEY, C15.fmt(abt_seq),
                                    ABT_FAR_KEY, abt_fars))


# ---------------------------------------------------------------------------------------------------
# THE SELF-TEST.  Every fixture is SYNTHETIC, generated in a fresh /tmp directory, and never derived
# from a real capture by hand-editing (a fixture cut from the real thing inherits whatever the real
# thing got wrong - and the whole point is that the reader can FAIL).
# ---------------------------------------------------------------------------------------------------
def _mk(tmpdir, name, lines):
    path = os.path.join(tmpdir, name)
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def self_test():
    import tempfile
    ok = True
    tmpdir = tempfile.mkdtemp(prefix="check1021d-selftest-")
    cases = []

    def cell_of(lines, base_lines, tag):
        p = _mk(tmpdir, tag + ".txt", lines)
        cap = C15.Capture(p)
        cap.path = p
        base = None
        if base_lines is not None:
            bp = _mk(tmpdir, tag + "-base.txt", base_lines)
            base = C15.Capture(bp)
            base.path = bp
        with contextlib.redirect_stdout(io.StringIO()):
            return adjudicate(cap, base, bp if base is not None else None)

    def want(name, lines, expected, base_lines=None):
        nonlocal ok
        got = cell_of(lines, base_lines, name)
        cases.append((name, got, expected))
        if got != expected:
            say("SELFTEST-FAIL", "%s: expected %s, got %s" % (name, expected, got))
            ok = False

    # ---- THE FIXTURE VOCABULARY.  Every value a real capture carries, written the way the real
    #      recorders write it.
    BOOT = ("MI4IOS6_STAGE90_XNU stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled="
            "0x00000001")
    BOOT_BAD = ("MI4IOS6_STAGE90_XNU stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled="
                "0x00000000")
    PRINTF = (" launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x39605a54 in map "
              "0xc00057b8 (pmap 0xc0ba3cc0)")
    # The live undef witness: `_seq` opens the row, the detail keys follow in the recorder's order.
    RAISE = [" xnu_live_undef_seq=0x00000001", " xnu_live_undef_user_seq=0x00000001",
             " xnu_live_undef_pc=0x39605a54", " xnu_live_undef_lr=0x00000007",
             " xnu_live_undef_spsr=0x20000030", " xnu_live_undef_user=0x00000001",
             " xnu_live_undef_seq=0x00000002", " xnu_live_undef_user_seq=0x00000002",
             " xnu_live_undef_pc=0x39605a54", " xnu_live_undef_lr=0x00000007",
             " xnu_live_undef_spsr=0x20000030", " xnu_live_undef_user=0x00000001"]
    STORM = [" xnu_live_abt_seq=0x00000200", " xnu_live_abt_seq=0x00000100",
             " xnu_live_abt_far=0x000e6000", " xnu_live_abt_far=0x2bdf0f7c",
             " xnu_live_abt_far=0x3d577000", " xnu_live_wdt_pets=0x00000061",
             " xnu_live_wdt_pets=0x00000030", " xnu_live_cap=0x00006000"]
    MEM = "MI4IOS6_STAGE90_XNU xnu_entry_args_memSize=0x1e400000"

    def pe(v):
        return " xnu_entry_panic_entered=0x%08x" % v

    def trap(pc=REF_UNDEF_PC, lr=REF_UNDEF_LR, spsr=0x40000093, user=0x0):
        """The 1020b trap record, verbatim in shape (entry_stubs.c:11321-11376)."""
        return ["MI4IOS6_STAGE90_XNU trap record:",
                " xnu_entry_undef_lr=0x%08x" % lr,
                " xnu_entry_undef_pc=0x%08x" % pc,
                " xnu_entry_undef_spsr=0x%08x" % spsr,
                " xnu_entry_undef_user=0x%08x" % user,
                " xnu_entry_panic_str=0xe3060cc4",
                " xnu_entry_panic_caller=0xe3060cac",
                " xnu_entry_panic_message=0xe3060c98"]

    WHY_EXC = [" xnu_entry_why=0x804a851c", " xnu_entry_why_byte=0x00000065"]     # 'e'
    WHY_PANIC = [" xnu_entry_why=0x804a85ec", " xnu_entry_why_byte=0x00000070"]   # 'p'

    # ================= A-RETIRED =================
    # No trap record, panic_entered = 0, the anchor holds on the printf channel.
    a1 = [BOOT, MEM] + RAISE + [pe(0)] + [PRINTF] + STORM
    want("A-retired", a1, "A-RETIRED")
    # The anchor's SECOND channel alone (no printf): the bounded user-mode witness.
    want("A-retired-witness", [BOOT, MEM] + RAISE + [pe(0)], "A-RETIRED")
    # The frontier NAMED downstream: a trap record that is not an undef.
    want("A-retired-named",
         [BOOT, MEM] + RAISE + [PRINTF, "MI4IOS6_STAGE90_XNU trap record:",
                                " xnu_entry_panic_str=0x80601111",
                                " xnu_entry_panic_caller=0x8047b0b0", pe(1)] + WHY_PANIC,
         "A-RETIRED")

    # ================= B-STILL-THERE / E-NO-CHANGE =================
    b1 = [BOOT, MEM] + RAISE + [PRINTF] + trap() + [pe(1)] + WHY_EXC + STORM
    want("B-still-there", b1, "B-STILL-THERE")
    # E: the SAME shape against a DISTINCT baseline file - identical on every gated key.
    want("E-no-change", b1, "E-NO-CHANGE", base_lines=b1)
    # ... and E must NOT fire when the frontier record differs (spsr is a gated key).
    want("B-not-E", [BOOT, MEM] + RAISE + [PRINTF] + trap(spsr=0x60000113) + [pe(1)] + WHY_EXC,
         "B-STILL-THERE", base_lines=b1)
    # ... nor when the undef's MODE differs (the user bit is gated).
    want("B-not-E-user", [BOOT, MEM] + RAISE + [PRINTF] + trap(user=0x1) + [pe(1)] + WHY_EXC,
         "B-STILL-THERE", base_lines=b1)

    # ================= C-NEW-FAULT =================
    # (a) a DIFFERENT kernel undef pc.
    want("C-diff-pc", [BOOT, MEM] + RAISE + [PRINTF] + trap(pc=0x8047B0B4, lr=0x8047B0B8) + [pe(1)],
         "C-NEW-FAULT")
    # THE BOUNDARY IS EXACT: the next instruction is NOT the named frontier.
    want("C-boundary", [BOOT, MEM] + RAISE + trap(pc=REF_UNDEF_PC + 4, lr=REF_UNDEF_PC + 8) + [pe(1)],
         "C-NEW-FAULT")
    # (b) the link half: a build REFUSAL naming an `__aeabi_f` symbol.
    want("C-link-refusal",
         ["REFUSING: undefined symbol __aeabi_fadd", "stage90_xnu_link_undefined_symbol_count="
          "0x00000001"],
         "C-NEW-FAULT")
    # (c) a bare non-zero undefined-symbol count with no trap record.
    want("C-link-count", ["stage90_xnu_link_undefined_symbol_count=0x00000003"], "C-NEW-FAULT")
    # ... and a capture that merely MENTIONS one while the run is the unchanged frontier is NOT C.
    want("C-not-fired-on-mention",
         [BOOT, MEM] + RAISE + trap() + [pe(1), " xnu_live_note=0x00000001",
                                         " note: __aeabi_uldivmod is resolved by $LIBGCC"],
         "B-STILL-THERE")

    # ================= D-DIFFERENT-FAILURE =================
    # A VFP self-test panic string, before any thread ran.  (panic_entered = 0: `fleh_undef` never ran,
    # which is consistent with no trap record.)
    want("D-vfp-panic",
         [BOOT, "MI4IOS6_STAGE90_XNU panic: Leo VFP access unavailable CPACR=00000000", pe(0)],
         "D-DIFFERENT-FAILURE")
    # The same by ORDER: an epilogue reason that is NOT the exception path ('p' = XNU rejected).
    want("D-why-panic", [BOOT, pe(0)] + WHY_PANIC, "D-DIFFERENT-FAILURE")
    # ... and 's' ("_start ran to completion and branched to arm_init") is the same cell.
    want("D-why-start", [BOOT, pe(0), " xnu_entry_why=0x804a8580",
                         " xnu_entry_why_byte=0x00000073"], "D-DIFFERENT-FAILURE")

    # ================= R-CTL =================
    # (1) the boot self-test row is ABSENT, while the anchor holds - MUST NOT be A.
    want("R-CTL-no-boot", [MEM] + RAISE + [pe(0), PRINTF], "R-CTL")
    # (2) the boot self-test row is present but reads 0.
    want("R-CTL-boot-bad", [BOOT_BAD, MEM] + RAISE + [pe(0), PRINTF], "R-CTL")
    # (3) the handler RAN (panic_entered != 0) but NO trap record reached DRAM - the buffer-loss case
    #     (entry_stubs.c:6382).  MUST NOT be read as A.
    want("R-CTL-buffer-lost", [BOOT, MEM] + RAISE + [pe(1), PRINTF] + STORM, "R-CTL")
    # (4) the recorder's own arithmetic: pc != lr - 4 is a capture no working producer can emit.
    want("R-CTL-lr-pc", [BOOT, MEM] + RAISE + trap(pc=REF_UNDEF_PC, lr=REF_UNDEF_PC + 8) + [pe(1)],
         "R-CTL")
    # (5) the live channel CAPPED with nothing after it, and no anchor: the silence is the channel.
    want("R-CTL-capped",
         [BOOT, MEM, " xnu_live_capped=0x00006000", " xnu_live_abt_seq=0x00000010", pe(0)],
         "R-CTL")
    # (6) NOTHING AT ALL: no record, no panic, no why, no anchor.
    want("R-CTL-nothing", [BOOT, MEM, pe(0)], "R-CTL")
    # (7) the anchor holds on the printf channel but the DIRECT witness published NOTHING and the live
    #     channel was NOT capped - an instrument shape.  MUST NOT be A.
    want("R-CTL-no-witness", [BOOT, MEM, PRINTF, pe(0)], "R-CTL")

    print("\n  == SELF-TEST ==")
    bad = [(n, g, e) for n, g, e in cases if g != e]
    print("  %d case(s), %d failed" % (len(cases), len(bad)))
    for n, g, e in bad:
        print("    FAIL: %s (got %s, want %s)" % (n, g, e))
    # Every cell must be REACHED by at least one fixture - a pre-registered cell nothing can reach is a
    # dead cell ([[mi4-measurement-contracts]]: the table must be able to fire).
    got_cells = {g for _n, g, _e in cases}
    missing = [c for c in CELLS if c not in got_cells]
    if missing:
        print("    FAIL: no fixture reached %s" % ", ".join(missing))
        bad.append(("coverage", None, missing))
    print("  SELF-TEST: %s" % ("PASS" if not bad else "FAIL"))
    return 0 if not bad else 1


# ---------------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="1021d -msoft-float / kernel-NEON-undef capture reader")
    ap.add_argument("capture", nargs="?")
    ap.add_argument("--baseline", help="a capture to compare against for E-NO-CHANGE (default: the "
                                       "1020b recovery, when it is on disk)")
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
        say("BASE", "no --baseline given; using the 1020b recovery on disk (%s)" % cand)
    if cand is not None:
        if not os.path.isfile(cand):
            say("BASE", "the named baseline does not exist (%s) - E-NO-CHANGE will be UNAVAILABLE"
                % cand)
        else:
            try:
                base = C15.Capture(cand)
                base.path = cand
                bpath = cand
            except (OSError, IOError) as exc:
                say("BASE", "cannot read the baseline (%s): %s" % (cand, exc))
    cell = adjudicate(cap, base, bpath)
    print("\n[1021d] VERDICT: %s" % cell)
    return 0


if __name__ == "__main__":
    sys.exit(main())