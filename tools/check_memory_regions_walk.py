#!/usr/bin/env python3
"""915-B rung 0 (host half): the ONE /memory/reg region accessor, compiled AND exercised on the host.

WHY THIS EXISTS.  915-B v2 section 2.4/6.8 makes `/memory/reg` a REGION LIST whose first pair is the
boot region, and requires a SINGLE accessor whose walk is a BASE-MATCH - the boot region is the pair
whose base equals the boot base, and it MUST be pair 0.  `src/xnu_memory_regions.c` is that accessor,
kept PURE (`<stdint.h>` only) so this check can compile it with the HOST compiler and run it against
the exact word arrays the payload emits - no target compiler, no device.  A claim in a comment is not
a check ([[mi4-a-claim-in-a-comment-is-not-a-check]]); the walk's refusals are the claim, and this
runs them.

The check:

  1. GATING: `src/xnu_memory_regions.c` is gated `#if STAGE90_XNU_REGIONS`, NOT `#ifdef`.  The switch
     is always defined 0 or 1, so `#ifdef` would read a 0 as "on" and diverge the "off" spelling from
     the "on" one ([[mi4-off-option-two-spellings]]).
  2. PURE: the module and its header include ONLY `<stdint.h>` - no DT header, no XNU type - which is
     what lets the host compiler build it (a stray include would break the host build, and this
     check would say so).
  3. BYTE-FROZEN ARM: the module is NOT yet in `scripts/build.sh`'s SOURCES.  Wiring it in is the
     region-registration rung's job; while rung 0 is host-only the shipped press arm must stay
     byte-frozen, so this check REFUSES the module appearing in build.sh (the class behind the most
     repeated rung failures is an edit that moves the shipped bytes; [[mi4-build-variant-comes-from-an-env-default]]).
  4. BEHAVIOUR: compile `src/xnu_memory_regions.c` + `tools/test_memory_regions_walk.c` with the host
     `cc` at `-Wall -Wextra -Werror` and RUN it; it must exit 0.  Its cases hold the boot pair at
     index 0 for the shipped DT and for 976's rung-1 DT, and refuse a low-bank-first list, an odd
     list, an empty list, an empty later region, and a pair-0 base that is not the boot base.

`--selftest` mutates fact 1 and fact 3 in temp copies and asserts the check refuses each.
Exit 0 when the facts hold, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGIONS_C = os.path.join(REPO, "src", "xnu_memory_regions.c")
REGIONS_H = os.path.join(REPO, "src", "xnu_memory_regions.h")
TEST_C = os.path.join(REPO, "tools", "test_memory_regions_walk.c")
BUILD = os.path.join(REPO, "scripts", "build.sh")


def refuse(msg):
    sys.stderr.write("check_memory_regions_walk: %s\n" % msg)
    return 1


def _structural(c_text, h_text, build_text):
    """The facts a host test cannot see: gating, purity, and that the arm is not yet wired in."""
    if re.search(r"#\s*ifdef\s+STAGE90_XNU_REGIONS", c_text):
        return ("src/xnu_memory_regions.c gates on `#ifdef STAGE90_XNU_REGIONS` - the macro is always "
                "defined 0 or 1, so `#ifdef` reads a 0 as 'on' ([[mi4-off-option-two-spellings]])")
    if not re.search(r"#\s*if\s+STAGE90_XNU_REGIONS\b", c_text):
        return "src/xnu_memory_regions.c no longer gates on `#if STAGE90_XNU_REGIONS`"

    for name, text in (("src/xnu_memory_regions.c", c_text), ("src/xnu_memory_regions.h", h_text)):
        includes = re.findall(r'#\s*include\s+([<"][^>"]+[>"])', text)
        if includes != ['"xnu_memory_regions.h"'] and name.endswith(".c"):
            return ("%s includes %s - the module must be PURE (only its own header, which pulls "
                    "<stdint.h>) so the host compiler can build it" % (name, includes))
        if name.endswith(".h") and includes != ["<stdint.h>"]:
            return ("%s includes %s - the header must be pure (<stdint.h> only)" % (name, includes))

    if re.search(r"^\s*xnu_memory_regions\.c\s*$", build_text, re.M):
        return ("scripts/build.sh lists xnu_memory_regions.c in SOURCES - rung 0 is HOST-ONLY; wiring "
                "the module into the payload is the region-registration rung's job, and while rung 0 "
                "is host-only the shipped press arm must stay byte-frozen")
    return ""


def _compile_and_run():
    """Compile the module against the host test at -Werror and run it.  Returns None on pass, a message on fail."""
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "tmr")
        cmd = [cc, "-I", os.path.join(REPO, "src"), "-DSTAGE90_XNU_REGIONS=1",
               "-Wall", "-Wextra", "-Werror", "-o", exe, TEST_C, REGIONS_C]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return "the host compile FAILED (rc %d):\n%s%s" % (r.returncode, r.stdout, r.stderr)
        run = subprocess.run([exe], capture_output=True, text=True)
        if run.returncode != 0:
            return "the region walk test FAILED (rc %d):\n%s%s" % (run.returncode, run.stdout, run.stderr)
        sys.stdout.write(run.stdout)
    return None


def check(c_text, h_text, build_text, run_behavior=True):
    rc = _structural(c_text, h_text, build_text)
    if rc:
        return rc
    if run_behavior:
        rc = _compile_and_run()
        if rc:
            return rc
    return ""


def selftest():
    c = open(REGIONS_C).read()
    h = open(REGIONS_H).read()
    b = open(BUILD).read()
    if check(c, h, b) != "":
        sys.stderr.write("check_memory_regions_walk --selftest: the clean tree is REFUSED - the "
                         "check is wrong, not the tree\n")
        return 1
    muts = {
        "#ifdef spelling": (c.replace("#if STAGE90_XNU_REGIONS", "#ifdef STAGE90_XNU_REGIONS", 1), h, b),
        "wired into build.sh": (c, h, b.replace("  stage90_main.c\n", "  stage90_main.c\n  xnu_memory_regions.c\n", 1)),
    }
    for what, (cc_, hh, bb) in muts.items():
        if check(cc_, hh, bb, run_behavior=False) == "":
            sys.stderr.write("check_memory_regions_walk --selftest: a tree with `%s` mutated was "
                             "ACCEPTED - the check does not bite\n" % what)
            return 1
    print("check_memory_regions_walk: selftest ok - the clean module passes and each mutation is refused")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    rc = check(open(REGIONS_C).read(), open(REGIONS_H).read(), open(BUILD).read())
    if rc:
        return refuse(rc)
    print("check_memory_regions_walk: ok - one pure region accessor, gated #if, off the shipped arm, "
          "and the walk holds the boot pair 0 and refuses every malformed list")
    return 0


if __name__ == "__main__":
    sys.exit(main())