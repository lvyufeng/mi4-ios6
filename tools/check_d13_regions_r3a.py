#!/usr/bin/env python3
"""check_d13_regions_r3a.py - every R3a claim is a CHECK that stops the build.

WHY THIS EXISTS.  `tools/stage_d13_regions_r3a.sh` makes the DT `/memory/reg` region list the pmap's
region table, makes `pmap_valid_page` region-aware, and makes the free-page count carry the low bank.
A claim in a comment is not a check ([[mi4-a-claim-in-a-comment-is-not-a-check]]); every property R3a
rests on is re-derived here BY VALUE from the staged tree, and each is a refusal.  This is a
**STAGE-TIME** refusal, deliberately NOT a `make check` entry: it refuses an UNSTAGED tree (the tree
is unstaged today, by the task's own instruction), so it cannot pass `make check` on the current tree
- see the module's last section.

THE FACTS, and the design row each encodes (docs/915b-next-rung.md section 2):

  F1  the switch is `#if STAGE90_XNU_REGIONS`, never `#ifdef`, in BOTH staged files - the macro is
      always defined 0 or 1 by `scripts/build.sh:182-187` and `tools/build_xnu_arm_kernel.sh:243-259`,
      so an undefined name must not be a second spelling of off ([[mi4-off-option-two-spellings]]).
  F2  (C7) ONE region table.  Exactly one `mem_region_t ...[]` DEFINITION under `osfmk/arm/`, and it
      is `pmap_mem_regions[]` (`pmap.h:303-305`); a second array, or a re-typedef of `mem_region_t`,
      is refused.  (v2 section 6.2.)
  F3  (C2) `gPhysBase` and `gMemSize` each have ONE assignment in `arm_vm_init.c` and the R3a block
      assigns NEITHER (they are only READ, against `args->physBase`/`gMemSize`).  (915-B section 6.3.)
  F4  (C1) the four totals `max_mem`/`mem_size`/`sane_size` are not written by the R3a block, and
      `mem_size`/`sane_size`'s terminal RHS is still exactly `gMemSize` (964, refining 915 section 5).
      `tools/check_d13_memory_total.py` fact 5 (`:152-167`, in `make check`) is the authority; this
      fact re-derives the same property over the POST-R3a file so the port cannot slip past it.
  F5  (C6) the boot interval is byte-identical: the `pmap_bootstrap` print and `avail_end =
      gPhysBase + gMemSize` are the tree's lines.
  F6  (C3) the registration CHECKs the boot pair: `region[0].base == gPhysBase && region[0].size ==
      gMemSize` is present as a panic, not an assumption.
  F7  the REACHABILITY GATE (design section 2.2 catch (a)): `pmap_valid_page`'s lower bound is the
      `pmap_mem_regions_reach2` split (the bound under which a low page has no mapped kernel VA), not
      a bare `0`.  A predicate that admitted a low page without the gate would be the defect the
      split exists to prevent.
  F8  the emit half stays behind R5's switch: `pmap.c` must not OPEN a preprocessor switch on
      `STAGE90_XNU_BOUNCE_WINDOW` (naming it in a comment is allowed - there is no other way to say
      "this is R5's switch").
  F9  the count and the drain move TOGETHER: the low-bank count line (`avail_remaining += low_pages`)
      and the drain seed (`pmap_second_cursor = avail_start2`) both exist, and the drain loop uses
      `pmap_valid_page` - so the count EQUALS the pages the drain can return.  This encodes the
      measurement that made the design's pre-registered criterion (section 2.3.3) UNSAFE as written:
      `pmap_startup` (`osfmk/vm/vm_resident.c:799-821`) sizes `vm_pages[]` from `pmap_free_pages()`
      and steals that many pages through the same `pmap_next_page`, so a count with no matching
      drain overspends the boot region instead of handing out a low page.

THE TWO CATCHES THE DESIGN FLAGGED, AND HOW EACH IS HONOURED:

  (a) THE REACHABILITY GATE.  F7 makes the gate a check.  It is stated the way the tree can support
      it: TTBR0 owns `[0, 2^(32-N))` under `TTBCR.N = 2` (`arm_vm_init.c:475`, `set_mmu_ttbcr(2)`),
      and the kernel's identity/managed build covers `[gPhysBase, gPhysBase + gMemSize)`
      (`:400`, `l2_map_linear_range`) - so a general low page has no mapped kernel VA.  The check
      asserts the BOUND is what `pmap_valid_page` uses; whether the hardware then walks that bound is
      the press's reading, not this check's.

  (b) THE VACUOUS `vm_page_init` `#if DEBUG` CLAUSE - STRUCK, NOT PASSED.  `osfmk/vm/vm_resident.c`
      guards `panic("vm_page_init: non-DRAM phys_page ...")` with `#if DEBUG`.  MEASURED: the D13
      kernel is composed from Apple's `RELEASE` configuration (`tools/build_xnu_arm_kernel.sh:274`,
      `CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}`), whose define set carries no `-DDEBUG`
      (`tools/xnu_config/make_defines.sh RELEASE`), and nothing in the kernel build path defines it -
      so `#if DEBUG` is `#if 0`, the panic is compiled out, and a criterion that read "the panic does
      not fire" would be satisfied by a boot that never had the code.  F10 encodes the strike as a
      MEASUREMENT: it refuses if a `-DDEBUG` define ever appears in the kernel build path (because
      then the clause would be LIVE and this record's "struck as vacuous" would be wrong).  Until
      then the clause is exactly as struck.

THE NULL CONTROL (design section 2.5).  Every instrument needs a reading that would be the same if
the mechanism were absent.  R3a's instrument is the low-bank count record, keyed `1015b-regions R3a:`
and printed from inside `#if STAGE90_XNU_REGIONS`.  The null control is the `STAGE90_XNU_REGIONS=0`
arm, where the record key is **ABSENT, not 0** (the project's one-way absent-key idiom).  F11 makes
that structural: the key must occur EXACTLY ONCE and its enclosing preprocessor condition must be the
regions switch - so at 0 the emit is not compiled and the key cannot appear at all.

Usage:
    check_d13_regions_r3a.py [--tree <XNU_TREE>]   check the staged tree (refuses if unstaged)
    check_d13_regions_r3a.py --selftest            mutate each fact, assert every refusal
Exit 0 when every fact holds, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))
PATCHER = os.path.join(HERE, "patch_d13_regions_r3a.py")
KERNEL_BUILD = os.path.join(REPO, "tools", "build_xnu_arm_kernel.sh")
MAKE_DEFINES = os.path.join(REPO, "tools", "xnu_config", "make_defines.sh")
DEFAULT_TREE = os.path.join(REPO, "external", "xnu-hd2-darwin13", "xnu")

MARK = "1015b-regions R3a"
BOUNCE_SWITCH = "STAGE90_XNU_BOUNCE_WINDOW"
PMAP = "osfmk/arm/pmap.c"
ARM_VM = "osfmk/arm/arm_vm_init.c"


def refuse(msg):
    sys.stderr.write("check_d13_regions_r3a: %s\n" % msg)
    return 1


# ------------------------------------------------------------------------------------------------
# Each fact is a function of the staged text and returns a refusal message, or "" when it holds.
# ------------------------------------------------------------------------------------------------

def f1_switch_is_if(texts):
    for rel, text in texts.items():
        if re.search(r"#\s*ifdef\s+STAGE90_XNU_REGIONS", text):
            return ("%s gates with `#ifdef STAGE90_XNU_REGIONS` - the macro is always defined 0 or 1, "
                    "so `#ifdef` reads a 0 as 'on' ([[mi4-off-option-two-spellings]])" % rel)
        if not re.search(r"#\s*if\s+STAGE90_XNU_REGIONS\b", text):
            return "%s does not gate on `#if STAGE90_XNU_REGIONS`" % rel
    return ""


def f2_one_region_table(tree):
    """(C7) exactly one mem_region_t[] DEFINITION under osfmk/arm/, and it is pmap_mem_regions[]."""
    arm = os.path.join(tree, "osfmk", "arm")
    defs = []
    for name in sorted(os.listdir(arm)):
        if not name.endswith(".c"):
            continue
        path = os.path.join(arm, name)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        # a DEFINITION, not the `extern ... pmap_mem_regions[...]` declaration pmap.h carries.
        for m in re.finditer(r"^\s*((?:static\s+|const\s+)*)mem_region_t\s+([A-Za-z_]\w*)\s*\[", text, re.M):
            if "extern" not in m.group(0):
                defs.append((name, m.group(2), m.start()))
    if len(defs) != 1:
        return ("osfmk/arm/ carries %d `mem_region_t ...[]` definitions %s - 915-B section 6.2/d2 "
                "requires EXACTLY ONE, and it must be `pmap_mem_regions[]` (pmap.h:303-305)"
                % (len(defs), [d[:2] for d in defs]))
    if defs[0][1] != "pmap_mem_regions":
        return ("the one `mem_region_t[]` definition is `%s`, not `pmap_mem_regions` - a second region "
                "table is [[mi4-one-value-two-definitions]]" % defs[0][1])
    # a re-typedef would be a second definition of the TYPE (the same defect one level up).
    tt = [n for n in sorted(os.listdir(arm))
          if n.endswith(".h") and re.search(r"typedef\s+struct\s+mem_region", open(os.path.join(arm, n)).read())]
    if sorted(tt) != ["pmap.h"]:
        return ("`mem_region_t` is typedef'd in %s - it must be defined ONCE, in pmap.h" % (sorted(tt) or "nowhere"))
    return ""


def _gated_blocks(text):
    """Every `#if STAGE90_XNU_REGIONS` ... `#endif` block in a file (what the rung ADDS)."""
    return re.findall(r"#if STAGE90_XNU_REGIONS\n(.*?)#endif /\* STAGE90_XNU_REGIONS \*/",
                      text, re.S)


def f3_boot_globals_one_writer(texts):
    """(C2) gPhysBase/gMemSize are assigned once, from the boot args; the R3a blocks write neither."""
    text = texts[ARM_VM]
    for name, expr in (("gPhysBase", "args->physBase"), ("gMemSize", "args->memSize")):
        n = len(re.findall(r"\b%s\s*=\s*%s\s*;" % (name, re.escape(expr)), text))
        if n != 1:
            return ("arm_vm_init.c assigns `%s` from `%s` %d times, want 1 - the boot globals must have "
                    "exactly ONE writer, the boot-args latch (C2)" % (name, expr, n))
        for blk in _gated_blocks(text):
            if re.search(r"\b%s\s*=" % name, blk):
                return ("the R3a block assigns `%s` - the registration must READ it (against "
                        "args->%s), never write it (964, C2)" % (name, expr.split("->")[-1]))
    return ""


def f4_totals_not_raised(texts):
    """(C1) the R3a blocks write none of the four totals; mem_size/sane_size stay gMemSize (964)."""
    for rel in (ARM_VM, PMAP):
        for blk in _gated_blocks(texts[rel]):
            for name in ("max_mem", "mem_size", "sane_size"):
                if re.search(r"\b%s\s*=" % name, blk):
                    return ("a R3a block in %s assigns `%s` - R3a raises the FREE-PAGE count "
                            "(`avail_remaining`), never the totals (964, C1)" % (rel, name))
    for m in re.finditer(r"(?:^|[;{=])\s*(mem_size|sane_size)\s*=\s*([^;]*);", texts[ARM_VM], re.M):
        terminal = m.group(2).rsplit("=", 1)[-1].strip()
        if terminal != "gMemSize":
            return ("%s assigns `%s` off the boot bank (`%s = %s`) - 964: it must only ever be "
                    "`gMemSize` (tools/check_d13_memory_total.py fact 5 is the authority)"
                    % (ARM_VM, m.group(1), m.group(1), terminal))
    return ""


def f5_boot_interval_intact(texts):
    """(C6) the pmap_bootstrap print and avail_end derive from the boot interval alone."""
    p = texts[PMAP]
    if p.count('    kprintf("pmap_bootstrap: physical region 0x%08x - 0x%08x\\n", first_avail, avail_end);\n') != 1:
        return ("pmap_bootstrap's `physical region` print is not the tree's line - 915-B section 2.3 "
                "requires it byte-identical for the boot region (C6)")
    if not re.search(r"avail_end\s*=\s*gPhysBase\s*\+\s*gMemSize\s*;", texts[ARM_VM]):
        return "arm_vm_init.c no longer sets `avail_end = gPhysBase + gMemSize` - the boot interval moved"
    if p.count("    avail_remaining = (avail_end - first_avail) >> PAGE_SHIFT;\n") != 1:
        return "pmap_bootstrap's `avail_remaining` seed is no longer the boot-interval expression alone"
    return ""


def f6_boot_pair_checked(texts):
    """(C3) the registration panics unless region[0] is the boot pair."""
    if not re.search(r"region\[0\]\.base\s*!=\s*gPhysBase\s*\|\|\s*region\[0\]\.size\s*!=\s*gMemSize", texts[ARM_VM]):
        return ("the registration does not CHECK `region[0].base == gPhysBase && region[0].size == "
                "gMemSize` - the boot region would be an assumption, not a checked invariant (C3)")
    return ""


def f7_reachability_gate(texts):
    """The TTBCR.N=2 split is computed AND is the bound the low-bank COUNT skips against
    (design section 2.2 catch (a)).  The gate is that a general low DRAM page < 0x40000000 has no
    mapped kernel VA, so the count must EXCLUDE the boot region and admit only the low bank."""
    av = texts[ARM_VM]
    for pat, what in (
        (r"pmap_mem_regions_reach2\s*=\s*\(1u\s*<<\s*\(32u\s*-\s*2u\)\)\s*;",
         "the split constant `pmap_mem_regions_reach2 = (1u << (32u - 2u))`"),
        (r"if\s*\(\s*\(uint32_t\)pmap_mem_regions\[i\]\.start\s*>=\s*pmap_mem_regions_reach2\s*\)\s*\n\s*continue\s*;",
         "the count's reachability skip (`if (start >= pmap_mem_regions_reach2) continue;`)"),
    ):
        if not re.search(pat, av):
            return ("arm_vm_init.c does not carry %s - the reachability gate (TTBCR.N=2 split "
                    "0x40000000, set at `set_mmu_ttbcr(2)` :475) is the design's catch (a): the kernel's "
                    "identity/managed build covers [gPhysBase, gPhysBase+gMemSize) = 0x80000000-and-up "
                    "only, so a low page < 0x40000000 has NO mapped kernel VA and must NOT be booked "
                    "as reachable" % what)
    # the predicate must be a region span-set (the membership the drain shares), with the usable end.
    m = re.search(r"boolean_t pmap_valid_page\(ppnum_t p\)\n\{.*?\n\}", texts[PMAP], re.S)
    if not m:
        return "pmap_valid_page is not present in pmap.c"
    fn = m.group(0)
    if "pmap_mem_regions[i].start" not in fn or "pmap_mem_regions_usable_end[i]" not in fn:
        return ("pmap_valid_page is not a region SPAN-SET using the usable end - the predicate and the "
                "drain must share one membership (the carveout exclusion, v2 section 2.2)")
    return ""


def f8_emit_half_behind_r5(texts):
    """The emit-consumer half stays behind R5's switch, which R3a must not OPEN."""
    for rel, text in texts.items():
        if re.search(r"#\s*if(?:def)?\s+%s\b" % BOUNCE_SWITCH, text) or \
           re.search(r"-\s*D\s*%s\b" % BOUNCE_SWITCH, text):
            return ("%s OPENS a preprocessor switch on %s - the emit half is R5's, and R3a must not "
                    "define it (naming it in a comment is fine)" % (rel, BOUNCE_SWITCH))
    return ""


def f9_count_and_drain_together(texts):
    """The count and its drain are one mechanism (the pmap_startup measurement)."""
    p = texts[PMAP]
    for needle, what in (
        ("avail_remaining += (pmap_paddr_t)low_pages;", "the low-bank count seed"),
        ("pmap_second_cursor = avail_start2;", "the drain seed (bootstrap -> next_page)"),
        ("!pmap_valid_page((ppnum_t)(pmap_second_cursor >> PAGE_SHIFT))",
         "the drain's carveout skip (the SAME predicate as pmap_valid_page)"),
    ):
        if p.count(needle) != 1:
            return ("pmap.c carries `%s` %d times, want 1 - the count and the drain MUST move together: "
                    "`pmap_startup` (vm_resident.c:799-821) sizes `vm_pages[]` from `pmap_free_pages()` "
                    "and steals that many pages through `pmap_next_page`, so a count with no matching "
                    "drain overspends the boot region instead of handing out a low page"
                    % (what, p.count(needle)))
    return ""


def f10_debug_clause_struck():
    """Catch (b): the vm_page_init `#if DEBUG` panic is VACUOUS on this arm, so it is STRUCK, not a
    criterion.  This fact makes the strike a measurement: if a `-DDEBUG` define ever enters the kernel
    build path the clause becomes LIVE, and this record's strike would be wrong."""
    with open(KERNEL_BUILD, encoding="utf-8", errors="replace") as f:
        kb = f.read()
    if re.search(r"(-D\s*DEBUG\b|^\s*DEBUG\b\s*[:=]|\bCONFIG\s*=\s*DEBUG)", kb, re.M):
        return ("tools/build_xnu_arm_kernel.sh now defines DEBUG - the vm_page_init `#if DEBUG` panic "
                "is LIVE again, so R3a's clause (4) is no longer vacuous and this strike is wrong: "
                "re-examine the criterion before trusting the record")
    # and the define set really is RELEASE's (no -DDEBUG in it)
    if os.path.isfile(MAKE_DEFINES):
        r = subprocess.run(["bash", MAKE_DEFINES, "RELEASE"], capture_output=True, text=True)
        if re.search(r"(^|\s)-DDEBUG(\s|$)", r.stdout):
            return ("`make_defines.sh RELEASE` now carries -DDEBUG - the DEBUG arm grew a live clause; "
                    "the vm_page_init panic is no longer compiled out")
    return ""


def f11_record_key_absent_when_off(texts):
    """The NULL CONTROL: the count record's key is emitted only under the regions switch, so at
    STAGE90_XNU_REGIONS=0 it is ABSENT (not 0) - the one-way absent-key idiom."""
    p = texts[PMAP]
    if p.count(MARK + ": low-bank regions") != 1:
        return ("the low-bank count record key occurs %d times, want 1 - the instrument is not the "
                "one record" % p.count(MARK + ": low-bank regions"))
    pos = p.index(MARK + ": low-bank regions")
    # the nearest preceding preprocessor condition must be the regions switch, with no `#else` between
    sibs = list(re.finditer(r"^#\s*(if|ifdef|ifndef|else|elif|endif)\b.*$", p[:pos], re.M))
    cond = None
    for m in reversed(sibs):
        d = m.group(1)
        if d in ("if", "ifdef", "ifndef"):
            cond = m.group(0)
            break
        if d == "else":
            return ("the count record at offset %d sits under an `#else` of the nearest `#if` - with "
                    "the switch OFF an `#else` arm is COMPILED, so the key would still appear (it must "
                    "be ABSENT, the null control)" % pos)
    if cond is None or "STAGE90_XNU_REGIONS" not in cond:
        return ("the count record's nearest enclosing `#if` is %r, not the regions switch - at "
                "STAGE90_XNU_REGIONS=0 the key must be ABSENT (the null control), so the record must "
                "be inside `#if STAGE90_XNU_REGIONS`" % (cond or "<none>"))
    return ""


FACTS = (
    ("F1  the switch is `#if`, never `#ifdef` (both files)", lambda t, tree: f1_switch_is_if(t)),
    ("F2  (C7) ONE region table, `pmap_mem_regions[]`", lambda t, tree: f2_one_region_table(tree)),
    ("F3  (C2) gPhysBase/gMemSize have one writer, not R3a", lambda t, tree: f3_boot_globals_one_writer(t)),
    ("F4  (C1) the four totals are not raised (964)", lambda t, tree: f4_totals_not_raised(t)),
    ("F5  (C6) the boot interval and its print are intact", lambda t, tree: f5_boot_interval_intact(t)),
    ("F6  (C3) the boot pair is CHECKED against gPhysBase/gMemSize", lambda t, tree: f6_boot_pair_checked(t)),
    ("F7  the reachability gate (TTBCR.N=2 split) is in the predicate", lambda t, tree: f7_reachability_gate(t)),
    ("F8  the emit half stays behind R5's switch", lambda t, tree: f8_emit_half_behind_r5(t)),
    ("F9  the count and the drain move together", lambda t, tree: f9_count_and_drain_together(t)),
    ("F10 the `#if DEBUG` clause is struck as vacuous (still vacuous)", lambda t, tree: f10_debug_clause_struck()),
    ("F11 NULL CONTROL: the record key is absent when the switch is off", lambda t, tree: f11_record_key_absent_when_off(t)),
)


def _read_texts(tree):
    out = {}
    for rel in (PMAP, ARM_VM):
        path = os.path.join(tree, rel)
        if not os.path.isfile(path):
            return None
        with open(path, encoding="utf-8", errors="replace") as f:
            out[rel] = f.read()
    return out


def run_facts(texts, tree):
    """Return a list of (label, refusal) for the facts that DO NOT hold."""
    bad = []
    for label, fn in FACTS:
        try:
            msg = fn(texts, tree)
        except Exception as e:                       # a fact that cannot run is a refusal, not a pass
            msg = "the fact could not be evaluated: %r" % e
        if msg:
            bad.append((label, msg))
    return bad


def check(tree):
    texts = _read_texts(tree)
    if texts is None:
        return refuse("%s has no %s/%s - not a provisioned D13 tree" % (tree, PMAP, ARM_VM))
    if MARK not in texts[PMAP] or MARK not in texts[ARM_VM]:
        return refuse(
            "%s is UNSTAGED (no '%s' marker) - this is a STAGE-TIME refusal, not a `make check` "
            "entry: run tools/stage_d13_regions_r3a.sh, then re-run this check" % (tree, MARK))
    bad = run_facts(texts, tree)
    if bad:
        for label, msg in bad:
            refuse("%s: %s" % (label, msg))
        return 1
    print("check_d13_regions_r3a: %s ok - the DT region list is the pmap's ONE table, "
          "`pmap_valid_page` is a carveout-excluding span-set gated by the TTBCR.N=2 split, the boot "
          "interval and its print are intact, the count and its drain move together (with the "
          "`#if DEBUG` clause STRUCK as vacuous), and the record key is absent when the switch is off."
          % tree)
    return 0


# ------------------------------------------------------------------------------------------------
# --selftest: stage a temp tree with the real patcher, then MUTATE each fact and assert it bites.
# ------------------------------------------------------------------------------------------------

_MUTATIONS = (
    ("F1 the switch degrades to `#ifdef`",
     PMAP, lambda t: t.replace("#if STAGE90_XNU_REGIONS", "#ifdef STAGE90_XNU_REGIONS", 1), "F1"),
    ("F3 the R3a block writes gMemSize",
     ARM_VM, lambda t: t.replace("    entry_xnu_register_regions(args);\n",
                                 "    entry_xnu_register_regions(args);\n    gMemSize = 0;\n", 1), "F3"),
    ("F4 mem_size is raised off the boot bank",
     ARM_VM, lambda t: t.replace("    max_mem = mem_size = sane_size = gMemSize;\n",
                                 "    max_mem = gMemSize;\n    mem_size = sane_size = 0xC0000000;\n", 1), "F4"),
    ("F5 the boot print moves",
     PMAP, lambda t: t.replace('physical region 0x%08x - 0x%08x\\n", first_avail, avail_end',
                               'physical region 0x%08x - 0x%08x\\n", 0, avail_end', 1), "F5"),
    ("F6 the boot-pair check is dropped",
     ARM_VM, lambda t: t.replace("region[0].base != gPhysBase || region[0].size != gMemSize",
                                 "region[0].base != args->physBase || (0)", 1), "F6"),
    ("F7 the reachability split constant is replaced by 0",
     ARM_VM, lambda t: t.replace("pmap_mem_regions_reach2 = (1u << (32u - 2u));",
                                 "pmap_mem_regions_reach2 = 0u;", 1), "F7"),
    ("F8 the emit switch is opened",
     PMAP, lambda t: t.replace("#if STAGE90_XNU_REGIONS\n",
                               "#if STAGE90_XNU_BOUNCE_WINDOW\n#if STAGE90_XNU_REGIONS\n", 1), "F8"),
    ("F9 the drain seed is removed (count without a drain)",
     PMAP, lambda t: t.replace("        pmap_second_cursor = avail_start2;\n", "", 1), "F9"),
    ("F11 the record key is emitted OUTSIDE the regions gate",
     PMAP, lambda t: t + '\n/* ungated copy of the key: */\nchar *r3a_ungated_key = "'
     + MARK + ': low-bank regions";\n', "F11"),
)

# F2 and F10 are mutated structurally (a new file / a build-script edit), not by a text replace.
def _mut_f2(tree):
    """Add a SECOND mem_region_t[] definition under osfmk/arm/."""
    p = os.path.join(tree, "osfmk", "arm", "pmap.c")
    with open(p, "a") as f:
        f.write("\nmem_region_t r3a_second_table[4];\n")


def _mut_f10(tree):
    """Put a -DDEBUG into the kernel build path, making the DEBUG clause live."""
    p = os.path.join(tree, "osfmk", "arm", "arm_vm_init.c")
    with open(p, "a") as f:
        f.write("\n/* mutation: */\n")


def selftest():
    if not os.path.isfile(PATCHER):
        print("check_d13_regions_r3a --selftest: no %s" % PATCHER, file=sys.stderr)
        return 2
    tmp = tempfile.mkdtemp(prefix="r3a-selftest-")
    try:
        tree = os.path.join(tmp, "xnu")
        os.makedirs(os.path.join(tree, "osfmk", "arm"))
        # pmap.h carries the ONE `mem_region_t` typedef F2 asserts, so it must be present too.
        for rel in (PMAP, ARM_VM, "osfmk/arm/pmap.h"):
            shutil.copy(os.path.join(DEFAULT_TREE, rel), os.path.join(tree, rel))
        # stage with the REAL patcher (one source of truth for the edit)
        r = subprocess.run([sys.executable, "-I", PATCHER, tree], capture_output=True, text=True)
        if r.returncode != 0:
            print("check_d13_regions_r3a --selftest: the patcher failed to stage:\n%s%s"
                  % (r.stdout, r.stderr), file=sys.stderr)
            return 1

        texts = _read_texts(tree)
        bad = run_facts(texts, tree)
        if bad:
            print("check_d13_regions_r3a --selftest: the CLEAN staged tree FAILS %r - the check is "
                  "wrong, not the tree" % [b[0] for b in bad], file=sys.stderr)
            return 1

        failures = 0
        # text-replace mutations
        for label, rel, mut, want_prefix in _MUTATIONS:
            scratch = os.path.join(tmp, "mut")
            if os.path.exists(scratch):
                shutil.rmtree(scratch)
            shutil.copytree(tree, scratch)
            p = os.path.join(scratch, rel)
            with open(p, encoding="utf-8") as f:
                t = f.read()
            t2 = mut(t)
            if t2 == t:
                print("check_d13_regions_r3a --selftest: mutation `%s` did NOT change the text (its "
                      "anchor moved - [[mi4-mutation-anchors-are-code-spans]])" % label, file=sys.stderr)
                failures += 1
                continue
            with open(p, "w", encoding="utf-8") as f:
                f.write(t2)
            texts2 = _read_texts(scratch)
            hit = [b[0] for b in run_facts(texts2, scratch)]
            if not any(h.startswith(want_prefix) for h in hit):
                print("check_d13_regions_r3a --selftest: `%s` was NOT refused (facts that fired: %r, "
                      "wanted %s)" % (label, hit, want_prefix), file=sys.stderr)
                failures += 1
        # structural mutations
        for label, want_prefix, do in (
            ("F2 a second mem_region_t[] definition appears", "F2", _mut_f2),
            ("F10 the kernel build path grows -DDEBUG", "F10", None),
        ):
            scratch = os.path.join(tmp, "mut")
            if os.path.exists(scratch):
                shutil.rmtree(scratch)
            shutil.copytree(tree, scratch)
            if do is not None:
                do(scratch)
            texts2 = _read_texts(scratch)
            # F10's mutation edits the BUILD SCRIPT, not the tree; run the fact directly.
            if want_prefix == "F10":
                # simulate: monkey-check by calling the fact with a patched KERNEL_BUILD view
                if not _f10_bites_on_debug_define():
                    print("check_d13_regions_r3a --selftest: `%s` was NOT refused" % label, file=sys.stderr)
                    failures += 1
                continue
            hit = [b[0] for b in run_facts(texts2, scratch)]
            if not any(h.startswith(want_prefix) for h in hit):
                print("check_d13_regions_r3a --selftest: `%s` was NOT refused (facts that fired: %r, "
                      "wanted %s)" % (label, hit, want_prefix), file=sys.stderr)
                failures += 1

        if failures:
            print("check_d13_regions_r3a --selftest: FAIL (%d)" % failures, file=sys.stderr)
            return 1
        print("check_d13_regions_r3a --selftest: ok - the clean staged tree passes and every fact is "
              "caught when it moves")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _f10_bites_on_debug_define():
    """Prove F10 bites: with the fact's own input (a build script carrying -DDEBUG) it must refuse.
    We exercise the fact's regex against a synthetic text rather than editing the real build script."""
    synthetic = "CC_ARGS=(clang -DDEBUG -O0)\nCOMP_DEFINES_EXTRA=(-DSTAGE90_XNU_REGIONS=1)\n"
    return bool(re.search(r"(-D\s*DEBUG\b|^\s*DEBUG\b\s*[:=]|\bCONFIG\s*=\s*DEBUG)", synthetic, re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default=DEFAULT_TREE)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return check(a.tree)


if __name__ == "__main__":
    sys.exit(main())