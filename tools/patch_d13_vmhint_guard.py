#!/usr/bin/env python3
"""Treat an implausible `vm_object.memq_hint` as absent inside `vm_page_lookup` (991).

WHY. 990's press (arm `armed-d13-0d531453`, commit `ce0195e`) HELD 990's own fix - sp was
0xcfdbba88, 8-byte aligned, and 989's `vld1.32 {d18},[r1 :64]` fault did not recur - and ran the
WHOLE mount + bsd_init + the pid-1 launchd exec: `BSD root: md0` -> `bsd_init: done` ->
`attempting to start init of /sbin/launchd` -> NEW first abort:

  * BY VALUE: `Fatal exception: sleh_abort: alignment fault`; `pc=0x80020838` = `vm_page_lookup`,
    instruction `e1c501d8  ldrd r0, [r5, #24]` (`mem->offset`), `r5 = 0x1`, `far = 0x19` (1 + 24),
    `fsr = 0x1`. Prologued by `ldr r5, [r0, #32]` (`mem = object->memq_hint`, so `memq_hint` sits at
    `vm_object + 0x20`), `object = r0 = r4 = 0xc0005b48`, `lr = 0x8003d8ec` = `vm_map_pmap_enter`'s
    `vm_page_lookup(object, 0)` call (the exec path: mach_loader -> vm_map_enter tail ->
    vm_map_pmap_enter). 214 `recursive abort` at pc=0x8046a464 (the Debugger trap) followed, then a
    watchdog reset ~46 s later; the log came from /proc/last_kmsg (the USB capture was empty).

WHY 0x1 CANNOT BE A HINT. Every writer of `memq_hint` in this tree writes either a valid `vm_page_t`
or NULL: `vm_object_template` initializes it to `VM_PAGE_NULL` and `_vm_object_allocate` copies the
template (`vm_object.c:449-466`), so every allocated object STARTS at NULL; `VM_PAGE_INSERT` sets the
page, `VM_PAGE_REMOVE` sets a neighbor or NULL; and this file's own hint paths (`:1393`, `:1407`,
`:1453`) store the page they are about to return. So the field was clobbered after allocation, or the
`object` is not a vm_object at all (0xc0005b48 sits ~11 MB below every other heap pointer recorded in
the log, consistent with an early zalloc allocation).

THE FIX. `memq_hint` is only a cache. When it is implausible - `(vm_offset_t)mem < 0x80000000u`
(not a kernel address) or `(vm_offset_t)mem & 3u` (not even word-aligned, so `ldrd mem->offset`
faults) - the guard treats it as absent FOR THIS CALL ONLY: a LOCAL SHADOW (`mem = VM_PAGE_NULL`)
lets the hash search below run as if there were no hint. THE FIELD ITSELF IS NEVER WRITTEN: the
object may not be ours, and repairing a field of something we cannot identify is not a repair. A
bounded (max 8) `kprintf` beginning `VMHINT991:` records the object's words (`w00`/`w04` = memq
next/prev - a self-linked queue says the object is real; `w18`/`w1c` = the low half of `vo_size`;
`w24` = the word past `memq_hint`) and the caller (`__builtin_return_address(0)`), so the log names
the path even if the boot does not survive. `kprintf` output lands in the same /proc/last_kmsg
capture the previous rungs read (precedent: `bsd_init`'s own enabled kprintf, and the exception
dumps themselves).

The edit is wrapped in `#if STAGE90_XNU_VMHINT_GUARD ... #endif`, so switch OFF compiles the tree's
original text byte for byte and neither the pool nor an old arm's entry image moves
(`stage_d13_kextb_login.sh`'s rule, `mi4-two-branch-split-4570-vs-master`). The linked-image
property is asserted BY VALUE by `tools/check_vmhint_guard.py` (the `VMHINT991:` string referenced
from `vm_page_lookup`'s own body, the `& 3u` test, the kprintf call, and the `memq_hint` load at
+0x20) - the same checker refuses the 990 image.

The tree is a re-provisionable, gitignored checkout; this script is the single source of truth for
the edit (`stage_d13_board.sh`'s rule). Idempotent: a second run verifies the inserted form and is a
no-op.
"""
import sys

# The marker that proves this patch ran.
MARK = "991: VMHINT_GUARD"

# --- osfmk/vm/vm_resident.c ------------------------------------------------------------------------
# The anchor is the tree's EXACT text (TAB-indented - written as \t escapes so no editor can turn
# them into spaces; verified by value: the three lines occur exactly once in the file). `new`
# contains `old` around the inserted `#if` block, so switch OFF is textually the original and
# compiles identically.
_LOOKUP_OLD = (
    "\tvm_object_lock_assert_held(object);\n"
    "\tmem = object->memq_hint;\n"
    "\n"
    "\tif (mem != VM_PAGE_NULL) {\n"
)
_LOOKUP_NEW = (
    "\tvm_object_lock_assert_held(object);\n"
    "\tmem = object->memq_hint;\n"
    "\n"
    "#if STAGE90_XNU_VMHINT_GUARD\n"
    "\t/* **991: VMHINT_GUARD - an implausible memq_hint is treated as absent, for THIS call only.**\n"
    "\t * 990's press ran the whole mount + bsd_init + the pid-1 launchd exec and died HERE:\n"
    "\t * pc=0x80020838 `ldrd r0,[r5,#24]`, r5 = object->memq_hint = 0x1, object = 0xc0005b48,\n"
    "\t * lr = vm_map_pmap_enter's vm_page_lookup call (offset 0). No legitimate writer of\n"
    "\t * memq_hint (the template's NULL copies in via _vm_object_allocate; VM_PAGE_INSERT and\n"
    "\t * VM_PAGE_REMOVE write a real page or NULL; this file's own hint paths write the page\n"
    "\t * they return) can produce 0x1 - so the field was clobbered after allocation, or\n"
    "\t * `object` is not a vm_object. The hint is only a cache: ignoring it for this lookup is\n"
    "\t * semantically safe and the hash search below runs as if there were no hint. THE FIELD\n"
    "\t * IS NEVER WRITTEN (the object may not be ours); the reading is printed, bounded, so the\n"
    "\t * log names the object's words and the caller even if the boot does not survive.\n"
    "\t * Switch OFF => Apple's text, byte for byte. See tools/patch_d13_vmhint_guard.py. */\n"
    "\tif (mem != VM_PAGE_NULL &&\n"
    "\t    ((vm_offset_t) mem < 0x80000000u || ((vm_offset_t) mem & 3u))) {\n"
    "\t\tstatic unsigned int vmhint991_reports;\n"
    "\n"
    "\t\tif (vmhint991_reports < 8u) {\n"
    "\t\t\tvmhint991_reports++;\n"
    "\t\t\tkprintf(\"VMHINT991: obj=%p mem=%p lr=%p w00=%x w04=%x w18=%x w1c=%x w24=%x\\n\",\n"
    "\t\t\t    (void *)object, (void *)mem, __builtin_return_address(0),\n"
    "\t\t\t    ((unsigned int *)(void *)object)[0],\n"
    "\t\t\t    ((unsigned int *)(void *)object)[1],\n"
    "\t\t\t    ((unsigned int *)(void *)object)[6],\n"
    "\t\t\t    ((unsigned int *)(void *)object)[7],\n"
    "\t\t\t    ((unsigned int *)(void *)object)[9]);\n"
    "\t\t}\n"
    "\t\tmem = VM_PAGE_NULL;\n"
    "\t}\n"
    "#endif /* STAGE90_XNU_VMHINT_GUARD */\n"
    "\n"
    "\tif (mem != VM_PAGE_NULL) {\n"
)

# (relative path, [(old, new), ...]) - one entry per file, all edits listed for that file.
EDITS = (
    ("osfmk/vm/vm_resident.c", (
        (_LOOKUP_OLD, _LOOKUP_NEW),
    )),
)


def patch_text(text: str, pairs, path: str) -> tuple[str, bool]:
    """Return (new_text, changed). Idempotent, and a marker present is not taken on trust: every
    inserted form must be present exactly once, or this refuses rather than certifying a tree the
    switch does not actually carry (`patch_d13_kextb_login.py`'s rule)."""
    if MARK in text:
        for _old, new in pairs:
            if text.count(new) != 1:
                raise SystemExit(
                    f"patch_d13_vmhint_guard: the 991 marker is present in {path} but an inserted "
                    f"form occurs {text.count(new)} times - moved by a hand edit, or half-applied. "
                    "Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_vmhint_guard: anchor in {path} occurs {n} times, not once - the tree is "
                f"not the expected revision, or it was edited by hand. Anchor begins: {old[:60]!r}. "
                "Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_vmhint_guard: an inserted form did not land once in {path} - "
                             "refusing to write")
    return text, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_vmhint_guard.py <XNU_TREE>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    for rel, pairs in EDITS:
        path = root + "/" + rel
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            print(f"patch_d13_vmhint_guard: no {path}", file=sys.stderr)
            return 2
        new, changed = patch_text(text, pairs, rel)
        if not changed:
            print(f"patch_d13_vmhint_guard: already applied ({path})")
            continue
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)
        print(f"patch_d13_vmhint_guard: {rel}'s vm_page_lookup now treats an implausible memq_hint "
              f"as absent for the call (local shadow; the field is never written) behind "
              f"STAGE90_XNU_VMHINT_GUARD ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())