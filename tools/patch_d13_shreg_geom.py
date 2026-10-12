#!/usr/bin/env python3
"""Open the tree's own iOS 7 shared-region geometry + stack on CANCRO - G1 (draft shape v2).

STATUS: DRAFT.  Nothing here is staged or authorised.  The window constants stay the tree's own
(`osfmk/vm/vm_shared_region.c:633-635`, `0x2C000000U` / `0x14000000U` / the "IOS7LAB shared region"
printf) and are frozen only after 997's `IOS7LAB cache mapping va=...` readings decide unslid-vs-slid
(see docs/reference/d13-shreg-frontier.md 6b/8).  This patcher does not touch them.

WHAT IT OPENS.  Two dormant two-layer gates, one runtime helper, four 1:1 line substitutions:

  osfmk/vm/vm_shared_region.c
    :626  `#if IOS7_COMPAT_PLATFORM`                    -> `#if IOS7_COMPAT_PLATFORM || (defined(SW) && SW)`
    :629  `if (ios7_compat_vmabi_enabled()) {`          -> `if (ios7_shreg_geom_enabled()) {`
  osfmk/arm/status.c  (the stack half - a HARD requirement of the same arm, audit 2: without it the
  FIXED stack allocation at `trunc(user_stack - MAXSSIZ)` falls INSIDE the new window and every
  process relays out)
    :62   `#if IOS7_COMPAT_PLATFORM`                    -> the same gate spelling
    :66   `if (ios7_compat_vmabi_enabled())`            -> `if (ios7_shreg_geom_enabled())`
  osfmk/arm/ios7_compat.h  (beside `ios7_traps_enabled()`, the 994/995 shape)
    +     `static inline int ios7_shreg_geom_enabled(void)` - `#if defined(SW) && SW` -> 1,
          `#else` -> `ios7_compat_vmabi_enabled()`.

WHY 1:1 SUBSTITUTIONS AND NOT AN INLINE `#if` WRAP (MEASURED - the draft's v1 shape failed this).
This tree's `panic` embeds its own location in the compiled string: `osfmk/kern/debug.h:264-266`
`#define PANIC_LOCATION __FILE__ ":" LINE_NUMBER(__LINE__)` /
`#define panic(ex, ...) (panic)(# ex "@" PANIC_LOCATION, ## __VA_ARGS__)`.  A `+4`-line wrap at :626
shifts every later `panic` line number, so a switch-undefined compile of the wrapped file is NOT the
pre-image object: the v1 rehearsal measured three changed strings - the panic sites now at
`:1568` (was `:1564`), `:1696` (was `:1692`), `:1700` (was `:1696`) - while `.text` stayed identical.
The 1:1 shape adds no line to any .c file, so no `.c` line number moves; the helper lives in a HEADER
whose own line count is never a `.c`'s `__LINE__`.  With the switch undefined the helper's body
expands, token for token, to `return ios7_compat_vmabi_enabled();` at both call sites.

The switch name `STAGE90_XNU_SHREG_GEOM` is the draft's proposal (the audit left `<SW>` open).

Usage:
  patch_d13_shreg_geom.py <xnu-tree-root>              stage (or report nothing to do)
  patch_d13_shreg_geom.py <xnu-tree-root> --dry-run    plan + unified diffs, write nothing
  patch_d13_shreg_geom.py <xnu-tree-root> --verify-off round-trip proof against the pre-images

Pre-images measured 2026-10-12 (this is what `--verify-off` round-trips):
  osfmk/vm/vm_shared_region.c  sha256 f91c34608cf701c973e7fd9dfa59c7f91e1f1c6917598bc1339c5737e72e4605
  osfmk/arm/status.c           sha256 d8f41a7b471dfde5b5b09cee64565ebcd8583540d0af0b4631e55467877c5524
  osfmk/arm/ios7_compat.h      sha256 079c7a17f82768f45d225fbb0f23a7049b4f7fc52cfb436c00e023185b5ff17a
"""
import difflib
import hashlib
import sys
from pathlib import Path

XNU_VM_SHARED_REGION = "osfmk/vm/vm_shared_region.c"
XNU_ARM_STATUS = "osfmk/arm/status.c"
XNU_ARM_IOS7_COMPAT = "osfmk/arm/ios7_compat.h"
FILES = (XNU_VM_SHARED_REGION, XNU_ARM_STATUS, XNU_ARM_IOS7_COMPAT)

SWITCH = "STAGE90_XNU_SHREG_GEOM"
HELPER = "ios7_shreg_geom_enabled"

# --- edit (A): the ONE gate LINE per .c, 994's exact spelling (`defined()` first) -------------------
BARE_IF = "#if IOS7_COMPAT_PLATFORM\n"
GATE_IF = ("#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_SHREG_GEOM) "
           "&& STAGE90_XNU_SHREG_GEOM)\n")

# --- edit (B): the two per-file runtime selector lines, 1:1 -> the header helper --------------------
VMSR_CALL_C = "                if (ios7_compat_vmabi_enabled()) {\n"
VMSR_CALL_S = "                if (ios7_shreg_geom_enabled()) {\n"
STATUS_CALL_C = "    if (ios7_compat_vmabi_enabled())\n"
STATUS_CALL_S = "    if (ios7_shreg_geom_enabled())\n"

# --- edit (C): the header helper, inserted after the ios7_traps_enabled() block ---------------------
HEADER_TAIL_C = ("    return ios7_compat_vmabi_enabled();\n"
                 "#endif\n"
                 "}\n"
                 "#endif\n")
HELPER_BLOCK = ("\n"
                "/* iOS 7 shared-region geometry and stack top, one switch (G1, 2026-10-12).  The two\n"
                " * travel together: the region window is 0x2c000000/0x14000000 and the stack top must\n"
                " * clear it.  OFF: this expands to exactly the shared runtime selector, token for\n"
                " * token; this header's line count is never a .c file's __LINE__. */\n"
                "static inline int ios7_shreg_geom_enabled(void)\n"
                "{\n"
                "#if defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM\n"
                "    return 1;\n"
                "#else\n"
                "    return ios7_compat_vmabi_enabled();\n"
                "#endif\n"
                "}\n")
HEADER_TAIL_S = ("    return ios7_compat_vmabi_enabled();\n"
                 "#endif\n"
                 "}\n"
                 + HELPER_BLOCK + "#endif\n")

KIND = {XNU_VM_SHARED_REGION: "vmsr", XNU_ARM_STATUS: "status", XNU_ARM_IOS7_COMPAT: "header"}


class Refusal(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code
        self.msg = msg


def sha256(text):
    return hashlib.sha256(text.encode()).hexdigest()


def state_of(text, kind):
    """('pre'|'staged'|'foreign', counts-string).  The counts are the refusal's evidence."""
    if kind in ("vmsr", "status"):
        call_c, call_s = ((VMSR_CALL_C, VMSR_CALL_S) if kind == "vmsr"
                          else (STATUS_CALL_C, STATUS_CALL_S))
        bare, gate = text.count(BARE_IF), text.count(GATE_IF)
        c, s = text.count(call_c), text.count(call_s)
        pre = bare == 1 and gate == 0 and c == 1 and s == 0
        staged = bare == 0 and gate == 1 and c == 0 and s == 1
        counts = (f"{bare} bare `#if IOS7_COMPAT_PLATFORM` (want 1|0), {gate} gated (want 0|1), "
                  f"{c} selector line (want 1|0), {s} helper call (want 0|1)")
    else:
        c, s = text.count(HEADER_TAIL_C), text.count(HEADER_TAIL_S)
        # NOTE: HEADER_TAIL_C is a substring of HEADER_TAIL_S (the helper's own tail looks the same),
        # so `staged` keys on HEADER_TAIL_S alone.
        pre = c == 1 and s == 0
        staged = s == 1
        counts = f"{c} traps-tail (want 1), {s} staged-tail (want 0|1)"
    return ("pre" if pre else "staged" if staged else "foreign"), counts


def stage_text(text, kind):
    if kind == "vmsr":
        return text.replace(BARE_IF, GATE_IF).replace(VMSR_CALL_C, VMSR_CALL_S)
    if kind == "status":
        return text.replace(BARE_IF, GATE_IF).replace(STATUS_CALL_C, STATUS_CALL_S)
    return text.replace(HEADER_TAIL_C, HEADER_TAIL_S)


def strip_text(text, kind):
    if kind == "vmsr":
        return text.replace(VMSR_CALL_S, VMSR_CALL_C).replace(GATE_IF, BARE_IF)
    if kind == "status":
        return text.replace(STATUS_CALL_S, STATUS_CALL_C).replace(GATE_IF, BARE_IF)
    return text.replace(HEADER_TAIL_S, HEADER_TAIL_C)


def plan(read):
    """read(relpath) -> text.  Returns (staged_texts, actions) or raises Refusal.

    All three files pre-image -> stage all three.  All three staged -> Refusal(0) ("already").  Any
    mix -> Refusal(6) naming the odd file: this patcher writes three files and never repairs one.
    """
    texts = {rel: read(rel) for rel in FILES}
    states = {rel: state_of(texts[rel], KIND[rel]) for rel in FILES}
    verdicts = {rel: st for rel, (st, _c) in states.items()}
    if all(v == "pre" for v in verdicts.values()):
        out = {rel: stage_text(texts[rel], KIND[rel]) for rel in FILES}
        for rel in FILES:
            st, counts = state_of(out[rel], KIND[rel])
            if st != "staged":
                raise Refusal(6, f"the plan did not land cleanly in {rel}: {counts} - "
                                 f"refusing rather than writing it")
        return out, {  # first -> last, for the printout
            XNU_VM_SHARED_REGION: [
                f"{XNU_VM_SHARED_REGION}: the geometry #if line -> the 994 gate spelling",
                f"{XNU_VM_SHARED_REGION}: the ios7_compat_vmabi_enabled() test -> the helper"],
            XNU_ARM_STATUS: [
                f"{XNU_ARM_STATUS}: the stack #if line -> the same gate spelling",
                f"{XNU_ARM_STATUS}: the ios7_compat_vmabi_enabled() test -> the same helper"],
            XNU_ARM_IOS7_COMPAT: [
                f"{XNU_ARM_IOS7_COMPAT}: + ios7_shreg_geom_enabled() beside ios7_traps_enabled()"],
        }
    if all(v == "staged" for v in verdicts.values()):
        raise Refusal(0, "already staged")
    detail = "; ".join(f"{rel}: {verdicts[rel]} [{states[rel][1]}]" for rel in FILES)
    raise Refusal(6, f"the three files are not all pre-image nor all this patcher's output: {detail}"
                     f" - a partial or foreign edit, refusing rather than repairing it")


def verify_off(pre_texts, staged_texts):
    """strip(staged) == pre, and re-plan(pre) == staged - all three files."""
    for rel in FILES:
        back = strip_text(staged_texts[rel], KIND[rel])
        if back != pre_texts[rel]:
            return False, f"removing the edits does not reproduce the pre-image of {rel}"
    try:
        again, _acts = plan(lambda rel: pre_texts[rel])
    except Refusal as exc:
        return False, f"re-planning the pre-images refused: {exc.msg}"
    for rel in FILES:
        if again[rel] != staged_texts[rel]:
            return False, f"re-planning the pre-image does not reproduce the staged text of {rel}"
    return True, "round trip holds in all three files: strip(staged) == pre-image, re-plan(pre) == staged"


def diff_text(pre, staged, rel):
    return "".join(difflib.unified_diff(pre.splitlines(keepends=True),
                                        staged.splitlines(keepends=True),
                                        fromfile="a/" + rel, tofile="b/" + rel, n=3))


def main() -> int:
    argv = sys.argv[1:]
    dry_run = "--dry-run" in argv
    verify = "--verify-off" in argv
    argv = [a for a in argv if not a.startswith("--")]
    if len(argv) != 1:
        print("usage: patch_d13_shreg_geom.py <xnu-tree-root> [--dry-run] [--verify-off]",
              file=sys.stderr)
        return 2
    root = Path(argv[0])
    for rel in FILES:
        if not (root / rel).is_file():
            print(f"patch_d13_shreg_geom: {root / rel} does not exist "
                  f"(is {root} an xnu tree root?)", file=sys.stderr)
            return 2

    try:
        staged, actions = plan(lambda rel: (root / rel).read_text())
        already = False
    except Refusal as exc:
        if exc.code != 0:
            print(f"patch_d13_shreg_geom: REFUSING: {exc.msg}", file=sys.stderr)
            return 1
        already = True

    pre_texts = {rel: ((strip_text((root / rel).read_text(), KIND[rel]) if already
                        else (root / rel).read_text())) for rel in FILES}
    staged_texts = {rel: (root / rel).read_text() for rel in FILES} if already else staged

    if verify:
        ok, why = verify_off(pre_texts, staged_texts)
        print(f"patch_d13_shreg_geom: --verify-off {'PASS' if ok else 'FAIL'} "
              f"({'already-staged' if already else 'unstaged'} input) - {why}")
        return 0 if ok else 1

    if already:
        print(f"patch_d13_shreg_geom: nothing to do - all three files are already {SWITCH}'s shape")
        return 0
    if dry_run:
        for rel in FILES:
            for a in actions[rel]:
                print(f"  {a}")
            sys.stdout.write(diff_text(pre_texts[rel], staged_texts[rel], rel))
        return 0

    for rel in FILES:
        for a in actions[rel]:
            print(f"  {a}")
        (root / rel).write_text(staged_texts[rel])
        print(f"  {rel}: pre-image sha256 {sha256(pre_texts[rel])} -> staged sha256 "
              f"{sha256(staged_texts[rel])}")
    print(f"patch_d13_shreg_geom: staged {len(FILES)} file(s) for {SWITCH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())