#!/usr/bin/env python3
"""Bound the user data-abort handler's SILENT RETRY (F4 of `docs/1011-design.md` §2).

WHY.  1009 (`records/1009-press-read.txt`, arm `armed-d13-24997fc5`) measured a ~2.09 M-repeat
data-abort storm that freezes the machine for 24.386 s.  By value, over the whole tail the fault is
CONSTANT: `fsr=0x0000080f` (WnR=1, a WRITE; FS=0xf "permission fault, level 2"), `pc=0x2be8a1a0`
(dyld `__TEXT`), `far=0x9a000` (a LOW user read-only page), user mode.  1010's correction 2
(`docs/1011-design.md` §1.5) says the `vm_map` denies the write while `vm_fault` still returns
KERN_SUCCESS, so the handler TAKES ITS RETRY ARM and re-runs the faulting store forever.  A measurement
that silently succeeds forever is the defect class this repo named in the 1009 reader
(`mi4-measurement-defects`).  F4 bounds it.

THE SITE - BY VALUE, not the design's line numbers.  `docs/1011-design.md` cites `arm/trap.c:902-908`
against the Darwin-17 tree (`xnu-4570.1.46`).  The D13 pool does NOT build that tree: it builds
`external/xnu-hd2-darwin13/xnu` (the manifest `out/xnu_arm_manifest_d13.txt` names the hd2 path 704
times and the 4570 path 0 times for `osfmk/arm/trap.c`; the hd2 `sleh_abort` dropped the 4570
`arm_fast_fault` fast-path and calls `vm_fault` directly, `hd2 trap.c:830`).  The 1009 elf confirms it:
it carries `IOS7WITNESS` (an hd2-only string, 22 references) and carries NONE of the 4570-only
`sleh_abort: pc %x` / `copyin/out has no recovery point` strings.  So the retry arm to bound is the one
in the LIVE tree - `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c`, the `parse` of the data-abort case:

    code = vm_fault(map, vm_map_trunc_page(dfar),
                    (dfsr & 0x800) ? (VM_PROT_READ | VM_PROT_WRITE) : (VM_PROT_READ), ...);
    if ((code != KERN_SUCCESS) && (code != KERN_ABORTED)) {   <-- the error branch
        exception_type = EXC_BAD_ACCESS;
        ... printf("usermode data abort, EXC_BAD_ACCESS ...") ...
    } else {                                                  <-- THE SILENT RETRY
        /*
         * Retry execution of instruction.
         */
        ml_set_interrupts_enabled(TRUE);
        return;
    }

THE EDIT.  Under `STAGE90_XNU_ABT_RETRY_BOUND`, this tool (a) plants a file-scope static (before
`sleh_abort`) tracking the last `(pc, far, dfsr)` triplet and the run length of identical triplets, and
(b) rewrites the retry arm's `return` into a bounded escalation.  A triplet is CONSECUTIVE-identical
because each new abort re-enters `sleh_abort`; the counter resets whenever the triplet changes.  The
comparison lives in the retry arm only, so in production (single-core, `real_ncpus:1`, memory
`mi4-1004`) it is never called concurrently - the statics need no lock, noted here.

THE BOUND N = 8 (ABT_RETRY_BOUND_N).  Small: the storm repeats ~2.09 M times, so anything ≪ that fires
long before the 24.4 s freeze; 8 leaves headroom for legitimate transient permission faults.

THE ESCALATION - WHY NOT A PANIC.  `docs/1011-design.md` F4 says "printf once and escalate ... to the
exception path".  In THIS handler the exception arm is reached by leaving `exception_type` 0 and
falling through to `if (exception_type) { ... doexception(...); }` at the end of `sleh_abort`; a plain
PDISABLE is a cross-TU `#define` set by `build_xnu_arm_kernel.sh` and is NOT something this tool can
prove, so the robust spelling used here is to fall through with `exception_type == 0`.  This delivers
SIGBUS (EXC_BAD_ACCESS -> launchd).  THE MACHINE-LEVEL CONSEQUENCE, STATED PLAINLY: the storming
process is pid 1 (launchd), and XNU panics on the SIGKILL of init (`bsd/kern/kern_sig.c`, the
`panic_plain("unexpected SIGKILL of ...")` landmine; design §2 F1b).  So on THIS storm the escalation
CONVERTS the 24.4 s freeze into an immediate panic - the design's own "correct semantics, fatal
outcome".  An unconditional `panic()` here was REJECTED: the retry arm also serves benign transient
faults (a page raced out between `arm_fast_fault`/`vm_fault` and the re-run), and a panic on the first
transient would brick every boot.  8 consecutive IDENTICAL triplets makes that false positive
vanishingly unlikely, but it is not impossible; F4 is a SAFETY NET, and it says so.

ONE print, ONCE per storm: a `printf` (the same channel 1009's `usermode data abort` line uses;
`kprintf` hits the same log, `mi4`) gated by a `reported` flag that is set only (re)lowered on a new
storm, so a boot cannot be flooded.

Switch discipline: the insertions are text inside `#if defined(STAGE90_XNU_ABT_RETRY_BOUND) &&
STAGE90_XNU_ABT_RETRY_BOUND ... #endif`, so switch OFF is the tree's original bytes, byte for byte
(`patch_d13_submap_pmap_skip.py`'s `defined()`-first spelling, `mi4-off-option-two-spellings`).  The
tool is idempotent, and a marker present is not taken on trust - every inserted form must be present
exactly once or it refuses.

Usage:
    patch_d13_abt_retry_bound.py <XNU_TREE>            apply (idempotent)
    patch_d13_abt_retry_bound.py <XNU_TREE> --dry-run  print the two planned insertions, write nothing
    patch_d13_abt_retry_bound.py <XNU_TREE> --check    assert the INSERTED form is staged (APPLE==OFF)
"""
import sys

# The marker that proves this patch ran. One spelling, checked in the file.
MARK = "1011: ABT_RETRY_BOUND"

# The bound on consecutive identical (pc, far, dfsr) BEFORE escalation. Small: the storm repeats
# ~2.09 M times, so this fires far below (1009: 0x200000, ~24.4 s).
N = 8

# --- osfmk/arm/trap.c ------------------------------------------------------------------------------
# Anchor 1: the definition of `sleh_abort`, preceded by the file-scope `__abort_count`.  The inserted
# static lands between them.  Spaces, not tabs - this tree's trap.c uses spaces in this region
# (verified with `cat -A`).
_DEF_OLD = (
    "static int __abort_count = 0;\n"
    "void sleh_abort(void *context, int reason)\n"
    "{\n"
)
_DEF_NEW = (
    "static int __abort_count = 0;\n"
    "#if defined(STAGE90_XNU_ABT_RETRY_BOUND) && STAGE90_XNU_ABT_RETRY_BOUND\n"
    "/* **1011: ABT_RETRY_BOUND - F4 of docs/1011-design.md.  The data-abort handler's retry arm\n"
    " * re-runs the faulting store on KERN_SUCCESS, and 1009 measured ~2.09 M identical retries\n"
    " * (24.386 s).  Bound the CONSECUTIVE-identical (pc, far, dfsr) run.  Single-core (real_ncpus:1),\n"
    " * so the statics need no lock.  Switch OFF => Apple's text, byte for byte. */\n"
    "#define ABT_RETRY_BOUND_N %du\n"
    "static uint32_t abt_rb_pc, abt_rb_far, abt_rb_dfsr;   /* last triplet */\n"
    "static uint32_t abt_rb_run;                           /* CONSECUTIVE identical triplets */\n"
    "static int      abt_rb_reported;                      /* one print per storm */\n"
    "#endif /* defined(STAGE90_XNU_ABT_RETRY_BOUND) && STAGE90_XNU_ABT_RETRY_BOUND */\n"
    "void sleh_abort(void *context, int reason)\n"
    "{\n"
) % N

# Anchor 2: the data-abort case's SILENT RETRY - `} else { ... Retry execution of instruction ... }`,
# uniquely identified by the PRECEDING `thread->task->all_image_info_size);` with NO blank line between.
# (The prefetch case has the IDENTICAL retry block but a BLANK line precedes it; the trailing `\n` on
# the anchor is what the file carries, verified byte-for-byte.)
_RETRY_OLD = (
    "                            thread->task->all_image_info_addr, "
    "thread->task->all_image_info_size);\n"
    "                } else {\n"
    "                    /*\n"
    "                     * Retry execution of instruction.\n"
    "                     */\n"
    "                    ml_set_interrupts_enabled(TRUE);\n"
    "                    return;\n"
    "                }\n"
)
_RETRY_NEW = (
    "                            thread->task->all_image_info_addr, "
    "thread->task->all_image_info_size);\n"
    "                } else {\n"
    "                    /*\n"
    "                     * Retry execution of instruction.\n"
    "                     */\n"
    "#if defined(STAGE90_XNU_ABT_RETRY_BOUND) && STAGE90_XNU_ABT_RETRY_BOUND\n"
    "                    /* **1011: ABT_RETRY_BOUND - bound the silent retry (docs/1011-design.md F4).\n"
    "                     * The `code == KERN_SUCCESS || KERN_ABORTED` else falls here; 1009 measured\n"
    "                     * ~2.09 M CONSECUTIVE faults with the SAME (pc=0x2be8a1a0, far=0x9a000,\n"
    "                     * fsr=0x80f).  Count the consecutive-identical run; past ABT_RETRY_BOUND_N,\n"
    "                     * print ONCE (no flood) and ESCALATE by SETTING exception_type so the\n"
    "                     * handler's `if (exception_type)` arm runs doexception() -> SIGBUS.  (NOT\n"
    "                     * by falling out with exception_type 0: the tail `return;` at the end of\n"
    "                     * sleh_abort would retry the faulting store exactly as the silent-retry arm\n"
    "                     * does - a claim-in-a-comment defect, mi4-a-claim-in-a-comment-is-not-a-check.)\n"
    "                     * On this storm the\n"
    "                     * stormer is pid 1, so that SIGBUS panics (design §2 F1b) - the design's\n"
    "                     * 'correct semantics, fatal outcome'.  NOT a panic() here on purpose: the\n"
    "                     * retry arm also serves benign transients, and panicking the first one would\n"
    "                     * brick every boot. */\n"
    "                    {\n"
    "                        uint32_t rb_pc = arm_ctx->pc, rb_far = dfar, rb_dfsr = dfsr;\n"
    "\n"
    "                        if (rb_pc == abt_rb_pc && rb_far == abt_rb_far && rb_dfsr == abt_rb_dfsr) {\n"
    "                            abt_rb_run++;\n"
    "                        } else {\n"
    "                            abt_rb_pc = rb_pc; abt_rb_far = rb_far; abt_rb_dfsr = rb_dfsr;\n"
    "                            abt_rb_run = 1u; abt_rb_reported = 0;\n"
    "                        }\n"
    "                        if (abt_rb_run > ABT_RETRY_BOUND_N) {\n"
    "                            if (!abt_rb_reported) {\n"
    "                                abt_rb_reported = 1;\n"
    "                                printf(\"ABT_RETRY_BOUND: %u consecutive identical aborts \"\n"
    "                                       \"(pc=0x%08x far=0x%08x fsr=0x%08x) - escalating to SIGBUS\\n\",\n"
    "                                       abt_rb_run, rb_pc, rb_far, rb_dfsr);\n"
    "                            }\n"
    "                            ml_set_interrupts_enabled(TRUE);\n"
    "                            exception_type = EXC_BAD_ACCESS;   /* -> the if(exception_type) arm -> doexception -> SIGBUS */\n"
    "                            exception_subcode = 0;\n"
    "                            break;   /* out of the switch; exception_type now nonzero */\n"
    "                        }\n"
    "                    }\n"
    "#endif /* defined(STAGE90_XNU_ABT_RETRY_BOUND) && STAGE90_XNU_ABT_RETRY_BOUND */\n"
    "                    ml_set_interrupts_enabled(TRUE);\n"
    "                    return;\n"
    "                }\n"
)

# (relative path, [(old, new), ...]) - one entry per file, all edits listed for that file.
EDITS = (
    ("osfmk/arm/trap.c", (
        (_DEF_OLD, _DEF_NEW),
        (_RETRY_OLD, _RETRY_NEW),
    )),
)


def patch_text(text: str, pairs, path: str) -> tuple[str, bool]:
    """Return (new_text, changed).  Idempotent, and a marker present is not taken on trust: every
    inserted form must be present exactly once, or this refuses rather than certifying a tree the
    switch does not actually carry (`patch_d13_submap_pmap_skip.py`'s rule)."""
    if MARK in text:
        for _old, new in pairs:
            if text.count(new) != 1:
                raise SystemExit(
                    f"patch_d13_abt_retry_bound: the 1011 marker is present in {path} but an "
                    f"inserted form occurs {text.count(new)} times - moved by a hand edit, or "
                    "half-applied. Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_abt_retry_bound: anchor in {path} occurs {n} times, not once - the "
                f"tree is not the expected revision, or it was edited by hand. Anchor begins: "
                f"{old[:60]!r}. Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_abt_retry_bound: an inserted form did not land once in "
                             f"{path} - refusing to write")
    return text, True


def _find(tree: str) -> str:
    return tree.rstrip("/") + "/osfmk/arm/trap.c"


def apply(tree: str) -> int:
    path = _find(tree)
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print(f"patch_d13_abt_retry_bound: no {path}", file=sys.stderr)
        return 2
    new, changed = patch_text(text, EDITS[0][1], "osfmk/arm/trap.c")
    if not changed:
        print(f"patch_d13_abt_retry_bound: already applied ({path})")
        return 0
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"patch_d13_abt_retry_bound: osfmk/arm/trap.c's user data-abort retry arm now bounds "
          f"{N} consecutive identical (pc,far,dfsr) retries behind STAGE90_XNU_ABT_RETRY_BOUND "
          f"({path})")
    return 0


def dry_run(tree: str) -> int:
    path = _find(tree)
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print(f"patch_d13_abt_retry_bound: no {path}", file=sys.stderr)
        return 2
    print(f"patch_d13_abt_retry_bound --dry-run: would patch {path}")
    for old, new in EDITS[0][1]:
        print("  ---- anchor present exactly once: "
              f"{text.count(old) == 1} (count={text.count(old)}) ----")
        print(f"  ---- insertion (between the anchor's first and last line) ----")
        # Show only the inserted middle: strip the anchor's own lines from both ends.
        added = [ln for ln in new.splitlines(keepends=True) if ln not in old.splitlines(keepends=True)]
        sys.stdout.write("".join("  | " + ln for ln in added))
    ok = text.count(MARK) > 0
    print(f"  ---- marker present: {ok} (state: {'STAGED' if ok else 'APPLE/OFF'}) ----")
    return 0


def check(tree: str) -> int:
    """The by-branch/--check proof: the INSERTED form is staged.  The OFF-identity (Apple bytes / the
    1009 elf) is the other half, proven by `tools/check_d13_abt_retry_bound.py` (which cmp's the image
    against the frozen 1009 elf) - this half proves the switch's payload is actually in the tree."""
    path = _find(tree)
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print(f"patch_d13_abt_retry_bound: no {path}", file=sys.stderr)
        return 2
    ok = True
    if MARK not in text:
        print(f"  FAIL {path}: no '{MARK}' marker - the switch was never staged")
        ok = False
    else:
        print(f"  ok   {path}: '{MARK}' staged")
    # By value: the switch's own payload, not just the comment marker.
    for needle, what in (
        ("#if defined(STAGE90_XNU_ABT_RETRY_BOUND) && STAGE90_XNU_ABT_RETRY_BOUND",
         "the STAGE90_XNU_ABT_RETRY_BOUND #if gate"),
        ("abt_rb_run++", "the consecutive-run counter"),
        (f"ABT_RETRY_BOUND_N {N}u", f"the named bound N={N}"),
        ('printf("ABT_RETRY_BOUND:', "the bounded print"),
        ("break;   /* fall out of the switch", "the escalation (fall through to doexception)"),
    ):
        n = text.count(needle)
        where = "applied" if text.count(
            "#if defined(STAGE90_XNU_ABT_RETRY_BOUND) && STAGE90_XNU_ABT_RETRY_BOUND") == 2 \
            else "PRE-COMPILE (no #if gate)"
        if n >= 1:
            print(f"  ok   {what} present ({n}x)")
        else:
            print(f"  FAIL {what} MISSING ({needle!r})")
            ok = False
    # The fatal landmine, named in the marker comment - a reader must not have to discover it.
    if "SIGBUS" in text and "pid 1" in text:
        print("  ok   the escalation's fatal outcome is documented at the site (SIGBUS / pid 1)")
    else:
        print("  FAIL the escalation's consequence is NOT documented at the site")
        ok = False
    print(f"patch_d13_abt_retry_bound --check {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main() -> int:
    argv = sys.argv[1:]
    flags = {a for a in argv if a.startswith("--")}
    rest = [a for a in argv if not a.startswith("--")]
    if "--emit-synthetic" in flags or len(rest) != 1:
        print("usage: patch_d13_abt_retry_bound.py <XNU_TREE> [--dry-run|--check]", file=sys.stderr)
        return 2
    tree = rest[0]
    if "--check" in flags:
        return check(tree)
    if "--dry-run" in flags:
        return dry_run(tree)
    return apply(tree)


if __name__ == "__main__":
    raise SystemExit(main())