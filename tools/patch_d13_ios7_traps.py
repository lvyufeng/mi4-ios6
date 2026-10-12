#!/usr/bin/env python3
"""Stage 993+'s iOS 7 `svc #0x80` r12 = -3 (`mach_absolute_time` fallback) wiring into the D13 tree.

THE FAULT THIS ANSWERS, BY VALUE.  iOS 7 userspace's `mach_absolute_time` reads the commpage gate
`_COMM_PAGE_USER_TIMEBASE` at `_COMM_PAGE + 0x90`; when that word is 0 it falls back to
`mvn ip, #2` (r12 = -3) then `svc #0x80` (Apple's own `_mach_absolute_time` stub in libsystem).  On
D13 that gate IS 0: `osfmk/arm/commpage/commpage.c` never writes `_COMM_PAGE_USER_TIMEBASE` (only the
992 alias's own commpage writes are gated, and neither writes +0x90), so every userspace
`mach_absolute_time` call takes the -3 fallback.

What the kernel then does with -3 is decided by THREE `BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`
gates in `osfmk/arm/traps_lo.s`, all CLOSED on CANCRO:

  :116  `#if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)`  -- OPEN on CANCRO
        `cmn r12, #3` / `beq swi_trap_tb` at the top of `EnterARM(fleh_swi)`.  On CANCRO this
        compiles IN and takes the fast path: `swi_trap_tb` (`:260`) is `movs pc, lr`, which returns
        to userspace WITHOUT writing r0:r1 - so a -3 syscall returns the caller's own leftover
        registers as "time".  The DISASSEMBLY of the linked image proves it: at `locore_fleh_swi`
        (0x8001af90) the first two instructions are `e37c0003 cmn ip, #3` / `0a000033 beq
        <swi_trap_tb>`.

  :162  `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`     -- CLOSED on CANCRO
        `cmn r11, #3` / `beq ios7lab_timebase` on the SAVED-STATE path (r11 = the saved r12, set by
        `mov r11, r12` at `:135`).  This is the arm that ROUTES -3 to the real clock.

  :264  `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`     -- CLOSED on CANCRO
        `ios7lab_timebase: blx _mach_absolute_time` / `b swi_exit64`.  `swi_exit64` (`:195`) is
        `str r1, [r8, #4]` - it writes the 64-bit result back into the saved state, and `swi_exit`
        (`:197`) stores r0 into `[r8]`, so BOTH halves of the clock reach userspace.  D13's
        `mach_absolute_time()` is already ns-scaled (`osfmk/arm/rtclock.c:188-193` -> `:82-93`, raw
        CNTVCT via `pe_msm8974.c:423`), so the value is the real 64-bit monotonic ns clock.

THE FIX (three whole-line edits, no instruction touched):

  * :116 - append ` || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)` INSIDE the
    negation, so `-DSTAGE90_XNU_IOS7_TRAPS=1` closes it and the saved-state path is taken.
  * :162, :264 - append the same clause to the two positive gates, so `-DSTAGE90_XNU_IOS7_TRAPS=1`
    opens them and `ios7lab_timebase: blx _mach_absolute_time; b swi_exit64` runs.

`defined()` comes FIRST in the clause (`defined(X) && X`), so the identifier is never evaluated
undefined and a build with the switch off emits the tree's original text byte for byte
(`mi4-off-option-two-spellings`).  Verified with the real preprocessor: with no define the negated
gate INCLUDES its block and both positive gates EXCLUDE theirs - i.e. the pre-fix behaviour; with
`=1` the reverse; with `=0` the pre-fix behaviour.

The three lines are the ONLY three `BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` gate lines in
the file that are NOT `defined(...)`-prefixed (lines 137/202/555/659 are `#if defined(...)` for the
LEO frontier blocks and are untouched).  The two positive gates at :162/:264 share ONE byte-identical
OLD string, so its expected count is 2; the negated gate at :116 is unique, count 1.

Idempotent, LINE-based (`mi4-an-edit-on-a-prefix-deletes-the-suffix`: the OLD line is a PREFIX of the
NEW one, so a substring replace would re-match its own output).  Both gates are DECIDED before either
is written and each one's match count is asserted exactly, so a file with a partial or double edit is
REFUSED with the file untouched - no half-patched tree behind a non-zero exit.  Re-run: the counts say
"already staged" and nothing is written.
"""
import sys
from pathlib import Path

# The negated gate at :116 - unique (count 1).  The clause goes INSIDE the negation.
NEG_OLD = "#if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)"
NEG_NEW = ("#if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO"
           " || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS))")

# The two positive gates at :162/:264 - BYTE-IDENTICAL, so ONE string with count 2.
POS_OLD = "#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO"
POS_NEW = ("#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO"
           " || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)")

REL = "osfmk/arm/traps_lo.s"
# An anchor that proves this is the file the patch was written for, not a same-named one.
ANCHOR = "EnterARM(fleh_swi)"


def plan_gates(path: Path, specs) -> tuple:
    """Decide every gate BEFORE writing any of them.  (lines, [(indices, new, what), ...]).

    `specs` is [(old, new, expect, what), ...] over one file.  Each gate's two counts are asserted
    EXACTLY, in either spelling - not their sum: a file that mixes the two spellings is a hand edit
    (this script rewrites every matching line in one write, so its own runs cannot leave a partial
    state), and completing it would silently bless whatever else was touched
    (`mi4-a-claim-in-a-comment-is-not-a-check`).  A refusal (SystemExit) therefore happens with the
    file UNTOUCHED - the two gates live in the same file, so writing gate 1 before checking gate 2
    would leave a half-patched tree behind a non-zero exit, which is the state this is here to avoid.
    """
    lines = path.read_text().split("\n")
    work = []
    for old, new, expect, what in specs:
        at_old = [i for i, ln in enumerate(lines) if ln == old]
        at_new = [i for i, ln in enumerate(lines) if ln == new]
        if len(at_old) == 0 and len(at_new) == expect:
            print(f"  {REL}: {what} already staged ({len(at_new)} gate line(s) carry the switch)")
            continue
        if len(at_old) != expect or at_new:
            print(f"patch_d13_ios7_traps: REFUSING: {REL} has {len(at_old)} original + "
                  f"{len(at_new)} staged {what} gate line(s), expected {expect} original and 0 "
                  f"staged (or 0 and {expect}) - a partial or double edit is not repaired in place",
                  file=sys.stderr)
            raise SystemExit(4)
        work.append((at_old, new, what))
    return lines, work


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_ios7_traps.py <xnu-tree-root>", file=sys.stderr)
        return 2
    path = Path(sys.argv[1]) / REL
    if not path.is_file():
        print(f"patch_d13_ios7_traps: no {REL} under {sys.argv[1]} (D13 tree not provisioned?)",
              file=sys.stderr)
        return 2
    if ANCHOR not in path.read_text():
        print(f"patch_d13_ios7_traps: REFUSING: {REL} carries no [{ANCHOR}] - this is not the tree "
              f"this patch was written for", file=sys.stderr)
        return 3
    lines, work = plan_gates(path, [
        (NEG_OLD, NEG_NEW, 1, "negated (-3 fast path)"),
        (POS_OLD, POS_NEW, 2, "positive (saved-state -3 route)"),
    ])
    for at_old, new, what in work:
        for i in at_old:
            lines[i] = new
        print(f"  {REL}: {len(at_old)} {what} gate line(s) opened, at line(s) "
              f"{', '.join(str(i + 1) for i in at_old)}")
    if not work:
        print("patch_d13_ios7_traps: nothing to do")
        return 0
    path.write_text("\n".join(lines))
    print(f"patch_d13_ios7_traps: staged {sum(len(at_old) for at_old, _new, _what in work)} gate "
          f"line(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
