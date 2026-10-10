#!/usr/bin/env python3
"""915-B rung 1: the low bank appended to DT /memory, and PROVABLY INERT.

WHY THIS EXISTS.  `STAGE90_XNU_REGIONS=1` appends the device's low bank `[0x00000000, 0x60000000)` to
the payload's `/memory/reg` as words 2/3, leaving the boot pair `{RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE}`
at words 0/1 (`src/stage90_main.c`).  Rung 1 is deliberately a NO-OP for every reader: it only puts the
region list into the DT where a LATER rung (region registration in `arm_vm_init.c`) can read it.  That
inertness is a claim about THREE readers, and a claim in a comment is not a check
([[mi4-a-claim-in-a-comment-is-not-a-check]]).  This guard makes it structural:

  1. `memory_reg[]` is gated by `#if STAGE90_XNU_REGIONS` - NOT `#ifdef`.  `#define X 0` is not "off"
     to `#ifdef`, so an always-defined 0/1 switch must be written `#if`, or the "off" spelling
     diverges from the "on" one ([[mi4-off-option-two-spellings]]).
  2. The BOOT PAIR IS FIRST in both arms, and the ENABLED arm APPENDS the low bank - so `reg_value(…,
     0)`, `mem_reg[0]`, `reg[0]`/`reg[1]` (the readers below) cannot see anything new.
  3. The boot pair's WORDS are unchanged between the arms - the append adds, it does not reorder.
  4. The three `/memory/reg` readers read ONLY words 0 and 1: `pe_state.c` (`reg_value` index 0/1),
     `pexpert.c` (`mem_reg[0]`/`mem_reg[1]`), `xnu_pe_init_platform_false.c`
     (`stage90_pe_init_reg_word` index 0u/1u).  An edit that starts reading word 2/3 (consuming the
     low bank without the rest of the port) is REFUSED here, not discovered on the device.
  5. `scripts/build.sh` ALWAYS defines the macro 0 or 1 (a `case` guard) and RECORDS it in
     `stage90-build-config.txt`, so a press the gate reads knows which DT shape the image carries.

Every refusal is a build/check stop.  `--selftest` mutates each fact and asserts the refusal.

Usage:
    check_d13_regions.py            check the tree
    check_d13_regions.py --selftest
Exit 0 when the facts hold, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(REPO, "src", "stage90_main.c")
PE_STATE = os.path.join(REPO, "src", "pe_state.c")
PEXPERT = os.path.join(REPO, "src", "pexpert.c")
PE_FALSE = os.path.join(REPO, "src", "xnu_pe_init_platform_false.c")
BUILD = os.path.join(REPO, "scripts", "build.sh")

# The two /memory/reg arms, whitespace-normalised. The enabled arm appends the low bank; the disabled
# arm is the boot pair alone. Both must start with the boot pair, in this order.
BOOT_PAIR = "RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE"
LOW_BANK_APPEND = "0x00000000u, 0x60000000u"


def refuse(msg):
    sys.stderr.write("check_d13_regions: %s\n" % msg)
    return 1


def _arms(main_text):
    """Return (enabled_arm, disabled_arm) memory_reg initialiser bodies, whitespace-normalised."""
    arms = re.findall(r"static\s+const\s+uint32_t\s+memory_reg\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;",
                      main_text, re.S)
    norm = [re.sub(r"\s+", " ", a).strip().rstrip(",").strip() for a in arms]
    for a in norm:
        if LOW_BANK_APPEND in a:
            return a, next((b for b in norm if LOW_BANK_APPEND not in b), None)
    return None, None


def _tree_facts(main_text, pe_state, pexpert, pe_false, build):
    # 1. `#if` not `#ifdef`.
    if re.search(r"#\s*ifdef\s+STAGE90_XNU_REGIONS", main_text):
        return ("src/stage90_main.c gates memory_reg with `#ifdef STAGE90_XNU_REGIONS` - the macro is "
                "always defined 0 or 1, so `#ifdef` reads a 0 as 'on' ([[mi4-off-option-two-spellings]])")
    if not re.search(r"#\s*if\s+STAGE90_XNU_REGIONS\b", main_text):
        return "src/stage90_main.c no longer gates memory_reg on `#if STAGE90_XNU_REGIONS`"

    # 2/3. The arms: boot pair FIRST, low bank appended only in the enabled arm.
    en, dis = _arms(main_text)
    if en is None or dis is None:
        return ("src/stage90_main.c no longer carries BOTH memory_reg arms (an enabled one appending "
                "the low bank and a disabled one with the boot pair alone)")
    for name, arm in (("enabled", en), ("disabled", dis)):
        if not arm.startswith(BOOT_PAIR):
            return ("the %s memory_reg arm does not begin with the boot pair `%s` - the boot pair moved "
                    "off index 0/1" % (name, BOOT_PAIR))
    if dis != BOOT_PAIR:
        return ("the disabled memory_reg arm is `%s`, not the boot pair alone `%s` - the off spelling "
                "diverged from the shipped bytes" % (dis, BOOT_PAIR))
    if en != "%s, %s" % (BOOT_PAIR, LOW_BANK_APPEND):
        return ("the enabled memory_reg arm is `%s`, not `<boot pair>, 0x00000000u, 0x60000000u` - the "
                "low bank is not appended exactly (or is reordered)" % en)

    # 4. The three readers read words 0/1 only. Scope to the `memory` NODE - pe_state.c also reads
    # the GIC node's `reg` word 2, which is a different node's array, not /memory.
    if not re.search(r'reg_value\([^;]*\bmemory\b[^;]*"reg"\s*,\s*0\s*,', pe_state) or \
       not re.search(r'reg_value\([^;]*\bmemory\b[^;]*"reg"\s*,\s*1\s*,', pe_state):
        return "src/pe_state.c no longer reads the /memory node's reg words 0 and 1"
    if re.search(r'reg_value\([^;]*\bmemory\b[^;]*"reg"\s*,\s*[2-9]\s*,', pe_state):
        return ("src/pe_state.c reads the /memory node's reg word >= 2 - it now CONSUMES the appended "
                "low bank (the rest of the 915-B port is not present)")
    if "mem_reg[0]" not in pexpert or "mem_reg[1]" not in pexpert:
        return "src/pexpert.c no longer reads mem_reg[0] and mem_reg[1]"
    if re.search(r"mem_reg\s*\[\s*[2-9]", pexpert):
        return "src/pexpert.c reads mem_reg[>=2] - it now consumes the appended low bank"
    if not re.search(r'stage90_pe_init_reg_word\([^;]*,\s*memory\s*,\s*"reg"\s*,\s*0u', pe_false) or \
       not re.search(r'stage90_pe_init_reg_word\([^;]*,\s*memory\s*,\s*"reg"\s*,\s*1u', pe_false):
        return "src/xnu_pe_init_platform_false.c no longer reads the /memory node's reg words 0u and 1u"
    if re.search(r'stage90_pe_init_reg_word\([^;]*,\s*memory\s*,\s*"reg"\s*,\s*[2-9]u', pe_false):
        return ("src/xnu_pe_init_platform_false.c reads the /memory node's reg word >= 2u - it consumes "
                "the low bank")

    # 5. build.sh always defines 0/1 and records it.
    if not re.search(r"STAGE90_XNU_REGIONS=\$\{STAGE90_XNU_REGIONS:-0\}", build):
        return "scripts/build.sh no longer defaults STAGE90_XNU_REGIONS to 0"
    if not re.search(r"CFLAGS\+=\s*\(-DSTAGE90_XNU_REGIONS=\$STAGE90_XNU_REGIONS\)", build):
        return "scripts/build.sh no longer passes -DSTAGE90_XNU_REGIONS - the switch cannot reach the C"
    if not re.search(r"^  0\|1\)\s*;;", build, re.M):
        return ("scripts/build.sh no longer refuses a STAGE90_XNU_REGIONS value other than 0 or 1 - a "
                "non-zero/one value would be a different spelling of on")
    if not re.search(r'#define STAGE90_XNU_REGIONS 1" >> \$REPO_ROOT/out/stage90/stage90-build-config\.txt',
                     build, re.S) or not re.search(r'if \[\[ \$STAGE90_XNU_REGIONS -eq 1 \]\]', build):
        return ("scripts/build.sh no longer conditionally RECORDS STAGE90_XNU_REGIONS in "
                "stage90-build-config.txt (only when ON, so the default record is byte-neutral) - a "
                "press could not tell which DT shape the image carries")
    return ""


def _facts_selftest_mutants():
    """Deep-copy the on-disk truths once, then mutate per fact - so the selftest reads the real files."""
    return (open(MAIN).read(), open(PE_STATE).read(), open(PEXPERT).read(),
            open(PE_FALSE).read(), open(BUILD).read())


def selftest():
    m, ps, px, pf, b = _facts_selftest_mutants()
    if _tree_facts(m, ps, px, pf, b) != "":
        sys.stderr.write("check_d13_regions --selftest: the clean tree is REFUSED - the check is "
                         "wrong, not the tree\n")
        return 1
    muts = {
        "#ifdef spelling": (m.replace("#if STAGE90_XNU_REGIONS", "#ifdef STAGE90_XNU_REGIONS", 1), ps, px, pf, b),
        "boot pair not first": (m.replace("RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, 0x00000000u, 0x60000000u",
                                          "0x00000000u, 0x60000000u, RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE", 1), ps, px, pf, b),
        "reader uses word 2": (m, ps.replace('"reg", 1,', '"reg", 2,', 1), px, pf, b),
        "pexpert reads mem_reg[2]": (m, ps, px.replace("mem_reg[1]", "mem_reg[2]", 1), pf, b),
        "build records not": (m, ps, px, pf, b.replace('#define STAGE90_XNU_REGIONS 1" >> $REPO_ROOT/out/stage90/stage90-build-config.txt', '#define STAGE90_XNU_REGIONS 0" >> $REPO_ROOT/out/stage90/stage90-build-config.txt', 1)),
    }
    for what, (mm, pss, pxx, pff, bb) in muts.items():
        if _tree_facts(mm, pss, pxx, pff, bb) == "":
            sys.stderr.write("check_d13_regions --selftest: a tree with `%s` mutated was ACCEPTED - the "
                             "check does not bite\n" % what)
            return 1
    print("check_d13_regions: ok - the low bank is appended after the boot pair, the three /memory "
          "readers read words 0/1 only, and the switch is always-defined 0/1 and recorded")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    rc = _tree_facts(open(MAIN).read(), open(PE_STATE).read(), open(PEXPERT).read(),
                     open(PE_FALSE).read(), open(BUILD).read())
    if rc:
        return refuse(rc)
    print("check_d13_regions: ok - the low bank is appended after the boot pair, the three /memory "
          "readers read words 0/1 only, and the switch is always-defined 0/1 and recorded")
    return 0


if __name__ == "__main__":
    sys.exit(main())