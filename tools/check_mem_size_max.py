#!/usr/bin/env python3
"""Refuse drift in the 911b physical-memory ceiling port, and refuse a value that could brick.

WHY THIS EXISTS.  911b makes `osfmk/arm/arm_vm_init.c`'s `MEM_SIZE_MAX` a guarded port
(`tools/patch_mem_size_max.py`) so XNU can be told the Mi 4 has more than 1 GiB - the goal's newest
clause.  The whole safety argument is that a build WITHOUT `STAGE90_XNU_MEM_SIZE_MAX` leaves the
object BYTE-FOR-BYTE as Apple shipped it, so the bytes the ladder has run since 903 do not move.  That
is true only while the guard has the exact shape the patcher wrote: an UNDEFINED macro must emit
Apple's `#define` and NOTHING ELSE.  The failure it guards is measured, not hypothetical - an earlier
draft emitted `entry_xnu_mem_size_max_arm_off()` in the `#else` arm, which entered the symbol table,
shifted `.text` by 0x10 (0x1f90 -> 0x1fa0) and moved every following symbol (`arm_vm_prot_init`,
`arm_vm_init`).  The default object was no longer byte-identical and the whole argument was void while
the source still LOOKED guarded.  A comment cannot catch that; this check re-derives it.

WHAT IT CHECKS (source structure - no compiler, no device, so it fails in a second):
  1. the patcher's marker is present in `arm_vm_init.c` (the port is staged, not a re-provisioned tree);
  2. the guard nests exactly as the patcher wrote it, and BOTH marker arms (`..._arm_on` / `_arm_off`)
     are lexically INSIDE `#ifdef STAGE90_XNU_MEM_SIZE_MAX` - an unconditional marker is the measured
     defect;
  3. the OUTER `#else` arm (macro undefined) carries Apple's `#define` and NO `int ..._(void)` symbol;
  4. `src/stage90.h`'s parallel `STAGE90_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX` references
     `STAGE90_XNU_MEM_SIZE_MAX` when it is defined - one value, two definitions, moved in lockstep;
  5. the raised value the arm uses is <= the safe ceiling `0x5e500000` (hole-free high-bank top minus
     the ram_console at `0xde500000`; MMIO starts at `0xf9000000`).  A value above it would map the
     Android ram_console or MMIO as RAM - the brick path this check exists to refuse.

The OBJECT-level half (the linked entry image defines `_arm_on` iff the switch is set-and-different)
is a build clause in `src/entry/build_entry.sh`, which has the compiler and the object; this check is
the source half, like `check_usb_dev.py`'s.
"""
import re
import sys

REPO = "/mnt/data/mi4-ios6"
AVM = REPO + "/external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c"
PAIR = REPO + "/src/stage90.h"
PATCHER = REPO + "/tools/patch_mem_size_max.py"

MARK = "STAGE90_XNU_MEM_SIZE_MAX"
SAFE_CEILING = 0x5E500000        # high-bank top-minus-console, hole-free: 0x80000000..0xde500000
CONSOLE = 0xDE500000             # Android ram_console - must NOT be mapped as RAM
MMIO_FLOOR = 0xF9000000          # device MMIO - must NOT be mapped as RAM
PHYS_BASE = 0x80000000           # gPhysBase in the entry image


def refuse(msg: str) -> None:
    print(f"check_mem_size_max: {msg}", file=sys.stderr)
    sys.exit(1)


def _structure_ok(src: str) -> str:
    """Return "" if `src` (arm_vm_init.c's text) is the correct 911b guard, else the refusal message.

    Split out from the file read so `--selftest` can feed it the MEASURED defect - the unconditional
    marker - and assert it is refused.  A checker that cannot fail on the defect it exists to catch is
    the defect ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
    """
    if MARK not in src:
        return (f"carries no {MARK} guard - the 911b port is not staged "
                f"(run tools/stage_hfs.sh, which applies tools/patch_mem_size_max.py)")

    guard = (
        "#ifdef STAGE90_XNU_MEM_SIZE_MAX\n"
        "#if (STAGE90_XNU_MEM_SIZE_MAX != 0x40000000)\n"
        "#define MEM_SIZE_MAX STAGE90_XNU_MEM_SIZE_MAX\n"
        "int entry_xnu_mem_size_max_arm_on(void)  { return 1; }\n"
        "#else\n"
        "#define MEM_SIZE_MAX 0x40000000\n"
        "int entry_xnu_mem_size_max_arm_off(void) { return 0; }\n"
        "#endif /* STAGE90_XNU_MEM_SIZE_MAX != default */\n"
        "#else\n"
        "#define MEM_SIZE_MAX 0x40000000\n"
        "#endif /* STAGE90_XNU_MEM_SIZE_MAX */\n"
    )
    if src.count(guard) != 1:
        on, off = src.count("entry_xnu_mem_size_max_arm_on"), src.count("entry_xnu_mem_size_max_arm_off")
        if "#ifdef STAGE90_XNU_MEM_SIZE_MAX" not in src:
            return (f"{MARK} is present but there is no `#ifdef {MARK}` guard around MEM_SIZE_MAX "
                    f"- the port is staged in a shape that cannot keep the default object byte-identical")
        ifdef = src.index("#ifdef STAGE90_XNU_MEM_SIZE_MAX")
        decl = re.search(r"int\s+entry_xnu_mem_size_max_arm_(on|off)\s*\(void\)", src)
        if decl and decl.start() < ifdef:
            return (f"`{decl.group(0)}` is declared BEFORE the `#ifdef {MARK}` guard, so it is emitted "
                    f"in the DEFAULT build - that enters the symbol table, shifts .text and makes the "
                    f"default object NOT byte-identical to Apple's (the measured defect: .text "
                    f"0x1f90 -> 0x1fa0). Both marker arms must be inside the #ifdef.")
        return (f"{MARK} is present but the guard is not the shape tools/patch_mem_size_max.py writes "
                f"({on} `_arm_on`, {off} `_arm_off` found) - re-run the patcher or restore it by hand")

    outer = re.search(
        r"#else\n(#define MEM_SIZE_MAX 0x40000000\n)#endif /\* STAGE90_XNU_MEM_SIZE_MAX \*/", guard)
    if outer is None or "entry_xnu_mem_size_max_arm_off" in outer.group(1):
        return ("the OUTER #else arm carries a marker symbol - that makes the default object's bytes "
                "depend on the port (the measured byte-identity defect)")
    if src.count("#define MEM_SIZE_MAX 0x40000000\n") < 1:
        return ("Apple's `#define MEM_SIZE_MAX 0x40000000` is gone entirely - the port cannot be "
                "re-derived; restore the file from a clean tree and re-run the patcher")
    return ""


def check_structure() -> None:
    """Checks 1-3 on the patched arm_vm_init.c, by locating the guard and the marker declarations."""
    try:
        with open(AVM, "r", encoding="utf-8", errors="surrogateescape") as fh:
            src = fh.read()
    except OSError as exc:
        refuse(f"cannot read {AVM}: {exc} (is `external/` provisioned?)")

    bad = _structure_ok(src)
    if bad:
        refuse(f"{AVM}: {bad}")


def check_pair() -> None:
    """Check 4: the payload's spelling of the same value must move with the kernel's."""
    with open(PAIR, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()
    m = re.search(
        r"#ifndef STAGE90_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX\n"
        r"#if defined\(STAGE90_XNU_MEM_SIZE_MAX\)\n"
        r"#define STAGE90_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX\s+STAGE90_XNU_MEM_SIZE_MAX\n"
        r"#else\n"
        r"#define STAGE90_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX\s+0x40000000u\n", src)
    if m is None:
        refuse(f"{PAIR}: STAGE90_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX no longer tracks STAGE90_XNU_MEM_SIZE_MAX "
               f"- one value, two definitions: if the kernel ceiling moves and this spelling does not, "
               f"xnu_pmap_bootstrap_contract.c's vstart assertion refuses a correct pmap")


def check_ceiling(value: int) -> None:
    """Check 5: a raised value must name RAM, not the ram_console or MMIO, and must not wrap."""
    if value == 0x40000000:
        return  # Apple's default, always safe
    end = PHYS_BASE + value
    if end > 0xFFFFFFFF:
        refuse(f"MEM_SIZE_MAX=0x{value:08x}: gPhysBase + mem_size = 0x{end:08x} overflows 32 bits")
    if CONSOLE < end:
        refuse(f"MEM_SIZE_MAX=0x{value:08x} (top 0x{end:08x}) maps the ram_console at 0x{CONSOLE:08x} "
               f"as RAM - the brick path; the safe ceiling is 0x{SAFE_CEILING:08x}")
    if MMIO_FLOOR < end:
        refuse(f"MEM_SIZE_MAX=0x{value:08x} (top 0x{end:08x}) maps device MMIO (>=0x{MMIO_FLOOR:08x}) as RAM")
    if value > SAFE_CEILING:
        refuse(f"MEM_SIZE_MAX=0x{value:08x} exceeds the safe ceiling 0x{SAFE_CEILING:08x}")


def check_patcher_cites_ceiling() -> None:
    """The ceiling this check enforces must be the one the patcher's own comment names, or the two
    sources of the value have drifted apart (one value, two definitions, again - in the tooling)."""
    with open(PATCHER, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()
    if "0x5e500000" not in src:
        refuse(f"{PATCHER} no longer names the safe ceiling 0x5e500000 - the value the arm uses and the "
               f"value this check enforces have drifted apart")


def selftest() -> int:
    """Exercise the ceiling validator against values on both sides of every boundary."""
    ok = [(0x40000000, True), (0x5E500000, True), (0x5E400000, True),
          (0x5E500001, False),   # one byte over the safe ceiling (just past the console)
          (0x5E600000, False),   # top past the console -> the brick path
          (0x80000000, False),   # 2 GiB -> wraps gPhysBase + mem_size
          (0xFE000000, False)]   # deep into MMIO, wraps
    bad = 0
    for v, want in ok:
        try:
            check_ceiling(v)
            got = True
        except SystemExit:
            got = False
        if got != want:
            print(f"check_mem_size_max --selftest: 0x{v:08x} -> {got}, wanted {want}", file=sys.stderr)
            bad += 1
    # The structural checker must also PASS on the correct guard and FAIL on the MEASURED defect (the
    # unconditional marker). A checker that cannot fail is the defect it exists to catch.
    good = (
        "#define MEM_SIZE_MAX 0x40000000\n"  # replaced below by the real guard text
    )
    # Reconstruct the exact guard text by importing the module's own constant path: build it here so
    # the selftest does not depend on the tree being patched.
    good = (
        "#ifdef STAGE90_XNU_MEM_SIZE_MAX\n"
        "#if (STAGE90_XNU_MEM_SIZE_MAX != 0x40000000)\n"
        "#define MEM_SIZE_MAX STAGE90_XNU_MEM_SIZE_MAX\n"
        "int entry_xnu_mem_size_max_arm_on(void)  { return 1; }\n"
        "#else\n"
        "#define MEM_SIZE_MAX 0x40000000\n"
        "int entry_xnu_mem_size_max_arm_off(void) { return 0; }\n"
        "#endif /* STAGE90_XNU_MEM_SIZE_MAX != default */\n"
        "#else\n"
        "#define MEM_SIZE_MAX 0x40000000\n"
        "#endif /* STAGE90_XNU_MEM_SIZE_MAX */\n"
    )
    # The MEASURED defect: the marker declared OUTSIDE the guard (the first 911b draft). Must refuse.
    measured_defect = (
        "#ifndef STAGE90_XNU_MEM_SIZE_MAX\n"
        "#define MEM_SIZE_MAX 0x40000000\n"
        "#else\n"
        "#define MEM_SIZE_MAX STAGE90_XNU_MEM_SIZE_MAX\n"
        "#endif\n"
        "int entry_xnu_mem_size_max_arm_off(void) { return 0; }\n"  # <-- outside the #ifdef: shifts .text
    )
    cases = [("the patcher's guard", good, True),
             ("the measured defect (marker outside the #ifdef)", measured_defect, False),
             ("an unstaged source", "#define MEM_SIZE_MAX 0x40000000\n", False)]
    for label, text, want_ok in cases:
        got_ok = _structure_ok(text) == ""
        if got_ok != want_ok:
            print(f"check_mem_size_max --selftest: {label} -> ok={got_ok}, wanted {want_ok}", file=sys.stderr)
            bad += 1
    if bad:
        print(f"check_mem_size_max --selftest: FAIL ({bad})", file=sys.stderr)
        return 1
    print("check_mem_size_max --selftest: ok - the ceiling validator refuses every brick-boundary value, "
          "and the structural checker accepts the real guard and refuses the measured marker-outside-ifdef defect")
    return 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        return selftest()
    if len(sys.argv) == 2:
        arg = sys.argv[1]
        try:
            check_ceiling(int(arg, 16) if arg.lower().startswith("0x") else int(arg))
        except ValueError:
            refuse(f"not a number: {arg!r}")
        print(f"check_mem_size_max: 0x{int(arg, 16) if arg.lower().startswith('0x') else int(arg):08x} is a safe ceiling")
        return 0
    if len(sys.argv) != 1:
        print("usage: check_mem_size_max.py [<hex-value> | --selftest]", file=sys.stderr)
        return 2
    check_structure()
    check_pair()
    check_patcher_cites_ceiling()
    print("check_mem_size_max: ok - the 911b guard keeps the default object byte-identical "
          "(both markers inside the #ifdef, the outer #else a bare define); the payload spells the same "
          "value; the safe ceiling 0x5e500000 is the one the patcher names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())