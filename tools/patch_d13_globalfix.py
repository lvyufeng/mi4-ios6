#!/usr/bin/env python3
"""patch_d13_globalfix.py - stage rung 1015b's ONE edit into the D13 tree, behind its own switch.

1015b IS THE BEHAVIOUR HALF OF 1015 (docs/1015-design.md §2b), AND IT IS A SOURCE EDIT INTO THE
KERNEL POOL, so `build_entry.sh` cannot carry it: that script only LINKS
`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`.  The switch must reach
`tools/build_xnu_arm_kernel.sh`'s define set - which this script's caller does through BOTH
`XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_GLOBAL_FIX=1` AND the exported variable of the same name
([[mi4-one-value-two-definitions]]).

WHAT IT CHANGES, AND WHY IT IS ONE EDIT.

    osfmk/arm/pmap.c, `pmap_flush_tlbs`, the arm that follows the `pmap_is_shared` test:

        if (flush_self) {
            if (pmap_asid_ncpus) {
                pmap_asid_validate_cpu(pmap, my_cpu);
                if (pmap_is_shared)
                    arm_tlb_flushID();
                else                                  <-- ONE ARM INSERTED HERE
                    arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);
            }

    staged, with the switch ON:

                if (pmap_is_shared)
                    arm_tlb_flushID();
    #if defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX
                else if (pmap != kernel_pmap)
                    arm_tlb_flushID();
    #endif /* defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX */
                else
                    arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);

    One comparison, one call.  **Apple's `if (pmap_is_shared)` TEST AND ITS FULL-TLBIALL ARM ARE
    NOT TOUCHED - they are byte-identical in both switch positions**, and the ASID statement is still
    the terminal `else`.  The inserted condition is deliberately spelled out rather than left bare,
    and it is TAUTOLOGICAL in this position: the `else` is reached only when `pmap_is_shared` is
    FALSE, and `pmap_is_shared` is `pmap->pm_shared || (pmap == kernel_pmap)` (pmap.c:1291), so
    `pmap != kernel_pmap` already holds.  It is written anyway because it is the CONDITION THE
    HYPOTHESIS NAMES ("for a NON-KERNEL pmap take the full flush") - the reason for the edit is
    readable at the edit site, and a reader is not left to derive it from the negated test above
    ([[mi4-a-claim-in-a-comment-is-not-a-check]] runs the other way here: a check that states its own
    predicate can be read; one that hides it in a negation cannot).  The build asserts the tautology
    is what reaches the object: the honest measure of this switch is that the ASID arm becomes
    UNREACHABLE for non-kernel pmaps, not that a new decision appeared.

THE MECHANISM (H-G, records/1014-press-read.txt §7; docs/1015-design.md):
  * EVERY user leaf this port emits is GLOBAL - `L2_NG_BIT 0x800` (osfmk/arm/pmap.h:231) is never
    ORed anywhere in the tree.  The storm's own walked leaf is `0x841e91bb`: bit11 = 0, i.e.
    nG = 0 = GLOBAL.
  * On ARMv7, `TLBIASID` (the `arm_tlb_flushID_ASID` arm, `mcr p15,0,rX,c8,c7,2`) invalidates ONLY
    ASID-tagged entries.  A GLOBAL entry SURVIVES it; only `TLBIALL` (`c8,c7,0`) evicts one.
  * The only steady-state flush the storm rides is that ASID arm - 1014 MEASURED it (`_wfkind=2`,
    with `_wfasid == _wctx`), and `pmap_switch` performs a full TLBIALL only on a pmap CHANGE.
  => a stale GLOBAL translation of the storm's VA can survive every ASID-scoped flush and SHADOW
     the newer user-RW leaf.

THE PRE-REGISTERED DECISIVE TEST (tools/check_1015b_capture.py, whose cells this rung does not
redefine): storm STOPPED => `G-CONFIRMED`; storm PERSISTS at the same `far`/`pc`/`fsr` =>
`G-REFUTED`, and 1015's §3b permission-inputs ladder runs.  A truncated / key-missing capture =>
`G-WITHHOLD`, which is never a negative.

**WHY A SEPARATE TOOL AND NOT A FIFTH INSERTION IN `tools/patch_d13_flushwitness.py`**
(docs/1015-design.md §2b prescribes the extension; this rung does the opposite, and the reason is
measured):

  1. THE 1014 TOOL'S `--check` WOULD FAIL ON A CORRECT 1015b TREE.  It asserts, BY SUBSTRING, that
     `"                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n"` occurs and calls that clause
     "the :1319 ASID-scoped arm" (`patch_d13_flushwitness.py:333-337`).  1015b makes that arm
     CONDITIONAL, and the four-space displaced copy inside the new `#if` arm does not restore the
     count - so the 1014 tool's own check would reject the tree it must validate.  A check that
     fails for the wrong reason is not a check ([[mi4-measurement-defects]]).
  2. ITS `--revert` WOULD UNDO THIS EDIT.  `revert()` replaces each of the four inserted forms with
     Apple's text and then refuses if the 1014 marker survives.  A 1015b insertion nested inside the
     1014 payload's neighbour region would be collaterally removed, or the tool would refuse and
     leave the tree half-staged - and the 1015 build's own OFF proof calls `--revert` in the middle
     of its run (`scripts/build_1015.sh:832`).
  3. SEPARATION IS THE POINT: with two tools, `--check`/`--revert` remain ONE-EDIT operations with a
     single owner each, and each rung's payload can be staged, checked and reverted without the
     other's semantics in the way.

**IDEMPOTENT, AND IT REFUSES RATHER THAN HALF-APPLYING.**  Every anchor must occur EXACTLY ONCE
before anything is written.  The anchor is the FOUR-LINE block `if (pmap_is_shared)` /
`arm_tlb_flushID()` / `else` / `arm_tlb_flushID_ASID(...)`, not the bare ASID statement: the
statement alone is also a substring of the block's own tail, and anchoring on a prefix is how a
`replace(..., 1)` silently deletes the suffix ([[mi4-an-edit-on-a-prefix-deletes-the-suffix]]).
Because the insertion is placed BETWEEN Apple's `arm_tlb_flushID();` and Apple's `else`, the block
above is not the text being replaced - it is REBUILT, and `--check` asserts both halves are back
(see `_A1_TAIL` below).

    patch_d13_globalfix.py <XNU_TREE>              stage the insertion
    patch_d13_globalfix.py <XNU_TREE> --dry-run    print the planned insertion, write nothing
    patch_d13_globalfix.py <XNU_TREE> --check      assert the INSERTED form is staged (switch ON)
    patch_d13_globalfix.py <XNU_TREE> --revert     reverse the insertion (Apple's bytes exactly)
    patch_d13_globalfix.py <XNU_TREE> --anchors    print the anchor census this tool refuses on
"""
import sys

MARK = "1015b: GLOBALFIX"

SWITCH = "STAGE90_XNU_GLOBAL_FIX"

# The gate, spelled `defined()`-first, the tree's own convention.
_GATE_OPEN = f"#if defined({SWITCH}) && {SWITCH}\n"
_GATE_CLOSE = (f"#endif /* defined({SWITCH}) && {SWITCH} */\n")

# ----------------------------------------------------------------------------------------------------
# THE ANCHOR SET - ONE anchor, because this is ONE edit.  It is the FOUR-LINE inner block, not the
# bare statement: the statement alone is also a SUBSTRING of the replacement above it (the displaced
# copy at twenty-four spaces), and a bare-statement anchor would be a prefix edit that deletes the
# `else` ([[mi4-an-edit-on-a-prefix-deletes-the-suffix]]).
# ----------------------------------------------------------------------------------------------------
_A0_OLD = (
    "            if (pmap_is_shared)\n"
    "                arm_tlb_flushID();\n"
    "            else\n"
    "                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n"
)

# The stem is Apple's test and its arm, IDENTICAL in both switch positions and hence not an anchor
# that moves: it is the text the insertion is placed AFTER.  `_A1_TAIL` is Apple's terminal pair, the
# text the insertion is placed BEFORE, and it is BYTE-IDENTICAL to the `_A0_OLD` tail - which is why
# the anchor could not be the bare ASID statement.
_A0_STEM = ("            if (pmap_is_shared)\n"
            "                arm_tlb_flushID();\n")
_A1_TAIL = ("            else\n"
            "                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n")

_INSERT = (
    "            /* **" + MARK + ": for a NON-KERNEL pmap take the FULL TLBIALL, not the ASID-scoped\n"
    "             * TLBIASID (docs/1015-design.md §2b; H-G, records/1014-press-read.txt §7).  Every\n"
    "             * user leaf this port emits is GLOBAL - `L2_NG_BIT 0x800` (osfmk/arm/pmap.h:231) is\n"
    "             * never ORed anywhere in the tree, and the storm's own walked leaf 0x841e91bb has\n"
    "             * bit11 = 0 - so an ASID-tagged invalidation cannot evict it.  The condition below is\n"
    "             * TAUTOLOGICAL in this position (this `else` is reached only when `pmap_is_shared`,\n"
    "             * which is `pmap->pm_shared || (pmap == kernel_pmap)`, is FALSE) and is written out\n"
    "             * anyway: it is the hypothesis's own predicate, and the honest reading of this\n"
    "             * switch is that the ASID arm becomes UNREACHABLE for non-kernel pmaps. */\n"
    f"{_GATE_OPEN}"
    "            else if (pmap != kernel_pmap)\n"
    "                arm_tlb_flushID();\n"
    f"{_GATE_CLOSE}"
)

_A0_NEW = _A0_STEM + _INSERT + _A1_TAIL

# The forms `--check` requires once staged.  Apple's stem and tail are NOT here: they are byte-
# identical in both switch positions, which is the claim, and `--check` asserts that claim directly
# below rather than through a count of the staged form.
_INSERTED = ((_INSERT, "the 1015b GLOBAL-FIX insertion (the `else if (pmap != kernel_pmap)` arm "
                       "between Apple's full-flush arm and Apple's ASID else)", 1),)

# THE PURITY SET - everything this rung must NOT have moved.  Each is asserted present EXACTLY ONCE,
# by substring, and each is a thing a careless edit into this neighbourhood would break.
_PURITY = (
    ("    if (flush_self) {\n", "the `if (flush_self) {` that opens the arm", 1),
    ("    if (pmap_asid_ncpus) {\n"
     "        pmap_asid_invalidate_all_cpus(pmap);\n", "the :1287 asid-invalidate arm", 1),
    ("        if ((ttb == ttbr_pmap) ||\n"
     "            (pmap_is_shared)) {\n", "the :1303-1304 flush_self test", 1),
    ('        panic("pmap_flush_tlbs: pmap == kernel_pmap && flush_self != TRUE");\n',
     "the kernel_pmap panic guard", 1),
    ("        else\n"
     "            arm_tlb_flushID();\n", "the outer :1352 full-TLBIALL fallback", 1),
)


def _path(tree):
    import os
    return os.path.join(tree, "osfmk", "arm", "pmap.c")


def _read(tree):
    path = _path(tree)
    try:
        with open(path, "r") as fh:
            return path, fh.read()
    except FileNotFoundError:
        print(f"  FAIL {path}: not found (D13 tree not provisioned?)")
        return path, None


def anchor_census(tree):
    """Print the anchor census and refuse unless every anchor is UNIQUE.

    Uniqueness is the property this tool depends on and the one a later tree edit can silently take
    away: with two occurrences, `str.replace(..., 1)` would edit whichever came first, which is not a
    decision this tool makes ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
    """
    path, text = _read(tree)
    if text is None:
        return 2
    ok = True
    # Apple's whole block, CONTIGUOUS: the text `patch()` splits.  It is 1x only while UNSTAGED -
    # staging places the arm between its halves, so the staged tree reports 0 here and that is the
    # expected reading, not a failure.  (This tool's first draft asserted 1x unconditionally and
    # therefore FAILED on the tree it had just correctly staged -
    # [[mi4-measurement-defects]]: the expected number, not the code, was the defect.)
    n = text.count(_A0_OLD)
    want = 0 if MARK in text else 1
    print(f"  {'ok  ' if n == want else 'FAIL'} the :1346-1349 contiguous `pmap_is_shared`/ASID "
          f"block: {n}x (want {want} - {'staged, so the insertion has split it' if want == 0 else 'unstaged'})")
    ok = ok and n == want
    # The two halves the insertion is placed between.  EACH is byte-identical in both switch
    # positions - this is the reading that makes "Apple's test and its full-flush arm are untouched"
    # a measurement rather than a claim, and it is what `patch()` anchors on.
    for needle, what in ((_A0_STEM, "Apple's test + full-TLBIALL arm (the insertion is placed AFTER it)"),
                         (_A1_TAIL, "Apple's `else` + ASID statement (the insertion is placed BEFORE it)")):
        k = text.count(needle)
        print(f"  {'ok  ' if k == 1 else 'FAIL'} {what}: {k}x (want 1, in BOTH switch positions)")
        ok = ok and k == 1
    n_ins = text.count(_INSERT)
    print(f"  {'ok  ' if n_ins == (1 if MARK in text else 0) else 'FAIL'} the insertion: {n_ins}x "
          f"(want {1 if MARK in text else 0} - {'staged' if MARK in text else 'unstaged'})")
    ok = ok and n_ins == (1 if MARK in text else 0)
    print(f"  {'ok  ' if (MARK in text) else '--  '} marker '{MARK}' "
          f"{'present (STAGED)' if MARK in text else 'absent (APPLE/OFF)'}")
    print(f"patch_d13_globalfix --anchors {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def patch(tree):
    """Stage the ONE insertion.  Refuses unless the anchor is UNIQUE and the tree is not already
    staged (idempotent: a second run re-checks and prints the same summary)."""
    path, text = _read(tree)
    if text is None:
        return 2
    if MARK in text:
        print(f"patch_d13_globalfix: already staged in {path} (marker '{MARK}' present) - verifying")
        return check(tree)
    n = text.count(_A0_OLD)
    if n != 1:
        raise SystemExit(
            f"patch_d13_globalfix: the anchor occurs {n} times in {path}, want exactly 1 - refusing "
            "to stage (with two candidates, `replace(..., 1)` edits whichever came first, and that is "
            "not a decision this tool is allowed to make)"
        )
    text = text.replace(_A0_OLD, _A0_NEW, 1)
    if MARK not in text:
        raise SystemExit("patch_d13_globalfix: the marker did not survive the substitution - refusing")
    with open(path, "w") as fh:
        fh.write(text)
    print(f"patch_d13_globalfix: staged the 1015b GLOBAL-FIX arm in {path}")
    print(f"patch_d13_globalfix: NOW RE-RUN tools/build_xnu_arm_kernel.sh with "
          f"{SWITCH}=1 in the ENVIRONMENT and -D{SWITCH}=1 in XNU_KERNEL_EXTRA_DEFINES "
          f"(BOTH, or the two spellings can diverge - mi4-one-value-two-definitions)")
    return check(tree)


def dry_run(tree):
    path, text = _read(tree)
    if text is None:
        return 2
    if MARK in text:
        print(f"patch_d13_globalfix --dry-run: {path} is ALREADY staged (marker '{MARK}' present) - "
              "nothing would be written; run --check to verify or --revert to undo")
        return 0
    n = text.count(_A0_OLD)
    print(f"patch_d13_globalfix --dry-run: {path} (anchor occurs {n}x, want 1)")
    print("  ---- would be REPLACED by ----")
    for line in _A0_NEW.splitlines():
        print("  +" + line)
    return 0 if n == 1 else 1


def check(tree):
    """The --check proof: the INSERTED form is staged, and nothing this rung must not touch moved.

    This is the tree-side half.  The OFF-identity half (switch 0 is Apple's bytes) is proven by a
    rebuild + `cmp` in `scripts/build_1015b.sh`, because a `#if` can gate the wrong region and still
    make this half green ([[mi4-a-claim-in-a-comment-is-not-a-check]])."""
    path, text = _read(tree)
    if text is None:
        return 2
    ok = True
    if MARK not in text:
        print(f"  FAIL {path}: no '{MARK}' marker - the 1015b edit was never staged")
        ok = False
    else:
        print(f"  ok   {path}: '{MARK}' staged")
    for needle, what, want in _INSERTED + (
        (_GATE_OPEN, f"the {SWITCH} #if gate", 1),
        (_GATE_CLOSE, "the matching #endif", 1),
        ("            else if (pmap != kernel_pmap)\n", "the widened arm's test", 1),
    ):
        n = text.count(needle)
        if n == want:
            print(f"  ok   {what} present ({n}x)")
        else:
            print(f"  FAIL {what} count {n}x, want {want}x ({needle.splitlines()[0][:60]!r})")
            ok = False
    for needle, what, want in _PURITY:
        n = text.count(needle)
        if n == want:
            print(f"  ok   {what} intact ({n}x)")
        else:
            print(f"  FAIL {what} occurs {n}x, want {want}x ({needle.splitlines()[0].strip()[:48]!r})")
            ok = False
    # **APPLE'S OWN TEST AND ITS FULL-TLBIALL ARM ARE BYTE-IDENTICAL IN BOTH SWITCH POSITIONS, AND
    # THIS IS THE CLAUSE THAT SAYS SO.**  The insertion sits BETWEEN them, so what must be asserted is
    # not a count of a staged form but the presence of BOTH halves - each exactly once, unchanged.
    # (`_A0_OLD`, the contiguous four-line block, is 0x in the staged tree BY CONSTRUCTION; asserting
    # it here would re-create the defect the anchor set exists to avoid.)
    for needle, what in ((_A0_STEM, "Apple's `if (pmap_is_shared)` + its full-TLBIALL arm"),
                         (_A1_TAIL, "Apple's `else` + the ASID-scoped statement")):
        n = text.count(needle)
        if n == 1:
            print(f"  ok   {what} is byte-identical (x1) - the switch ADDS an arm, it does not "
                  "replace Apple's")
        else:
            print(f"  FAIL {what} occurs {n}x, want 1x ({needle.splitlines()[0].strip()[:48]!r}) - "
                  "the switch replaced or moved Apple's own arm")
            ok = False
    # ...and the thing the switch BUYS has to be readable in the text rather than inferred: the widened
    # arm's body is exactly the one call.  A tree-wide `arm_tlb_flushID()` count would be measuring
    # other callers too ([[mi4-measurement-defects]]).
    if text.count("            else if (pmap != kernel_pmap)\n"
                  "                arm_tlb_flushID();\n") == 1:
        print("  ok   the widened arm's body is the ONE call `arm_tlb_flushID();` (the full TLBIALL)")
    else:
        print("  FAIL the widened arm's body is not `arm_tlb_flushID();` exactly once - the arm does "
              "not take the full flush the hypothesis names")
        ok = False
    print(f"patch_d13_globalfix --check {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def revert(tree):
    """Reverse the insertion, restoring Apple's bytes exactly.

    Refuses rather than half-reverting: the inserted form must be present EXACTLY ONCE before
    anything is written, so a tree that is not exactly this tool's output is left untouched."""
    path, text = _read(tree)
    if text is None:
        return 2
    if MARK not in text:
        print(f"patch_d13_globalfix: nothing to revert in {path}")
        return 0
    n = text.count(_A0_NEW)
    if n != 1:
        raise SystemExit(
            f"patch_d13_globalfix: the inserted form occurs {n} times in {path} - refusing to revert "
            "a tree that is not exactly the staged one"
        )
    text = text.replace(_A0_NEW, _A0_OLD, 1)
    if MARK in text or _GATE_OPEN in text:
        raise SystemExit(
            f"patch_d13_globalfix: the 1015b marker or gate survived the revert in {path} - "
            "refusing to write a half-reverted tree"
        )
    if text.count(_A0_OLD) != 1:
        raise SystemExit(
            f"patch_d13_globalfix: after the revert the Apple block occurs "
            f"{text.count(_A0_OLD)} times in {path}, want 1 - refusing to write"
        )
    with open(path, "w") as fh:
        fh.write(text)
    print(f"patch_d13_globalfix: reverted {path} to the tree's original text")
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[-8].strip(), file=sys.stderr)
        print(f"usage: {argv[0]} <XNU_TREE> [--dry-run|--check|--revert|--anchors]", file=sys.stderr)
        return 2
    tree = argv[1]
    flags = set(argv[2:])
    if "--anchors" in flags:
        return anchor_census(tree)
    if "--check" in flags:
        return check(tree)
    if "--revert" in flags:
        return revert(tree)
    if "--dry-run" in flags:
        return dry_run(tree)
    return patch(tree)


if __name__ == "__main__":
    sys.exit(main(sys.argv))