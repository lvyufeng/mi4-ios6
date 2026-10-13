#!/usr/bin/env python3
"""Stage the 996/999 SHARED-REGION syscall probe into the D13 tree (idempotent, ONE file).

WHY, by value.  995 read dyld's real halt message (`Library not loaded: /usr/lib/libbsm.0.dylib`,
out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt:4111-4138).  That library is in the
dyld shared cache (696 images) but has no file on the volume, so the minimal explanation is that dyld's
cache was never mapped: `mapSharedCache` failed silently and every cached dylib degraded to a file
lookup whose first miss halted dyld.  The two entry points are BSD syscalls 294
(`shared_region_check_np`, dyld wrapper 0x2be05774, called from mapSharedCache 0x2be04fa0) and 438
(`shared_region_map_and_slide_np`, wrapper 0x2be0579c, `mov.w r0,#438`).  THIS TREE WIRES BOTH
(bsd/kern/syscalls.master:452/:671 -> out/xnu_generated_d13/bsd/init_sysent.c:440/:649; bodies linked
at 0x8041fd84 / 0x804201c0), so the table is complete and only the RUNTIME returns are unknown.

WHAT IS ALREADY SUSPECTED, from the same by-value read (docs/reference/d13-shreg-frontier.md sec 2):
dyld's `struct shared_file_mapping_np` is 32 bytes (`add.w r0, r4, r3, lsl #5` at 0x2be057ba;
`add.w r8, r8, #32` at 0x2be05176; initProt read at [r8,#28]) while this kernel's is 20
(`shared_region_copyin_mappings` @0x8041fdf0: `add r1, r2, r2, lsl #2; lsl r2, r1, #2`), because
mach_vm_address_t/_offset_t/_size_t are uint32 here (osfmk/mach/arm/vm_types.h:107-113).  So the ungated
copyin reads 3*20 = 60 of dyld's 96 bytes and misparses m[0] to size=0 -> vm_map_enter_mem_object
returns KERN_INVALID_ARGUMENT (vm_map.c:2344) -> EINVAL.  The probe does not fix that; it MEASURES
which exit is reached (`IOS7SHREG map_file kr=`, `IOS7SHREG map_slide ret=`).

WHAT THIS SCRIPT STAGES - bsd/vm/vm_unix.c ONLY (a POOL file; syscalls.master, mach_kernelrpc.c and
every other lane's file are untouched), all inside `#if defined(STAGE90_XNU_SHREG_PROBE) &&
STAGE90_XNU_SHREG_PROBE` blocks, so with the switch OFF the preprocessed text is the tree's and the
image is byte-identical:

  1. `shared_region_check_np` entry  -> `IOS7SHREG check_np pid=%d out=0x%08x`
  2. `shared_region_check_np` exit   -> `IOS7SHREG check_np ret=%d`   (the errno actually returned)
  3. `shared_region_map_and_slide_np` entry -> `IOS7SHREG map_slide pid=%d fd=%d count=%u
     mappings=0x%08x slide=0x%x slide_start=0x%08x size=0x%08x`
  4. all THREE returns of `shared_region_map_and_slide_np` -> `IOS7SHREG map_slide ret=%d`
  5. immediately after the `vm_shared_region_map_file()` call INSIDE `_shared_region_map` ->
     `IOS7SHREG map_file kr=%d`  (its kr is not visible at 438's exit sites: `_shared_region_map`
     returns the errno it folded, vm_unix.c:1418-1433)
  6. the 999 slide half - `_shared_region_slide`'s entry, the step past `slide_init`, and the step
     past the slide_info copyin -> `IOS7SHREG slide_enter` / `slide_init` / `slide_copyin` (with G3
     and G1 open, 438 first reaches this function; these three lines say how far it gets)

The 996-only subset of the ten blocks is a state in its own right: the living tree was COMMITTED with
the first seven staged (the 996 probe, before the 999 slide half existed), so the patcher must be
able to stage from there.  `classify()` accepts exactly three states - none staged, the 996-only
subset, all ten - and refuses anything else, never repairing it in place.

Bounded (the first 4 calls of each syscall; one entry line and one exit line per bounded call) and
fail-soft: every line is a single `printf`, no lock, no allocation, no sleep, no panic - the same shape
994 uses (`IOS7LAB vm_allocate64`).

NOT staged here: the 294 copyout width (dyld allocated 8 bytes for it and reads only the low word -
see the doc, sec 3) and the 32-byte wire branch at vm_unix.c:1174-1204 (it is gated on
`ios7lab_cache_abi()`, which is 0 on CANCRO; opening it is a separate rung, doc sec 8).

CLI:
  patch_d13_shreg_probe.py <xnu-tree-root>            stage (idempotent)
  patch_d13_shreg_probe.py <xnu-tree-root> --dry-run  plan + unified diff, write nothing
  patch_d13_shreg_probe.py <xnu-tree-root> --verify-off
                                                      stage into a THROWAWAY copy in /tmp and prove
                                                      the guard structure is removable exactly
  patch_d13_shreg_probe.py --emit-synthetic <dir>     write the ON rehearsal TU (probe.c + stub.c)
                                                      from THIS file's own block text
"""
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

XNU_VM_UNIX = "bsd/vm/vm_unix.c"

GUARD_OPEN = "#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE\n"
GUARD_CLOSE = "#endif\n"
# Every block carries one of these: the print literal's prefix (upper) or a counter's name (lower).
# classify() proves that none is left over after removing the blocks it can account for.
PROBE_TEXT = ("IOS7SHREG", "ios7shreg")

# ---- the ten print sites (996's seven + 999's slide half) ---------------------------------------

# plan_insert() places the block immediately AFTER its anchor; the anchor must be unique and the
# insertion must be undone exactly by removing `block`, so every block is one GUARD_OPEN..GUARD_CLOSE
# region and nothing else.

NP_ENTRY_ANCHOR = ("\tvm_shared_region_t\tshared_region;\n"
                   "\tmach_vm_offset_t\tstart_address = 0;\n"
                   "\tint\t\t\terror;\n"
                   "\tkern_return_t\t\tkr;\n")
NP_ENTRY_BLOCK = "\n" + GUARD_OPEN + """\t/* 996: bounded, fail-soft probe of the two dyld shared-cache
\t * syscalls.  One entry line and one exit line per bounded call; nothing here changes behaviour. */
\tstatic unsigned int ios7shreg_np_logged = 0;
\tint ios7shreg_np_probe = (ios7shreg_np_logged++ < 4);

\tif (ios7shreg_np_probe)
\t\tprintf("IOS7SHREG check_np pid=%d out=0x%08x\\n",
\t\t       p->p_pid, (unsigned int)uap->start_address);
""" + GUARD_CLOSE

NP_EXIT_ANCHOR = "\t\t (uint64_t)uap->start_address, (uint64_t)start_address, error));\n"
NP_EXIT_BLOCK = "\n" + GUARD_OPEN + """\tif (ios7shreg_np_probe)
\t\tprintf("IOS7SHREG check_np ret=%d\\n", error);
""" + GUARD_CLOSE

SLIDE_ENTRY_ANCHOR = "\tstruct shared_file_mapping_np\tstack_mappings[SFM_MAX_STACK];\n"
SLIDE_ENTRY_BLOCK = "\n" + GUARD_OPEN + """\tstatic unsigned int ios7shreg_slide_logged = 0;
\tint ios7shreg_slide_probe = (ios7shreg_slide_logged++ < 4);

\tif (ios7shreg_slide_probe)
\t\tprintf("IOS7SHREG map_slide pid=%d fd=%d count=%u mappings=0x%08x slide=0x%x slide_start=0x%08x size=0x%08x\\n",
\t\t       p->p_pid, uap->fd, (unsigned int)uap->count,
\t\t       (unsigned int)uap->mappings, uap->slide,
\t\t       (unsigned int)uap->slide_start, uap->slide_size);
""" + GUARD_CLOSE

SLIDE_RET_EARLY_PRINT = """\t#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE
\t\tif (ios7shreg_slide_probe)
\t\t\tprintf("IOS7SHREG map_slide ret=%d\\n", kr);
\t#endif
"""

# return 1 of 3: the `if (kr != KERN_SUCCESS) { return kr; }` right after _shared_region_map
SLIDE_RET1_ANCHOR = ("\tkr = _shared_region_map(p, uap->fd, mappings_count, mappings,"
                     " &sr_file_control, &mapping_to_slide);\n"
                     "\tif (kr != KERN_SUCCESS) {\n")
SLIDE_RET1_BLOCK = SLIDE_RET_EARLY_PRINT
# return 2 of 3: the slide failure path, after the undo
SLIDE_RET2_ANCHOR = ("\t\t\tvm_shared_region_undo_mappings(NULL, 0, mappings, mappings_count);\n")
SLIDE_RET2_BLOCK = ("\t\t#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE\n"
                    "\t\t\tif (ios7shreg_slide_probe)\n"
                    "\t\t\t\tprintf(\"IOS7SHREG map_slide ret=%d\\n\", kr);\n"
                    "\t\t#endif\n")
# return 3 of 3: the shared `done:` exit.  `done:` alone occurs THREE times in this file (1458, 1523,
# 1603), so the anchor carries the slide block's closing braces to be unique - measured: this text
# occurs exactly once, while "\t}\ndone:\n" occurs twice.
SLIDE_RET3_ANCHOR = "\t\t\treturn kr;\n\t\t}\n\t}\ndone:\n"
SLIDE_RET3_BLOCK = "\t#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE\n" \
                   "\tif (ios7shreg_slide_probe)\n" \
                   "\t\tprintf(\"IOS7SHREG map_slide ret=%d\\n\", kr);\n" \
                   "\t#endif\n"

MAPFILE_ANCHOR = "\t\t\t\t       mapping_to_slide);\n"
MAPFILE_BLOCK = "\n" + GUARD_OPEN + """\t{
\t\tstatic unsigned int ios7shreg_mapfile_logged = 0;

\t\tif (ios7shreg_mapfile_logged++ < 4)
\t\t\tprintf("IOS7SHREG map_file kr=%d\\n", kr);
\t}
""" + GUARD_CLOSE

# ---- the 999 slide half: three lines inside `_shared_region_slide` (438's success path) ---------
# Placed after their anchors, each in its own braced guard region (the :1435 shape): one static
# counter bounded at 4, fail-soft like the rest.  `slide`, `slide_start`, `slide_size`, `error` and
# `slide_info_entry` are all in scope at their anchors (measured against the live tree, 2026-10-13).

SLIDE999_ENTER_ANCHOR = ("\tvoid *slide_info_entry = NULL;\n"
                         "\tint\t\t\terror;\n")
SLIDE999_ENTER_BLOCK = "\n" + GUARD_OPEN + """\t{
\t\tstatic unsigned int ios7shreg_slide_enter_logged = 0;

\t\tif (ios7shreg_slide_enter_logged++ < 4)
\t\t\tprintf("IOS7SHREG slide_enter slide=0x%x src=0x%08x n=0x%x\\n", slide, (unsigned)slide_start, (unsigned)slide_size);
\t}
""" + GUARD_CLOSE

SLIDE999_INIT_ANCHOR = ("\t\tprintf(\"slide_info initialization failed with kr=%d\\n\", error);\n"
                        "\t\tgoto done;\n"
                        "\t}\n")
SLIDE999_INIT_BLOCK = "\n" + GUARD_OPEN + """\t{
\t\tstatic unsigned int ios7shreg_slide_init_logged = 0;

\t\tif (ios7shreg_slide_init_logged++ < 4)
\t\t\tprintf("IOS7SHREG slide_init kr=%d entry=%p n=0x%x\\n", error, slide_info_entry, (unsigned)slide_size);
\t}
""" + GUARD_CLOSE

SLIDE999_COPYIN_ANCHOR = ("\t\t\t       (vm_size_t)slide_size);\n"
                          "\t}\n")
SLIDE999_COPYIN_BLOCK = "\n" + GUARD_OPEN + """\t{
\t\tstatic unsigned int ios7shreg_slide_copyin_logged = 0;

\t\tif (ios7shreg_slide_copyin_logged++ < 4)
\t\t\tprintf("IOS7SHREG slide_copyin kr=%d\\n", error);
\t}
""" + GUARD_CLOSE

# (name, anchor, block) - order matters: EDITS[:EDITS_996] must stay the 996 staging (the tree's
# committed partial state), so new edits are APPENDED; each anchor is unique in the tree's file.
EDITS_996 = 7   # the 996-era prefix; classify() accepts exactly this subset as the partial state
EDITS = (
    ("vm_unix.c: check_np entry", NP_ENTRY_ANCHOR, NP_ENTRY_BLOCK),
    ("vm_unix.c: check_np exit", NP_EXIT_ANCHOR, NP_EXIT_BLOCK),
    ("vm_unix.c: map_slide entry", SLIDE_ENTRY_ANCHOR, SLIDE_ENTRY_BLOCK),
    ("vm_unix.c: map_slide return after _shared_region_map", SLIDE_RET1_ANCHOR, SLIDE_RET1_BLOCK),
    ("vm_unix.c: map_slide return after the slide failure", SLIDE_RET2_ANCHOR, SLIDE_RET2_BLOCK),
    ("vm_unix.c: map_slide return at done:", SLIDE_RET3_ANCHOR, SLIDE_RET3_BLOCK),
    ("vm_unix.c: map_file kr in _shared_region_map", MAPFILE_ANCHOR, MAPFILE_BLOCK),
    ("vm_unix.c: _shared_region_slide entry (999)", SLIDE999_ENTER_ANCHOR, SLIDE999_ENTER_BLOCK),
    ("vm_unix.c: _shared_region_slide past slide_init (999)", SLIDE999_INIT_ANCHOR,
     SLIDE999_INIT_BLOCK),
    ("vm_unix.c: _shared_region_slide past the copyin (999)", SLIDE999_COPYIN_ANCHOR,
     SLIDE999_COPYIN_BLOCK),
)

# The eight format literals the checker looks for in the linked image (by value, in .rodata).
LITERALS = (
    b"IOS7SHREG check_np pid=%d out=0x%08x\n",
    b"IOS7SHREG check_np ret=%d\n",
    b"IOS7SHREG map_slide pid=%d fd=%d count=%u mappings=0x%08x slide=0x%x slide_start=0x%08x "
    b"size=0x%08x\n",
    b"IOS7SHREG map_slide ret=%d\n",
    b"IOS7SHREG map_file kr=%d\n",
    b"IOS7SHREG slide_enter slide=0x%x src=0x%08x n=0x%x\n",
    b"IOS7SHREG slide_init kr=%d entry=%p n=0x%x\n",
    b"IOS7SHREG slide_copyin kr=%d\n",
)


class Refusal(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


def plan_insert(text, what, anchor, block):
    """Insert `block` immediately after `anchor`.  (new_text, changed, note) or Refusal.

    Idempotence is keyed on `anchor + block` - the anchor stays in the file BELOW the insertion
    (mi4-an-edit-on-a-prefix-deletes-the-suffix).  Any other state is refused, never re-guessed.
    """
    n_anchor = text.count(anchor)
    n_staged = text.count(anchor + block)
    if n_anchor == 1 and n_staged == 1:
        return text, False, f"{what}: already staged"
    if n_staged != 0 or n_anchor != 1:
        raise Refusal(6, f"{what}: found {n_anchor} anchor(s) and {n_staged} staged copy/copies, "
                         f"expected exactly one anchor and zero-or-one staged copy - the region was "
                         f"edited by hand; not repaired in place")
    at = text.index(anchor) + len(anchor)
    return text[:at] + block + text[at:], True, f"{what}: staged"


def classify(text):
    """Name the file's staging state: (verdict, present, base, why).

    `present` = the names of the edits whose `anchor + block` the text carries (EDITS order); `base`
    = the text with each of those peeled back to its anchor.  Verdicts:
      pre      - nothing staged; base == text
      partial  - EXACTLY the 996 prefix (EDITS[:EDITS_996]) staged; the state this tree was committed
                 with, and what the 999 stage script legitimately feeds in
      staged   - all ten staged
      foreign  - anything else, never repaired in place
    The foreign test is the fragment test: after peeling what it can account for, the remainder must
    carry ZERO guard regions and zero PROBE_TEXT occurrences (upper in print literals, lower in
    counter names) - so a hand edit that deletes part of a block
    (mi4-an-edit-on-a-prefix-deletes-the-suffix) leaves a guard line or probe bytes the peel cannot
    explain, and is refused.
    """
    present, base = [], text
    for what, anchor, block in EDITS:
        n = base.count(anchor + block)
        if n > 1:
            return ("foreign", present, base,
                    f"{what}: {n} staged copies - the region was edited by hand; not repaired")
        if n == 1:
            base = base.replace(anchor + block, anchor)
            present.append(what)
    guards = base.count(GUARD_OPEN.strip())
    left = sum(base.count(t) for t in PROBE_TEXT)
    if guards or left:
        return ("foreign", present, base,
                f"after removing the {len(present)} block(s) it can account for, the text still "
                f"carries {guards} `{GUARD_OPEN.strip()}` region(s) and {left} probe-text "
                f"occurrence(s) - a partial or foreign edit is present; not repaired in place")
    if len(present) == len(EDITS):
        return "staged", present, base, f"all {len(EDITS)} blocks staged"
    if present == [what for what, _anchor, _block in EDITS[:EDITS_996]]:
        return ("partial", present, base,
                f"the 996 staging ({EDITS_996} of {len(EDITS)} block(s) staged)")
    if not present:
        return "pre", present, base, "no blocks staged"
    return ("foreign", present, base,
            f"{len(present)} of {len(EDITS)} block(s) staged, but not the 996 prefix "
            f"({present!r}) - not a named state; not repaired in place")


def plan(text):
    """Apply every edit in memory.  [(what, anchor, block, note)], staged_text, changed.

    Pre-flight is classify(): plan_insert re-inserts a block whose `anchor + block` is absent, so a
    hand edit that deleted part of a staged block would get a SECOND copy beside the fragment - the
    failure `mi4-an-edit-on-a-prefix-deletes-the-suffix` names.  classify() peels every edit the text
    actually carries and demands the remainder be clean, so the 996-only partial state stages to the
    full ten and every fragment is refused, never repaired in place.
    """
    verdict, _present, _base, why = classify(text)
    if verdict == "foreign":
        raise Refusal(6, why)
    notes, out, changed = [], text, False
    for what, anchor, block in EDITS:
        out, ch, note = plan_insert(out, what, anchor, block)
        notes.append((what, anchor, block, note))
        changed |= ch
    if out.count(GUARD_OPEN.strip()) != len(EDITS):
        raise Refusal(6, f"the plan would leave {out.count(GUARD_OPEN.strip())} guard regions, "
                         f"expected {len(EDITS)} - refusing before writing")
    return notes, out, changed


def guard_balance(block):
    """(opens, closes) of the guard in one inserted block - must be 1/1, or the OFF preprocessed text
    would not be the tree's (a stray #if with an #else, or an unbalanced #endif, changes it)."""
    return block.count("#if defined(STAGE90_XNU_SHREG_PROBE)"), block.count("#endif")


def verify_off(base, staged):
    """Prove the staged text is the base text plus removable guarded blocks.  Returns (ok, why).

    Removal is by exact string equality: strip every inserted block and the result must be the base
    text BYTE FOR BYTE.  That proves insertion-only and that each block is a self-contained guard
    region; it does NOT prove the preprocessor's answer - that is the build's byte-identity check
    plus `check_d13_shreg_probe.py --expect off` on the image.  `base` is the tree's text with the
    edits classify() found peeled back (== the tree's text when nothing was staged).
    """
    for what, anchor, block in EDITS:
        if block not in staged:
            return False, f"{what}: the block is not in the staged text"
        if not block.lstrip("\n\t").startswith(GUARD_OPEN):
            return False, f"{what}: the block does not open on the guard"
        if not block.endswith(GUARD_CLOSE) and "\t" + GUARD_CLOSE not in block:
            return False, f"{what}: the block does not close on #endif"
        opens, closes = guard_balance(block)
        if (opens, closes) != (1, 1):
            return False, f"{what}: guard structure is {opens} open / {closes} close, not 1/1"
        stripped = staged.replace(anchor + block, anchor)
        if len(stripped) == len(staged):
            return False, f"{what}: the block was not found behind its anchor"
        staged = stripped
    if staged != base:
        return False, "removing the blocks does not reproduce the base text"
    return True, f"removing all {len(EDITS)} guarded blocks reproduces the base text exactly"


def diff_text(original, staged):
    import difflib
    return "".join(difflib.unified_diff(original.splitlines(True), staged.splitlines(True),
                                        "bsd/vm/vm_unix.c (tree)", "bsd/vm/vm_unix.c (staged)",
                                        n=3))


SYNTHETIC_PROBE_C = """/* ON rehearsal TU, emitted by patch_d13_shreg_probe.py from its own block text.
 * NOT part of the kernel: it exercises check_d13_shreg_probe.py --expect on the way 995's lane did
 * (a linked synthetic probe), because the real image cannot be rebuilt in this lane. */
#include <stdint.h>
typedef unsigned int user_addr_t;
typedef unsigned int vm_size_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
struct proc { int p_pid; };
struct shared_region_check_np_args { user_addr_t start_address; };
struct shared_region_map_and_slide_np_args {
    int fd; uint32_t count; user_addr_t mappings; uint32_t slide;
    user_addr_t slide_start; uint32_t slide_size;
};
struct shared_file_mapping_np { uint32_t a, b, c; int d, e; };   /* 20 bytes, as in the image */
struct sysent_ent { int16_t narg; int8_t resv, flags; void *call, *m32, *m64;
                    int32_t ret; uint16_t arg_bytes; };          /* 24 bytes, as in the image */
extern int printf(const char *, ...);
extern int vm_shared_region_map_file(void);
extern int ios7synth_kr(void);
extern void ios7synth_undo(void);
extern int copyin(unsigned int, void *, unsigned int);
extern int ios7synth_slide_init(unsigned int, unsigned int, unsigned int, unsigned int);
extern void *ios7synth_entry(void);

/* The 20 B wire, modelled so the rehearsal exercises the checker's mode-independent anchor too.  The
 * expression is the tree's own (vm_unix.c:1210, `mappings_count * sizeof(mappings[0])`); clang emits
 * the same `add rX, rY, rY, lsl #2` / `lsl rZ, rX, #2` pair here as in the real image - so the anchor
 * is a property of the expression, not of this harness. */
int
shared_region_copyin_mappings(unsigned int count, unsigned int user, void *dst)
{
\tstruct shared_file_mapping_np *mappings = dst;

\tif (copyin(user, mappings, count * sizeof(mappings[0])))
\t\treturn 14;
\treturn 0;
}

int
shared_region_check_np(struct proc *p, struct shared_region_check_np_args *uap, int *retvalp)
{
\tint error = ios7synth_kr();
\t(void)retvalp;
%(np_entry)s%(np_exit)s\
\treturn error;
}

int
_shared_region_map(void)
{
\tint kr = vm_shared_region_map_file();
%(mapfile)s\
\treturn kr;
}

/* The calls are OPAQUE on purpose: with `int kr = 0` clang constant-folds the `if (kr != 0)`
 * branches away and the two exit prints never reach the image - measured once in this rehearsal. */
int
shared_region_map_and_slide_np(struct proc *p, struct shared_region_map_and_slide_np_args *uap,
                               int *retvalp)
{
\tint kr = ios7synth_kr();
\t(void)retvalp;
%(slide_entry)s\
\tif (kr != 0) {
%(slide_ret1)s\
\t\treturn kr;
\t}
\tkr = ios7synth_kr();
\tif (kr  != 0) {
\t\tios7synth_undo();
%(slide_ret2)s\
\t\treturn kr;
\t}
done:
%(slide_ret3)s\
\treturn kr;
}

/* The 999 slide half, modelled after the tree's `_shared_region_slide`: the three blocks are
 * substituted at the points plan_insert lands them, behind byte-identical anchor context (the local
 * pair, the tree's own failure printf + goto + brace, the copyin tail + brace), so this rehearsal
 * compiles the same block bytes the real staging inserts.  The tree's failure printf's conversion is
 * doubled for this template's own string-interpolation; the calls are OPAQUE for the reason above. */
int
_shared_region_slide(unsigned int slide, unsigned int entry_start, unsigned int entry_size,
                     unsigned int slide_start, unsigned int slide_size)
{
\tvoid *slide_info_entry = NULL;
\tint\t\t\terror;
%(slide999_enter)s\
\tif((error = ios7synth_slide_init(slide_size, entry_start, entry_size, slide))) {
\t\tprintf("slide_info initialization failed with kr=%%d\\n", error);
\t\tgoto done;
\t}
%(slide999_init)s\
\tslide_info_entry = ios7synth_entry();
\tif (slide_info_entry == NULL){
\t\terror = 14;
\t} else {
\t\terror = copyin((user_addr_t)slide_start,
\t\t\t       slide_info_entry,
\t\t\t       (vm_size_t)slide_size);
\t}
%(slide999_copyin)s\
\tif (error) {
\t\tgoto done;
\t}
done:
\treturn error;
}

struct sysent_ent sysent[439] = {
\t[294] = {1, 0, 0, (void *)shared_region_check_np, 0, 0, 3, 4},
\t[438] = {6, 0, 0, (void *)shared_region_map_and_slide_np, 0, 0, 3, 24},
};
"""

SYNTHETIC_STUB_C = """/* the stubs, in their own TU so clang cannot fold the probe's literal loads away */
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int vm_shared_region_map_file(void) { return 0; }
int ios7synth_kr(void) { return 0; }
void ios7synth_undo(void) { }
int copyin(unsigned int a, void *b, unsigned int c) { (void)a; (void)b; (void)c; return 0; }
int ios7synth_slide_init(unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
void *ios7synth_entry(void) { return (void *)0x80001000; }
"""


def emit_synthetic(dirpath):
    d = Path(dirpath)
    d.mkdir(parents=True, exist_ok=True)
    (d / "probe.c").write_text(SYNTHETIC_PROBE_C % {
        "np_entry": NP_ENTRY_BLOCK.lstrip("\n"),
        "np_exit": NP_EXIT_BLOCK.lstrip("\n"),
        "mapfile": MAPFILE_BLOCK.lstrip("\n"),
        "slide_entry": SLIDE_ENTRY_BLOCK.lstrip("\n"),
        "slide_ret1": SLIDE_RET1_BLOCK,
        "slide_ret2": SLIDE_RET2_BLOCK,
        "slide_ret3": SLIDE_RET3_BLOCK,
        "slide999_enter": SLIDE999_ENTER_BLOCK.lstrip("\n"),
        "slide999_init": SLIDE999_INIT_BLOCK.lstrip("\n"),
        "slide999_copyin": SLIDE999_COPYIN_BLOCK.lstrip("\n"),
    })
    (d / "stub.c").write_text(SYNTHETIC_STUB_C)
    print(f"patch_d13_shreg_probe: wrote {d}/probe.c + {d}/stub.c")
    print("  build:  clang --target=$(tools/xnu_config/arm_target.sh) -mcpu=cortex-a15 -marm \\")
    print("                -mfpu=neon-vfpv4 -mfloat-abi=softfp -ffreestanding -fno-builtin \\")
    print("                -fno-common -fno-pic -O2 -w -DSTAGE90_XNU_SHREG_PROBE=1 \\")
    print("                -c probe.c && (same for stub.c)")
    print("          (the define is REQUIRED: the guard is `defined(X) && X`, so the bare name")
    print("           would not compile - mi4-off-option-two-spellings)")
    print("          arm-none-eabi-ld -Ttext=0x80000000 -o probe.elf probe.o stub.o")
    return 0


def main() -> int:
    argv = sys.argv[1:]
    if "--emit-synthetic" in argv:
        i = argv.index("--emit-synthetic")
        if i + 1 >= len(argv):
            print("usage: patch_d13_shreg_probe.py --emit-synthetic <dir>", file=sys.stderr)
            return 2
        return emit_synthetic(argv[i + 1])
    dry_run = "--dry-run" in argv
    verify = "--verify-off" in argv
    argv = [a for a in argv if not a.startswith("--")]
    if len(argv) != 1:
        print("usage: patch_d13_shreg_probe.py <xnu-tree-root> [--dry-run] [--verify-off]",
              file=sys.stderr)
        return 2

    if verify:
        # Rehearse on a throwaway copy so the tree is never touched, then prove the ROUND TRIP on the
        # copy's bytes: classify() names the tree's state and peels every block it carries (the tree
        # ships the 996-only partial state - the 999 stage script feeds exactly that in), verify_off()
        # proves the staged text minus the ten guarded blocks is that base BYTE FOR BYTE, and
        # re-staging the base reproduces the staged text BYTE FOR BYTE.  Holds in every state.
        tmp = Path(tempfile.mkdtemp(prefix="shreg_verify_"))
        src = Path(argv[0]) / XNU_VM_UNIX
        if not src.is_file():
            print(f"patch_d13_shreg_probe: no {src}", file=sys.stderr)
            return 2
        (tmp / "bsd/vm").mkdir(parents=True)
        shutil.copy2(src, tmp / XNU_VM_UNIX)
        rc = subprocess.call([sys.executable, "-I", __file__, str(tmp)])
        if rc != 0:
            print("patch_d13_shreg_probe: verification: staging into the sandbox FAILED",
                  file=sys.stderr)
            return rc
        original = src.read_text()
        staged = (tmp / XNU_VM_UNIX).read_text()
        verdict, present, base, why = classify(original)
        if verdict == "foreign":
            print(f"patch_d13_shreg_probe: verification: FAIL - {why}")
            shutil.rmtree(tmp, ignore_errors=True)
            return 1
        ok_v, why_v = verify_off(base, staged)
        re_staged = base
        try:
            for what, anchor, block in EDITS:
                re_staged, _ch, _n = plan_insert(re_staged, what, anchor, block)
        except Refusal as e:
            print(f"patch_d13_shreg_probe: verification: FAIL - re-staging refused: {e}")
            shutil.rmtree(tmp, ignore_errors=True)
            return 1
        round_trip = re_staged == staged
        print(f"patch_d13_shreg_probe: verification: the tree file was {verdict.upper()} "
              f"({len(present)} of {len(EDITS)} block(s) staged)")
        print(f"  base:   peeling the {len(present)} staged block(s) leaves "
              f"{base.count('IOS7SHREG')} literal(s) and {base.count(GUARD_OPEN.strip())} guard "
              f"region(s); base sha256 {hashlib.sha256(base.encode()).hexdigest()}")
        print(f"  structure: {'ok' if ok_v else 'FAIL'} - {why_v}")
        print(f"  restage: re-staging that base reproduces the staged text byte for byte "
              f"({'PASS' if round_trip else 'FAIL'})")
        ok = ok_v and round_trip
        print(f"patch_d13_shreg_probe: verification: {'PASS' if ok else 'FAIL'}")
        shutil.rmtree(tmp, ignore_errors=True)
        return 0 if ok else 1

    root = Path(argv[0])
    path = root / XNU_VM_UNIX
    if not path.is_file():
        print(f"patch_d13_shreg_probe: no {path} (D13 tree not provisioned?)", file=sys.stderr)
        return 2

    original = path.read_text()
    try:
        notes, staged, changed = plan(original)
    except Refusal as e:
        print(f"patch_d13_shreg_probe: REFUSING: {e}", file=sys.stderr)
        return e.code

    for what, anchor, block, note in notes:
        print(f"  {note}")

    if dry_run or not changed:
        _verdict, _present, base, _why = classify(original)
        ok, why = verify_off(base, staged)
        print(f"  guard structure: {'ok' if ok else 'FAIL'} - {why}")
        if changed and dry_run:
            print(diff_text(original, staged))
        print(f"patch_d13_shreg_probe: {'nothing to do' if not changed else 'would stage'} "
              f"{XNU_VM_UNIX}")
        return 0 if ok else 1

    path.write_text(staged)
    digest = hashlib.sha256(staged.encode()).hexdigest()
    print(f"patch_d13_shreg_probe: staged {XNU_VM_UNIX} (sha256 {digest})")
    print(f"  {len(EDITS)} guard regions, {len(LITERALS)} print literals, "
          f"{sum(staged.count(l.decode().replace(chr(10), chr(92) + 'n')) for l in LITERALS)} "
          f"literal occurrence(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())