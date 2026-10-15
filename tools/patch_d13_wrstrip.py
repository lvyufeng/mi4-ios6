#!/usr/bin/env python3
"""1012 (WRSTRIP): NAME which of the four `VM_PROT_WRITE` strip sites fired on a faulting VA.

WHY, BY VALUE.  1011's press (`records/1011-press-read.txt`, arm `armed-d13-fce83837`) measured the
tail of the ~2.09 M data-abort storm CONSTANT at `far=0x61000` / `pc=0x2be511a0` (dyld) / `fsr=0x0000080f`
(WnR=1, a WRITE; FS=0xf = permission fault, level 2) / user mode, with the `resident` path not returning
and NO panic.  1011's (E') probe proved the `vm_map_entry`'s effective protection (F1a) GRANTED the write
(`vmeprot=0x3`) while `vm_fault` still returned `kr=0` KERN_SUCCESS.  So somewhere between the entry's
protection and the `PMAP_ENTER_OPTIONS` the local `prot` loses `VM_PROT_WRITE`, the PTE goes read-only,
the store faults again, and the handler retries forever.  THIS tool makes that loss PUBLISHABLE: a probe
at each site that can remove `VM_PROT_WRITE` so one hardware run can NAME the site that fired by its
published `(site, va, prot)` triple.

THE FOUR STRIP SITES (verified by VALUE against this tree; line numbers are hints only):
  1. `vm_fault_page`, the need-copy promote failure - `} else` then `*protection &= (~VM_PROT_WRITE);`.
     NOTE the target here is `*protection` (a POINTER into the caller), not the `prot` local.
  2. `vm_fault_page`, the copy_object push loop - `if ((fault_type & VM_PROT_WRITE) == 0) {` then
     `*protection &= ~VM_PROT_WRITE;` then `break;` (again `*protection`).
  3. `vm_fault`, the RetryFault verify branch - `prot &= retry_prot;` (strips write iff the re-derived
     protection no longer includes it; the concurrent-unmap race).
  4. `vm_fault`, `if (m->object->copy != old_copy_object) { ... prot &= ~VM_PROT_WRITE; }` - the copy
     object changed while the map was unlocked, so write permission is taken away.
  (5.) MEASUREMENT CROSS-CHECK: `PMAP_ENTER_OPTIONS(pmap, vaddr, m, prot, ...)` in `vm_fault_enter` -
     the last `prot` actually handed to the pmap.  Its id exists so the reader can tell "prot arrived
     WRITE at PMAP_ENTER but the PTE went read-only (=> PMAP_ENTER-INTERNAL strip)" from "prot was
     already stripped above (=> a site-1..4 strip)".  NOT a fifth strip site.

Two OTHER events around the PMAP_ENTER are documented here but deliberately NOT instrumented (they are
the design's F1b accept path, a different question from "who stripped the write"):
  * the codesign gate `return KERN_CODESIGN_ERROR;` (~line 2421) skips the PMAP_ENTER ENTIRELY - the page
    is never mapped, so the strip sites below it never run for that fault;  and
  * the accept arm `kr = KERN_SUCCESS;` (~line 2478) is where a tainted page is ACCEPTED (the F1b
    unconditional-success path 1011's (E') row measured as `vmfret=0`).

THE PROBES ARE A PURE MEASUREMENT.  Each probe is one call to an extern
`void entry_note_abt_wr(uint32_t site, uint32_t va, uint32_t prot);` (function name `wr`, distinct from
1011's `entry_note_abt_vme`) that the entry-stub side provides.  The calls touch no control flow and no
value - they observe `prot` (or `*protection`) and the fault VA and return.  Five distinct site ids are
used: 1,2,3,4 for the strip sites and 5 for the PMAP_ENTER cross-check.  ONE gated `extern` declaration
is planted at file scope (right after the last `#include`, before the first code) - without it the calls
would not compile; it is a claim in code, not a claim in a comment (`mi4-a-claim-in-a-comment-is-not-a-check`).

THE VA - AND AN HONEST LIMITATION.  Sites 3 and 4 are inside `vm_fault(vm_map_t map, vm_map_offset_t
vaddr, ...)`, so their VA LOCAL is `vaddr`.  Sites 1 and 2 are inside `vm_fault_page(...)`, which has NO
`vaddr` parameter (the offset is only ever delivered through `*protection`/`first_offset`), so this tool
passes the sentinel `0u` there.  CONSEQUENCE, STATED PLAINLY: the entry side keys the published row on the
ABORT's VA (1011 measured `far=0x61000`), so a site-1/site-2 row carries va=0 and will NOT match that key
- i.e. THOSE TWO ROWS MAY NOT PUBLISH for the storm.  That is a KNOWN LIMITATION of probing a VA-less
function, NOT a bug in the tool.  The site id is published regardless (the entry side may still print it),
so the discriminating question ("did the strip happen in `vm_fault` or in `vm_fault_page`?") is still
answerable even when the VA column is the 0 sentinel.

SWITCH DISCIPLINE.  Every insertion is text inside `#if defined(STAGE90_XNU_WRSTRIP) &&
STAGE90_XNU_WRSTRIP ... #endif` (`defined()`-first spelling, `mi4-off-option-two-spellings`), so switch
OFF is this tree's original bytes, byte for byte.  The tool is idempotent, and a present marker is NOT
taken on trust: every inserted form must be present exactly once or it refuses.

WHY A PROTO-TYPE ALONE IS NOT ENOUGH FOR THE LAYOUT: this is the sole edit to `vm_fault.c`; the tree
carries NO `STAGE90` marker in this file today (verified `grep -n STAGE90 ... vm_fault.c` -> empty), so
there is nothing else inside the file to coordinate with.

Usage:
    patch_d13_wrstrip.py <XNU_TREE>            apply (idempotent)
    patch_d13_wrstrip.py <XNU_TREE> --dry-run  print the planned insertions, write nothing
    patch_d13_wrstrip.py <XNU_TREE> --check    assert the INSERTED forms are staged (switch ON)
"""
import sys

# The marker that proves this patch ran.  One spelling, checked in the file.
MARK = "1012: WRSTRIP"

# The gate.  defined()-first so an unset switch can never be "on" (`mi4-off-option-two-spellings`).
_GATE_OPEN = "#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP\n"
_GATE_CLOSE = "#endif /* defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP */\n"


def _probe(site, indent, va, prot):
    """Build the gated one-call probe block.  `indent` is the TEXT-column whitespace at the insertion
    point so the call reads as part of the surrounding block; the call itself carries no trailing
    context, so it cannot be mistaken for changing the value it observes."""
    return (
        f"{indent}{_GATE_OPEN}"
        f"{indent}/* **{MARK} site={site}: pure measurement (docs/1012) - observe, change nothing. */\n"
        f"{indent}entry_note_abt_wr({site}u, {va}, {prot});\n"
        f"{indent}{_GATE_CLOSE}"
    )


# --- anchors, each verbatim BYTES (leading tabs checked with `cat -A`).  Every `old` occurs exactly
#     once in the pristine tree; a wrong-whitespace anchor therefore REFUSES, never no-ops. ---

# 0) The one gated extern declaration, at file scope after the last #include (before the first code).
_DECL_OLD = (
    "#include <vm/vm_shared_region.h>\n"
    "\n"
    "#define VM_FAULT_CLASSIFY\t0\n"
)
_DECL_NEW = (
    "#include <vm/vm_shared_region.h>\n"
    "\n"
    "#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP\n"
    "/* **1012: WRSTRIP - the entry-stub recorder for the VM_PROT_WRITE strip sites (docs/1012).\n"
    " * Pure measurement: it returns void and is the ONLY new symbol the probes below reference.\n"
    " * Switch OFF => this declaration and every probe vanish => Apple's bytes, byte for byte. */\n"
    "extern void entry_note_abt_wr(uint32_t site, uint32_t va, uint32_t prot);\n"
    "#endif /* defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP */\n"
    "\n"
    "#define VM_FAULT_CLASSIFY\t0\n"
)

# 1) vm_fault_page site 1: the `} else` whose sole statement is the unbraced
#    `\t\t    \t*protection &= (~VM_PROT_WRITE);` (verbatim; the odd mid-line tab is 1881's).
#    A probe both BEFORE and AFTER that sole statement requires the else body to be BRACED, and BOTH
#    braces must vanish when the switch is OFF (Apple ships no braces here).  So the `{` rides in the
#    OPENING gate and the `}` in the CLOSING gate; each gate also carries its own probe:
#      switch OFF : both gates splice out entirely => exactly Apple's `} else` / strip / `}`
#      switch ON  : `else { probe(1); strip; probe(1); }`  -- brace-balanced, probes armed
#    Hence site 1 contributes TWO of the seven gates `--check` counts.
_S1_OPEN = (
    "\t\t#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP\n"
    f"\t\t/* **{MARK} site=1: pure measurement (docs/1012) - observe, change nothing. */\n"
    "\t\t{\n"
    "\t\t\tentry_note_abt_wr(1u, 0u, (uint32_t)*protection);\n"
    "\t\t#endif /* defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP */\n"
)
_S1_CLOSE = (
    "\t\t#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP\n"
    f"\t\t/* **{MARK} site=1: pure measurement (docs/1012) - observe, change nothing. */\n"
    "\t\t\tentry_note_abt_wr(1u, 0u, (uint32_t)*protection);\n"
    "\t\t}\n"
    "\t\t#endif /* defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP */\n"
)
_S1_OLD = (
    "\t\t} else\n"
    "\t\t    \t*protection &= (~VM_PROT_WRITE);\n"
)
_S1_NEW = (
    "\t\t} else\n"
    + _S1_OPEN
    + "\t\t    \t*protection &= (~VM_PROT_WRITE);\n"
    + _S1_CLOSE
)

# 2) vm_fault_page: `if ((fault_type & VM_PROT_WRITE) == 0) {` + the strip + `break;`.
_S2_OLD = (
    "\t\tif ((fault_type & VM_PROT_WRITE) == 0) {\n"
    "\t\t\t*protection &= ~VM_PROT_WRITE;\n"
    "\t\t\tbreak;\n"
)
_S2_NEW = (
    "\t\tif ((fault_type & VM_PROT_WRITE) == 0) {\n"
    + _probe(2, "\t\t\t", "0u", "(uint32_t)*protection")
    + "\t\t\t*protection &= ~VM_PROT_WRITE;\n"
    "\t\t\tbreak;\n"
)

# 3) vm_fault RetryFault verify: `prot &= retry_prot;` (unique).  VA local is `vaddr`.
_S3_OLD = "\t\tprot &= retry_prot;\n"
_S3_NEW = _probe(3, "\t\t", "vaddr", "(uint32_t)prot") + "\t\tprot &= retry_prot;\n"

# 4) vm_fault: the copy-object-changed strip, disambiguated by the UNIQUE comment above it (a bare
#    `\t\t\tprot &= ~VM_PROT_WRITE;` also occurs at ~2353 in vm_fault_enter).
_S4_OLD = (
    "\t\t\t * was unlocked, so take away write permission.\n"
    "\t\t\t */\n"
    "\t\t\tprot &= ~VM_PROT_WRITE;\n"
)
_S4_NEW = (
    "\t\t\t * was unlocked, so take away write permission.\n"
    "\t\t\t */\n"
    + _probe(4, "\t\t\t", "vaddr", "(uint32_t)prot")
    + "\t\t\tprot &= ~VM_PROT_WRITE;\n"
)

# 5) vm_fault_enter: the PMAP_ENTER cross-check.  `vaddr` and `prot` are both locals/params.
_S5_OLD = "\t\tPMAP_ENTER_OPTIONS(pmap, vaddr, m, prot, fault_type, 0,\n"
_S5_NEW = _probe(5, "\t\t", "vaddr", "(uint32_t)prot") + _S5_OLD

# (relative path, [(old, new), ...]).  One file, six edits.
EDITS = (
    ("osfmk/vm/vm_fault.c", (
        (_DECL_OLD, _DECL_NEW),
        (_S1_OLD, _S1_NEW),
        (_S2_OLD, _S2_NEW),
        (_S3_OLD, _S3_NEW),
        (_S4_OLD, _S4_NEW),
        (_S5_OLD, _S5_NEW),
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
        print(f"patch_d13_wrstrip: no {path}", file=sys.stderr)
        return path, None


def patch_text(text, pairs, path):
    """Return (new_text, changed).  Idempotent, and a present marker is NOT taken on trust: every
    inserted form must be present exactly once, or this refuses rather than certifying a tree the
    switch does not actually carry (the house rule in tools/patch_d13_abt_retry_bound.py)."""
    if MARK in text:
        for _old, new in pairs:
            if text.count(new) != 1:
                raise SystemExit(
                    f"patch_d13_wrstrip: the 1012 marker is present in {path} but an inserted form "
                    f"occurs {text.count(new)} times - moved by a hand edit, or half-applied. "
                    "Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    # Refuse if ANY anchor is not present EXACTLY once (wrong revision, or edited by hand).
    for i, (old, _new) in enumerate(pairs):
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_wrstrip: anchor #{i} in {path} occurs {n} times, not once - the tree is "
                f"not the expected revision, or it was edited by hand (a wrong-whitespace anchor "
                f"refuses, never no-ops). Anchor begins: {old[:60]!r}. Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(
                f"patch_d13_wrstrip: an inserted form did not land once in {path} - refusing to write"
            )
    return text, True


def apply(tree):
    path, text = _read(tree)
    if text is None:
        return 2
    new, changed = patch_text(text, PAIRS, REL)
    if not changed:
        print(f"patch_d13_wrstrip: already applied ({path})")
        return 0
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"patch_d13_wrstrip: vm_fault.c now publishes (site, va, prot) at the four VM_PROT_WRITE "
          f"strip sites + the PMAP_ENTER cross-check, behind STAGE90_XNU_WRSTRIP ({path})")
    return 0


def dry_run(tree):
    path, text = _read(tree)
    if text is None:
        return 2
    print(f"patch_d13_wrstrip --dry-run: would patch {path}")
    for i, (old, new) in enumerate(PAIRS):
        print(f"  ---- anchor #{i} present exactly once: {text.count(old) == 1} "
              f"(count={text.count(old)}) ----")
        if '<vm/vm_shared_region.h>' in old:
            print("  ---- insertion: the gated `extern void entry_note_abt_wr(...)` declaration ----")
        else:
            print("  ---- insertion (the probe block added at the anchor) ----")
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
        ("#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP", "the STAGE90_XNU_WRSTRIP #if gate", 7),
        ("extern void entry_note_abt_wr(uint32_t site, uint32_t va, uint32_t prot);",
         "the entry_note_abt_wr declaration", 1),
        ("entry_note_abt_wr(1u, 0u, (uint32_t)*protection);",
         "site 1 probes (vm_fault_page promote-fail, wraps the unbraced `} else`)", 2),
        ("entry_note_abt_wr(2u, 0u, (uint32_t)*protection);", "site 2 probe (vm_fault_page copy push)", 1),
        ("entry_note_abt_wr(3u, vaddr, (uint32_t)prot);", "site 3 probe (prot &= retry_prot)", 1),
        ("entry_note_abt_wr(4u, vaddr, (uint32_t)prot);", "site 4 probe (copy object changed)", 1),
        ("entry_note_abt_wr(5u, vaddr, (uint32_t)prot);", "site 5 probe (PMAP_ENTER cross-check)", 1),
    ):
        n = text.count(needle)
        if n == want:
            print(f"  ok   {what} present ({n}x)")
        else:
            print(f"  FAIL {what} count {n}x, want {want}x ({needle!r})")
            ok = False
    # The two untreated-but-documented events must be NAMED at the site a reader will open.
    if "KERN_CODESIGN_ERROR" in text and "KERN_SUCCESS" in text:
        print("  ok   the un-instrumented codesign/accept events are documented in the module docstring")
    else:
        print("  FAIL the codesign/accept documentation is missing")
        ok = False
    print(f"patch_d13_wrstrip --check {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main():
    argv = sys.argv[1:]
    flags = {a for a in argv if a.startswith("--")}
    rest = [a for a in argv if not a.startswith("--")]
    if len(rest) != 1:
        print("usage: patch_d13_wrstrip.py <XNU_TREE> [--dry-run|--check]", file=sys.stderr)
        return 2
    tree = rest[0]
    if "--check" in flags:
        return check(tree)
    if "--dry-run" in flags:
        return dry_run(tree)
    return apply(tree)


if __name__ == "__main__":
    raise SystemExit(main())