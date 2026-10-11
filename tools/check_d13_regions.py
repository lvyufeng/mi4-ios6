#!/usr/bin/env python3
"""915-B rung 2: the low bank CARVED into spans and appended to DT /memory, and PROVABLY INERT.

WHY THIS EXISTS.  `STAGE90_XNU_REGIONS=1` appends the device's low bank `[0x00000000, 0x60000000)` to
the payload's `/memory/reg`, after the boot pair `{RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE}` at words 0/1
(`src/stage90_main.c`).  Rung 1 appended the WHOLE low bank, which admits the two carveout holes; rung
2 appends instead the THREE spans the PMAP actually owns, so the allocator cannot hand out carveout.
The spans are derived BY VALUE from the live device's own `/proc/iomem` capture
(`out/stage90/device-reads/mi4-iomem-and-mem.txt`, `System RAM` lines below the boot bank).  Rung 2
is still deliberately a NO-OP for every reader: it only puts the region list into the DT where a
LATER rung (region registration in `arm_vm_init.c`) can read it.  That inertness is a claim about the
readers, and a claim in a comment is not a check ([[mi4-a-claim-in-a-comment-is-not-a-check]]).  This
guard makes it structural:

  1. `memory_reg[]` is gated by `#if STAGE90_XNU_REGIONS` - NOT `#ifdef`.  `#define X 0` is not "off"
     to `#ifdef`, so an always-defined 0/1 switch must be written `#if`, or the "off" spelling
     diverges from the "on" one ([[mi4-off-option-two-spellings]]).
  2. The BOOT PAIR IS FIRST in both arms, and the ENABLED arm APPENDS exactly the three low spans -
     so `reg_value(…, 0)`, `mem_reg[0]`, `reg[0]`/`reg[1]` (the readers below) cannot see anything new.
     The enabled arm is word-count 8 (boot pair + three spans), the disabled arm 2, and `src/stage90_main.c` carries a
     `_Static_assert` on each.
  3. The boot pair's WORDS are unchanged between the arms - the append adds, it does not reorder.
  4. The `/memory/reg` readers read ONLY words 0 and 1: `pe_state.c` (`reg_value` index 0/1),
     `pexpert.c` (`mem_reg[0]`/`mem_reg[1]`), `xnu_pe_init_platform_false.c`
     (`stage90_pe_init_reg_word` index 0u/1u), and `mmu.c` (`memory_reg[0]`/`memory_reg[1]`, guarded
     by `memory_reg_len >= 8u`).  An edit that starts reading word 2 (consuming the appended spans
     without the rest of the port) is REFUSED here, not discovered on the device.
  5. The spans are the live `/proc/iomem` lines, carried as PROVENANCE in the source so a dropped
     capture line (an edit that keeps the numbers but loses where they came from) is REFUSED.
  6. `scripts/build.sh` ALWAYS defines the macro 0 or 1 (a `case` guard) and RECORDS it in
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
MMU = os.path.join(REPO, "src", "mmu.c")
BUILD = os.path.join(REPO, "scripts", "build.sh")

# The two /memory/reg arms, whitespace-normalised. The enabled arm appends the low spans; the
# disabled arm is the boot pair alone. Both must start with the boot pair, in this order.
BOOT_PAIR = "RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE"
LOW_SPANS = "0x00000000u, 0x05a00000u, 0x0d200000u, 0x02800000u, 0x0ff00000u, 0x50100000u"
ENABLED_ARM = "%s, %s" % (BOOT_PAIR, LOW_SPANS)

# The provenance: the capture file and the three `System RAM` lines the spans are read from, by value.
# The source carries these so a dropped capture line is a refusal, not a silent stale number.
IOMMEM_CAPTURE = "out/stage90/device-reads/mi4-iomem-and-mem.txt"
IOMMEM_RANGES = ("00000000-059fffff", "0d200000-0f9fffff", "0ff00000-5fffffff")


def refuse(msg):
    sys.stderr.write("check_d13_regions: %s\n" % msg)
    return 1


def _arms(main_text):
    """Return (enabled_arm, disabled_arm) memory_reg initialiser bodies, whitespace-normalised."""
    arms = re.findall(r"static\s+const\s+uint32_t\s+memory_reg\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;",
                      main_text, re.S)
    norm = [re.sub(r"\s+", " ", a).strip().rstrip(",").strip() for a in arms]
    en = next((a for a in norm if LOW_SPANS in a), None)
    dis = next((a for a in norm if LOW_SPANS not in a), None)
    return en, dis


def _tree_facts(main_text, pe_state, pexpert, pe_false, mmu, build):
    # 1. `#if` not `#ifdef`.
    if re.search(r"#\s*ifdef\s+STAGE90_XNU_REGIONS", main_text):
        return ("src/stage90_main.c gates memory_reg with `#ifdef STAGE90_XNU_REGIONS` - the macro is "
                "always defined 0 or 1, so `#ifdef` reads a 0 as 'on' ([[mi4-off-option-two-spellings]])")
    if not re.search(r"#\s*if\s+STAGE90_XNU_REGIONS\b", main_text):
        return "src/stage90_main.c no longer gates memory_reg on `#if STAGE90_XNU_REGIONS`"

    # 2/3. The arms: boot pair FIRST, the three low spans appended only in the enabled arm.
    en, dis = _arms(main_text)
    if en is None or dis is None:
        return ("src/stage90_main.c no longer carries BOTH memory_reg arms (an enabled one appending "
                "the low spans and a disabled one with the boot pair alone)")
    for name, arm in (("enabled", en), ("disabled", dis)):
        if not arm.startswith(BOOT_PAIR):
            return ("the %s memory_reg arm does not begin with the boot pair `%s` - the boot pair moved "
                    "off index 0/1" % (name, BOOT_PAIR))
    if dis != BOOT_PAIR:
        return ("the disabled memory_reg arm is `%s`, not the boot pair alone `%s` - the off spelling "
                "diverged from the shipped bytes" % (dis, BOOT_PAIR))
    if en != ENABLED_ARM:
        return ("the enabled memory_reg arm is `%s`, not `<boot pair>, %s` - the low bank's three spans "
                "are not appended exactly, in order (or are reordered)" % (en, LOW_SPANS))

    # 5. The spans' PROVENANCE: the capture path and the three /proc/iomem lines, carried in the
    # source so the numbers cannot go stale silently.  A dropped capture line is a refusal.
    if IOMMEM_CAPTURE not in main_text:
        return ("src/stage90_main.c no longer names the /proc/iomem capture (`%s`) the low spans are "
                "read from - the numbers became a claim in a comment" % IOMMEM_CAPTURE)
    for rng in IOMMEM_RANGES:
        if rng not in main_text:
            return ("src/stage90_main.c no longer carries the /proc/iomem source line `%s` - the span "
                    "beside it is a number with no derivation" % rng)

    # 4. The readers read words 0/1 only. Scope to the `memory` NODE - pe_state.c also reads the GIC
    # node's `reg` word 2, which is a different node's array, not /memory.
    if not re.search(r'reg_value\([^;]*\bmemory\b[^;]*"reg"\s*,\s*0\s*,', pe_state) or \
       not re.search(r'reg_value\([^;]*\bmemory\b[^;]*"reg"\s*,\s*1\s*,', pe_state):
        return "src/pe_state.c no longer reads the /memory node's reg words 0 and 1"
    if re.search(r'reg_value\([^;]*\bmemory\b[^;]*"reg"\s*,\s*[2-9]\s*,', pe_state):
        return ("src/pe_state.c reads the /memory node's reg word >= 2 - it now CONSUMES the appended "
                "low spans (the rest of the 915-B port is not present)")
    if "mem_reg[0]" not in pexpert or "mem_reg[1]" not in pexpert:
        return "src/pexpert.c no longer reads mem_reg[0] and mem_reg[1]"
    if re.search(r"mem_reg\s*\[\s*[2-9]", pexpert):
        return "src/pexpert.c reads mem_reg[>=2] - it now consumes the appended low spans"
    if not re.search(r'stage90_pe_init_reg_word\([^;]*,\s*memory\s*,\s*"reg"\s*,\s*0u', pe_false) or \
       not re.search(r'stage90_pe_init_reg_word\([^;]*,\s*memory\s*,\s*"reg"\s*,\s*1u', pe_false):
        return "src/xnu_pe_init_platform_false.c no longer reads the /memory node's reg words 0u and 1u"
    if re.search(r'stage90_pe_init_reg_word\([^;]*,\s*memory\s*,\s*"reg"\s*,\s*[2-9]u', pe_false):
        return ("src/xnu_pe_init_platform_false.c reads the /memory node's reg word >= 2u - it consumes "
                "the low spans")
    # mmu.c is the FOURTH reader (it reads the property straight out of the DT, not via reg_value).
    if "memory_reg[0]" not in mmu or "memory_reg[1]" not in mmu:
        return "src/mmu.c no longer reads the /memory node's reg words 0 and 1"
    if re.search(r"memory_reg\s*\[\s*[2-9]", mmu):
        return ("src/mmu.c reads memory_reg[>=2] - it now consumes the appended low spans (the rest of "
                "the 915-B port is not present)")

    # 6. build.sh always defines 0/1 and records it.
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
            open(PE_FALSE).read(), open(MMU).read(), open(BUILD).read())


def selftest():
    m, ps, px, pf, mu, b = _facts_selftest_mutants()
    if _tree_facts(m, ps, px, pf, mu, b) != "":
        sys.stderr.write("check_d13_regions --selftest: the clean tree is REFUSED - the check is "
                         "wrong, not the tree\n")
        return 1
    # The enabled arm's boot pair, then its first span - the two-line pattern that is unique to the
    # enabled arm (the disabled arm's boot pair is not followed by a span line).
    boot_then_span = "RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE,\n        0x00000000u, 0x05a00000u,"
    muts = {
        "#ifdef spelling": (m.replace("#if STAGE90_XNU_REGIONS", "#ifdef STAGE90_XNU_REGIONS", 1), ps, px, pf, mu, b),
        "boot pair not first": (m.replace(boot_then_span,
                                          "0x00000000u, 0x05a00000u,\n        RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE,", 1),
                                ps, px, pf, mu, b),
        "low span moved": (m.replace("0x0d200000u, 0x02800000u,\n        0x0ff00000u, 0x50100000u,",
                                     "0x0ff00000u, 0x50100000u,\n        0x0d200000u, 0x02800000u,", 1),
                           ps, px, pf, mu, b),
        "capture line dropped": (m.replace("0d200000-0f9fffff", "0d200000-0f9ffffe", 1), ps, px, pf, mu, b),
        "reader uses word 2": (m, ps.replace('"reg", 1,', '"reg", 2,', 1), px, pf, mu, b),
        "pexpert reads mem_reg[2]": (m, ps, px.replace("mem_reg[1]", "mem_reg[2]", 1), pf, mu, b),
        "mmu reads memory_reg[2]": (m, ps, px, pf, mu.replace("memory_reg[1]", "memory_reg[2]", 1), b),
        "build records not": (m, ps, px, pf, mu, b.replace('#define STAGE90_XNU_REGIONS 1" >> $REPO_ROOT/out/stage90/stage90-build-config.txt', '#define STAGE90_XNU_REGIONS 0" >> $REPO_ROOT/out/stage90/stage90-build-config.txt', 1)),
    }
    for what, (mm, pss, pxx, pff, muu, bb) in muts.items():
        if _tree_facts(mm, pss, pxx, pff, muu, bb) == "":
            sys.stderr.write("check_d13_regions --selftest: a tree with `%s` mutated was ACCEPTED - the "
                             "check does not bite\n" % what)
            return 1
    print("check_d13_regions: ok - the low bank's three spans are appended after the boot pair, the "
          "four /memory/reg readers read words 0/1 only, and the switch is always-defined 0/1 and recorded")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    rc = _tree_facts(open(MAIN).read(), open(PE_STATE).read(), open(PEXPERT).read(),
                     open(PE_FALSE).read(), open(MMU).read(), open(BUILD).read())
    if rc:
        return refuse(rc)
    print("check_d13_regions: ok - the low bank's three spans are appended after the boot pair, the "
          "four /memory/reg readers read words 0/1 only, and the switch is always-defined 0/1 and recorded")
    return 0


if __name__ == "__main__":
    sys.exit(main())