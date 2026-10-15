#!/usr/bin/env python3
"""1014 (FLUSHWITNESS): name WHICH TLB-flush arm ran for the faulting pmap, and under WHICH ASID/live TTBR0.

WHY, BY VALUE.  1013's press (arm `armed-d13-9cf2f15e`) read the leaf `pmap_enter_options` actually WROTE
and found it USER-WRITABLE (`_wwleaf`/`_wwapx=0` at `_wwl2pa`), yet the USER store still permission-faults
~2.1 M times (`far`/`pc`/`fsr=0x80f` constant - the 1009/1011/1012/1013 shape).  Every INPUT to the leaf
builder is correct by value and the store still faults.  The ONE mechanism nobody has read is the TLB:
did the flush that should invalidate the stale translation for THIS VA actually RUN, and under WHICH ASID?

THE FUNCTION THIS BRANCH IS ABOUT - `pmap_flush_tlbs` (`osfmk/arm/pmap.c`), read WHOLE by value:

    ttb = pmap->pm_l1_phys;                                       /* :1280 */
    pmap_is_shared = (pmap->pm_shared || (pmap == kernel_pmap));   /* :1282 */
    if (pmap_asid_ncpus) { pmap_asid_invalidate_all_cpus(pmap); }  /* :1287 */
    for each running cpu:
        uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;      /* :1295 - AT FLUSH TIME */
        if ((ttb == ttbr_pmap) || (pmap_is_shared)) {              /* :1298-1299 */
            if (cpu == my_cpu) { flush_self = TRUE; continue; }
        }
    if (flush_self) {
        if (pmap_asid_ncpus) {
            pmap_asid_validate_cpu(pmap, my_cpu);
            if (pmap_is_shared) arm_tlb_flushID();               /* :1317  kind = 1 (full) */
            else arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);     /* :1319  kind = 2 (ASID) */
        } else arm_tlb_flushID();                                /* :1322  kind = 1 */
    }

FIVE THINGS THE PROBE PUBLISHES, and the line each is read from: `ttb` (:1280), `pmap_is_shared` (:1282),
the live `armreg_ttbr_read() & 0xFFFFFF00` (:1295 - **read inside the cpu loop, AT FLUSH TIME, not at
enter time**), the runtime value of `pmap_asid_ncpus` (:1287/:1314), and **which of the three flush arms
ran** (:1317 full / :1319 ASID / neither).  `_wfkind` is the cell that decides: 2 = the ASID-scoped
invalidate, 1 = the full TLBIALL, 0 = **no flush ran at all** (the `:1298-1299` test was FALSE).

WHY A SOURCE EDIT AND NOT `--wrap=pmap_flush_tlbs`.  `--wrap` rewrites only linker-visible CROSS-object
calls; the call the storm's enter rides is INTRA-object.  MEASURED on the built object:

    arm-none-eabi-objdump -r out/xnu_kernel_obj_d13/osfmk_arm_pmap.o | grep -c pmap_flush_tlbs   -> 13
    (11 caller functions - pmap_page_protect, pmap_enter_options, pmap_unnest, pmap_remove_range,
     pmap_remove, pmap_protect, pmap_nest, pmap_init, pmap_expand, pmap_destroy, ... - ALL in this one
     object; `grep -rln pmap_flush_tlbs <tree>/osfmk/` -> arm/pmap.c ONLY)

A `--wrap` here would be a hook that cannot fire (`mi4-hooks-live-at-calls`).  So the call site MUST live
in `pmap.c` itself, which is a KERNEL-POOL file, so the switch goes to `tools/build_xnu_arm_kernel.sh`'s
`DEFINES` array - precisely where 1012's `STAGE90_XNU_WRSTRIP` goes (`:801`) - and the tree text is
inserted by THIS tool, shaped on `tools/patch_d13_wrstrip.py`.

**`wf_live`, NOT Apple's in-loop `ttbr_pmap` - A BUILD-SIDE FINDING THAT CONTRADICTS THE OBVIOUS
READING.**  Apple declares `ttbr_pmap` INSIDE the cpu loop's body (8-space indent, `:1295`), so at
function scope it is OUT OF SCOPE and a call there does not compile.  MEASURED on the first build:

    osfmk/arm/pmap.c:1356:46: error: use of undeclared identifier 'ttbr_pmap'

The gated, function-scope `static uint32_t wf_live` is the SAME READING carried down: it is assigned
the in-loop `armreg_ttbr_read() & 0xFFFFFF00` at the ONE place that read happens, and it is reset to
0 for the not-executed case (which `_wfseen` labels).  Apple's own declaration and the `:1298`
comparison are byte-identical, and on any call where the loop ran, `wf_live` holds exactly the value
that comparison used.

THE PROBE IS A PURE MEASUREMENT, AND THERE IS EXACTLY ONE CALL SITE.  The single call to the extern
`void entry_note_abt_wf(uint32_t va, uint32_t ttb, uint32_t live, uint32_t self, uint32_t shared,
                        uint32_t ncpus, uint32_t asid, uint32_t kind, uint32_t seen);`
sits at FUNCTION SCOPE, immediately before the `pmap == kernel_pmap` panic guard - i.e. AFTER the whole
flush decision.  It touches no control flow and no value.  The alternative (an enter probe plus an exit
probe) was REJECTED for two by-value reasons and is recorded here so it is not re-attempted:

  * `ttbr_pmap` is declared INSIDE the cpu loop's body (`:1295`, 8-space indent), so at function scope it
    is OUT OF SCOPE - an exit probe could not publish `_wflive` at all, which is one of the five cells
    the branch exists to read.  Hoisting the declaration would have edited an Apple statement and moved
    the loop's own by-value shape.
  * The design's §2.4 item 7 requires the image-wide `(b|bl)` count to `entry_note_abt_wf` be EXACTLY 1,
    which is also the anti-inline rule (`mi4-linked-code-order-is-not-live-artifacts`).  One call site is
    the shape that check certifies.

THE `_wfseen` CELL (the ninth argument), AND WHY IT EXISTS.  `ttbr_pmap` is read INSIDE the cpu loop, but
the loop's `continue` for a `!cpu_running` cpu happens BEFORE that read.  On a call where no cpu was
running the read never executes, so `_wflive` would carry the UNINITIALIZED local and `_wfself` would be
0 - a row INDISTINGUISHABLE from verdict cell F1 (H-FF CONFIRMED: "no flush ran"), when in truth no
decision was even reached.  It is kept by initializing `ttbr_pmap` to 0 at its declaration and setting a
gated `seen = 1` immediately AFTER the read; `_wfseen` = 1 means "the loop's TTBR read executed, so
`_wflive` is a reading and `_wfself = 0` is a DECISION".  That is the difference between a reading and a
null control, and it costs one cell (`mi4-measurement-defects`).  The reorder is by value a no-op: the
initializer is dead whenever the read executes, and it is not read at all until the loop exits.

SWITCH DISCIPLINE.  Every insertion is text inside `#if defined(STAGE90_XNU_FLUSH_WITNESS) &&
STAGE90_XNU_FLUSH_WITNESS ... #endif` (`defined()`-first spelling, `mi4-off-option-two-spellings`) -
INCLUDING the two added statements, which are each wrapped by their own gate - so switch OFF is this
tree's original bytes, byte for byte.  The tool is idempotent, and a present marker is NOT taken on trust:
every inserted form must be present EXACTLY ONCE or it refuses.  A wrong-whitespace anchor REFUSES rather
than no-ops.

THIS IS THE SOLE EDIT TO `pmap.c` THAT IS OURS: the tree carries FOUR `STAGE90` markers in this file
today (`:205`, `:2848`, `:3258`, `:3450`, all the 992/993 commpage-alias board gates), and NONE of them
is in `pmap_flush_tlbs` or touches the anchors below - every anchor was verified to occur EXACTLY ONCE in
the pristine tree before this tool was written.

**THE OFF HALF IS PROVED BY `cmp`, NOT BY READING THE `#if`.**  `--check` proves the payload is
STAGED; it says nothing about whether switch OFF really is Apple's bytes.  The proof is a pair of
compiles with the SAME command line and the switch OFF, one with the tree patched (the gate deletes
the insertions) and one with the tree REVERTED (`--revert`, Apple's text) - and `cmp` of the two
objects.  Byte-identical is the reading; this is what `scripts/build_1014_v1.sh` automates, and it is
the only form of the check that cannot be satisfied by a `#if` that gates the wrong region.

Usage:
    patch_d13_flushwitness.py <XNU_TREE>            apply (idempotent)
    patch_d13_flushwitness.py <XNU_TREE> --dry-run  print the planned insertions, write nothing
    patch_d13_flushwitness.py <XNU_TREE> --check    assert the INSERTED forms are staged (switch ON)
    patch_d13_flushwitness.py <XNU_TREE> --revert   reverse the insertions (Apple's bytes exactly)
"""
import sys

# The marker that proves this patch ran.  One spelling, checked in the file.
MARK = "1014: FLUSHWITNESS"

# The gate.  defined()-first so an unset switch can never be "on" (`mi4-off-option-two-spellings`).
_GATE_OPEN = "#if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS\n"
_GATE_CLOSE = "#endif /* defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS */\n"

# The ONE gated extern declaration.  Nine arguments, in the order the entry-side recorder declares them.
_DECL_SIG = ("extern void entry_note_abt_wf(uint32_t va, uint32_t ttb, uint32_t live, uint32_t self,\n"
             "                                     uint32_t shared, uint32_t ncpus, uint32_t asid,\n"
             "                                     uint32_t kind, uint32_t seen);\n")

# The ONE call, verbatim, at 4-space indent.  Kept as a single string so `--check` and the build script
# assert the SAME text (one spelling, `mi4-one-value-two-definitions`).
_CALL = ("    entry_note_abt_wf((uint32_t)startv, ttb, wf_live, flush_self, pmap_is_shared,\n"
         "                       pmap_asid_ncpus, (pmap->pm_asid & 0xFFu),\n"
         "                       (flush_self && pmap_asid_ncpus && !pmap_is_shared) ? 2u\n"
         "                           : ((flush_self && (pmap_is_shared || !pmap_asid_ncpus)) ? 1u : 0u),\n"
         "                       seen);\n")


# --- anchors, each verbatim BYTES (4-space indent, NO tabs - checked with `cat -A`).  Every `old`
#     occurs EXACTLY ONCE in the pristine tree; a wrong-whitespace anchor therefore REFUSES. ---

# 0) The one gated extern declaration, at file scope right after `#include "proc_reg.h"` (the last
#    include of pmap.c; the next non-blank line opens a comment) and before the first code.
_DECL_OLD = '#include "proc_reg.h"\n\n'
_DECL_NEW = (
    '#include "proc_reg.h"\n'
    '\n'
    '#if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS\n'
    '/* **1014: FLUSHWITNESS - the entry-stub recorder for the TLB-flush witness (docs/1014-design.md §2).\n'
    ' * Pure measurement: it returns void and is the ONLY new symbol the probes below reference.\n'
    ' * Switch OFF => this declaration and every probe vanish => Apple\'s bytes, byte for byte. */\n'
    + _DECL_SIG +
    '#endif /* defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS */\n'
    '\n'
)

# 1) THE `wf_live` + `seen` LOCALS - function scope, beside the decision variables they are read with.
#    `wf_live` is the in-loop TTBR0 reading carried down (Apple's `ttbr_pmap` is out of scope here);
#    `seen` says whether that read ever executed.  `static` keeps them out of the frame (this function
#    runs on every flush, including the 13-site storm); the explicit resets are what make them
#    per-CALL rather than per-process.  All four statements are gated.
_A0_OLD = (
    "    boolean_t   pmap_is_shared = (pmap->pm_shared || (pmap == kernel_pmap));\n"
)
_A0_NEW = (
    "    boolean_t   pmap_is_shared = (pmap->pm_shared || (pmap == kernel_pmap));\n"
    "    #if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS\n"
    f"    /* **{MARK}: `wf_live` + `seen`.  `wf_live` is the cpu loop's `armreg_ttbr_read()` (`:1295`) - the\n"
    "     * reading the `:1298` comparison is made against - carried to function scope, because Apple's\n"
    "     * own `ttbr_pmap` is declared inside the loop body and is out of scope at the probe (MEASURED:\n"
    "     * 'use of undeclared identifier \'ttbr_pmap\'' on the first build).  `seen` is published as\n"
    "     * `_wfseen`: without it a row where NO cpu was running (the loop's `continue` runs BEFORE that\n"
    "     * read) carries `_wflive` = 0 and `_wfself` = 0 - indistinguishable from verdict cell F1 \"no\n"
    "     * flush ran\", when no decision was even reached.  `static` gives them the whole call's\n"
    "     * lifetime (this function is on the storm's hottest path); the resets are what make them\n"
    "     * per-call.  All four statements are gated, so OFF declares neither. */\n"
    "    static uint32_t wf_live;\n"
    "    static uint32_t seen;\n"
    "    wf_live = 0u;\n"
    "    seen = 0u;\n"
    "    #endif /* defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS */\n"
)

# 2) THE CARRY-DOWN.  Apple's line `uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;` stays
#    BYTE-IDENTICAL in the `#else` arm; the gated arm adds one assignment beside it, so `wf_live` holds
#    the same value the `:1298` test compares.  `seen = 1u` immediately after says the read executed.
_A1_OLD = "        uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;\n"
_A1_NEW = (
    "        #if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS\n"
    "        uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;\n"
    "        wf_live = ttbr_pmap;\n"
    "        seen = 1u;\n"
    "        #else\n"
    "        uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;\n"
    "        #endif /* defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS */\n"
)

# 3) THE ONE PROBE - function scope, after the whole flush decision and before the kernel_pmap panic
#    guard.  `kind` is a nested ternary whose branches are the SAME reads, in the SAME order, as
#    `:1313-1322`: it OBSERVES the branch that ran, it does not choose it (the flush arms themselves are
#    untouched - `--check` asserts the two `mcr`-driving calls and the panic guard are intact).
_A2_OLD = (
    "    if (__improbable((pmap == kernel_pmap) && (flush_self != TRUE))) {\n"
)
_A2_NEW = (
    "    #if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS\n"
    f"    /* **{MARK}: pure measurement (docs/1014-design.md §2) - observe the flush decision, change\n"
    "     * NOTHING.  `_wfkind` (the last argument but one) is the cell that decides: 0 = no flush ran\n"
    "     * (the `:1298-1299` test was FALSE), 1 = the full `arm_tlb_flushID`, 2 = `arm_tlb_flushID_ASID`.\n"
    "     * `_wflive` is the in-loop `armreg_ttbr_read()` (`:1295`), read AT FLUSH TIME - the value the\n"
    "     * `:1298` comparison was actually made against.  `_wfseen` = 0 makes the row a null control. */\n"
    + _CALL +
    "    #endif /* defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS */\n"
    "    if (__improbable((pmap == kernel_pmap) && (flush_self != TRUE))) {\n"
)

# (relative path, [(old, new), ...]).  One file, four edits plus the declaration.
EDITS = (
    ("osfmk/arm/pmap.c", (
        (_DECL_OLD, _DECL_NEW),
        (_A0_OLD,   _A0_NEW),
        (_A1_OLD,   _A1_NEW),
        (_A2_OLD,   _A2_NEW),
    )),
)

REL = EDITS[0][0]
PAIRS = EDITS[0][1]


def _read(tree):
    path = tree.rstrip("/") + "/" + REL
    try:
        with open(path, encoding="utf-8") as f:
            return path, f.read()
    except FileNotFoundError:
        print(f"patch_d13_flushwitness: no {path}", file=sys.stderr)
        return path, None


def patch_text(text, pairs, path):
    """Return (new_text, changed).  Idempotent, and a present marker is NOT taken on trust: every
    inserted form must be present exactly once, or this refuses rather than certifying a tree the
    switch does not actually carry (the house rule in tools/patch_d13_wrstrip.py)."""
    if MARK in text:
        for _old, new in pairs:
            if text.count(new) != 1:
                raise SystemExit(
                    f"patch_d13_flushwitness: the 1014 marker is present in {path} but an inserted form "
                    f"occurs {text.count(new)} times - moved by a hand edit, or half-applied. "
                    "Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    # Refuse if ANY anchor is not present EXACTLY once (wrong revision, or edited by hand).
    for i, (old, _new) in enumerate(pairs):
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_flushwitness: anchor #{i} in {path} occurs {n} times, not once - the tree "
                f"is not the expected revision, or it was edited by hand (a wrong-whitespace anchor "
                f"refuses, never no-ops). Anchor begins: {old[:60]!r}. Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(
                f"patch_d13_flushwitness: an inserted form did not land once in {path} - refusing to write"
            )
    return text, True


def apply(tree):
    path, text = _read(tree)
    if text is None:
        return 2
    new, changed = patch_text(text, PAIRS, REL)
    if not changed:
        print(f"patch_d13_flushwitness: already applied ({path})")
        return 0
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"patch_d13_flushwitness: pmap_flush_tlbs now publishes (va, ttb, live, self, shared, ncpus, "
          f"asid, kind, seen) at the exit of its flush decision, behind STAGE90_XNU_FLUSH_WITNESS "
          f"({path})")
    return 0


def dry_run(tree):
    path, text = _read(tree)
    if text is None:
        return 2
    print(f"patch_d13_flushwitness --dry-run: would patch {path}")
    for i, (old, new) in enumerate(PAIRS):
        print(f"  ---- anchor #{i} present exactly once: {text.count(old) == 1} "
              f"(count={text.count(old)}) ----")
        # Show only the inserted text: strip the anchor's own lines from both ends.
        old_lines = old.splitlines(keepends=True)
        added = [ln for ln in new.splitlines(keepends=True) if ln not in old_lines]
        sys.stdout.write("".join("  | " + ln for ln in added))
    state = "STAGED" if MARK in text else "APPLE/OFF"
    print(f"  ---- marker present: {MARK in text} (state: {state}) ----")
    return 0


def check(tree):
    """The --check proof: the INSERTED forms are staged.  The OFF-identity (Apple's bytes) is the other
    half, proven by a build + cmp; this half proves the switch's payload is actually in the tree."""
    path, text = _read(tree)
    if text is None:
        return 2
    ok = True
    if MARK not in text:
        print(f"  FAIL {path}: no '{MARK}' marker - the switch was never staged")
        ok = False
    else:
        print(f"  ok   {path}: '{MARK}' staged")
    for needle, what, want in (
        ("#if defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS",
         "the STAGE90_XNU_FLUSH_WITNESS #if gate", 4),
        ("#endif /* defined(STAGE90_XNU_FLUSH_WITNESS) && STAGE90_XNU_FLUSH_WITNESS */",
         "the matching #endif", 4),
        ("    static uint32_t wf_live;\n", "the `wf_live` declaration (the carried in-loop reading)", 1),
        ("    static uint32_t seen;\n", "the `seen` declaration (the null-control cell)", 1),
        ("    seen = 0u;\n", "the per-call `seen` reset", 1),
        ("        wf_live = ttbr_pmap;\n", "the `wf_live` carry-down at the in-loop TTBR read", 1),
        ("        seen = 1u;\n", "the `seen` set immediately after the in-loop TTBR read", 1),
        ("extern void entry_note_abt_wf(uint32_t va, uint32_t ttb, uint32_t live, uint32_t self,",
         "the entry_note_abt_wf declaration", 1),
        (_CALL, "the ONE entry_note_abt_wf call site", 1),
        ("    wf_live = 0u;\n", "the per-call `wf_live` reset", 1),
    ):
        n = text.count(needle)
        if n == want:
            print(f"  ok   {what} present ({n}x)")
        else:
            print(f"  FAIL {what} count {n}x, want {want}x ({needle!r})")
            ok = False
    # The probe must OBSERVE the flush decision, never CHOOSE it: both flush primitives must still be
    # present at their own call sites, the panic guard must still be the original expression, and the
    # loop's own comparison must be byte-identical.
    for needle, what in (
        ("            if (pmap_is_shared)\n"
         "                arm_tlb_flushID();\n",
         "the :1317 full-TLBIALL arm"),
        ("                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n",
         "the :1319 ASID-scoped arm"),
        ("        else\n"
         "            arm_tlb_flushID();\n",
         "the :1322 full-TLBIALL fallback"),
        ("        if ((ttb == ttbr_pmap) ||\n"
         "            (pmap_is_shared)) {\n",
         "the :1298-1299 flush_self test"),
        ('        panic("pmap_flush_tlbs: pmap == kernel_pmap && flush_self != TRUE");\n',
         "the kernel_pmap panic guard"),
        ("    if (pmap_asid_ncpus) {\n"
         "        pmap_asid_invalidate_all_cpus(pmap);\n",
         "the :1287 asid-invalidate arm"),
    ):
        n = text.count(needle)
        if n == 1:
            print(f"  ok   {what} intact ({needle.splitlines()[0].strip()[:48]!r})")
        else:
            print(f"  FAIL {what} occurs {n}x, want 1x ({needle.splitlines()[0].strip()[:48]!r})")
            ok = False
    # The switch's payload must be INSERTED, never substituted for Apple's line.  The pristine
    # `ttbr_pmap` declarator is byte-identical in BOTH arms of the new `#if`/`#else`, so it must be
    # present EXACTLY TWICE - and the `#if` arm's twin is what `wf_live` is assigned from.
    if text.count(_A1_OLD) == 2:
        print("  ok   Apple's own `ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;` is preserved verbatim "
              "in BOTH arms (x2) - the switch adds, it does not replace")
    else:
        print(f"  FAIL Apple's `ttbr_pmap` declarator occurs {text.count(_A1_OLD)}x, want 2x (one per "
              "arm of the switch's #if/#else) - the switch replaced or moved it")
        ok = False
    print(f"patch_d13_flushwitness --check {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def revert(tree):
    """Reverse every insertion, restoring Apple's bytes exactly.  Refuses rather than half-reverting:
    every inserted form must be present exactly once BEFORE anything is written."""
    path, text = _read(tree)
    if text is None:
        return 2
    if MARK not in text:
        print(f"patch_d13_flushwitness: nothing to revert in {path}")
        return 0
    for _old, new in PAIRS:
        if text.count(new) != 1:
            raise SystemExit(
                f"patch_d13_flushwitness: an inserted form occurs {text.count(new)} times in {path} - "
                "refusing to revert a tree that is not exactly the staged one"
            )
    for old, new in PAIRS:
        text = text.replace(new, old, 1)
    for _old, new in PAIRS:
        if new in text:
            raise SystemExit(
                f"patch_d13_flushwitness: an inserted form survived the revert in {path} - refusing to "
                "write a half-reverted tree"
            )
    if MARK in text:
        raise SystemExit(
            f"patch_d13_flushwitness: the 1014 marker survived the revert in {path} - refusing to write"
        )
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"patch_d13_flushwitness: reverted - {path} is Apple's text again ({len(PAIRS)} anchors)")
    return 0


def main():
    argv = sys.argv[1:]
    flags = {a for a in argv if a.startswith("--")}
    rest = [a for a in argv if not a.startswith("--")]
    if len(rest) != 1:
        print("usage: patch_d13_flushwitness.py <XNU_TREE> [--dry-run|--check]", file=sys.stderr)
        return 2
    tree = rest[0]
    if "--check" in flags:
        return check(tree)
    if "--revert" in flags:
        return revert(tree)
    if "--dry-run" in flags:
        return dry_run(tree)
    return apply(tree)


if __name__ == "__main__":
    raise SystemExit(main())